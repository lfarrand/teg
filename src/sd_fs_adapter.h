#ifndef SD_FS_ADAPTER_H
#define SD_FS_ADAPTER_H

// Presents this firmware's existing SdFat `SdFs` card as a Teensy-core `FS`,
// which is what MTP_Teensy's addFilesystem() requires. SdFat's SdFs is not
// derived from FS, and the core's own SD library wrapper refuses to build
// against upstream SdFat (it demands its bundled, older copy), so this thin
// adapter is what lets MTP share the card the firmware already mounted -
// without changing a single existing `sd.` call site or downgrading SdFat.
//
// READ-ONLY by construction. Every mutating entry point returns failure, as
// a second line of defence behind the dispatcher-level refusal in the
// vendored MTP library: a host must never be able to delete, overwrite,
// move or format anything, both because those paths are unbounded inside a
// single service call and because the card holds /settings.cfg, /presets
// and uploaded waveforms. Writes go through the authenticated HTTP API.

#include <Arduino.h>
#include <FS.h>
#include <SdFat.h>
#include "mtp_wdog.h"

inline bool mtpAsciiEqualIgnoreCase(const char *a, const char *b) {
  while (*a && *b) {
    char ca = *a++;
    char cb = *b++;
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + ('a' - 'A'));
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + ('a' - 'A'));
    if (ca != cb) return false;
  }
  return *a == '\0' && *b == '\0';
}

// Config and presets contain the write PIN, MQTT password and Influx token.
// Read-only MTP is still plaintext exfiltration, so these objects are absent
// from both direct opens and directory enumeration.
inline bool mtpSensitiveName(const char *path) {
  while (*path == '/') ++path;
  const char *name = path;
  for (const char *p = path; *p; ++p) {
    if (*p == '/') name = p + 1;
  }
  return mtpAsciiEqualIgnoreCase(name, "settings.cfg") ||
         mtpAsciiEqualIgnoreCase(name, "settings.tmp") ||
         mtpAsciiEqualIgnoreCase(name, "settings.bak") ||
         mtpAsciiEqualIgnoreCase(name, "presets");
}

// Upstream SdFat makes FsFile non-copyable (a copy would double-close the
// underlying handle), so each impl OPENS its own handle in place rather than
// taking one by value.
class SdFsFileImpl : public FileImpl {
public:
  SdFsFileImpl(SdFs &sd, const char *path, oflag_t flags) {
    opened_ = file_.open(&sd, path, flags);
  }
  SdFsFileImpl(FsFile &parent, oflag_t flags) {
    opened_ = file_.openNext(&parent, flags);
  }
  ~SdFsFileImpl() override { file_.close(); }

  bool opened() const { return opened_; }

  size_t write(const void *, size_t) override { return 0; } // read-only
  int peek() override { return file_.peek(); }
  int available() override { return file_.available(); }
  void flush() override { file_.flush(); }
  size_t read(void *buf, size_t nbyte) override {
    const int n = file_.read(static_cast<uint8_t *>(buf), nbyte);
    return n > 0 ? static_cast<size_t>(n) : 0;
  }
  bool truncate(uint64_t) override { return false; } // read-only
  bool seek(uint64_t pos, int mode) override {
    if (mode == SeekSet) return file_.seekSet(pos);
    if (mode == SeekCur) return file_.seekCur(pos);
    // SeekEnd: the core's reference FileImpl treats pos as an offset applied
    // directly (negative moves back from the end), matching SdFat's seekEnd
    if (mode == SeekEnd) return file_.seekEnd(static_cast<int64_t>(pos));
    return false;
  }
  uint64_t position() override { return file_.curPosition(); }
  uint64_t size() override { return file_.fileSize(); }
  void close() override { file_.close(); }
  bool isOpen() override { return file_.isOpen(); }
  const char *name() override {
    file_.getName(nameBuf_, sizeof(nameBuf_));
    return nameBuf_;
  }
  bool isDirectory() override { return file_.isDirectory(); }

  File openNextFile(uint8_t mode) override {
    for (;;) {
      SdFsFileImpl *impl = new SdFsFileImpl(file_, mode == FILE_WRITE ? O_RDWR : O_RDONLY);
      if (!impl->opened()) {
        delete impl;
        return File();
      }
      if (mtpSensitiveName(impl->name())) {
        delete impl;
        continue;
      }
      return File(impl);
    }
  }
  void rewindDirectory() override { file_.rewindDirectory(); }

