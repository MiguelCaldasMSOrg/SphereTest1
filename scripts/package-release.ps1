[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $projectRoot 'app_manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$assetsDirectory = Join-Path $projectRoot 'out\release-assets'
New-Item -ItemType Directory -Path $assetsDirectory -Force | Out-Null

$checksums = foreach ($preset in @('ARM-Debug', 'ARM-Release')) {
    $buildDirectory = Join-Path $projectRoot "out\$preset"
    $files = @(
        (Join-Path $buildDirectory "$($manifest.Name).imagepackage"),
        (Join-Path $buildDirectory "$($manifest.Name).out"),
        $manifestPath,
        (Join-Path $projectRoot 'GETTING_STARTED.txt')
    )
    foreach ($file in $files) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -eq 0) {
            throw "Missing or empty release input: $file. Build both ARM configurations first."
        }
    }
    $archiveName = "$($manifest.Name)-$preset.zip"
    $archive = Join-Path $assetsDirectory $archiveName
    Compress-Archive -LiteralPath $files -DestinationPath $archive -CompressionLevel Optimal -Force
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $archiveName"
}

[IO.File]::WriteAllText(
    (Join-Path $assetsDirectory 'SHA256SUMS'),
    ($checksums -join "`n") + "`n",
    [Text.UTF8Encoding]::new($false)
)
Write-Host "Release assets and SHA256SUMS are ready in $assetsDirectory."
