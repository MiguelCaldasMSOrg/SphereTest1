#include "uart_example.h"
#include "example_timer.h"

#include <applibs/log.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct UartExample {
  EventLoop *loop;
  EventRegistration *registration;
  ExampleTimer *timer;
  int fd;
  bool failed;
  bool sending;
  unsigned char output[256];
  size_t outputLength;
  size_t outputOffset;
  UartExampleReceive received;
  void *receiveContext;
  UartExampleSent completed;
  void *sendContext;
};

static void Fail(UartExample *example, int error) {
  example->failed = true;
  if (example->registration != NULL) {
    if (EventLoop_UnregisterIo(example->loop, example->registration) == -1) {
      Log_Debug("UART unregister failed: %s\n", strerror(errno));
    }
    example->registration = NULL;
  }
  if (ExampleTimer_Set(example->timer, 0, 0) == -1) {
    Log_Debug("Cannot stop UART timeout: %s\n", strerror(errno));
  }
  if (example->sending) {
    example->sending = false;
    example->completed(error, example->sendContext);
  }
  example->received(error, NULL, 0, example->receiveContext);
}

static void SendTimedOut(int error, uint64_t expirations, void *context) {
  (void)expirations;
  UartExample *example = context;
  if (example->sending && !example->failed) {
    Fail(example, error == 0 ? ETIMEDOUT : error);
  }
}

static void UartReady(EventLoop *loop, int fd, EventLoop_IoEvents events, void *context) {
  (void)loop;
  UartExample *example = context;
  if (example->failed) {
    return;
  }
  if ((events & EventLoop_Error) != 0) {
    Fail(example, EIO);
    return;
  }
  if ((events & EventLoop_Input) != 0) {
    // Limit work per notification so a busy serial peer cannot starve local tasks.
    for (unsigned int attempt = 0; attempt < 4; ++attempt) {
      unsigned char data[64];
      ssize_t count = read(fd, data, sizeof(data));
      if (count == -1 && (errno == EAGAIN || errno == EINTR)) {
        break;
      }
      if (count == -1) {
        Fail(example, errno);
        return;
      }
      if (count == 0) {
        break;
      }
      example->received(0, data, (size_t)count, example->receiveContext);
    }
  }
  if ((events & EventLoop_Output) != 0 && example->sending) {
    ssize_t count = write(fd, example->output + example->outputOffset, example->outputLength - example->outputOffset);
    if (count == -1 && (errno == EAGAIN || errno == EINTR)) {
      return;
    }
    if (count <= 0) {
      Fail(example, count == -1 ? errno : EIO);
      return;
    }
    example->outputOffset += (size_t)count;
    if (example->outputOffset == example->outputLength) {
      if (EventLoop_ModifyIoEvents(example->loop, example->registration, EventLoop_Input) == -1 || ExampleTimer_Set(example->timer, 0, 0) == -1) {
        Fail(example, errno);
        return;
      }
      example->sending = false;
      example->completed(0, example->sendContext);
    }
  }
}

UartExample *UartExample_Start(EventLoop *loop, UART_Id id, unsigned int baud, UartExampleReceive received, void *context) {
  if (loop == NULL || received == NULL || id < 0 || baud == 0) {
    errno = EINVAL;
    return NULL;
  }
  UartExample *example = calloc(1, sizeof(*example));
  if (example == NULL) {
    return NULL;
  }
  example->fd = -1;
  example->loop = loop;
  example->received = received;
  example->receiveContext = context;
  UART_Config config;
  UART_InitConfig(&config);
  config.baudRate = baud;
  example->fd = UART_Open(id, &config);
  if (example->fd == -1) {
    goto failure;
  }
  int flags = fcntl(example->fd, F_GETFL);
  if (flags == -1 || fcntl(example->fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    goto failure;
  }
  example->timer = ExampleTimer_Create(loop, SendTimedOut, example);
  if (example->timer == NULL) {
    goto failure;
  }
  example->registration = EventLoop_RegisterIo(loop, example->fd, EventLoop_Input, UartReady, example);
  if (example->registration == NULL) {
    goto failure;
  }
  return example;

failure: {
  int error = errno;
  UartExample_Close(example);
  errno = error;
  return NULL;
}
}

int UartExample_Send(UartExample *example, const unsigned char *data, size_t length, UartExampleSent completed, void *context) {
  if (example == NULL || data == NULL || completed == NULL || length == 0 || length > sizeof(example->output)) {
    errno = EINVAL;
    return -1;
  }
  if (example->failed) {
    errno = EIO;
    return -1;
  }
  if (example->sending) {
    errno = EBUSY;
    return -1;
  }
  if (ExampleTimer_Set(example->timer, 1000, 0) == -1) {
    return -1;
  }
  if (EventLoop_ModifyIoEvents(example->loop, example->registration, EventLoop_Input | EventLoop_Output) == -1) {
    int error = errno;
    if (ExampleTimer_Set(example->timer, 0, 0) == -1) {
      Log_Debug("Cannot cancel UART send timer: %s\n", strerror(errno));
    }
    errno = error;
    return -1;
  }
  memcpy(example->output, data, length);
  example->outputLength = length;
  example->outputOffset = 0;
  example->completed = completed;
  example->sendContext = context;
  example->sending = true;
  return 0;
}

void UartExample_Close(UartExample *example) {
  if (example == NULL) {
    return;
  }
  if (example->registration != NULL && EventLoop_UnregisterIo(example->loop, example->registration) == -1) {
    Log_Debug("UART unregister failed: %s\n", strerror(errno));
  }
  ExampleTimer_Close(example->timer);
  if (example->fd != -1 && close(example->fd) == -1) {
    Log_Debug("UART close failed: %s\n", strerror(errno));
  }
  if (example->sending) {
    example->sending = false;
    example->failed = true;
    example->completed(ECANCELED, example->sendContext);
  }
  free(example);
}
