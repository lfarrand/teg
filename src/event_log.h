#ifndef EVENT_LOG_H
#define EVENT_LOG_H

// Timestamped event ring: fixed capacity, no allocation, monotonic sequence
// numbers so a client can poll for "everything since N" without missing or
// repeating entries. No hardware dependencies, unit-tested natively.
//
// Each entry carries both wall-clock seconds (0 when the clock has never
// been set) and the uptime millis at which it happened, so events recorded
// before the first time sync are still ordered and relatable.

#include <stdint.h>
#include <stddef.h>

constexpr uint32_t EventLogCapacity = 128;
constexpr uint32_t EventTextMax = 96;

enum EventLevel : uint8_t {
  EventInfo = 0,
  EventWarn = 1,
  EventError = 2,
};

struct EventEntry {
  uint32_t seq = 0;      // 1-based; 0 means "unused slot"
  uint32_t epoch = 0;    // unix seconds, 0 = clock not set when recorded
  uint32_t uptimeMs = 0;
  uint8_t level = EventInfo;
  char text[EventTextMax] = {};
};

struct EventLog {
  EventEntry entries[EventLogCapacity];
  uint32_t nextSeq = 1;
  uint32_t written = 0; // total ever recorded (>= entries retained)
};

inline void eventLogClear(EventLog &log) {
  for (uint32_t i = 0; i < EventLogCapacity; i++) {
    log.entries[i] = EventEntry{};
  }
  log.nextSeq = 1;
  log.written = 0;
}

inline void eventLogAdd(EventLog &log, uint8_t level, const char *text, uint32_t epoch,
                        uint32_t uptimeMs) {
  EventEntry &e = log.entries[log.written % EventLogCapacity];
  e.seq = log.nextSeq++;
  e.epoch = epoch;
  e.uptimeMs = uptimeMs;
  e.level = level;
  uint32_t i = 0;
  if (text != nullptr) {
    for (; i < EventTextMax - 1 && text[i] != '\0'; i++) {
      // Keep the payload JSON- and terminal-safe: control bytes become
      // spaces so a log line can never break the response it lands in
      const char c = text[i];
      e.text[i] = (c >= 0x20 && c < 0x7F) ? c : ' ';
    }
  }
  e.text[i] = '\0';
  log.written++;
}

// Oldest sequence number still retained (1 when nothing has been evicted)
inline uint32_t eventLogOldestSeq(const EventLog &log) {
  if (log.written <= EventLogCapacity) {
    return 1;
  }
  return log.written - EventLogCapacity + 1;
}

// Number of entries with seq > since that are still retained
inline uint32_t eventLogCountSince(const EventLog &log, uint32_t since) {
  const uint32_t newest = log.nextSeq - 1;
  if (newest == 0 || since >= newest) {
    return 0;
  }
  const uint32_t oldest = eventLogOldestSeq(log);
  const uint32_t from = since + 1 > oldest ? since + 1 : oldest;
  return newest - from + 1;
}

// Fetch the i-th retained entry with seq > since (i in [0, countSince)).
// Returns nullptr when out of range.
inline const EventEntry *eventLogAt(const EventLog &log, uint32_t since, uint32_t i) {
  if (i >= eventLogCountSince(log, since)) {
    return nullptr;
  }
  const uint32_t oldest = eventLogOldestSeq(log);
  const uint32_t from = since + 1 > oldest ? since + 1 : oldest;
  const uint32_t seq = from + i;
  return &log.entries[(seq - 1) % EventLogCapacity];
}

// True when the caller's cursor is older than anything retained, i.e. some
// entries were evicted before they could be read
inline bool eventLogGapSince(const EventLog &log, uint32_t since) {
  // No +1 on since: at UINT32_MAX that would wrap to 0 and report a
  // permanent phantom gap
  return since < eventLogOldestSeq(log) - 1;
}

inline uint32_t eventLogNewestSeq(const EventLog &log) {
  return log.nextSeq - 1;
}

inline const char *eventLevelName(uint8_t level) {
  switch (level) {
    case EventWarn: return "warn";
    case EventError: return "error";
    default: return "info";
  }
}

// ---------------------------------------------------------------------------
// Time formatting: unix epoch -> "YYYY-MM-DDTHH:MM:SSZ" without <time.h>
// (days-from-civil inverse, valid for the whole 32-bit epoch range)
// ---------------------------------------------------------------------------

struct CivilTime {
  int32_t year;
  uint8_t month; // 1-12
  uint8_t day;   // 1-31
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

inline CivilTime civilFromEpoch(uint32_t epoch) {
  const uint32_t daysPart = epoch / 86400u;
  uint32_t rem = epoch % 86400u;
  CivilTime t;
  t.hour = static_cast<uint8_t>(rem / 3600u);
  rem %= 3600u;
  t.minute = static_cast<uint8_t>(rem / 60u);
  t.second = static_cast<uint8_t>(rem % 60u);

  // Howard Hinnant's civil_from_days, shifted so the era starts 0000-03-01
  int64_t z = static_cast<int64_t>(daysPart) + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  t.day = static_cast<uint8_t>(doy - (153 * mp + 2) / 5 + 1);
  t.month = static_cast<uint8_t>(mp < 10 ? mp + 3 : mp - 9);
  t.year = static_cast<int32_t>(t.month <= 2 ? y + 1 : y);
  return t;
}

// Writes exactly "YYYY-MM-DDTHH:MM:SSZ" (20 chars + NUL). Returns false if
// the buffer is too small; epoch 0 yields an empty string (clock unset).
inline bool formatIso8601(char *out, size_t size, uint32_t epoch) {
  if (out == nullptr || size < 21) {
    return false;
  }
  if (epoch == 0) {
    out[0] = '\0';
    return true;
  }
  const CivilTime t = civilFromEpoch(epoch);
  auto two = [](char *p, uint32_t v) {
    p[0] = static_cast<char>('0' + (v / 10) % 10);
    p[1] = static_cast<char>('0' + v % 10);
  };
  uint32_t y = static_cast<uint32_t>(t.year);
  out[0] = static_cast<char>('0' + (y / 1000) % 10);
  out[1] = static_cast<char>('0' + (y / 100) % 10);
  out[2] = static_cast<char>('0' + (y / 10) % 10);
  out[3] = static_cast<char>('0' + y % 10);
  out[4] = '-';
  two(out + 5, t.month);
  out[7] = '-';
  two(out + 8, t.day);
  out[10] = 'T';
  two(out + 11, t.hour);
  out[13] = ':';
  two(out + 14, t.minute);
  out[16] = ':';
  two(out + 17, t.second);
  out[19] = 'Z';
  out[20] = '\0';
  return true;
}

#endif
