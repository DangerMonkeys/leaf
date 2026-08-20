// Arduino FS / File for the host emulator, backed by a directory on the host filesystem.
//
// The emulated SD card is a real folder (sim/sdcard by default), so IGC files, logbooks, GPX
// waypoints, routes and settings written by the firmware are ordinary files you can open,
// diff and check in.  Paths are absolute in the firmware's world ("/logs/x.igc") and get resolved
// under the card root here.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include <memory>
#include <string>
#include <vector>

#include "Print.h"
#include "Stream.h"
#include "WString.h"

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace fs {

  class FileImpl;

  class File : public Stream {
   public:
    File();
    ~File();
    File(const File&);
    File(File&&);
    File& operator=(const File&);
    File& operator=(File&&);
    explicit File(FileImpl* impl);

    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buffer, size_t size) override;
    using Print::write;

    int available() override;
    int read() override;
    int peek() override;
    int read(uint8_t* buffer, size_t length);
    size_t readBytes(char* buffer, size_t length) override;
    void flush() override;

    bool seek(uint32_t position);
    bool seek(uint32_t position, int mode);
    size_t position() const;
    size_t size() const;
    void close();

    const char* name() const;
    const char* path() const;
    bool isDirectory() const;
    File openNextFile(const char* mode = FILE_READ);
    String getNextFileName();
    String getNextFileName(bool* isDir);
    void rewindDirectory();
    time_t getLastWrite();

    operator bool() const;

   private:
    // Shared so the firmware's habit of copying File values around behaves like the device's
    // reference-counted implementation rather than closing the underlying handle twice.
    std::shared_ptr<FileImpl> impl_;

    friend class FS;
  };

  class FS {
   public:
    explicit FS(const std::string& root = "") : root_(root) {}

    File open(const char* path, const char* mode = FILE_READ, bool create = false);
    File open(const String& path, const char* mode = FILE_READ, bool create = false);
    bool exists(const char* path);
    bool exists(const String& path);
    bool remove(const char* path);
    bool remove(const String& path);
    bool rename(const char* from, const char* to);
    bool rename(const String& from, const String& to);
    bool mkdir(const char* path);
    bool mkdir(const String& path);
    bool rmdir(const char* path);
    bool rmdir(const String& path);

    // Host path of the directory standing in for the card.
    void setRoot(const std::string& root) { root_ = root; }
    const std::string& root() const { return root_; }
    std::string hostPath(const char* path) const;

   protected:
    std::string root_;
  };

}  // namespace fs

using fs::File;
using fs::FS;
