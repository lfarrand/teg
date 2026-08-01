// Tests for the SD-streaming double-buffer scheduler (stream_ring.h)

#include <unity.h>
#include <stream_ring.h>

static int16_t bufA[4];
static int16_t bufB[4];
static const int16_t *const bufs[2] = {bufA, bufB};
static StreamRing ring;

void setUp() {
  ring.reset();
  for (int i = 0; i < 4; i++) {
    bufA[i] = static_cast<int16_t>(100 + i);
    bufB[i] = static_cast<int16_t>(200 + i);
  }
}
void tearDown() {}

void test_consumes_and_flips_buffers() {
  ring.markFilled(0, 4);
  ring.markFilled(1, 4);
  // Drain buffer 0
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_EQUAL_INT16(100 + i, ring.next(bufs));
  }
  TEST_ASSERT_FALSE(ring.refillNeeded(0));
  // Next sample flips to buffer 1 and flags 0 for refill
  TEST_ASSERT_EQUAL_INT16(200, ring.next(bufs));
  TEST_ASSERT_TRUE(ring.refillNeeded(0));
  TEST_ASSERT_EQUAL_UINT32(0, ring.underruns);
}

void test_refill_during_playback_keeps_flowing() {
  ring.markFilled(0, 2);
  ring.markFilled(1, 2);
  TEST_ASSERT_EQUAL_INT16(100, ring.next(bufs));
  TEST_ASSERT_EQUAL_INT16(101, ring.next(bufs));
  TEST_ASSERT_EQUAL_INT16(200, ring.next(bufs)); // flipped; 0 flagged
  bufA[0] = 111; // loop refills buffer 0 while 1 plays
  ring.markFilled(0, 1);
  TEST_ASSERT_EQUAL_INT16(201, ring.next(bufs));
  TEST_ASSERT_EQUAL_INT16(111, ring.next(bufs)); // flipped back to refreshed 0
  TEST_ASSERT_EQUAL_UINT32(0, ring.underruns);
}

void test_underrun_holds_last_sample_and_recovers() {
  ring.markFilled(0, 2);
  // Buffer 1 never filled: after draining 0 we underrun and hold
  TEST_ASSERT_EQUAL_INT16(100, ring.next(bufs));
  TEST_ASSERT_EQUAL_INT16(101, ring.next(bufs));
  TEST_ASSERT_EQUAL_INT16(101, ring.next(bufs)); // hold
  TEST_ASSERT_EQUAL_INT16(101, ring.next(bufs)); // still holding
  TEST_ASSERT_EQUAL_UINT32(2, ring.underruns);
  TEST_ASSERT_TRUE(ring.refillNeeded(0));
  TEST_ASSERT_TRUE(ring.refillNeeded(1));
  // Recovery once the loop refills
  ring.markFilled(1, 2);
  TEST_ASSERT_EQUAL_INT16(200, ring.next(bufs));
  TEST_ASSERT_EQUAL_INT16(201, ring.next(bufs));
}

void test_reset_clears_everything() {
  ring.markFilled(0, 4);
  ring.next(bufs);
  ring.reset();
  TEST_ASSERT_EQUAL_UINT16(0, ring.validCount(0));
  TEST_ASSERT_EQUAL_UINT32(0, ring.underruns);
  TEST_ASSERT_EQUAL_INT16(0, ring.next(bufs)); // empty: holds the reset value
  TEST_ASSERT_EQUAL_UINT32(1, ring.underruns);
}

void test_refill_claim_prevents_duplicate_producers() {
  ring.markFilled(0, 1);
  ring.next(bufs);
  ring.next(bufs); // exhausts 0 and requests both empty buffers
  TEST_ASSERT_TRUE(ring.beginRefill(0));
  TEST_ASSERT_FALSE(ring.beginRefill(0));
  ring.markFilled(0, 2);
  TEST_ASSERT_EQUAL_UINT16(2, ring.validCount(0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_consumes_and_flips_buffers);
  RUN_TEST(test_refill_during_playback_keeps_flowing);
  RUN_TEST(test_underrun_holds_last_sample_and_recovers);
  RUN_TEST(test_reset_clears_everything);
  RUN_TEST(test_refill_claim_prevents_duplicate_producers);
  return UNITY_END();
}
