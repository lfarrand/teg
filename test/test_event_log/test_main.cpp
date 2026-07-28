// Tests for the event ring and time formatting (event_log.h)

#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <event_log.h>

void setUp() {}
void tearDown() {}

static EventLog elog;

static void addN(uint32_t n, uint32_t startEpoch = 0) {
  char buf[32];
  for (uint32_t i = 0; i < n; i++) {
    snprintf(buf, sizeof(buf), "event %u", static_cast<unsigned>(i));
    eventLogAdd(elog, EventInfo, buf, startEpoch ? startEpoch + i : 0, i * 10);
  }
}

void test_empty_log() {
  eventLogClear(elog);
  TEST_ASSERT_EQUAL_UINT32(0, eventLogCountSince(elog, 0));
  TEST_ASSERT_NULL(eventLogAt(elog, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(1, eventLogOldestSeq(elog));
  TEST_ASSERT_FALSE(eventLogGapSince(elog, 0));
}

void test_sequence_numbers_and_since() {
  eventLogClear(elog);
  addN(5);
  TEST_ASSERT_EQUAL_UINT32(5, eventLogCountSince(elog, 0));
  TEST_ASSERT_EQUAL_UINT32(1, eventLogAt(elog, 0, 0)->seq);
  TEST_ASSERT_EQUAL_STRING("event 0", eventLogAt(elog, 0, 0)->text);
  TEST_ASSERT_EQUAL_UINT32(5, eventLogAt(elog, 0, 4)->seq);

  // A cursor at 3 yields only 4 and 5
  TEST_ASSERT_EQUAL_UINT32(2, eventLogCountSince(elog, 3));
  TEST_ASSERT_EQUAL_UINT32(4, eventLogAt(elog, 3, 0)->seq);
  TEST_ASSERT_EQUAL_UINT32(5, eventLogAt(elog, 3, 1)->seq);
  TEST_ASSERT_NULL(eventLogAt(elog, 3, 2));

  // Caught up, and a cursor from the future
  TEST_ASSERT_EQUAL_UINT32(0, eventLogCountSince(elog, 5));
  TEST_ASSERT_EQUAL_UINT32(0, eventLogCountSince(elog, 99));
}

void test_poll_loop_never_repeats_or_skips() {
  // The client contract: poll with the last seq seen, append what comes back
  eventLogClear(elog);
  uint32_t cursor = 0;
  uint32_t seen = 0;
  for (int round = 0; round < 10; round++) {
    addN(7);
    const uint32_t n = eventLogCountSince(elog, cursor);
    for (uint32_t i = 0; i < n; i++) {
      const EventEntry *e = eventLogAt(elog, cursor, i);
      TEST_ASSERT_NOT_NULL(e);
      TEST_ASSERT_EQUAL_UINT32(seen + i + 1, e->seq); // strictly consecutive
    }
    seen += n;
    if (n > 0) {
      cursor = eventLogAt(elog, cursor, n - 1)->seq;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(70, seen); // 10 rounds x 7, nothing lost
}

void test_wrap_evicts_oldest() {
  eventLogClear(elog);
  addN(EventLogCapacity + 10);
  // Only the capacity most recent survive
  TEST_ASSERT_EQUAL_UINT32(EventLogCapacity, eventLogCountSince(elog, 0));
  TEST_ASSERT_EQUAL_UINT32(11, eventLogOldestSeq(elog));
  const EventEntry *first = eventLogAt(elog, 0, 0);
  TEST_ASSERT_EQUAL_UINT32(11, first->seq);
  TEST_ASSERT_EQUAL_STRING("event 10", first->text);
  const EventEntry *last = eventLogAt(elog, 0, EventLogCapacity - 1);
  TEST_ASSERT_EQUAL_UINT32(EventLogCapacity + 10, last->seq);
}

void test_gap_detection_after_eviction() {
  eventLogClear(elog);
  addN(10);
  TEST_ASSERT_FALSE(eventLogGapSince(elog, 5)); // 6..10 still retained
  addN(EventLogCapacity + 5);                  // evict well past seq 5
  TEST_ASSERT_TRUE(eventLogGapSince(elog, 5));
  // A stale cursor still returns the retained window, starting at the oldest
  const uint32_t n = eventLogCountSince(elog, 5);
  TEST_ASSERT_EQUAL_UINT32(EventLogCapacity, n);
  TEST_ASSERT_EQUAL_UINT32(eventLogOldestSeq(elog), eventLogAt(elog, 5, 0)->seq);
}

void test_text_truncation_and_sanitizing() {
  eventLogClear(elog);
  char longText[EventTextMax + 50];
  memset(longText, 'x', sizeof(longText) - 1);
  longText[sizeof(longText) - 1] = '\0';
  eventLogAdd(elog, EventWarn, longText, 0, 0);
  const EventEntry *e = eventLogAt(elog, 0, 0);
  TEST_ASSERT_EQUAL_UINT32(EventTextMax - 1, strlen(e->text));
  TEST_ASSERT_EQUAL_UINT8(EventWarn, e->level);

  // Control bytes and quotes that would corrupt a JSON response
  eventLogAdd(elog, EventError, "a\nb\tc\x01""d", 0, 0);
  const EventEntry *e2 = eventLogAt(elog, 1, 0);
  TEST_ASSERT_EQUAL_STRING("a b c d", e2->text);
  TEST_ASSERT_EQUAL_UINT8(EventError, e2->level);

  eventLogAdd(elog, EventInfo, nullptr, 0, 0);
  TEST_ASSERT_EQUAL_STRING("", eventLogAt(elog, 2, 0)->text);
}

void test_gap_flag_does_not_overflow_at_max_cursor() {
  // since = UINT32_MAX must report no gap and no events, not wrap to 0 and
  // claim a permanent gap
  eventLogClear(elog);
  addN(5);
  TEST_ASSERT_FALSE(eventLogGapSince(elog, 0xFFFFFFFFu));
  TEST_ASSERT_EQUAL_UINT32(0, eventLogCountSince(elog, 0xFFFFFFFFu));
  addN(EventLogCapacity + 20); // force eviction
  TEST_ASSERT_FALSE(eventLogGapSince(elog, 0xFFFFFFFFu));
  // A cursor exactly at the oldest retained entry is not a gap
  TEST_ASSERT_FALSE(eventLogGapSince(elog, eventLogOldestSeq(elog) - 1));
  TEST_ASSERT_TRUE(eventLogGapSince(elog, eventLogOldestSeq(elog) - 2));
}

void test_newest_seq() {
  eventLogClear(elog);
  TEST_ASSERT_EQUAL_UINT32(0, eventLogNewestSeq(elog));
  addN(7);
  TEST_ASSERT_EQUAL_UINT32(7, eventLogNewestSeq(elog));
}

void test_level_names() {
  TEST_ASSERT_EQUAL_STRING("info", eventLevelName(EventInfo));
  TEST_ASSERT_EQUAL_STRING("warn", eventLevelName(EventWarn));
  TEST_ASSERT_EQUAL_STRING("error", eventLevelName(EventError));
  TEST_ASSERT_EQUAL_STRING("info", eventLevelName(99));
}

void test_timestamps_recorded() {
  eventLogClear(elog);
  eventLogAdd(elog, EventInfo, "boot", 0, 1234);       // before a time sync
  eventLogAdd(elog, EventInfo, "synced", 1700000000, 5678);
  TEST_ASSERT_EQUAL_UINT32(0, eventLogAt(elog, 0, 0)->epoch);
  TEST_ASSERT_EQUAL_UINT32(1234, eventLogAt(elog, 0, 0)->uptimeMs);
  TEST_ASSERT_EQUAL_UINT32(1700000000, eventLogAt(elog, 0, 1)->epoch);
}

void test_iso8601_formatting() {
  char buf[24];
  // Unix epoch itself
  TEST_ASSERT_TRUE(formatIso8601(buf, sizeof(buf), 1));
  TEST_ASSERT_EQUAL_STRING("1970-01-01T00:00:01Z", buf);
  // A known instant: 2023-11-14T22:13:20Z
  TEST_ASSERT_TRUE(formatIso8601(buf, sizeof(buf), 1700000000));
  TEST_ASSERT_EQUAL_STRING("2023-11-14T22:13:20Z", buf);
  // Leap day
  TEST_ASSERT_TRUE(formatIso8601(buf, sizeof(buf), 1709208000));
  TEST_ASSERT_EQUAL_STRING("2024-02-29T12:00:00Z", buf);
  // Just before a year boundary
  TEST_ASSERT_TRUE(formatIso8601(buf, sizeof(buf), 1735689599));
  TEST_ASSERT_EQUAL_STRING("2024-12-31T23:59:59Z", buf);
  // And just after
  TEST_ASSERT_TRUE(formatIso8601(buf, sizeof(buf), 1735689600));
  TEST_ASSERT_EQUAL_STRING("2025-01-01T00:00:00Z", buf);
  // Beyond the 2038 signed-32-bit rollover (we use unsigned)
  TEST_ASSERT_TRUE(formatIso8601(buf, sizeof(buf), 2147483648u));
  TEST_ASSERT_EQUAL_STRING("2038-01-19T03:14:08Z", buf);
  // Top of the unsigned range
  TEST_ASSERT_TRUE(formatIso8601(buf, sizeof(buf), 4294967295u));
  TEST_ASSERT_EQUAL_STRING("2106-02-07T06:28:15Z", buf);
}

void test_iso8601_edge_cases() {
  char buf[24];
  // Clock never set: empty string rather than a misleading 1970 date
  TEST_ASSERT_TRUE(formatIso8601(buf, sizeof(buf), 0));
  TEST_ASSERT_EQUAL_STRING("", buf);
  // Too small a buffer fails instead of truncating
  char small[20];
  TEST_ASSERT_FALSE(formatIso8601(small, sizeof(small), 1700000000));
  TEST_ASSERT_FALSE(formatIso8601(nullptr, 24, 1700000000));
}

void test_civil_conversion_matches_known_dates() {
  struct { uint32_t epoch; int32_t y; uint8_t mo, d, h, mi, s; } cases[] = {
    {946684800u, 2000, 1, 1, 0, 0, 0},    // Y2K
    {951782400u, 2000, 2, 29, 0, 0, 0},   // 2000 was a leap year
    {1078012800u, 2004, 2, 29, 0, 0, 0},
    {1583020800u, 2020, 3, 1, 0, 0, 0},
    {1609459200u, 2021, 1, 1, 0, 0, 0},
  };
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    const CivilTime t = civilFromEpoch(cases[i].epoch);
    TEST_ASSERT_EQUAL_INT32(cases[i].y, t.year);
    TEST_ASSERT_EQUAL_UINT8(cases[i].mo, t.month);
    TEST_ASSERT_EQUAL_UINT8(cases[i].d, t.day);
    TEST_ASSERT_EQUAL_UINT8(cases[i].h, t.hour);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_log);
  RUN_TEST(test_sequence_numbers_and_since);
  RUN_TEST(test_poll_loop_never_repeats_or_skips);
  RUN_TEST(test_wrap_evicts_oldest);
  RUN_TEST(test_gap_detection_after_eviction);
  RUN_TEST(test_text_truncation_and_sanitizing);
  RUN_TEST(test_gap_flag_does_not_overflow_at_max_cursor);
  RUN_TEST(test_newest_seq);
  RUN_TEST(test_level_names);
  RUN_TEST(test_timestamps_recorded);
  RUN_TEST(test_iso8601_formatting);
  RUN_TEST(test_iso8601_edge_cases);
  RUN_TEST(test_civil_conversion_matches_known_dates);
  return UNITY_END();
}
