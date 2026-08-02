#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "waveform_parse.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size > 64 * 1024) {
    return 0;
  }

  std::string input;
  if (size != 0) {
    input.assign(reinterpret_cast<const char *>(data), size);
  }
  input.push_back('\0');
  std::array<int16_t, 256> samples{};
  std::array<int16_t, MaxWaveSegments> levels{};
  std::array<uint32_t, MaxWaveSegments> durations{};
  WaveParser parser;
  parser.samples = samples.data();
  parser.maxSamples = samples.size();
  parser.segLevelsQ15 = levels.data();
  parser.segMicros = durations.data();
  parser.maxSegments = levels.size();

  const char *cursor = input.data();
  const char *limit = input.data() + size;
  while (cursor < limit) {
    const char *end = cursor;
    while (end < limit && *end != '\n') {
      ++end;
    }
    if (waveParseLine(parser, cursor, end) != 0) {
      break;
    }
    cursor = end < limit ? end + 1 : end;
  }
  waveParseFinish(parser);

  if (size >= WaveBinaryHeaderSize) {
    uint8_t type = WaveTypeNone;
    waveBinaryHeaderRead(data, &type);
  }
  return 0;
}
