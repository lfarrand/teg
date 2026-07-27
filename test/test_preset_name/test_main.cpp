// Tests for preset name validation and path building (preset_name.h).
// These names arrive from the network and become filenames, so the
// rejection cases are a security boundary, not cosmetics.

#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <preset_name.h>

void setUp() {}
void tearDown() {}

void test_ordinary_names_accepted() {
  TEST_ASSERT_TRUE(presetNameValid("default"));
  TEST_ASSERT_TRUE(presetNameValid("Night mode"));
  TEST_ASSERT_TRUE(presetNameValid("50Hz_grid-tie"));
  TEST_ASSERT_TRUE(presetNameValid("test.1"));
  TEST_ASSERT_TRUE(presetNameValid("A"));
  // Exactly at the length limit
  TEST_ASSERT_TRUE(presetNameValid("12345678901234567890123456789012"));
}

void test_path_traversal_rejected() {
  TEST_ASSERT_FALSE(presetNameValid(".."));
  TEST_ASSERT_FALSE(presetNameValid("../settings"));
  TEST_ASSERT_FALSE(presetNameValid("../../settings.cfg"));
  TEST_ASSERT_FALSE(presetNameValid("a/../../b"));
  TEST_ASSERT_FALSE(presetNameValid("sub/name"));
  TEST_ASSERT_FALSE(presetNameValid("sub\\name"));
  TEST_ASSERT_FALSE(presetNameValid("/settings"));
  TEST_ASSERT_FALSE(presetNameValid("C:name"));
  TEST_ASSERT_FALSE(presetNameValid("name..json"));
}

void test_hidden_and_edge_forms_rejected() {
  TEST_ASSERT_FALSE(presetNameValid(""));
  TEST_ASSERT_FALSE(presetNameValid(nullptr));
  TEST_ASSERT_FALSE(presetNameValid(".hidden"));
  TEST_ASSERT_FALSE(presetNameValid(" leading"));
  TEST_ASSERT_FALSE(presetNameValid("trailing "));
  TEST_ASSERT_FALSE(presetNameValid("trailing."));
  // One over the limit
  TEST_ASSERT_FALSE(presetNameValid("123456789012345678901234567890123"));
}

void test_dangerous_characters_rejected() {
  const char *bad[] = {"na*me", "na?me", "na\"me", "na<me", "na>me", "na|me",
                       "na:me", "na\tme", "na\nme", "na\rme", "na;me", "na$me"};
  for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    TEST_ASSERT_FALSE(presetNameValid(bad[i]));
  }
}

void test_path_building() {
  char path[PresetPathMax];
  TEST_ASSERT_TRUE(presetPath(path, sizeof(path), "Night mode"));
  TEST_ASSERT_EQUAL_STRING("/presets/Night mode.json", path);
  TEST_ASSERT_TRUE(presetPath(path, sizeof(path), "a"));
  TEST_ASSERT_EQUAL_STRING("/presets/a.json", path);
}

void test_path_building_refuses_invalid_and_small_buffers() {
  char path[PresetPathMax];
  TEST_ASSERT_FALSE(presetPath(path, sizeof(path), "../escape"));
  TEST_ASSERT_FALSE(presetPath(path, sizeof(path), ""));
  // A buffer too small must fail rather than truncate into a wrong path
  char tiny[12];
  TEST_ASSERT_FALSE(presetPath(tiny, sizeof(tiny), "12345678901234567890"));
  // The declared maximum always fits
  TEST_ASSERT_TRUE(presetPath(path, sizeof(path), "12345678901234567890123456789012"));
  TEST_ASSERT_EQUAL_STRING("/presets/12345678901234567890123456789012.json", path);
}

void test_name_from_filename() {
  char name[PresetNameMax + 1];
  TEST_ASSERT_TRUE(presetNameFromFile(name, sizeof(name), "Night mode.json"));
  TEST_ASSERT_EQUAL_STRING("Night mode", name);
  TEST_ASSERT_TRUE(presetNameFromFile(name, sizeof(name), "a.json"));
  TEST_ASSERT_EQUAL_STRING("a", name);

  // Not presets
  TEST_ASSERT_FALSE(presetNameFromFile(name, sizeof(name), "settings.cfg"));
  TEST_ASSERT_FALSE(presetNameFromFile(name, sizeof(name), ".json"));
  TEST_ASSERT_FALSE(presetNameFromFile(name, sizeof(name), "json"));
  TEST_ASSERT_FALSE(presetNameFromFile(name, sizeof(name), ""));
  TEST_ASSERT_FALSE(presetNameFromFile(name, sizeof(name), nullptr));
  // A file whose stem would be an invalid name is not listed
  TEST_ASSERT_FALSE(presetNameFromFile(name, sizeof(name), ".hidden.json"));
  // Too long to fit the output buffer
  TEST_ASSERT_FALSE(presetNameFromFile(
    name, sizeof(name), "123456789012345678901234567890123456789.json"));
}

