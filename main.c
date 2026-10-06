#include <applibs/eventloop.h>
#include <applibs/log.h>

#include "examples/examples.h"

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

static volatile sig_atomic_t stopRequested = 0;
static int exitCode = EXIT_SUCCESS;
static uint64_t heartbeatCount = 0;

static void HandleTermination(int signalNumber) {
  (void)signalNumber;
  stopRequested = 1;
}

static void HandleHeartbeat(EventLoop *eventLoop, int timerFd, EventLoop_IoEvents events, void *context) {
  (void)eventLoop;
  (void)context;

  if ((events & EventLoop_Error) != 0) {
    Log_Debug("Heartbeat timer reported an I/O error.\n");
    exitCode = EXIT_FAILURE;
    stopRequested = 1;
    return;
  }

  uint64_t expirations;
  ssize_t count = read(timerFd, &expirations, sizeof(expirations));
  if (count == -1 && (errno == EINTR || errno == EAGAIN)) {
    return;
  }
  if (count != (ssize_t)sizeof(expirations)) {
    if (count == -1) {
      Log_Debug("Cannot read heartbeat timer: %s\n", strerror(errno));
    } else {
      Log_Debug("Unexpected heartbeat timer read size: %zd\n", count);
    }
    exitCode = EXIT_FAILURE;
    stopRequested = 1;
    return;
  }

  heartbeatCount += expirations;
  Log_Debug("SphereTest1: local heartbeat %" PRIu64 "\n", heartbeatCount);
}

int main(void) {
  struct sigaction termination = {0};
  termination.sa_handler = HandleTermination;
  if (sigemptyset(&termination.sa_mask) == -1 || sigaction(SIGTERM, &termination, NULL) == -1) {
    Log_Debug("Cannot install termination handler: %s\n", strerror(errno));
    return EXIT_FAILURE;
  }

  EventLoop *eventLoop = EventLoop_Create();
  if (eventLoop == NULL) {
    Log_Debug("Cannot create event loop: %s\n", strerror(errno));
    return EXIT_FAILURE;
  }

  int timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerFd == -1) {
    Log_Debug("Cannot create heartbeat timer: %s\n", strerror(errno));
    exitCode = EXIT_FAILURE;
    goto cleanup;
  }

  const struct itimerspec interval = {.it_interval = {.tv_sec = 1, .tv_nsec = 0}, .it_value = {.tv_sec = 1, .tv_nsec = 0}};
  if (timerfd_settime(timerFd, 0, &interval, NULL) == -1) {
    Log_Debug("Cannot start heartbeat timer: %s\n", strerror(errno));
    exitCode = EXIT_FAILURE;
    goto cleanup;
  }

  if (EventLoop_RegisterIo(eventLoop, timerFd, EventLoop_Input, HandleHeartbeat, NULL) == NULL) {
    Log_Debug("Cannot register heartbeat timer: %s\n", strerror(errno));
    exitCode = EXIT_FAILURE;
    goto cleanup;
  }

  // Local work starts immediately; future network features must not gate this loop.
  Log_Debug("SphereTest1 started. Internet and Azure authentication are not required.\n");
  if (Examples_Start(eventLoop) == -1) {
    Log_Debug("Optional examples could not fully start: %s; heartbeat continues.\n", strerror(errno));
  }
  while (!stopRequested) {
    if (EventLoop_Run(eventLoop, 500, true) == EventLoop_Run_Failed && errno != EINTR) {
      Log_Debug("Event loop failed: %s\n", strerror(errno));
      exitCode = EXIT_FAILURE;
      break;
    }
  }

cleanup:
  if (Examples_Stop(eventLoop) == -1) {
    Log_Debug("Example shutdown could not finish: %s\n", strerror(errno));
    // Preserve the loop and request memory until process teardown if a worker is still live.
    return EXIT_FAILURE;
  }
  EventLoop_Close(eventLoop);
  if (timerFd != -1 && close(timerFd) == -1) {
    Log_Debug("Cannot close heartbeat timer: %s\n", strerror(errno));
    exitCode = EXIT_FAILURE;
  }
  Log_Debug("SphereTest1 stopped.\n");
  return exitCode;
}
