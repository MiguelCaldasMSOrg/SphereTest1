#include "example_timer.h"

#include <applibs/log.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

struct ExampleTimer {
  EventLoop *loop;
  EventRegistration *registration;
  int fd;
  ExampleTimerCallback callback;
  void *context;
};

static void TimerReady(EventLoop *loop, int fd, EventLoop_IoEvents events, void *context) {
  (void)loop;
  ExampleTimer *timer = context;
  int error = 0;
  uint64_t expirations = 0;
  if ((events & EventLoop_Error) != 0) {
    error = EIO;
  } else {
    ssize_t count = read(fd, &expirations, sizeof(expirations));
    if (count == -1 && (errno == EAGAIN || errno == EINTR)) {
      return;
    }
    if (count != (ssize_t)sizeof(expirations)) {
      error = count == -1 ? errno : EIO;
    }
  }
  timer->callback(error, expirations, timer->context);
}

ExampleTimer *ExampleTimer_Create(EventLoop *loop, ExampleTimerCallback callback, void *context) {
  if (loop == NULL || callback == NULL) {
    errno = EINVAL;
    return NULL;
  }
  ExampleTimer *timer = calloc(1, sizeof(*timer));
  if (timer == NULL) {
    return NULL;
  }
  timer->loop = loop;
  timer->callback = callback;
  timer->context = context;
  timer->fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (timer->fd == -1) {
    int error = errno;
    free(timer);
    errno = error;
    return NULL;
  }
  timer->registration = EventLoop_RegisterIo(loop, timer->fd, EventLoop_Input, TimerReady, timer);
  if (timer->registration == NULL) {
    int error = errno;
    ExampleTimer_Close(timer);
    errno = error;
    return NULL;
  }
  return timer;
}

int ExampleTimer_Set(ExampleTimer *timer, unsigned int delayMs, unsigned int periodMs) {
  if (timer == NULL || (delayMs == 0 && periodMs != 0)) {
    errno = EINVAL;
    return -1;
  }
  const struct itimerspec value = {.it_value = {.tv_sec = (time_t)(delayMs / 1000U), .tv_nsec = (long)(delayMs % 1000U) * 1000000L}, .it_interval = {.tv_sec = (time_t)(periodMs / 1000U), .tv_nsec = (long)(periodMs % 1000U) * 1000000L}};
  return timerfd_settime(timer->fd, 0, &value, NULL);
}

void ExampleTimer_Close(ExampleTimer *timer) {
  if (timer == NULL) {
    return;
  }
  if (timer->registration != NULL && EventLoop_UnregisterIo(timer->loop, timer->registration) == -1) {
    Log_Debug("Timer unregister failed: %s\n", strerror(errno));
  }
  if (close(timer->fd) == -1) {
    Log_Debug("Timer close failed: %s\n", strerror(errno));
  }
  free(timer);
}
