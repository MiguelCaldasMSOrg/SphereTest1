[CmdletBinding()]
param(
    [ValidateSet('Build', 'Deploy', 'PrepareDebug', 'Status', 'Log', 'Stop')]
    [string]$Action = 'Build',
    [ValidateSet('ARM-Debug', 'ARM-Release')]
    [string]$Preset = 'ARM-Debug',
    [ValidatePattern('^$|^[0-9a-fA-F]{128}$')]
    [string]$DeviceId = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content -LiteralPath (Join-Path $projectRoot 'app_manifest.json') -Raw |
    ConvertFrom-Json
$componentId = $manifest.ComponentId

function Build-Application {
    $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
    Push-Location $projectRoot
    try {
        & $cmake --preset $Preset
        if ($LASTEXITCODE -ne 0) {
            throw 'CMake configuration failed.'
        }
        & $cmake --build --preset $Preset
        if ($LASTEXITCODE -ne 0) {
            throw 'Application build failed.'
        }
    } finally {
        Pop-Location
    }
}

if ($Action -eq 'Build') {
    Build-Application
    exit 0
}

$az = (Get-Command az.cmd -ErrorAction Stop).Source

function Invoke-SphereJson {
    param([string[]]$Arguments)
    $output = & $az sphere @Arguments --output json --only-show-errors
    if ($LASTEXITCODE -ne 0) {
        throw ('Azure Sphere command failed: ' + ($Arguments -join ' '))
    }
    if (-not $output) {
        throw ('Azure Sphere returned no JSON: ' + ($Arguments -join ' '))
    }
    return ($output | ConvertFrom-Json)
}

$devices = @(Invoke-SphereJson -Arguments @('device', 'list-attached'))
if ($DeviceId) {
    $devices = @($devices | Where-Object { $_.deviceIdentifier -ieq $DeviceId })
}
if ($devices.Count -ne 1) {
    throw 'Connect exactly one Sphere board, or pass -DeviceId with the ID of an attached board.'
}
$device = $devices[0]
if (-not $device.isResponsive -or $device.deviceIdentifier -notmatch '^[0-9a-fA-F]{128}$') {
    throw 'The selected board is not responding. No application changes were made.'
}
$deviceId = $device.deviceIdentifier
Write-Host ("Board: {0} ({1})" -f $deviceId, $device.ipAddress)

if ($Action -eq 'Status') {
    foreach ($arguments in @(
        @('device', 'show-os-version'),
        @('device', 'capability', 'show-attached'),
        @('device', 'wifi', 'show-status'),
        @('device', 'network', 'show-status'),
        @('device', 'image', 'list-installed')
    )) {
        Write-Host ($arguments -join ' ')
        Invoke-SphereJson -Arguments ($arguments + @('--device', $deviceId)) |
            ConvertTo-Json -Depth 8
    }
    exit 0
}

if ($Action -eq 'Log') {
    $client = [Net.Sockets.TcpClient]::new()
    $reader = $null
    try {
        $connection = $client.ConnectAsync([string]$device.ipAddress, 2342)
        if (-not $connection.Wait(10000)) {
            throw 'Application log connection timed out. Start the app or debug session first.'
        }
        $reader = [IO.StreamReader]::new($client.GetStream())
        Write-Host 'Application output (Ctrl+C to stop listening):'
        while ($null -ne ($line = $reader.ReadLine())) {
            Write-Output $line
        }
    } finally {
        if ($null -ne $reader) {
            $reader.Dispose()
        }
        $client.Dispose()
    }
    exit 0
}

if ($Action -eq 'Stop') {
    & $az sphere device app stop --device $deviceId --component-id $componentId
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not stop SphereTest1.'
    }
    exit 0
}

$capabilities = @(Invoke-SphereJson -Arguments @(
    'device', 'capability', 'show-attached', '--device', $deviceId
))
if ($capabilities -notcontains 'EnableAppDevelopment') {
    throw 'The selected board needs application development enabled before deployment.'
}
if ($Action -eq 'PrepareDebug') {
    if ($Preset -ne 'ARM-Debug') {
        throw 'The F5 configuration requires the ARM-Debug preset.'
    }
    if ($device.ipAddress -ne '192.168.35.2') {
        throw 'F5 expects USB address 192.168.35.2. Connect one board or update launch.json first.'
    }
}

Build-Application
$image = Join-Path $projectRoot "out\$Preset\$($manifest.Name).imagepackage"
if (-not (Test-Path -LiteralPath $image)) {
    throw ('Build did not produce the expected image: ' + $image)
}
$deployArguments = @(
    'sphere', 'device', 'sideload', 'deploy',
    '--device', $deviceId, '--image-package', $image
)
if ($Action -eq 'PrepareDebug') {
    $deployArguments += '--manual-start'
}
# Replace only this component; never delete unrelated applications.
& $az @deployArguments
if ($LASTEXITCODE -ne 0) {
    throw 'Sideloading SphereTest1 failed.'
}

if ($Action -eq 'PrepareDebug') {
    Invoke-SphereJson -Arguments @(
        'device', 'app', 'start', '--device', $deviceId,
        '--component-id', $componentId, '--debug-mode'
    ) | ConvertTo-Json -Depth 5
}
