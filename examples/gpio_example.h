#pragma once

#include <applibs/eventloop.h>
#include <applibs/gpio.h>

typedef struct GpioExample GpioExample;
typedef void (*GpioExampleCallback)(int error, GPIO_Value_Type value, void *context);

/* Main-loop APIs. Choose safe, board-specific pins; callback must not close this object. */
GpioExample *GpioExample_Start(EventLoop *loop, GPIO_Id inputPin, GPIO_Id outputPin, GPIO_Value_Type inactiveValue, GpioExampleCallback changed, void *context);
int GpioExample_SetOutput(GpioExample *example, GPIO_Value_Type value);
void GpioExample_Close(GpioExample *example);
