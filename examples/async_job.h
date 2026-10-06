#ifndef SPHERE_EXAMPLES_ASYNC_JOB_H
#define SPHERE_EXAMPLES_ASYNC_JOB_H

#include <stdbool.h>

#include <applibs/eventloop.h>

typedef struct AsyncJob AsyncJob;
typedef int (*AsyncJobWork)(void *context);
typedef void (*AsyncJobCompletion)(int error, void *context);

/*
 * These functions, and the EventLoop, belong to the caller's loop thread.
 * Work runs on a joinable pthread with a 64 KiB stack: return 0 or a positive
 * errno-style error, without calling EventLoop APIs, pthread_exit or cancellation.
 *
 * This offloads synchronous I/O; it does not make an Applibs API asynchronous.
 * A periodic 20 ms timer and nonblocking join deliver completion on the loop
 * thread only after the worker has exited. The timer also ticks while idle.
 * Link with pthread; this example uses SDK 18's pthread_tryjoin_np extension.
 */
AsyncJob *AsyncJob_Create(EventLoop *loop);

/*
 * Accepts at most one operation; returns -1 with errno (including EBUSY) on
 * validation/setup failure, without calling completed. NULL context is allowed.
 * Once accepted, completed is called once while the caller keeps running loop.
 * Keep context, buffers and referenced strings alive and untouched until then.
 * Completion may resubmit work or close this same job: it is already idle and
 * the dispatcher never accesses the job after calling completed.
 */
int AsyncJob_Start(AsyncJob *job, AsyncJobWork work, AsyncJobCompletion completed, void *context);

/* Busy includes a finished worker whose completion has not been dispatched. */
bool AsyncJob_IsBusy(const AsyncJob *job);

/*
 * Never waits for work. EBUSY leaves the job and its context alive.
 * Stop new submissions, drain the loop with a deadline, then close idle jobs
 * before closing the EventLoop. If the deadline expires, retain a busy job,
 * its loop and context until process exit; do not cancel/free a live worker.
 * NULL is invalid. An unregister failure also leaves the job alive; after
 * successful unregister, the job is consumed even if closing its fd fails.
 */
int AsyncJob_Close(AsyncJob *job);

#endif
