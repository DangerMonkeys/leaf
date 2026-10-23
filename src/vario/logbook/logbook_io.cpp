#include "logbook_io.h"

#include <SD_MMC.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include "logging/log.h"
#include "storage/sd_card.h"

namespace logbook_io {
  namespace {
    std::recursive_mutex mutex;
    Result result;
    constexpr size_t MAX_BYTES = 64 * 1024;
#ifdef LEAF_SIM
    FaultHook faultHook = nullptr;
#endif
    bool fail(const char* stage, int error, size_t expected = 0, size_t actual = 0) {
      result = {stage, error ? error : EIO, expected, actual};
      Serial.printf("logbook: %s error=%d expected=%u actual=%u\n", stage, result.error,
                    unsigned(expected), unsigned(actual));
      return false;
    }
    int fault(const char* stage) {
#ifdef LEAF_SIM
      if (faultHook) return faultHook(stage);
#endif
      return 0;
    }
    bool accessible() {
      return (sdcard.firmwareCanAccessFilesystem() && SDCard::isCardPresent()) ||
             fail("ownership", ENODEV);
    }
    String physical(const String& path) {
#ifdef LEAF_SIM
      return String(SD_MMC.hostPath(path.c_str()).c_str());
#else
      return String("/sdcard") + path;
#endif
    }
    bool safePath(const String& path) {
      return path.startsWith("/logbook/") && path.endsWith(".json") && path.indexOf("..") < 0 &&
             path.indexOf('/', 9) < 0 && path.indexOf('\\') < 0;
    }
    bool existsChecked(const String& path, bool& exists) {
      struct stat info;
      if (stat(physical(path).c_str(), &info) == 0) {
        exists = true;
        return true;
      }
      if (errno == ENOENT) {
        exists = false;
        return true;
      }
      return fail("stat", errno);
    }
    bool removeFile(const String& path, const char* stage) {
      bool exists;
      if (!existsChecked(path, exists)) return false;
      if (!exists) return true;
      const int injected = fault(stage);
      if (injected) return fail(stage, injected);
      errno = 0;
      return SD_MMC.remove(path) || fail(stage, errno);
    }
    bool renameFile(const String& from, const String& to, const char* stage) {
      const int injected = fault(stage);
      if (injected) return fail(stage, injected);
      // The host's overwrite-rename semantics must not conceal FatFs collisions.
      bool exists;
      if (!existsChecked(to, exists)) return false;
      if (exists) return fail(stage, EEXIST);
      errno = 0;
      return SD_MMC.rename(from, to) || fail(stage, errno);
    }
    struct Reader {
      FILE* file;
      int read() { return fgetc(file); }
      size_t readBytes(char* buffer, size_t count) { return fread(buffer, 1, count, file); }
    };
    struct HashWriter {
      uint64_t hash = 14695981039346656037ULL;
      size_t write(uint8_t c) {
        hash = (hash ^ c) * 1099511628211ULL;
        return 1;
      }
      size_t write(const uint8_t* bytes, size_t count) {
        for (size_t i = 0; i < count; ++i) write(bytes[i]);
        return count;
      }
    };
    // Fingerprints compare complete readback without keeping a second JSON document/String.
    uint64_t fingerprint(const JsonDocument& doc, bool core = false) {
      HashWriter writer;
      for (JsonPairConst pair : doc.as<JsonObjectConst>()) {
        if (!strcmp(pair.key().c_str(), "revision")) continue;
        if (core &&
            (!strcmp(pair.key().c_str(), "complete") || !strcmp(pair.key().c_str(), "leaf_log")))
          continue;
        writer.write(reinterpret_cast<const uint8_t*>(pair.key().c_str()), pair.key().size());
        writer.write(0);
        serializeJson(pair.value(), writer);
        writer.write(0);
      }
      return writer.hash;
    }
    struct Candidate {
      bool exists = false;
      bool valid = false;
      bool complete = false;
      bool versioned = false;
      uint32_t revision = 0;
      uint64_t payload = 0;
      uint64_t core = 0;
      String id;
    };
    bool validate(const JsonDocument& doc, const String& path) {
      if (doc.overflowed()) return fail("allocation", ENOMEM);
      const char* schema = doc["schema"] | "";
      const char* version = doc["schema_version"] | "";
      const char* id = doc["flight_id"] | "";
      if (!doc.is<JsonObjectConst>() || !*schema || !*version || !*id || strlen(id) > 64)
        return fail("schema", EBADMSG);
      if (strcmp(schema, "leaf.logbook.flight") || strcmp(version, "v0.1.0"))
        return fail("unsupported_schema", EPROTONOSUPPORT);
      // Standard generated filenames bind the payload to its flight. Sparse legacy names remain
      // readable.
      const int dot = path.indexOf(".json");
      if (dot >= 9 && path[dot - 9] == '_') {
        const String suffix = path.substring(dot - 8, dot);
        bool hex = true;
        for (char c : suffix)
          if (!isxdigit(static_cast<unsigned char>(c))) hex = false;
        if (hex && suffix != id) return fail("identity", EBADMSG);
      }
      const bool revision = !doc["revision"].isNull();
      const bool complete = !doc["complete"].isNull();
      if (revision != complete) return fail("metadata", EBADMSG);
      if (revision) {
        if (!doc["revision"].is<uint32_t>() || doc["revision"].as<uint32_t>() == 0 ||
            !doc["complete"].is<bool>() || !doc["track"]["saved"].is<bool>())
          return fail("metadata", EBADMSG);
        // The public legacy schema permits sparse records. A metadata-only status update may
        // preserve such a record; newly generated placeholders must contain their basic metrics.
        if ((!doc["start"]["time_valid"].is<bool>() ||
             !doc["metrics"]["duration_seconds"].is<uint32_t>()) &&
            !doc["leaf_log"].is<JsonObjectConst>())
          return fail("required_fields", EBADMSG);
        if (doc["complete"].as<bool>() && !doc["end"]["time_valid"].is<bool>())
          return fail("completion", EBADMSG);
      }
      return true;
    }
    bool readDocument(const String& path, JsonDocument& doc, size_t expected = 0) {
      doc.clear();
      if (int e = fault("read")) return fail("read", e);
      FILE* file = fopen(physical(path).c_str(), "rb");
      if (!file) return fail("read_open", errno);
      bool ok = true;
      if (fseek(file, 0, SEEK_END) != 0) ok = fail("read_size", errno);
      const long length = ftell(file);
      if (ok && length < 0) ok = fail("read_size", errno);
      if (ok && length > long(MAX_BYTES)) ok = fail("validation_limit", EFBIG);
      if (ok && (length <= 0 || (expected && size_t(length) != expected)))
        ok = fail("length", EBADMSG, expected, length > 0 ? length : 0);
      if (ok && fseek(file, 0, SEEK_SET) != 0) ok = fail("read_seek", errno);
      if (ok && fault("allocation")) ok = fail("allocation", ENOMEM);
      if (ok) {
        Reader reader{file};
        const auto error = deserializeJson(doc, reader, DeserializationOption::NestingLimit(16));
        if (error) ok = fail("parse", error == DeserializationError::NoMemory ? ENOMEM : EBADMSG);
        int c;
        while (ok && (c = fgetc(file)) != EOF) {
          if (!isspace(static_cast<unsigned char>(c))) ok = fail("trailing_content", EBADMSG);
        }
      }
      if (ferror(file)) ok = fail("read_io", errno);
      if (fclose(file) != 0) ok = fail("read_close", errno);
      return ok && validate(doc, path);
    }
    bool inspect(const String& path, Candidate& candidate, JsonDocument& doc) {
      candidate = Candidate();
      if (!existsChecked(path, candidate.exists)) return false;
      if (!candidate.exists) return true;
      if (!readDocument(path, doc)) {
        // Malformed bytes are not authoritative. An unreadable/OOM candidate is unknown, not
        // invalid.
        return result.error == EBADMSG;
      }
      candidate.valid = true;
      candidate.versioned = !doc["revision"].isNull();
      candidate.revision = doc["revision"] | 0U;
      candidate.complete =
          candidate.versioned ? doc["complete"].as<bool>() : doc["end"].is<JsonObject>();
      candidate.payload = fingerprint(doc);
      candidate.core = fingerprint(doc, true);
      candidate.id = doc["flight_id"].as<String>();
      return true;
    }
    // 2 means genuinely unordered. A legacy backup may be the parent of a metadata-only update.
    int compare(const Candidate& a, const Candidate& b) {
      if (a.id != b.id) return 2;
      if (a.complete != b.complete) return a.complete ? 1 : -1;
      if (a.versioned && b.versioned) {
        if (a.revision != b.revision) return a.revision > b.revision ? 1 : -1;
        return a.payload == b.payload ? 0 : 2;
      }
      if (a.payload == b.payload) return 0;
      if (a.core == b.core && a.versioned != b.versioned) return a.versioned ? 1 : -1;
      return 2;
    }
    bool syncCandidate(const String& path) {
      FILE* file = fopen(physical(path).c_str(), "r+b");
      if (!file) return fail("recovery_open", errno);
      bool ok = true;
      if (fflush(file) != 0) ok = fail("recovery_flush", errno);
      const int injected = fault("recovery_sync");
      const int synced = fsync(fileno(file));
      if (ok && (injected || synced)) ok = fail("recovery_sync", injected ? injected : errno);
      const int closeFault = fault("recovery_close");
      const int closed = fclose(file);
      if (ok && (closeFault || closed))
        ok = fail("recovery_close", closeFault ? closeFault : errno);
      return ok;
    }
    bool recoverLocked(const String& path) {
      if (!accessible()) return false;
      if (!safePath(path)) return fail("recovery_path", EINVAL);
      const String paths[] = {path, path + ".tmp", path + ".bak"};
      Candidate candidates[3];
      JsonDocument doc;
      for (int i = 0; i < 3; ++i) {
        if (!inspect(paths[i], candidates[i], doc)) return false;
      }
      int best = -1;
      bool anyValid = false;
      for (int i = 0; i < 3; ++i) {
        if (!candidates[i].valid) continue;
        anyValid = true;
        bool authoritative = true;
        for (int j = 0; j < 3; ++j) {
          if (i == j || !candidates[j].valid) continue;
          const int order = compare(candidates[i], candidates[j]);
          if (order < 0 || order == 2) authoritative = false;
        }
        if (authoritative && best < 0) best = i;
      }
      if (anyValid && best < 0) return fail("recovery_conflict", EEXIST);
      if (best < 0) {
        if (candidates[0].exists || candidates[1].exists || candidates[2].exists)
          return fail("no_valid_candidate", EBADMSG);
        result = {};
        return true;
      }
      if (best != 0) {
        // The previous attempt may have failed sync/close. Structural validity alone must
        // not turn that failed write into a successful commit on the next attempt.
        if (!syncCandidate(paths[best])) return false;
        if (candidates[0].valid && best == 1) {
          if (!removeFile(paths[2], "backup_rotate") || !renameFile(paths[0], paths[2], "backup"))
            return false;
        } else if (!removeFile(paths[0], "invalid_main_remove"))
          return false;
        if (!renameFile(paths[best], paths[0], "promote")) return false;
        if (!readDocument(paths[0], doc) || fingerprint(doc) != candidates[best].payload ||
            (doc["revision"] | 0U) != candidates[best].revision)
          return fail("promoted_validation", EBADMSG);
      }
      // A previous backup is intentional. Only the now-redundant temporary is disposable.
      if (!removeFile(paths[1], "temp_cleanup")) {
        // Commit/recovery succeeded; leave diagnostic evidence and retry cleanup on next access.
        return true;
      }
      result = {};
      return true;
    }
  }  // namespace

