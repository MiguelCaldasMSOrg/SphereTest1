#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE < 200809L
#undef _POSIX_C_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "network_example.h"

#include <applibs/log.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>

enum { RequestLimit = 256, ResponseLimit = 512 };

struct NetworkExample {
  EventLoop *loop;
  EventRegistration *socketRegistration;
  EventRegistration *timerRegistration;
  int socketFd;
  int timerFd;
  bool connecting;
  bool readingOnly;
  bool complete;
  int pendingError;
  unsigned char request[RequestLimit];
  size_t requestLength;
  size_t sent;
  unsigned char response[ResponseLimit + 1]; /* One extra byte detects overflow. */
  size_t received;
  NetworkExampleCallback callback;
  void *context;
};

static void ReleaseIo(NetworkExample *example) {
  int savedError = errno;
  if (example->socketRegistration != NULL) {
    if (EventLoop_UnregisterIo(example->loop, example->socketRegistration) < 0) {
      (void)Log_Debug("NetworkExample: unregister socket failed (errno=%d).\n", errno);
    }
    example->socketRegistration = NULL;
  }
  if (example->timerRegistration != NULL) {
    if (EventLoop_UnregisterIo(example->loop, example->timerRegistration) < 0) {
      (void)Log_Debug("NetworkExample: unregister timer failed (errno=%d).\n", errno);
    }
    example->timerRegistration = NULL;
  }
  if (example->socketFd >= 0) {
    if (close(example->socketFd) < 0) {
      (void)Log_Debug("NetworkExample: close socket failed (errno=%d).\n", errno);
    }
    example->socketFd = -1;
  }
  if (example->timerFd >= 0) {
    if (close(example->timerFd) < 0) {
      (void)Log_Debug("NetworkExample: close timer failed (errno=%d).\n", errno);
    }
    example->timerFd = -1;
  }
  errno = savedError;
}

void NetworkExample_Close(NetworkExample *example) {
  if (example != NULL) {
    ReleaseIo(example);
    free(example);
  }
}

static void Complete(NetworkExample *example, int error) {
  /* A stack copy keeps data valid even if the callback frees the object. */
  unsigned char data[ResponseLimit];
  size_t length = error == 0 ? example->received : 0U;
  memcpy(data, example->response, length);
  NetworkExampleCallback callback = example->callback;
  void *context = example->context;
  ReleaseIo(example);
  example->complete = true;
  callback(error, error == 0 ? data : NULL, length, context);
  /* Neither this function nor its callers may access example after callback. */
}

static bool WouldBlock(int error) {
  return error == EAGAIN || error == EWOULDBLOCK;
}

static bool CheckTimeout(NetworkExample *example) {
  uint64_t expirations;
  ssize_t count = read(example->timerFd, &expirations, sizeof(expirations));
  if (count < 0 && WouldBlock(errno)) {
    return false;
  }
  if (count < 0 && errno == EINTR) {
    return true; /* Yield; do not let repeated signals monopolize the loop. */
  }
  int error = count < 0 ? errno : ETIMEDOUT;
  if (count >= 0 && count != (ssize_t)sizeof(expirations)) {
    error = EIO;
  }
  Complete(example, error);
  return true;
}

static void TimerReady(EventLoop *loop, int fd, EventLoop_IoEvents events, void *context) {
  (void)loop;
  (void)fd;
  NetworkExample *example = context;
  if (example->complete || CheckTimeout(example)) {
    return;
  }
  if ((events & EventLoop_Error) != 0U) {
    Complete(example, EIO);
  }
}

static int SocketError(int fd) {
  int error = 0;
  socklen_t length = (socklen_t)sizeof(error);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0) {
    return errno;
  }
  return error;
}

static void ReadResponse(NetworkExample *example, EventLoop_IoEvents events) {
  /* One bounded read per dispatch, including when error readiness accompanies data. */
  ssize_t count = recv(example->socketFd, example->response + example->received, sizeof(example->response) - example->received, 0);
  if (count < 0) {
    int error = errno;
    if (error == EINTR) {
      return;
    }
    if (WouldBlock(error)) {
      if (example->pendingError == 0 && (events & EventLoop_Error) == 0U) {
        return;
      }
      error = example->pendingError != 0 ? example->pendingError : EIO;
    }
    Complete(example, error);
    return;
  }
  if (count == 0) {
    Complete(example, example->pendingError);
    return;
  }
  unsigned char *newline = memchr(example->response + example->received, '\n', (size_t)count);
  example->received += (size_t)count;
  if (newline != NULL) {
    example->received = (size_t)(newline - example->response) + 1U;
  }
  if (example->received > ResponseLimit) {
    Complete(example, EMSGSIZE);
    return;
  }
  if (newline != NULL) {
    Complete(example, 0);
  }
  /* Otherwise drain remaining buffered data on later dispatches before
     * reporting a saved socket error. A complete line wins over a later reset. */
}

