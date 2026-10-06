#include "examples.h"
#include "examples_config.h"
#include "example_timer.h"
#include "gpio_example.h"
#include "uart_example.h"
#include "network_example.h"
#include "async_job.h"
#include "i2c_example.h"
#include "storage_example.h"

#include <applibs/log.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

enum StoragePhase { PackageRead, MutableRead, MutableWrite, StorageDone };

/* Static request storage stays alive even if process shutdown overtakes a worker. */
static struct {
  EventLoop *loop;
  ExampleTimer *timer;
  GpioExample *gpio;
  UartExample *uart;
  NetworkExample *network;
  AsyncJob *job;
  I2cExampleTransfer i2c;
  StorageExampleRequest storage;
  enum StoragePhase storagePhase;
  unsigned int tick;
  bool started;
  bool stopping;
  bool gpioFailed;
  bool uartFailed;
  bool networkFinished;
  bool i2cWritePending;
  GPIO_Value_Type output;
} demo;

static void PrintBytes(const char *label, const unsigned char *data, size_t length) {
  if (length == 0) {
    Log_Debug("%s: empty result.\n", label);
  } else {
    Log_Debug("%s: %zu bytes; first byte 0x%02x.\n", label, length, (unsigned int)data[0]);
  }
}

static void GpioChanged(int error, GPIO_Value_Type value, void *context) {
  (void)context;
  if (error != 0) {
    demo.gpioFailed = true;
    Log_Debug("GPIO input example failed: %s\n", strerror(error));
  } else {
    Log_Debug("GPIO debounced input changed to %u.\n", (unsigned int)value);
  }
}

static void UartReceived(int error, const unsigned char *data, size_t length, void *context) {
  (void)context;
  if (error != 0) {
    demo.uartFailed = true;
    Log_Debug("UART receive example failed: %s\n", strerror(error));
  } else {
    PrintBytes("UART received", data, length);
  }
}

static void UartSent(int error, void *context) {
  (void)context;
  if (error != 0) {
    Log_Debug("UART send completed with error: %s\n", strerror(error));
  } else {
    Log_Debug("UART send accepted by the driver (not a peer acknowledgement).\n");
  }
}

static void NetworkCompleted(int error, const unsigned char *data, size_t length, void *context) {
  (void)context;
  demo.networkFinished = true;
  if (error != 0) {
    Log_Debug("Optional TCP request failed: %s; local work continues.\n", strerror(error));
  } else {
    PrintBytes("TCP response", data, length);
  }
}

static void I2cCompleted(int error, void *context) {
  I2cExampleTransfer *transfer = context;
  if (error != 0) {
    Log_Debug("I2C %s failed: %s\n", transfer->write ? "write" : "read", strerror(error));
  } else if (transfer->write) {
    Log_Debug("I2C register write completed.\n");
  } else {
    PrintBytes("I2C register read", transfer->data, transfer->length);
  }
}

static void StorageCompleted(int error, void *context) {
  StorageExampleRequest *request = context;
  if (error != 0) {
    Log_Debug("Storage example failed: %s\n", strerror(error));
    demo.storagePhase = StorageDone;
  } else if (request->packagedFile) {
    PrintBytes("Read-only packaged resource", request->data, request->length);
    demo.storagePhase = MutableRead;
  } else if (request->write) {
    Log_Debug("Mutable demo record saved and synchronized.\n");
    demo.storagePhase = StorageDone;
  } else {
    PrintBytes("Mutable demo record", request->data, request->length);
    demo.storagePhase = EXAMPLE_STORAGE_WRITE_ENABLED ? MutableWrite : StorageDone;
  }
}

static void SubmitStorage(void) {
  memset(&demo.storage, 0, sizeof(demo.storage));
  demo.storage.packagedFile = demo.storagePhase == PackageRead;
  demo.storage.resourcePath = "examples/resources/message.txt";
  demo.storage.write = demo.storagePhase == MutableWrite;
  if (demo.storage.write) {
    const unsigned char record[] = "SphereTest1: persistent local demo record\n";
    memcpy(demo.storage.data, record, sizeof(record) - 1U);
    demo.storage.length = sizeof(record) - 1U;
  }
  if (AsyncJob_Start(demo.job, StorageExample_Transfer, StorageCompleted, &demo.storage) == -1) {
    Log_Debug("Cannot submit storage request: %s\n", strerror(errno));
    demo.storagePhase = StorageDone;
  }
}

static void SubmitI2c(void) {
  demo.i2c.interfaceId = EXAMPLE_I2C_INTERFACE_ID;
  demo.i2c.address = EXAMPLE_I2C_ADDRESS;
  demo.i2c.registerAddress = EXAMPLE_I2C_REGISTER;
  demo.i2c.length = 1;
  demo.i2c.write = demo.i2cWritePending;
  demo.i2c.data[0] = EXAMPLE_I2C_WRITE_VALUE;
  if (AsyncJob_Start(demo.job, I2cExample_Transfer, I2cCompleted, &demo.i2c) == -1) {
    Log_Debug("Cannot submit I2C request: %s\n", strerror(errno));
  } else {
    demo.i2cWritePending = false;
  }
}

