#ifndef SPHERE_EXAMPLES_STORAGE_EXAMPLE_H
#define SPHERE_EXAMPLES_STORAGE_EXAMPLE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct StorageExampleRequest {
  bool write;
  bool packagedFile;
  const char *resourcePath;
  unsigned char data[128];
  size_t length;
} StorageExampleRequest;

/*
 * AsyncJob work function: returns 0 or a positive errno-style error.
 * Applibs Storage calls and all file I/O are synchronous, owned exclusively by
 * this worker. Serialize all storage jobs; do not use Storage APIs elsewhere
 * concurrently. Keep the request and resourcePath alive and untouched until
 * completion. No callback, EventLoop API, or caller logging runs in this worker.
 *
 * Packaged files are read-only; packagedFile + write returns EROFS.
 * resourcePath is a logical image-package path, using Sphere OS forward slashes,
 * for example "examples/resources/message.txt", not a Windows filesystem path.
 * Packaged reads return their actual byte count in length (not NUL-terminated);
 * files larger than data return EMSGSIZE instead of silently truncating.
 * On any read error, length is 0 and data is not valid.
 *
 * Mutable storage ignores resourcePath and uses only Storage_OpenMutableFile:
 * the app's single, dedicated DEMO record, never an arbitrary write path.
 * Opening it creates an empty file if absent; reading an empty file succeeds
 * with length 0. Mutable data/length describe the LOGICAL payload, not the file.
 *
 * The file is always 134 bytes after a successful write:
 *   bytes 0..3: ASCII "SPHR"; byte 4: version 1; byte 5: payload length 0..128;
 *   bytes 6..133: the payload, with all unused bytes zero.
 * Fields are encoded byte-by-byte, without struct padding or byte-order issues.
 * Invalid magic/version/length/padding or a truncated record returns EBADMSG;
 * excess file bytes return EMSGSIZE. Writes also validate an existing record
 * before modifying it. A corrupt record is not silently treated as empty.
 *
 * Writes require length <= 128, seek to zero, write the WHOLE fixed-size record,
 * handle partial writes/EINTR, then fsync. Shorter payloads are supported without
 * ftruncate/O_TRUNC (neither is available in SDK 18) or destructive deletion.
 * Overwriting is NOT power-failure atomic; errors can leave a partial record.
 * Valid framing is not a checksum and cannot rule out a torn/corrupted payload.
 * Use only for this opt-in demo, not valuable data or high-frequency writes.
 */
int StorageExample_Transfer(void *context);

#endif
