#pragma once

#include <applibs/eventloop.h>

int Examples_Start(EventLoop *loop);
/* Stop submissions and allow at most one second for an outstanding worker request. */
int Examples_Stop(EventLoop *loop);
