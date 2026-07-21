// Tests for the waveform-capture ring statistics (capture_math.h)

#include <unity.h>
#include <capture_math.h>

void setUp() {}
void tearDown() {}

void test_ring_mean_simple_and_wrapped() {
  uint16_t ring[8] = {10, 20, 30, 40, 50, 60, 70, 80};
  // head=4: most recent 3 are 40,30,20
  TEST_ASSERT_EQUAL_UINT32(30, ringMean(ring, 8, 4, 3));
  // head=1: most recent 3 wrap: 10, 80, 70
  TEST_ASSERT_EQUAL_UINT32((10 + 80 + 70) / 3, ringMean(ring, 8, 1, 3));
  // n larger than ring clamps to whole ring
  TEST_ASSERT_EQUAL_UINT32(45, ringMean(ring, 8, 0, 100));
  TEST_ASSERT_EQUAL_UINT32(0, ringMean(ring, 8, 4, 0));
}

void test_decimate_bins_cover_min_and_max() {
  uint16_t ring[100];
  for (int i = 0; i < 100; i++) ring[i] = i * 10;
  uint16_t mn[4], mx[4];
  // most recent 80 samples ending at head=100%100=0 -> samples 20..99
  ringDecimate(ring, 100, 0, 80, 4, mn, mx);
  TEST_ASSERT_EQUAL_UINT16(200, mn[0]); // samples 20..39
  TEST_ASSERT_EQUAL_UINT16(390, mx[0]);
  TEST_ASSERT_EQUAL_UINT16(800, mn[3]); // samples 80..99
  TEST_ASSERT_EQUAL_UINT16(990, mx[3]);
}

void test_decimate_wraparound() {
  uint16_t ring[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  uint16_t mn[2], mx[2];
  // head=3: most recent 6 samples are indices 7,8,9,0,1,2
  ringDecimate(ring, 10, 3, 6, 2, mn, mx);
  TEST_ASSERT_EQUAL_UINT16(7, mn[0]);
  TEST_ASSERT_EQUAL_UINT16(9, mx[0]);
  TEST_ASSERT_EQUAL_UINT16(0, mn[1]);
  TEST_ASSERT_EQUAL_UINT16(2, mx[1]);
}

void test_decimate_more_bins_than_samples() {
  uint16_t ring[4] = {5, 6, 7, 8};
  uint16_t mn[8], mx[8];
  ringDecimate(ring, 4, 0, 4, 8, mn, mx); // bins share/repeat samples, no crash
  for (int b = 0; b < 8; b++) {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(5, mn[b]);
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(8, mx[b]);
    TEST_ASSERT_TRUE(mn[b] <= mx[b]);
  }
}

void test_decimate_envelope_preserves_peaks() {
  // A single spike must survive decimation (the point of min/max binning)
  uint16_t ring[1000];
  for (int i = 0; i < 1000; i++) ring[i] = 2000;
  ring[537] = 4000;
  uint16_t mn[10], mx[10];
  ringDecimate(ring, 1000, 0, 1000, 10, mn, mx);
  uint16_t peak = 0;
  for (int b = 0; b < 10; b++) {
    if (mx[b] > peak) peak = mx[b];
  }
  TEST_ASSERT_EQUAL_UINT16(4000, peak);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ring_mean_simple_and_wrapped);
  RUN_TEST(test_decimate_bins_cover_min_and_max);
  RUN_TEST(test_decimate_wraparound);
  RUN_TEST(test_decimate_more_bins_than_samples);
  RUN_TEST(test_decimate_envelope_preserves_peaks);
  return UNITY_END();
}
