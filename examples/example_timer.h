#pragma once

#include <applibs/eventloop.h>
#include <stdint.h>

typedef struct ExampleTimer ExampleTimer;
typedef void (*ExampleTimerCallback)(int error, uint64_t expirations, void *context);

ExampleTimer *ExampleTimer_Create(EventLoop *loop, ExampleTimerCallback callback, void *context);
int ExampleTimer_Set(ExampleTimer *timer, unsigned int delayMs, unsigned int periodMs);
void ExampleTimer_Close(ExampleTimer *timer);
