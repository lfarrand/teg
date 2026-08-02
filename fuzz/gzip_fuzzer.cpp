#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "gzip_stream.h"

namespace {

struct Sink {
  size_t total = 0;
};

bool boundedSink(void *context, const uint8_t *, size_t length) {
  auto *sink = static_cast<Sink *>(context);
  if (length > 1024 * 1024 - std::min(sink->total, size_t{1024 * 1024})) {
    return false;
  }
  sink->total += length;
  return true;
}

constexpr uint8_t ValidGzip[] = {
  31,139,8,0,0,0,0,0,2,10,43,169,44,72,181,45,74,77,75,45,74,205,75,78,229,50,
  208,51,229,210,5,17,134,122,6,92,0,23,89,48,82,28,0,0,0
};

void feedInChunks(const uint8_t *data, size_t size, const uint8_t *chunkData, size_t chunkSize) {
  GzipInflater inflater;
  Sink sink;
  inflater.begin();
  size_t offset = 0;
  size_t selector = 0;
  while (offset < size) {
    const size_t requested = chunkSize == 0 ? size : 1 + chunkData[selector++ % chunkSize] % 97;
    const size_t count = std::min(requested, size - offset);
    if (!inflater.feed(data + offset, count, boundedSink, &sink)) {
      break;
    }
    offset += count;
  }
  inflater.finish();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size > 64 * 1024) {
    return 0;
  }
  feedInChunks(data, size, data, size);
  // Keep all valid container/deflate/trailer states covered while fuzz bytes
  // vary the network chunk boundaries.
  feedInChunks(ValidGzip, sizeof(ValidGzip), data, size);
  return 0;
}
