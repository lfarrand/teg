#include <unity.h>
#include <string.h>
#include "write_pin.h"

void setUp() {}
void tearDown() {}

// --- alphabet ---------------------------------------------------------------

void test_alphabet_has_exactly_32_symbols() {
  // Exactly 32 is what makes the low-5-bits mapping unbiased. If someone adds or
  // removes a character, the PIN silently stops being uniform.
  TEST_ASSERT_EQUAL_UINT8(WritePinAlphabetSize, strlen(WritePinAlphabet));
  TEST_ASSERT_EQUAL_UINT8(32, WritePinAlphabetSize);
}

void test_alphabet_excludes_ambiguous_characters() {
  // I, L, O and U are the ones misread off a small display or mistyped from a photo.
  TEST_ASSERT_NULL(strchr(WritePinAlphabet, 'I'));
  TEST_ASSERT_NULL(strchr(WritePinAlphabet, 'L'));
  TEST_ASSERT_NULL(strchr(WritePinAlphabet, 'O'));
  TEST_ASSERT_NULL(strchr(WritePinAlphabet, 'U'));
}

void test_alphabet_has_no_duplicates() {
  for (uint8_t i = 0; i < WritePinAlphabetSize; i++) {
    for (uint8_t j = i + 1; j < WritePinAlphabetSize; j++) {
      TEST_ASSERT_NOT_EQUAL(WritePinAlphabet[i], WritePinAlphabet[j]);
    }
  }
}

// --- rendering --------------------------------------------------------------

void test_renders_expected_length_and_terminates() {
  uint8_t entropy[WritePinLength] = {0};
  char pin[WritePinLength + 1];
  memset(pin, 'x', sizeof(pin));
  writePinFromEntropy(entropy, pin, sizeof(pin));
  TEST_ASSERT_EQUAL_UINT32(WritePinLength, strlen(pin));
  TEST_ASSERT_EQUAL_CHAR('\0', pin[WritePinLength]);
}

void test_mapping_uses_low_five_bits() {
  // Byte 0 -> first symbol, 31 -> last, and the high three bits are ignored so the
  // mapping stays uniform over a uniform byte source.
  uint8_t entropy[WritePinLength] = {0, 31, 32, 0xFF, 1, 0xE1, 5, 0x25};
  char pin[WritePinLength + 1];
  writePinFromEntropy(entropy, pin, sizeof(pin));
  TEST_ASSERT_EQUAL_CHAR(WritePinAlphabet[0], pin[0]);
  TEST_ASSERT_EQUAL_CHAR(WritePinAlphabet[31], pin[1]);
  TEST_ASSERT_EQUAL_CHAR(WritePinAlphabet[0], pin[2]);  // 32 & 31 == 0
  TEST_ASSERT_EQUAL_CHAR(WritePinAlphabet[31], pin[3]); // 255 & 31 == 31
  TEST_ASSERT_EQUAL_CHAR(WritePinAlphabet[1], pin[4]);
  TEST_ASSERT_EQUAL_CHAR(WritePinAlphabet[1], pin[5]);  // 0xE1 & 31 == 1
  TEST_ASSERT_EQUAL_CHAR(WritePinAlphabet[5], pin[6]);
  TEST_ASSERT_EQUAL_CHAR(WritePinAlphabet[5], pin[7]);  // 0x25 & 31 == 5
}

void test_every_byte_value_maps_into_the_alphabet() {
  // No input byte can produce a character outside the alphabet - which would be a
  // PIN the operator cannot type.
  for (int v = 0; v <= 255; v++) {
    uint8_t entropy[WritePinLength];
    memset(entropy, static_cast<uint8_t>(v), sizeof(entropy));
    char pin[WritePinLength + 1];
    writePinFromEntropy(entropy, pin, sizeof(pin));
    for (uint8_t i = 0; i < WritePinLength; i++) {
      TEST_ASSERT_NOT_NULL(strchr(WritePinAlphabet, pin[i]));
    }
  }
}

void test_mapping_is_unbiased_across_all_byte_values() {
  // Every symbol must be produced by exactly 8 of the 256 byte values. Anything else
  // means some PINs are likelier than others.
  int counts[WritePinAlphabetSize] = {0};
  for (int v = 0; v <= 255; v++) {
    uint8_t entropy[WritePinLength];
    memset(entropy, static_cast<uint8_t>(v), sizeof(entropy));
    char pin[WritePinLength + 1];
    writePinFromEntropy(entropy, pin, sizeof(pin));
    counts[strchr(WritePinAlphabet, pin[0]) - WritePinAlphabet]++;
  }
  for (uint8_t i = 0; i < WritePinAlphabetSize; i++) {
    TEST_ASSERT_EQUAL_INT(8, counts[i]);
  }
}

void test_short_buffer_truncates_rather_than_overruns() {
  uint8_t entropy[WritePinLength];
  memset(entropy, 0, sizeof(entropy));
  char pin[5];
  memset(pin, 'x', sizeof(pin));
  writePinFromEntropy(entropy, pin, sizeof(pin));
  TEST_ASSERT_EQUAL_UINT32(4, strlen(pin));
  TEST_ASSERT_EQUAL_CHAR('\0', pin[4]);
}

void test_degenerate_buffers_are_safe() {
  uint8_t entropy[WritePinLength] = {0};
  char pin[1] = {'x'};
  writePinFromEntropy(entropy, pin, 1);
  TEST_ASSERT_EQUAL_CHAR('\0', pin[0]);
  writePinFromEntropy(entropy, nullptr, 8);  // must not crash
  writePinFromEntropy(entropy, pin, 0);      // must not write
}

// --- recognising a generated PIN --------------------------------------------

void test_recognises_a_generated_pin() {
  uint8_t entropy[WritePinLength] = {1, 2, 3, 4, 5, 6, 7, 8};
  char pin[WritePinLength + 1];
  writePinFromEntropy(entropy, pin, sizeof(pin));
  TEST_ASSERT_TRUE(writePinLooksGenerated(pin));
}

void test_rejects_operator_chosen_pins() {
  TEST_ASSERT_FALSE(writePinLooksGenerated(""));          // empty
  TEST_ASSERT_FALSE(writePinLooksGenerated("1234"));      // too short
  TEST_ASSERT_FALSE(writePinLooksGenerated("123456789")); // too long
  TEST_ASSERT_FALSE(writePinLooksGenerated("hunter22")); // lower case, not in alphabet
  TEST_ASSERT_FALSE(writePinLooksGenerated("ABCDEFGI")); // right length, excluded char
  TEST_ASSERT_FALSE(writePinLooksGenerated(nullptr));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_alphabet_has_exactly_32_symbols);
  RUN_TEST(test_alphabet_excludes_ambiguous_characters);
  RUN_TEST(test_alphabet_has_no_duplicates);
  RUN_TEST(test_renders_expected_length_and_terminates);
  RUN_TEST(test_mapping_uses_low_five_bits);
  RUN_TEST(test_every_byte_value_maps_into_the_alphabet);
  RUN_TEST(test_mapping_is_unbiased_across_all_byte_values);
  RUN_TEST(test_short_buffer_truncates_rather_than_overruns);
  RUN_TEST(test_degenerate_buffers_are_safe);
  RUN_TEST(test_recognises_a_generated_pin);
  RUN_TEST(test_rejects_operator_chosen_pins);
  return UNITY_END();
}