static void DemoTick(int error, uint64_t expirations, void *context) {
  (void)expirations;
  (void)context;
  if (demo.stopping) {
    return;
  }
  if (error != 0) {
    Log_Debug("Example scheduler failed: %s\n", strerror(error));
    if (ExampleTimer_Set(demo.timer, 0, 0) == -1) {
      Log_Debug("Cannot stop example scheduler: %s\n", strerror(errno));
    }
    demo.stopping = true;
    return;
  }
  if (demo.gpio != NULL && !demo.gpioFailed) {
    demo.output = demo.output == GPIO_Value_Low ? GPIO_Value_High : GPIO_Value_Low;
    if (GpioExample_SetOutput(demo.gpio, demo.output) == -1) {
      demo.gpioFailed = true;
      Log_Debug("GPIO output example failed: %s\n", strerror(errno));
    }
  }
  if (demo.uart != NULL && !demo.uartFailed && demo.tick % 5U == 0U) {
    static const unsigned char message[] = "SphereTest1 UART ping\n";
    if (UartExample_Send(demo.uart, message, sizeof(message) - 1U, UartSent, NULL) == -1) {
      Log_Debug("Cannot queue UART send: %s\n", strerror(errno));
    }
  }
  if (demo.job != NULL && !AsyncJob_IsBusy(demo.job)) {
    if (demo.storagePhase != StorageDone) {
      SubmitStorage();
    } else if (EXAMPLE_I2C_ENABLED && EXAMPLE_I2C_REGISTER_CONFIRMED && demo.tick % 5U == 0U) {
      SubmitI2c();
    }
  }
  if (EXAMPLE_NETWORK_ENABLED && demo.tick % 30U == 0U) {
    if (demo.networkFinished) {
      NetworkExample_Close(demo.network);
      demo.network = NULL;
      demo.networkFinished = false;
    }
    if (demo.network == NULL) {
      static const unsigned char request[] = "SphereTest1 TCP ping\n";
      demo.network = NetworkExample_Start(demo.loop, EXAMPLE_NETWORK_IPV4, EXAMPLE_NETWORK_PORT, request, sizeof(request) - 1U, EXAMPLE_NETWORK_TIMEOUT_MS, NetworkCompleted, NULL);
      if (demo.network == NULL) {
        Log_Debug("Cannot start optional TCP request: %s\n", strerror(errno));
      }
    }
  }
  ++demo.tick;
}

int Examples_Start(EventLoop *loop) {
  if (loop == NULL || demo.started) {
    errno = loop == NULL ? EINVAL : EALREADY;
    return -1;
  }
  demo.started = true;
  demo.loop = loop;
  demo.storagePhase = EXAMPLE_STORAGE_ENABLED ? PackageRead : StorageDone;
  demo.i2cWritePending = EXAMPLE_I2C_WRITE_ENABLED != 0;
  demo.output = EXAMPLE_GPIO_INACTIVE_HIGH ? GPIO_Value_High : GPIO_Value_Low;
  if (!EXAMPLE_GPIO_ENABLED && !EXAMPLE_I2C_ENABLED && !EXAMPLE_UART_ENABLED && !EXAMPLE_STORAGE_ENABLED && !EXAMPLE_NETWORK_ENABLED) {
    return 0;
  }
  if (EXAMPLE_GPIO_ENABLED) {
    demo.gpio = GpioExample_Start(loop, EXAMPLE_GPIO_INPUT_ID, EXAMPLE_GPIO_OUTPUT_ID, demo.output, GpioChanged, NULL);
    if (demo.gpio == NULL) {
      Log_Debug("GPIO example not started: %s\n", strerror(errno));
    }
  }
  if (EXAMPLE_UART_ENABLED) {
    demo.uart = UartExample_Start(loop, EXAMPLE_UART_INTERFACE_ID, EXAMPLE_UART_BAUD, UartReceived, NULL);
    if (demo.uart == NULL) {
      Log_Debug("UART example not started: %s\n", strerror(errno));
    }
  }
  if (EXAMPLE_I2C_ENABLED && !EXAMPLE_I2C_REGISTER_CONFIRMED) {
    Log_Debug("I2C example disabled: confirm the target register in examples_config.h.\n");
  }
  if ((EXAMPLE_I2C_ENABLED && EXAMPLE_I2C_REGISTER_CONFIRMED) || EXAMPLE_STORAGE_ENABLED) {
    demo.job = AsyncJob_Create(loop);
    if (demo.job == NULL) {
      Log_Debug("I2C/storage worker not started: %s\n", strerror(errno));
    }
  }
  demo.timer = ExampleTimer_Create(loop, DemoTick, NULL);
  if (demo.timer == NULL || ExampleTimer_Set(demo.timer, 1, 1000) == -1) {
    return -1;
  }
  return 0;
}

int Examples_Stop(EventLoop *loop) {
  demo.stopping = true;
  ExampleTimer_Close(demo.timer);
  demo.timer = NULL;
  GpioExample_Close(demo.gpio);
  demo.gpio = NULL;
  UartExample_Close(demo.uart);
  demo.uart = NULL;
  NetworkExample_Close(demo.network);
  demo.network = NULL;

  if (demo.job != NULL) {
    struct timespec deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) == -1) {
      return -1;
    }
    ++deadline.tv_sec;
    while (AsyncJob_IsBusy(demo.job)) {
      struct timespec now;
      if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        return -1;
      }
      if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
        // Do not free a worker's request context while a system call still uses it.
        errno = ETIMEDOUT;
        return -1;
      }
      if (EventLoop_Run(loop, 50, true) == EventLoop_Run_Failed && errno != EINTR) {
        return -1;
      }
    }
    if (AsyncJob_Close(demo.job) == -1) {
      return -1;
    }
    demo.job = NULL;
  }
  return 0;
}
