#pragma once

/* Enable only after setting real hardware IDs and matching app_manifest.json capabilities. */
#define EXAMPLE_GPIO_ENABLED 0
#define EXAMPLE_GPIO_INPUT_ID (-1)
#define EXAMPLE_GPIO_OUTPUT_ID (-1)
#define EXAMPLE_GPIO_INACTIVE_HIGH 0

#define EXAMPLE_I2C_ENABLED 0
#define EXAMPLE_I2C_INTERFACE_ID (-1)
#define EXAMPLE_I2C_ADDRESS 0U
#define EXAMPLE_I2C_REGISTER_CONFIRMED 0
#define EXAMPLE_I2C_REGISTER 0U
#define EXAMPLE_I2C_WRITE_ENABLED 0
#define EXAMPLE_I2C_WRITE_VALUE 0U

#define EXAMPLE_UART_ENABLED 0
#define EXAMPLE_UART_INTERFACE_ID (-1)
#define EXAMPLE_UART_BAUD 115200U

#define EXAMPLE_STORAGE_ENABLED 0
/* Explicitly opt in: replaces this application's mutable demo record once per run. */
#define EXAMPLE_STORAGE_WRITE_ENABLED 0

#define EXAMPLE_NETWORK_ENABLED 0
#define EXAMPLE_NETWORK_IPV4 "192.0.2.1"
#define EXAMPLE_NETWORK_PORT 0U
#define EXAMPLE_NETWORK_TIMEOUT_MS 3000U