  Guard::Guard(bool wait) : lock_(mutex, std::defer_lock) {
    if (wait)
      lock_.lock();
    else
      (void)lock_.try_lock();
  }
  Result lastResult() {
    Guard guard;
    return guard ? result : Result{"busy", EBUSY, 0, 0};
  }
  bool retryable() {
    const auto e = lastResult().error;
    return e != ENOSPC && e != ENODEV && e != EINVAL && e != EEXIST && e != EOVERFLOW &&
           e != EBADMSG && e != EROFS && e != EFBIG && e != EPROTONOSUPPORT;
  }
#ifdef LEAF_SIM
  void setFaultHook(FaultHook hook) {
    Guard guard;
    if (guard) faultHook = hook;
  }
#endif
  bool load(const String& path, JsonDocument& document) {
    Guard guard;
    String base = path;
    if (base.endsWith(".tmp") || base.endsWith(".bak")) base.remove(base.length() - 4);
    return guard && accessible() && safePath(base) && readDocument(path, document);
  }
  bool recover(const String& path) {
    Guard guard;
    return guard && recoverLocked(path);
  }
  bool ensureDirectory() {
    Guard guard;
    if (!guard || !accessible()) return false;
    struct stat info;
    if (stat(physical("/logbook").c_str(), &info) == 0)
      return S_ISDIR(info.st_mode) || fail("directory", ENOTDIR);
    if (errno != ENOENT) return fail("directory", errno);
    return mkdir(physical("/logbook").c_str(), 0775) == 0 || fail("directory_create", errno);
  }
  bool prepare(const String& path, uint32_t& revision, uint64_t& previousPayload) {
    Guard guard;
    if (!guard) return false;
    if (!recover(path)) {
      if (strcmp(result.stage, "no_valid_candidate")) return false;
      // All existing candidates were read successfully and found malformed. They cannot
      // become a valid source on retry; make room for the new in-memory record.
      if (!removeFile(path + ".tmp", "invalid_temp_remove")) return false;
    }
    Candidate candidates[3];
    JsonDocument doc;
    uint32_t highest = 0;
    previousPayload = 0;
    const String paths[] = {path, path + ".tmp", path + ".bak"};
    for (int i = 0; i < 3; ++i) {
      if (!inspect(paths[i], candidates[i], doc)) return false;
      if (candidates[i].valid) {
        if (candidates[i].revision > highest) highest = candidates[i].revision;
        if (i == 0) previousPayload = candidates[i].payload;
      }
    }
    if (highest == UINT32_MAX) return fail("revision_overflow", EOVERFLOW);
    revision = highest + 1;
    return true;
  }
  bool commit(const String& path, JsonDocument& doc, uint64_t previousPayload) {
    Guard guard;
    if (!guard) return false;
    if (!accessible() || !safePath(path) || !validate(doc, path)) return false;
    const uint64_t payload = fingerprint(doc);
    const uint32_t revision = doc["revision"] | 0U;
    if (previousPayload && payload == previousPayload) return true;
    const String temp = path + ".tmp";
    const String backup = path + ".bak";
    // prepare must have resolved candidates; never truncate an unexplained surviving temporary.
    bool exists;
    if (!existsChecked(temp, exists)) return false;
    if (exists) return fail("unresolved_temp", EEXIST);
    const size_t expected = measureJson(doc) + 1;
    if (expected > MAX_BYTES) return fail("length", EFBIG);
    if (int e = fault("open")) return fail("open", e);
    FILE* file = fopen(physical(temp).c_str(), "wb");
    if (!file) return fail("open", errno);
    struct Writer {
      FILE* file;
      size_t write(uint8_t c) { return write(&c, 1); }
      size_t write(const uint8_t* bytes, size_t count) {
        if (int e = fault("write")) {
          errno = e;
          return 0;
        }
        if (fault("short_write")) count /= 2;
        return fwrite(bytes, 1, count, file);
      }
    } writer{file};
    size_t actual = serializeJson(doc, writer);
    actual += writer.write(uint8_t('\n'));
    bool ok = actual == expected && !ferror(file);
    if (!ok) fail("write", errno, expected, actual);
    // Always close; preserve the first failure instead of replacing it with cleanup errors.
    const int flushFault = fault("flush");
    const int flushed = fflush(file);
    if (ok && (flushFault || flushed)) ok = fail("flush", flushFault ? flushFault : errno);
    const int syncFault = fault("sync");
    const int synced = fsync(fileno(file));
    if (ok && (syncFault || synced)) ok = fail("sync", syncFault ? syncFault : errno);
    const int closeFault = fault("close");
    const int closed = fclose(file);
    if (ok && (closeFault || closed)) ok = fail("close", closeFault ? closeFault : errno);
    if (!ok) return false;
    // Reuse the caller's document only after serialization has finished.
    if (!readDocument(temp, doc, expected) || fingerprint(doc) != payload ||
        (doc["revision"] | 0U) != revision)
      return fail("temporary_validation", result.error ? result.error : EBADMSG);
    if (int e = fault("after_temp_validation")) return fail("after_temp_validation", e);
    if (!existsChecked(path, exists)) return false;
    if (exists) {
      if (!removeFile(backup, "backup_rotate") || !renameFile(path, backup, "backup")) return false;
    }
    if (int e = fault("after_backup")) return fail("after_backup", e);
    if (!renameFile(temp, path, "promote")) {
      const Result original = result;
      if (SD_MMC.exists(backup) && !SD_MMC.exists(path)) renameFile(backup, path, "rollback");
      result = original;
      return false;
    }
    if (int e = fault("after_promote")) return fail("after_promote", e);
    if (!readDocument(path, doc, expected) || fingerprint(doc) != payload ||
        (doc["revision"] | 0U) != revision)
      return fail("promoted_validation", result.error ? result.error : EBADMSG);
    result = {};
    return true;
  }

