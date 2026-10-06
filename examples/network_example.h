#ifndef NETWORK_EXAMPLE_H
#define NETWORK_EXAMPLE_H

#include <applibs/eventloop.h>
#include <stddef.h>
#include <stdint.h>

/* Optional raw TCP newline-echo example: unencrypted, never send secrets.
 * No network activity occurs until Start. The manifest's AllowedConnections
 * must permit the exact configured host/IP; Azure DeviceAuthentication is not
 * required. This API accepts numeric IPv4 only (no DNS).
 *
 * Call Start/Close only on the event-loop thread, and Close before closing the
 * loop. A request must contain 1..256 bytes; append '\n' if the server needs it.
 * Port and timeoutMs must be nonzero. Start copies the request, returning NULL
 * with errno on validation/setup failure, without invoking the callback.
 *
 * The callback runs once asynchronously: error is zero on success, otherwise
 * an errno value (including ETIMEDOUT and EMSGSIZE). Success ends at the first
 * newline (included) or orderly EOF, with at most 512 bytes, not NUL-terminated.
 * Bytes after the first newline are ignored. Errors return NULL data/zero length.
 * Data is valid only during the callback. The callback may Close this object
 * and Start a new operation; never recursively run the event loop.
 * Completion releases descriptors, but the caller must still Close the object.
 * Close(NULL) is harmless; Close cancels without a callback.
 */
typedef struct NetworkExample NetworkExample;
typedef void (*NetworkExampleCallback)(int error, const unsigned char *data, size_t length, void *context);

NetworkExample *NetworkExample_Start(EventLoop *loop, const char *ipv4Address, uint16_t port, const unsigned char *request, size_t requestLength, unsigned int timeoutMs, NetworkExampleCallback callback, void *context);
void NetworkExample_Close(NetworkExample *example);

#endif
