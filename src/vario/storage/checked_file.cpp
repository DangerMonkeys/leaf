#include "checked_file.h"

#include <SD_MMC.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include "logbook/logbook_io.h"
#include "storage/sd_card.h"

namespace checked_file {
  namespace {
#ifdef LEAF_SIM
    FaultHook faultHook = nullptr;
#endif
    bool fault(const char* stage) {
#ifdef LEAF_SIM
      if (faultHook && faultHook(stage)) return true;
#endif
      return false;
    }
    bool accessible() { return sdcard.firmwareCanAccessFilesystem() && SDCard::isCardPresent(); }
    String physical(const String& path) {
#ifdef LEAF_SIM
      return SD_MMC.hostPath(path.c_str()).c_str();
#else
      return String("/sdcard") + path;
#endif
    }
    bool safe(const String& path) {
      return path.startsWith("/") && path.indexOf("..") < 0 && path.indexOf('\\') < 0;
    }
    bool exists(const String& path, bool& found, size_t* size = nullptr) {
      struct stat info;
      if (stat(physical(path).c_str(), &info) == 0) {
        found = true;
        if (size) *size = info.st_size;
        return S_ISREG(info.st_mode);
      }
      found = false;
      return errno == ENOENT;
    }
    bool remove(const String& path) {
      bool found;
      return exists(path, found) && (!found || std::remove(physical(path).c_str()) == 0);
    }
    bool rename(const String& from, const String& to) {
      bool found;
      return exists(to, found) && !found &&
             std::rename(physical(from).c_str(), physical(to).c_str()) == 0;
    }
    uint32_t hashBytes(uint32_t hash, const uint8_t* bytes, size_t count) {
      for (size_t i = 0; i < count; ++i) hash = (hash ^ bytes[i]) * 16777619U;
      return hash;
    }
    bool promote(const String& path) {
      const String temp = path + ".tmp", backup = path + ".bak";
      bool hadExisting;
      if (!exists(path, hadExisting)) return false;
      if (hadExisting && (!remove(backup) || fault("backup") || !rename(path, backup)))
        return false;
      if (fault("promote") || !rename(temp, path)) {
        if (hadExisting) rename(backup, path);  // Leave backup for recovery if rollback fails.
        return false;
      }
      if (hadExisting) remove(backup);  // A cleanup failure does not invalidate the verified main.
      return true;
    }
  }  // namespace

  Writer::Writer(const String& path) {
    if (accessible() && safe(path) && !fault("open")) file_ = fopen(physical(path).c_str(), "wb");
  }
  Writer::~Writer() {
    if (file_) fclose(file_);
  }
  size_t Writer::write(const uint8_t* bytes, size_t count) {
    if (!file_ || failed_) return 0;
    if (fault("write")) {
      failed_ = true;
      return 0;
    }
    const size_t requested = count;
    if (fault("short_write")) count /= 2;
    const size_t written = fwrite(bytes, 1, count, file_);
    failed_ = written != requested;
    bytes_ += written;
    hash_ = hashBytes(hash_, bytes, written);
    return written;
  }
  bool Writer::finish(const String& path) {
    if (!file_) return false;
    bool ok = !failed_ && !ferror(file_);
    const int flushed = fflush(file_);
    if (flushed || fault("flush")) ok = false;
    const int closed = fclose(file_);  // FatFs close synchronizes the file, as in logbook_io.
    file_ = nullptr;
    if (closed || fault("close")) ok = false;
    if (!ok || !bytes_ || fault("verify")) return false;
    FILE* check = fopen(physical(path).c_str(), "rb");
    if (!check) return false;
    uint8_t buffer[128];
    uint32_t hash = 2166136261U;
    size_t total = 0, count;
    while ((count = fread(buffer, 1, sizeof(buffer), check))) {
      total += count;
      hash = hashBytes(hash, buffer, count);
    }
    ok = !ferror(check) && total == bytes_ && hash == hash_;
    if (fclose(check)) ok = false;
    return ok;
  }
  bool recover(const String& path) {
    logbook_io::Guard guard;
    if (!guard || !accessible() || !safe(path)) return false;
    bool main, backup;
    size_t mainSize = 0, backupSize = 0;
    const String backupPath = path + ".bak";
    if (!exists(path, main, &mainSize) || !exists(backupPath, backup, &backupSize)) return false;
    if (backup && backupSize && (!main || !mainSize)) {
      if (main && !remove(path)) return false;
      return rename(backupPath, path);
    }
    return true;
  }
  bool replaceJson(const String& path, const JsonDocument& document, bool pretty) {
    logbook_io::Guard guard;
    if (!guard || !recover(path)) return false;
    const String temp = path + ".tmp";
    if (!remove(temp)) return false;
    bool ok;
    {
      Writer writer(temp);
      if (!writer) return false;
      const size_t expected = pretty ? measureJsonPretty(document) : measureJson(document);
      const size_t written =
          pretty ? serializeJsonPretty(document, writer) : serializeJson(document, writer);
      const bool complete = writer.finish(temp);
      ok = written == expected && complete;
    }
    if (ok) ok = promote(path);
    if (!ok) remove(temp);
    return ok;
  }
  bool erase(const String& path) {
    logbook_io::Guard guard;
    return guard && accessible() && safe(path) && remove(path + ".bak") && remove(path + ".tmp") &&
           remove(path);
  }
  bool replaceBytes(const String& path, const String& bytes) {
    logbook_io::Guard guard;
    if (!guard || !recover(path)) return false;
    const String temp = path + ".tmp";
    if (!remove(temp)) return false;
    bool ok;
    {
      Writer writer(temp);
      if (!writer) return false;
      const size_t written =
          writer.write(reinterpret_cast<const uint8_t*>(bytes.c_str()), bytes.length());
      const bool complete = writer.finish(temp);
      ok = written == bytes.length() && complete;
    }
    if (ok) ok = promote(path);
    if (!ok) remove(temp);
    return ok;
  }
#ifdef LEAF_SIM
  void setFaultHook(FaultHook hook) { faultHook = hook; }
#endif
}  // namespace checked_file
