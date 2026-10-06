#ifndef SPHERE_EXAMPLES_I2C_EXAMPLE_H
#define SPHERE_EXAMPLES_I2C_EXAMPLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct I2cExampleTransfer {
  int interfaceId;
  uint32_t address;
  uint8_t registerAddress;
  bool write;
  uint8_t data[16];
  size_t length;
} I2cExampleTransfer;

/*
 * AsyncJob work function: returns 0 or a positive errno-style error.
 * Provide a nonnegative interface ID, 7-bit address 0x08..0x77, a register
 * appropriate for YOUR hardware, and length 1..16. The driver checks that the
 * interface exists and the manifest permits it. No board or sensor is assumed.
 *
 * Applibs I2C calls are synchronous and belong exclusively to this worker.
 * Serialize all I2C jobs; do not call I2C APIs elsewhere concurrently.
 * This worker opens/configures/transfers/closes its own bus descriptor.
 * Start with a register read; enable writes only with explicit user consent.
 * Even reads write a register address and may have device-specific effects.
 *
 * Keep this request alive and untouched until AsyncJob completion. On success,
 * reads replace data[0..length); on error, read data is not valid. Bus transfers
 * (including ones reporting EINTR) are not retried: hardware effects may already
 * have occurred. Speed is 100 kHz and operation timeout is 100 milliseconds.
 */
int I2cExample_Transfer(void *context);

#endif