  bool candidatePath(const String& entry, String& path) {
    path = entry.startsWith("/")
               ? entry
               : (entry.startsWith("logbook/") ? String("/") + entry : String("/logbook/") + entry);
    const bool auxiliary = path.endsWith(".tmp") || path.endsWith(".bak");
    const bool backup = path.endsWith(".bak");
    if (auxiliary) path.remove(path.length() - 4);
    if (!safePath(path)) return false;
    if (auxiliary && SD_MMC.exists(path)) return false;
    if (backup && SD_MMC.exists(path + ".tmp")) return false;
    return true;
  }
  bool relocate(const String& oldPath, const String& newPath) {
    Guard guard;
    if (!guard || !accessible() || !safePath(oldPath) || !safePath(newPath)) return false;
    if (oldPath == newPath) return true;
    if (!recoverLocked(oldPath)) return false;
    if (SD_MMC.exists(newPath) || SD_MMC.exists(newPath + ".tmp") ||
        SD_MMC.exists(newPath + ".bak"))
      return fail("rename_collision", EEXIST);
    if (!SD_MMC.exists(oldPath)) return true;
    // Resolve and remove redundant auxiliaries while the validated old main still exists.
    // Only one filename then moves: no old backup can resurrect a duplicate after reboot.
    if (!removeFile(oldPath + ".bak", "rename_backup_cleanup")) return false;
    return renameFile(oldPath, newPath, "rename_main");
  }
  bool erase(const String& path, const String& trackPath) {
    Guard guard;
    if (!guard || !accessible() || !safePath(path)) return false;
    if (flightTimer_ownsLogbookPath(path)) return fail("delete_active", EBUSY);
    auto eraseSet = [&](const String& base, const String& hint) {
      if (flightTimer_ownsLogbookPath(base)) return fail("delete_active", EBUSY);
      String tracks[4];
      tracks[0] = hint;
      {
        JsonDocument doc;
        unsigned index = 1;
        for (const char* suffix : {"", ".tmp", ".bak"}) {
          bool exists;
          if (!existsChecked(base + suffix, exists)) return false;
          if (!exists) {
            ++index;
            continue;
          }
          if (readDocument(base + suffix, doc)) {
            tracks[index] = doc["track"]["path"] | "";
          } else if (result.error != EBADMSG && result.error != EPROTONOSUPPORT &&
                     result.error != EFBIG) {
            return false;  // An I/O failure is not proof that a referenced track is absent.
          }
          ++index;
        }
      }
      // Keep the main until all recovery candidates and associated tracks are gone.
      bool ok = removeFile(base + ".tmp", "delete_temp");
      ok = removeFile(base + ".bak", "delete_backup") && ok;
      if (!ok) return false;
      for (const String& raw : tracks) {
        if (raw.isEmpty()) continue;
        const String track = raw.startsWith("/") ? raw : String("/") + raw;
        if (!track.startsWith("/tracks/") || track.indexOf("..") >= 0 || track.indexOf('\\') >= 0)
          return fail("delete_track_path", EINVAL);
        if (!removeFile(track, "delete_track")) return false;
      }
      return removeFile(base, "delete_main");
    };
    // Old firmware could leave auxiliaries at an unsynced filename after renaming the main.
    // Find one matching-ID alias at a time, and close the directory before changing its entries.
    const int dot = path.length() - 5;
    if (dot >= 9 && path[dot - 9] == '_') {
      const String suffix = path.substring(dot - 9);
      String cursor;
      while (true) {
        String next;
        {
          File directory = SD_MMC.open("/logbook");
          if (!directory) return fail("delete_directory", EIO);
          while (true) {
            String entry = directory.getNextFileName();
            if (entry.isEmpty()) break;
            if (!entry.startsWith("/")) entry = String("/logbook/") + entry;
            if (entry.endsWith(".tmp") || entry.endsWith(".bak")) entry.remove(entry.length() - 4);
            if (entry == path || !safePath(entry) || !entry.endsWith(suffix) ||
                (!cursor.isEmpty() && entry.compareTo(cursor) <= 0))
              continue;
            if (next.isEmpty() || entry.compareTo(next) < 0) next = entry;
          }
        }
        if (next.isEmpty()) break;
        if (!eraseSet(next, "")) return false;
        cursor = next;
      }
    }
    const bool ok = eraseSet(path, trackPath);
    if (ok) result = {};
    return ok;
  }
}  // namespace logbook_io
