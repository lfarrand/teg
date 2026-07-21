#ifndef GZIP_STREAM_H
#define GZIP_STREAM_H

// Streaming gunzip built on the vendored miniz inflate core: feed compressed
// bytes in arbitrary chunks, receive decompressed bytes via a callback.
// Parses the gzip container (header with optional FEXTRA/FNAME/FCOMMENT/FHCRC
// fields, deflate body, CRC32 + size trailer) and verifies the trailer CRC.
// No hardware dependencies - unit-tested natively.

#include <stdint.h>
#include <stddef.h>
#include "miniz.h"

using GzipEmitFn = bool (*)(void *ctx, const uint8_t *data, size_t len);

class GzipInflater {
 public:
  void begin();

  // Feed compressed bytes; emits decompressed bytes through emit(ctx,...).
  // Returns false on any error (bad container, corrupt deflate stream, or
  // emit() returning false to abort).
  bool feed(const uint8_t *data, size_t len, GzipEmitFn emit, void *ctx);

  // Call after the final feed: true if the stream ended cleanly with a
  // matching CRC32 trailer.
  bool finish();

  const char *error() const { return errorMsg; }

  // Peek helper: gzip files start 0x1F 0x8B
  static bool looksLikeGzip(const uint8_t *firstTwo) {
    return firstTwo[0] == 0x1F && firstTwo[1] == 0x8B;
  }

 private:
  enum State : uint8_t {
    HeaderFixed,
    HeaderExtraLen,
    HeaderExtraSkip,
    HeaderName,
    HeaderComment,
    HeaderCrc,
    Deflate,
    Trailer,
    Done,
    Failed,
  };

  bool fail(const char *msg) {
    state = Failed;
    errorMsg = msg;
    return false;
  }

  State state = HeaderFixed;
  const char *errorMsg = "";
  uint8_t headerFlags = 0;
  uint32_t fieldBytes = 0; // bytes consumed of the current variable field
  uint32_t fieldLen = 0;
  uint8_t scratch[10];

  tinfl_decompressor inflater;
  uint32_t dictOffset = 0;
  uint8_t dict[TINFL_LZ_DICT_SIZE];

  uint32_t crc = 0;
  uint32_t outTotal = 0;
  uint8_t trailer[8];
};

// ---------------------------------------------------------------------------
// Implementation (header-only so the native test build can use it without
// compiling src/*.cpp)
// ---------------------------------------------------------------------------

inline void GzipInflater::begin() {
  state = HeaderFixed;
  errorMsg = "";
  headerFlags = 0;
  fieldBytes = 0;
  fieldLen = 0;
  tinfl_init(&inflater);
  dictOffset = 0;
  crc = 0;
  outTotal = 0;
}

inline bool GzipInflater::feed(const uint8_t *data, size_t len, GzipEmitFn emit, void *ctx) {
  size_t pos = 0;
  while (pos < len) {
    switch (state) {
      case HeaderFixed:
        scratch[fieldBytes++] = data[pos++];
        if (fieldBytes == 10) {
          if (scratch[0] != 0x1F || scratch[1] != 0x8B) {
            return fail("not a gzip stream");
          }
          if (scratch[2] != 8) {
            return fail("unsupported gzip compression method");
          }
          headerFlags = scratch[3];
          fieldBytes = 0;
          if (headerFlags & 0x04) {
            state = HeaderExtraLen;
          } else if (headerFlags & 0x08) {
            state = HeaderName;
          } else if (headerFlags & 0x10) {
            state = HeaderComment;
          } else if (headerFlags & 0x02) {
            state = HeaderCrc;
          } else {
            state = Deflate;
          }
        }
        break;

      case HeaderExtraLen:
        scratch[fieldBytes++] = data[pos++];
        if (fieldBytes == 2) {
          fieldLen = scratch[0] | (scratch[1] << 8);
          fieldBytes = 0;
          state = fieldLen > 0 ? HeaderExtraSkip : ((headerFlags & 0x08) ? HeaderName
                                                    : (headerFlags & 0x10) ? HeaderComment
                                                    : (headerFlags & 0x02) ? HeaderCrc : Deflate);
        }
        break;

      case HeaderExtraSkip:
        pos++;
        if (++fieldBytes == fieldLen) {
          fieldBytes = 0;
          state = (headerFlags & 0x08) ? HeaderName
                : (headerFlags & 0x10) ? HeaderComment
                : (headerFlags & 0x02) ? HeaderCrc : Deflate;
        }
        break;

      case HeaderName:
      case HeaderComment: {
        const bool wasName = state == HeaderName;
        if (data[pos++] == 0) {
          state = wasName ? ((headerFlags & 0x10) ? HeaderComment
                             : (headerFlags & 0x02) ? HeaderCrc : Deflate)
                          : ((headerFlags & 0x02) ? HeaderCrc : Deflate);
        }
        break;
      }

      case HeaderCrc:
        pos++;
        if (++fieldBytes == 2) {
          fieldBytes = 0;
          state = Deflate;
        }
        break;

      case Deflate: {
        size_t inBytes = len - pos;
        size_t outBytes = TINFL_LZ_DICT_SIZE - dictOffset;
        const tinfl_status status =
          tinfl_decompress(&inflater, data + pos, &inBytes, dict, dict + dictOffset, &outBytes,
                           TINFL_FLAG_HAS_MORE_INPUT);
        pos += inBytes;
        if (outBytes > 0) {
          crc = static_cast<uint32_t>(
            mz_crc32(crc, dict + dictOffset, outBytes));
          outTotal += outBytes;
          if (!emit(ctx, dict + dictOffset, outBytes)) {
            return fail("aborted by consumer");
          }
          dictOffset = (dictOffset + outBytes) & (TINFL_LZ_DICT_SIZE - 1);
        }
        if (status == TINFL_STATUS_DONE) {
          fieldBytes = 0;
          state = Trailer;
        } else if (status < TINFL_STATUS_DONE) {
          return fail("corrupt deflate stream");
        }
        break;
      }

      case Trailer:
        if (fieldBytes < 8) {
          trailer[fieldBytes++] = data[pos++];
        }
        if (fieldBytes == 8) {
          const uint32_t expectCrc = static_cast<uint32_t>(trailer[0]) |
                                     (static_cast<uint32_t>(trailer[1]) << 8) |
                                     (static_cast<uint32_t>(trailer[2]) << 16) |
                                     (static_cast<uint32_t>(trailer[3]) << 24);
          if (expectCrc != crc) {
            return fail("gzip CRC mismatch");
          }
          state = Done;
        }
        break;

      case Done:
        pos = len; // ignore any padding after the member
        break;

      case Failed:
        return false;
    }
  }
  return true;
}

inline bool GzipInflater::finish() {
  if (state == Done) {
    return true;
  }
  if (state != Failed) {
    fail("truncated gzip stream");
  }
  return false;
}

#endif