void test_high_bytes_rejected_regardless_of_char_signedness() {
  // char is signed on the host and on ARM by default: a naive range check
  // can misclassify 0x80-0xFF. Sweep every non-ASCII byte.
  for (int b = 0x80; b <= 0xFF; b++) {
    char name[4] = {'a', static_cast<char>(b), 'b', '\0'};
    TEST_ASSERT_FALSE(presetNameValid(name));
  }
  // And every control byte
  for (int b = 1; b < 0x20; b++) {
    char name[4] = {'a', static_cast<char>(b), 'b', '\0'};
    TEST_ASSERT_FALSE(presetNameValid(name));
  }
  // Exactly the printable set we intend, and nothing else
  int accepted = 0;
  for (int b = 0x20; b < 0x7F; b++) {
    char name[4] = {'a', static_cast<char>(b), 'b', '\0'};
    if (presetNameValid(name)) {
      accepted++;
    }
  }
  // 26 + 26 + 10 letters/digits, plus space, underscore, hyphen, dot
  TEST_ASSERT_EQUAL_INT(66, accepted);
}

void test_case_insensitive_comparison() {
  // FAT resolves names case-insensitively; the fold must agree with it so
  // a case-variant save cannot silently destroy an existing preset
  TEST_ASSERT_TRUE(presetNamesEqualFold("Night mode", "night mode"));
  TEST_ASSERT_TRUE(presetNamesEqualFold("NIGHT", "night"));
  TEST_ASSERT_TRUE(presetNamesEqualFold("a", "A"));
  TEST_ASSERT_FALSE(presetNamesEqualFold("night", "nights"));
  TEST_ASSERT_FALSE(presetNamesEqualFold("nights", "night"));
  TEST_ASSERT_FALSE(presetNamesEqualFold("day", "night"));
  TEST_ASSERT_FALSE(presetNamesEqualFold(nullptr, "night"));
  TEST_ASSERT_TRUE(presetNamesEqualFold("", ""));
}

void test_uppercase_extension_listed() {
  // A card written on another machine may hold NAME.JSON; SdFat opens it
  // either way, so the listing must recognize it too
  char name[PresetNameMax + 1];
  TEST_ASSERT_TRUE(presetNameFromFile(name, sizeof(name), "Night mode.JSON"));
  TEST_ASSERT_EQUAL_STRING("Night mode", name);
  TEST_ASSERT_TRUE(presetNameFromFile(name, sizeof(name), "day.Json"));
  TEST_ASSERT_EQUAL_STRING("day", name);
}

void test_rejected_entry_leaves_output_untouched() {
  char name[PresetNameMax + 1];
  snprintf(name, sizeof(name), "untouched");
  TEST_ASSERT_FALSE(presetNameFromFile(name, sizeof(name), ".hidden.json"));
  TEST_ASSERT_EQUAL_STRING("untouched", name);
  TEST_ASSERT_FALSE(presetNameFromFile(name, sizeof(name), "settings.cfg"));
  TEST_ASSERT_EQUAL_STRING("untouched", name);
}

void test_round_trip() {
  const char *names[] = {"default", "Night mode", "50Hz_grid-tie", "test.1"};
  for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    char path[PresetPathMax];
    TEST_ASSERT_TRUE(presetPath(path, sizeof(path), names[i]));
    // Strip the directory the way a directory listing would deliver it
    const char *base = strrchr(path, '/') + 1;
    char back[PresetNameMax + 1];
    TEST_ASSERT_TRUE(presetNameFromFile(back, sizeof(back), base));
    TEST_ASSERT_EQUAL_STRING(names[i], back);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ordinary_names_accepted);
  RUN_TEST(test_path_traversal_rejected);
  RUN_TEST(test_hidden_and_edge_forms_rejected);
  RUN_TEST(test_dangerous_characters_rejected);
  RUN_TEST(test_path_building);
  RUN_TEST(test_path_building_refuses_invalid_and_small_buffers);
  RUN_TEST(test_name_from_filename);
  RUN_TEST(test_high_bytes_rejected_regardless_of_char_signedness);
  RUN_TEST(test_case_insensitive_comparison);
  RUN_TEST(test_uppercase_extension_listed);
  RUN_TEST(test_rejected_entry_leaves_output_untouched);
  RUN_TEST(test_round_trip);
  return UNITY_END();
}
