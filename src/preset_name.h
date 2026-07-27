#ifndef PRESET_NAME_H
#define PRESET_NAME_H

// Preset name validation and path building. Preset names arrive from the
// network and become filenames on the SD card, so this is a security
// boundary: anything that could escape the presets directory (separators,
// parent references, drive letters, NUL) must be rejected here rather than
// sanitized, because silent rewriting hides the attempt. No hardware
// dependencies, unit-tested natively.

#include <stdint.h>
#include <stddef.h>

constexpr const char *PresetDir = "/presets";
constexpr unsigned PresetNameMax = 32;
constexpr unsigned PresetPathMax = 64; // "/presets/" + 32 + ".json" + NUL

// Allowed: ASCII letters, digits, space, underscore, hyphen, dot - but a
// dot may not start the name and ".." can never appear, so no traversal or
// hidden-file forms survive.
inline bool presetNameValid(const char *name) {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }
  if (name[0] == '.' || name[0] == ' ') {
    return false;
  }
  unsigned n = 0;
  char prev = '\0';
  for (const char *c = name; *c != '\0'; c++, n++) {
    if (n >= PresetNameMax) {
      return false;
    }
    const char ch = *c;
    const bool alnum = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9');
    if (!alnum && ch != ' ' && ch != '_' && ch != '-' && ch != '.') {
      return false; // covers / \ : * ? " < > | and every control byte
    }
    if (ch == '.' && prev == '.') {
      return false; // parent-directory reference
    }
    prev = ch;
  }
  // A trailing space or dot is silently stripped by some filesystems, which
  // would make two distinct names collide
  return prev != ' ' && prev != '.';
}

// Build "/presets/<name>.json". Returns false when the name is invalid or
// the buffer is too small - callers must not fall back to an unchecked path.
inline bool presetPath(char *out, size_t size, const char *name) {
  if (!presetNameValid(name) || out == nullptr) {
    return false;
  }
  const char *dir = PresetDir;
  size_t w = 0;
  for (const char *c = dir; *c != '\0'; c++) {
    if (w + 1 >= size) return false;
    out[w++] = *c;
  }
  if (w + 1 >= size) return false;
  out[w++] = '/';
  for (const char *c = name; *c != '\0'; c++) {
    if (w + 1 >= size) return false;
    out[w++] = *c;
  }
  const char *ext = ".json";
  for (const char *c = ext; *c != '\0'; c++) {
    if (w + 1 >= size) return false;
    out[w++] = *c;
  }
  out[w] = '\0';
  return true;
}

// Recover a preset name from a directory entry ("Night mode.json" ->
// "Night mode"); returns false for anything that is not a valid preset file
inline char presetLower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Case-insensitive equality, matching how FAT resolves names
inline bool presetNamesEqualFold(const char *a, const char *b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  while (*a != '\0' && *b != '\0') {
    if (presetLower(*a++) != presetLower(*b++)) {
      return false;
    }
  }
  return *a == *b;
}

inline bool presetNameFromFile(char *out, size_t size, const char *filename) {
  if (filename == nullptr || out == nullptr) {
    return false;
  }
  size_t len = 0;
  while (filename[len] != '\0') {
    len++;
  }
  const char *ext = ".json";
  const size_t extLen = 5;
  if (len <= extLen || len - extLen >= size) {
    return false;
  }
  // Case-insensitive extension match: FAT resolves paths case-insensitively,
  // so a file listed as NAME.JSON is the same file the other operations open
  for (size_t i = 0; i < extLen; i++) {
    if (presetLower(filename[len - extLen + i]) != ext[i]) {
      return false;
    }
  }
  // Validate the stem BEFORE writing it out, so a rejected entry never
  // leaves a half-built name in the caller's buffer
  char stem[PresetNameMax + 1];
  if (len - extLen > PresetNameMax) {
    return false;
  }
  for (size_t i = 0; i < len - extLen; i++) {
    stem[i] = filename[i];
  }
  stem[len - extLen] = '\0';
  if (!presetNameValid(stem)) {
    return false;
  }
  for (size_t i = 0; i <= len - extLen; i++) {
    out[i] = stem[i];
  }
  return true;
}

#endif
