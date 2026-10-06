#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "async_job.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <applibs/log.h>

struct AsyncJob {
  EventLoop *loop;
  EventRegistration *registration;
  int timerFd;
  pthread_t thread;
  atomic_bool workReturned;
  bool busy;
  AsyncJobWork work;
  AsyncJobCompletion completed;
  void *context;
  int workError;
  int monitorError;
};

static void *RunWork(void *context) {
  AsyncJob *job = context;
  int error = job->work(job->context);
  job->workError = error >= 0 ? error : EIO;
  atomic_store_explicit(&job->workReturned, true, memory_order_release);
  return NULL;
}

static void RecordMonitorError(AsyncJob *job, int error) {
  if (job->monitorError == 0) {
    job->monitorError = error;
    Log_Debug("AsyncJob: completion monitor error %d; retaining any unjoined worker.\n", error);
  }
}

static void CheckCompletion(EventLoop *loop, int fd, EventLoop_IoEvents events, void *context) {
  (void)loop;
  AsyncJob *job = context;
  uint64_t expirations;
  ssize_t count;
  do {
    count = read(fd, &expirations, sizeof(expirations));
  } while (count < 0 && errno == EINTR);

  if (count < 0 && errno != EAGAIN) {
    RecordMonitorError(job, errno);
  } else if (count >= 0 && count != (ssize_t)sizeof(expirations)) {
    RecordMonitorError(job, EIO);
  }
  if ((events & EventLoop_Error) != 0U) {
    RecordMonitorError(job, EIO);
  }
  if (!job->busy || !atomic_load_explicit(&job->workReturned, memory_order_acquire)) {
    return;
  }

  /*
     * Publishing a result is not proof that the thread has exited. A blocking
     * join here would introduce a race; keep polling until tryjoin succeeds.
     */
  int joinError = pthread_tryjoin_np(job->thread, NULL);
  if (joinError != 0) {
    if (joinError != EBUSY && joinError != EINTR) {
      RecordMonitorError(job, joinError);
    }
    return;
  }

  AsyncJobCompletion completed = job->completed;
  void *workContext = job->context;
  int error = job->workError != 0 ? job->workError : job->monitorError;
  job->busy = false;
  job->work = NULL;
  job->completed = NULL;
  job->context = NULL;
  completed(error, workContext);
}

AsyncJob *AsyncJob_Create(EventLoop *loop) {
  if (loop == NULL) {
    errno = EINVAL;
    return NULL;
  }
  AsyncJob *job = calloc(1, sizeof(*job));
  if (job == NULL) {
    return NULL;
  }
  job->loop = loop;
  atomic_init(&job->workReturned, false);
  job->timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (job->timerFd < 0) {
    int error = errno;
    free(job);
    errno = error;
    return NULL;
  }

  const struct itimerspec interval = {
      .it_interval = {.tv_sec = 0, .tv_nsec = 20000000L},
      .it_value = {.tv_sec = 0, .tv_nsec = 20000000L},
  };
  int result;
  do {
    result = timerfd_settime(job->timerFd, 0, &interval, NULL);
  } while (result < 0 && errno == EINTR);

  if (result == 0) {
    job->registration = EventLoop_RegisterIo(loop, job->timerFd, EventLoop_Input, CheckCompletion, job);
  }
  if (result < 0 || job->registration == NULL) {
    int error = errno;
    (void)close(job->timerFd);
    free(job);
    errno = error;
    return NULL;
  }
  return job;
}

int AsyncJob_Start(AsyncJob *job, AsyncJobWork work, AsyncJobCompletion completed, void *context) {
  if (job == NULL || work == NULL || completed == NULL) {
    errno = EINVAL;
    return -1;
  }
  if (job->busy) {
    errno = EBUSY;
    return -1;
  }
  if (job->monitorError != 0) {
    errno = job->monitorError;
    return -1;
  }

  pthread_attr_t attributes;
  int error = pthread_attr_init(&attributes);
  if (error != 0) {
    errno = error;
    return -1;
  }
  error = pthread_attr_setstacksize(&attributes, 64U * 1024U);
  if (error == 0) {
    job->work = work;
    job->completed = completed;
    job->context = context;
    job->workError = 0;
    atomic_store_explicit(&job->workReturned, false, memory_order_relaxed);
    error = pthread_create(&job->thread, &attributes, RunWork, job);
  }
  (void)pthread_attr_destroy(&attributes);
  if (error != 0) {
    job->work = NULL;
    job->completed = NULL;
    job->context = NULL;
    errno = error;
    return -1;
  }
  job->busy = true;
  return 0;
}

bool AsyncJob_IsBusy(const AsyncJob *job) {
  return job != NULL && job->busy;
}

int AsyncJob_Close(AsyncJob *job) {
  if (job == NULL) {
    errno = EINVAL;
    return -1;
  }
  if (job->busy) {
    errno = EBUSY;
    return -1;
  }
  if (EventLoop_UnregisterIo(job->loop, job->registration) < 0) {
    return -1;
  }
  /* Linux releases the fd even if close reports EINTR; do not retry it. */
  int result = close(job->timerFd);
  int error = errno;
  free(job);
  if (result < 0) {
    errno = error;
  }
  return result;
}