  // Without these the host shows garbage dates for every file
  bool getCreateTime(DateTimeFields &tm) override {
    uint16_t d, t;
    if (!file_.getCreateDateTime(&d, &t) || !fatValid(d)) {
      return false;
    }
    fatToFields(d, t, tm);
    return true;
  }
  bool getModifyTime(DateTimeFields &tm) override {
    uint16_t d, t;
    if (!file_.getModifyDateTime(&d, &t) || !fatValid(d)) {
      return false;
    }
    fatToFields(d, t, tm);
    return true;
  }
  bool setCreateTime(const DateTimeFields &) override { return false; } // read-only
  bool setModifyTime(const DateTimeFields &) override { return false; }

private:
  // A zero FAT timestamp (no date recorded) must be reported as absent -
  // SdFat returns success for it, and converting it yields month 255/day 0
  static bool fatValid(uint16_t date) { return date != 0; }

  static void fatToFields(uint16_t date, uint16_t time, DateTimeFields &tm) {
    tm.sec = (time & 0x1F) * 2;
    tm.min = (time >> 5) & 0x3F;
    tm.hour = (time >> 11) & 0x1F;
    tm.mday = date & 0x1F;
    tm.mon = ((date >> 5) & 0x0F) - 1;
    tm.year = ((date >> 9) & 0x7F) + 80; // FAT epoch 1980 -> struct tm epoch 1900
    tm.wday = 0;
  }

  FsFile file_;
  bool opened_ = false;
  char nameBuf_[256] = {};
};

class SdFsAdapter : public FS {
public:
  explicit SdFsAdapter(SdFs &sd) : sd_(sd) {}

  File open(const char *filename, uint8_t mode = FILE_READ) override {
    if (mode != FILE_READ || mtpSensitiveName(filename)) {
      return File(); // read-only: never create or truncate
    }
    SdFsFileImpl *impl = new SdFsFileImpl(sd_, filename, O_RDONLY);
    if (!impl->opened()) {
      delete impl;
      return File();
    }
    return File(impl);
  }
  bool exists(const char *filepath) override {
    return !mtpSensitiveName(filepath) && sd_.exists(filepath);
  }
  bool mkdir(const char *) override { return false; }         // read-only
  bool rename(const char *, const char *) override { return false; }
  bool remove(const char *) override { return false; }
  bool rmdir(const char *) override { return false; }

  // Hosts poll these repeatedly while the device is mounted, and
  // freeClusterCount() rescans the whole FAT (seconds on a large card), so
  // the result is computed once and reused. freeClusterCount returns a
  // SIGNED -1 on error: storing that in an unsigned would make usedSize
  // exceed totalSize and the host's free space underflow to ~18 exabytes.
  uint64_t usedSize() override {
    cacheSizes();
    return usedBytes_;
  }
  uint64_t totalSize() override {
    cacheSizes();
    return totalBytes_;
  }

  // Never let a host format the operator's card
  bool format(int type = 0, char progressChar = 0, Print &pr = Serial) override {
    (void)type;
    (void)progressChar;
    (void)pr;
    return false;
  }
  bool mediaPresent() override { return sd_.card() != nullptr; }

private:
  void cacheSizes() {
    if (sizesValid_) {
      return;
    }
    mtpKickWatchdog(); // a full-FAT free-cluster scan can take seconds
    const uint32_t clusters = sd_.clusterCount();
    const int32_t freeClusters = sd_.freeClusterCount(); // signed: -1 on error
    const uint32_t bytesPerCluster = sd_.bytesPerCluster();
    totalBytes_ = static_cast<uint64_t>(clusters) * bytesPerCluster;
    usedBytes_ = freeClusters < 0
                   ? totalBytes_ // unknown: report full rather than underflow
                   : static_cast<uint64_t>(clusters - static_cast<uint32_t>(freeClusters)) *
                       bytesPerCluster;
    mtpKickWatchdog();
    sizesValid_ = true;
  }

  SdFs &sd_;
  uint64_t totalBytes_ = 0;
  uint64_t usedBytes_ = 0;
  bool sizesValid_ = false;
};

#endif