static void SocketReady(EventLoop *loop, int fd, EventLoop_IoEvents events, void *context) {
  (void)loop;
  (void)fd;
  NetworkExample *example = context;
  if (example->complete || CheckTimeout(example)) {
    return;
  }
  if (example->connecting || (events & EventLoop_Error) != 0U) {
    int error = SocketError(example->socketFd);
    if (error == EINTR) {
      return;
    }
    if (example->connecting && error != 0) {
      Complete(example, error);
      return;
    }
    example->connecting = false;
    if (error != 0) {
      example->pendingError = error;
    }
  }
  if (example->sent < example->requestLength && example->pendingError == 0) {
    ssize_t count = send(example->socketFd, example->request + example->sent, example->requestLength - example->sent, MSG_NOSIGNAL);
    if (count > 0) {
      example->sent += (size_t)count;
    } else if (count == 0) {
      example->pendingError = EPIPE;
    } else if (errno != EINTR && !WouldBlock(errno)) {
      example->pendingError = errno;
    }
  }
  if (!example->readingOnly && (example->sent == example->requestLength || example->pendingError != 0)) {
    if (EventLoop_ModifyIoEvents(example->loop, example->socketRegistration, EventLoop_Input) < 0) {
      Complete(example, errno);
      return;
    }
    example->readingOnly = true;
  }
  ReadResponse(example, events);
}

NetworkExample *NetworkExample_Start(EventLoop *loop, const char *ipv4Address, uint16_t port, const unsigned char *request, size_t requestLength, unsigned int timeoutMs, NetworkExampleCallback callback, void *context) {
  if (loop == NULL || ipv4Address == NULL || port == 0U || request == NULL || requestLength == 0U || timeoutMs == 0U || callback == NULL) {
    errno = EINVAL;
    return NULL;
  }
  if (requestLength > RequestLimit) {
    errno = EMSGSIZE;
    return NULL;
  }
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(port)};
  int parsed = inet_pton(AF_INET, ipv4Address, &address.sin_addr);
  if (parsed != 1) {
    if (parsed == 0) {
      errno = EINVAL;
    }
    return NULL;
  }
  NetworkExample *example = calloc(1, sizeof(*example));
  if (example == NULL) {
    return NULL;
  }
  example->loop = loop;
  example->socketFd = -1;
  example->timerFd = -1;
  example->callback = callback;
  example->context = context;
  example->requestLength = requestLength;
  memcpy(example->request, request, requestLength);

  /* SOCK_NONBLOCK sets O_NONBLOCK before connect; no DNS or blocking I/O. */
  example->socketFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (example->socketFd < 0) {
    goto fail;
  }
  example->timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (example->timerFd < 0) {
    goto fail;
  }
  struct itimerspec timeout = {.it_value = {.tv_sec = (time_t)(timeoutMs / 1000U), .tv_nsec = (long)(timeoutMs % 1000U) * 1000000L}};
  if (timerfd_settime(example->timerFd, 0, &timeout, NULL) < 0) {
    goto fail;
  }
  example->timerRegistration = EventLoop_RegisterIo(loop, example->timerFd, EventLoop_Input, TimerReady, example);
  if (example->timerRegistration == NULL) {
    goto fail;
  }
  if (connect(example->socketFd, (const struct sockaddr *)&address, (socklen_t)sizeof(address)) < 0) {
    if (errno != EINPROGRESS && errno != EINTR) {
      goto fail;
    }
    example->connecting = true;
  }
  example->socketRegistration = EventLoop_RegisterIo(loop, example->socketFd, EventLoop_Input | EventLoop_Output, SocketReady, example);
  if (example->socketRegistration == NULL) {
    goto fail;
  }
  return example;

fail: {
  int error = errno;
  NetworkExample_Close(example);
  errno = error;
  return NULL;
}
}
