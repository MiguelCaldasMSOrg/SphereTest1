#include "storage_example.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <applibs/storage.h>

enum { RecordHeaderSize = 6, RecordSize = RecordHeaderSize + 128 };
static const unsigned char RecordMagic[4] = {'S', 'P', 'H', 'R'};

static int ReadBoundedFile(int fd, unsigned char *buffer, size_t capacity, size_t *length) {
  *length = 0;
  size_t used = 0;
  while (used < capacity) {
    ssize_t count = read(fd, buffer + used, capacity - used);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      return errno;
    }
    if (count == 0) {
      *length = used;
      return 0;
    }
    used += (size_t)count;
  }

  /* One extra byte distinguishes an exact fit from a truncated record. */
  unsigned char extra;
  ssize_t count;
  do {
    count = read(fd, &extra, sizeof(extra));
  } while (count < 0 && errno == EINTR);
  if (count < 0) {
    return errno;
  }
  if (count != 0) {
    return EMSGSIZE;
  }
  *length = used;
  return 0;
}

static int ReadMutableRecord(int fd, StorageExampleRequest *request) {
  unsigned char record[RecordSize];
  size_t size;
  int error = ReadBoundedFile(fd, record, sizeof(record), &size);
  request->length = 0;
  if (error != 0 || size == 0) {
    return error;
  }
  if (size != sizeof(record) || memcmp(record, RecordMagic, sizeof(RecordMagic)) != 0 || record[4] != 1U || record[5] > sizeof(request->data)) {
    return EBADMSG;
  }
  size_t length = record[5];
  for (size_t i = RecordHeaderSize + length; i < sizeof(record); ++i) {
    if (record[i] != 0U) {
      return EBADMSG;
    }
  }
  memcpy(request->data, record + RecordHeaderSize, length);
  request->length = length;
  return 0;
}

static int WriteMutableRecord(int fd, const StorageExampleRequest *request) {
  /* Refuse to overwrite a malformed, truncated, oversized or unrelated file. */
  StorageExampleRequest previous = {0};
  int error = ReadMutableRecord(fd, &previous);
  if (error != 0) {
    return error;
  }

  off_t offset;
  do {
    offset = lseek(fd, 0, SEEK_SET);
  } while (offset < 0 && errno == EINTR);
  if (offset < 0) {
    return errno;
  }

  /* Encode bytes explicitly: no C struct padding or endianness dependence. */
  unsigned char record[RecordSize] = {0};
  memcpy(record, RecordMagic, sizeof(RecordMagic));
  record[4] = 1U;
  record[5] = (unsigned char)request->length;
  memcpy(record + RecordHeaderSize, request->data, request->length);

  size_t written = 0;
  while (written < sizeof(record)) {
    ssize_t count = write(fd, record + written, sizeof(record) - written);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      return errno;
    }
    if (count == 0) {
      return EIO;
    }
    written += (size_t)count;
  }

  int result;
  do {
    result = fsync(fd);
  } while (result < 0 && errno == EINTR);
  return result < 0 ? errno : 0;
}

int StorageExample_Transfer(void *context) {
  StorageExampleRequest *request = context;
  if (request == NULL) {
    return EINVAL;
  }
  if (!request->write) {
    request->length = 0;
  }
  if (request->packagedFile && request->write) {
    return EROFS;
  }
  if (request->write && request->length > sizeof(request->data)) {
    return EMSGSIZE;
  }
  if (request->packagedFile && (request->resourcePath == NULL || request->resourcePath[0] == '\0')) {
    return EINVAL;
  }

  int fd;
  do {
    fd = request->packagedFile ? Storage_OpenFileInImagePackage(request->resourcePath) : Storage_OpenMutableFile();
  } while (fd < 0 && errno == EINTR);
  if (fd < 0) {
    return errno;
  }

  int error;
  if (request->packagedFile) {
    error = ReadBoundedFile(fd, request->data, sizeof(request->data), &request->length);
  } else {
    error = request->write ? WriteMutableRecord(fd, request) : ReadMutableRecord(fd, request);
  }
  /* Linux releases the fd on close errors including EINTR; do not retry. */
  if (close(fd) < 0 && error == 0) {
    error = errno;
  }
  if (error != 0 && !request->write) {
    request->length = 0;
  }
  return error;
}
