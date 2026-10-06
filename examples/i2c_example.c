#include "i2c_example.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <applibs/i2c.h>

int I2cExample_Transfer(void *context) {
  I2cExampleTransfer *request = context;
  if (request == NULL || request->interfaceId < 0 || request->address < 0x08U || request->address > 0x77U || request->length == 0 || request->length > sizeof(request->data)) {
    return EINVAL;
  }

  int fd;
  do {
    fd = I2CMaster_Open(request->interfaceId);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0) {
    return errno;
  }

  int error = 0;
  int result;
  do {
    result = I2CMaster_SetBusSpeed(fd, I2C_BUS_SPEED_STANDARD);
  } while (result < 0 && errno == EINTR);
  if (result < 0) {
    error = errno;
    goto cleanup;
  }
  do {
    result = I2CMaster_SetTimeout(fd, 100U);
  } while (result < 0 && errno == EINTR);
  if (result < 0) {
    error = errno;
    goto cleanup;
  }

  ssize_t transferred;
  if (request->write) {
    uint8_t bytes[1U + sizeof(request->data)];
    bytes[0] = request->registerAddress;
    memcpy(bytes + 1, request->data, request->length);
    transferred = I2CMaster_Write(fd, request->address, bytes, 1U + request->length);
  } else {
    transferred = I2CMaster_WriteThenRead(fd, request->address, &request->registerAddress, 1U, request->data, request->length);
  }
  if (transferred < 0) {
    error = errno;
  } else if ((size_t)transferred != 1U + request->length) {
    /* WriteThenRead reports the combined write AND read byte count. */
    error = EIO;
  }

cleanup:
  /* Do not retry close on Linux, including EINTR: the fd has been released. */
  if (close(fd) < 0 && error == 0) {
    error = errno;
  }
  return error;
}
