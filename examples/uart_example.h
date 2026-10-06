#pragma once

#include <applibs/eventloop.h>
#include <applibs/uart.h>
#include <stddef.h>

typedef struct UartExample UartExample;
typedef void (*UartExampleReceive)(int error, const unsigned char *data, size_t length, void *context);
typedef void (*UartExampleSent)(int error, void *context);

/* Main-loop APIs; callbacks must not close this object. RX data lives only during callback. */
UartExample *UartExample_Start(EventLoop *loop, UART_Id id, unsigned int baud, UartExampleReceive received, void *context);
/* Copies up to 256 bytes. One pending send; EBUSY otherwise. Completion means driver accepted data. */
int UartExample_Send(UartExample *example, const unsigned char *data, size_t length, UartExampleSent completed, void *context);
/* Cancels a pending send with ECANCELED before releasing the object. */
void UartExample_Close(UartExample *example);
