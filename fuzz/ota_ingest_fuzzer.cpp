#include <cstddef>
#include <cstdint>

#include "ota_ingest.h"

namespace {

bool invalidCallback;

void eraseSector(uint32_t address) {
  if (address < OtaBufferBase || address >= OtaBufferBase + OtaImageMax ||
      (address - OtaBufferBase) % OtaSectorSize != 0) {
    invalidCallback = true;
  }
}

void writeBytes(uint32_t address, const uint8_t *, uint32_t count) {
  if (count == 0 || count > 255 || address < OtaBufferBase ||
      address >= OtaBufferBase + OtaImageMax || count > OtaBufferBase + OtaImageMax - address) {
    invalidCallback = true;
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size > 64 * 1024) {
    return 0;
  }
  OtaIngest ingest;
  otaIngestInit(ingest);
  const OtaFlashOps ops{eraseSector, writeBytes};
  invalidCallback = false;
  for (size_t i = 0; i < size && ingest.error == nullptr; ++i) {
    otaFeedByte(ingest, static_cast<char>(data[i]), ops);
  }
  if (ingest.error == nullptr) {
    otaIngestFinish(ingest, ops);
  }
  if (invalidCallback || ingest.lineLen >= sizeof(ingest.line) ||
      ingest.bytesWritten > OtaImageMax) {
    __builtin_trap();
  }
  return 0;
}
