// The emulated filesystem: firmware paths resolved under a host directory.
//
// The card is a folder, so everything the firmware writes -- IGC flights, logbook JSON, bus logs,
// waypoint files, routes -- lands on disk where you can read it after the run, and files
// you drop in beforehand are there for the firmware to find.

#include <FS.h>
#include <SD_MMC.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

SDMMCFS SD_MMC;

namespace fs {

  // ------------------------------------------------------------------ FileImpl

  class FileImpl {
   public:
    FileImpl(FILE* handle, std::string hostPath, std::string devicePath)
        : file_(handle), hostPath_(std::move(hostPath)), devicePath_(std::move(devicePath)) {}

    FileImpl(DIR* dir, std::string hostPath, std::string devicePath)
        : dir_(dir), hostPath_(std::move(hostPath)), devicePath_(std::move(devicePath)) {}

    ~FileImpl() { close(); }

    void close() {
      if (file_) {
        fclose(file_);
        file_ = nullptr;
      }
      if (dir_) {
        closedir(dir_);
        dir_ = nullptr;
      }
    }

    bool valid() const { return file_ || dir_; }
    bool isDirectory() const { return dir_ != nullptr; }
    FILE* file() const { return file_; }
    DIR* dir() const { return dir_; }
    const std::string& hostPath() const { return hostPath_; }
    const std::string& devicePath() const { return devicePath_; }

    // The device's File::name() returns the last path element.
    const char* name() {
      const size_t slash = devicePath_.find_last_of('/');
      nameCache_ = slash == std::string::npos ? devicePath_ : devicePath_.substr(slash + 1);
      return nameCache_.c_str();
    }

   private:
    FILE* file_ = nullptr;
    DIR* dir_ = nullptr;
    std::string hostPath_;
    std::string devicePath_;
    std::string nameCache_;
  };

  namespace {

    std::string joinPath(const std::string& root, const char* path) {
      std::string p = path ? path : "";
      if (p.empty()) p = "/";
      if (p.front() != '/') p.insert(p.begin(), '/');
      return root + p;
    }

    bool hostExists(const std::string& path) {
      struct stat st;
      return stat(path.c_str(), &st) == 0;
    }

    bool hostIsDir(const std::string& path) {
      struct stat st;
      return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    // "w"/"a" on the device create missing parents implicitly often enough that the firmware
    // relies on it after mkdir; mirror that so a missing folder is never a silent write failure.
    void ensureParentDirs(const std::string& hostPath) {
      const size_t slash = hostPath.find_last_of('/');
      if (slash == std::string::npos) return;
      std::string dir = hostPath.substr(0, slash);
      std::string accumulated;
      size_t start = 0;
      while (start < dir.size()) {
        size_t next = dir.find('/', start + 1);
        if (next == std::string::npos) next = dir.size();
        accumulated = dir.substr(0, next);
        if (!accumulated.empty() && !hostExists(accumulated)) mkdir(accumulated.c_str(), 0777);
        start = next;
      }
    }

  }  // namespace

  // ------------------------------------------------------------------ File

  // Defined here rather than in the header because File holds a shared_ptr to FileImpl, whose
  // definition only exists in this translation unit.
  File::File() = default;
  File::~File() = default;
  File::File(const File&) = default;
  File::File(File&&) = default;
  File& File::operator=(const File&) = default;
  File& File::operator=(File&&) = default;
  File::File(FileImpl* impl) : impl_(impl) {}

  size_t File::write(uint8_t c) { return write(&c, 1); }

  size_t File::write(const uint8_t* buffer, size_t size) {
    if (!impl_ || !impl_->file()) return 0;
    return fwrite(buffer, 1, size, impl_->file());
  }

  int File::available() {
    if (!impl_ || !impl_->file()) return 0;
    const long pos = ftell(impl_->file());
    fseek(impl_->file(), 0, SEEK_END);
    const long end = ftell(impl_->file());
    fseek(impl_->file(), pos, SEEK_SET);
    return (int)(end - pos);
  }

  int File::read() {
    if (!impl_ || !impl_->file()) return -1;
    const int c = fgetc(impl_->file());
    return c == EOF ? -1 : c;
  }

  int File::read(uint8_t* buffer, size_t length) {
    if (!impl_ || !impl_->file()) return -1;
    return (int)fread(buffer, 1, length, impl_->file());
  }

  size_t File::readBytes(char* buffer, size_t length) {
    const int n = read((uint8_t*)buffer, length);
    return n < 0 ? 0 : (size_t)n;
  }

  int File::peek() {
    if (!impl_ || !impl_->file()) return -1;
    const int c = fgetc(impl_->file());
    if (c != EOF) ungetc(c, impl_->file());
    return c == EOF ? -1 : c;
  }

  void File::flush() {
    if (impl_ && impl_->file()) fflush(impl_->file());
  }

  bool File::seek(uint32_t position) { return seek(position, SEEK_SET); }

  bool File::seek(uint32_t position, int mode) {
    if (!impl_ || !impl_->file()) return false;
    return fseek(impl_->file(), (long)position, mode) == 0;
  }

  size_t File::position() const {
    if (!impl_ || !impl_->file()) return 0;
    const long pos = ftell(impl_->file());
    return pos < 0 ? 0 : (size_t)pos;
  }

  size_t File::size() const {
    if (!impl_) return 0;
    struct stat st;
    if (stat(impl_->hostPath().c_str(), &st) != 0) return 0;
    return (size_t)st.st_size;
  }

  void File::close() {
    if (impl_) impl_->close();
    impl_.reset();
  }

  const char* File::name() const { return impl_ ? impl_->name() : ""; }
  const char* File::path() const { return impl_ ? impl_->devicePath().c_str() : ""; }
  bool File::isDirectory() const { return impl_ && impl_->isDirectory(); }
  time_t File::getLastWrite() {
    if (!impl_) return 0;
    struct stat st;
    if (stat(impl_->hostPath().c_str(), &st) != 0) return 0;
    return st.st_mtime;
  }

  File File::openNextFile(const char* mode) {
    if (!impl_ || !impl_->dir()) return File();
    for (;;) {
      struct dirent* entry = readdir(impl_->dir());
      if (!entry) return File();
      const std::string entryName = entry->d_name;
      if (entryName == "." || entryName == "..") continue;

      const std::string hostPath = impl_->hostPath() + "/" + entryName;
      std::string devicePath = impl_->devicePath();
      if (devicePath.empty() || devicePath.back() != '/') devicePath += "/";
      devicePath += entryName;

      if (hostIsDir(hostPath)) {
        DIR* dir = opendir(hostPath.c_str());
        if (!dir) continue;
        return File(new FileImpl(dir, hostPath, devicePath));
      }
      FILE* handle = fopen(hostPath.c_str(), mode ? mode : "r");
      if (!handle) continue;
      return File(new FileImpl(handle, hostPath, devicePath));
    }
  }

  String File::getNextFileName() {
    File next = openNextFile();
    return next ? String(next.path()) : String();
  }

  String File::getNextFileName(bool* isDir) {
    File next = openNextFile();
    if (!next) return String();
    if (isDir) *isDir = next.isDirectory();
    return String(next.path());
  }

  void File::rewindDirectory() {
    if (impl_ && impl_->dir()) rewinddir(impl_->dir());
  }

  File::operator bool() const { return impl_ && impl_->valid(); }

  // ------------------------------------------------------------------ FS

  std::string FS::hostPath(const char* path) const { return joinPath(root_, path); }

  File FS::open(const char* path, const char* mode, bool create) {
    if (root_.empty()) return File();
    const std::string host = joinPath(root_, path);
    const std::string device = path && *path == '/' ? path : std::string("/") + (path ? path : "");

    if (hostIsDir(host)) {
      DIR* dir = opendir(host.c_str());
      if (!dir) return File();
      return File(new FileImpl(dir, host, device));
    }

    const bool writing = mode && (mode[0] == 'w' || mode[0] == 'a');
    if (writing || create) ensureParentDirs(host);
    if (!writing && !create && !hostExists(host)) return File();

    FILE* handle = fopen(host.c_str(), mode ? mode : "r");
    if (!handle && create) handle = fopen(host.c_str(), "w+");
    if (!handle) return File();
    return File(new FileImpl(handle, host, device));
  }

  File FS::open(const String& path, const char* mode, bool create) {
    return open(path.c_str(), mode, create);
  }

  bool FS::exists(const char* path) { return !root_.empty() && hostExists(joinPath(root_, path)); }
  bool FS::exists(const String& path) { return exists(path.c_str()); }

  bool FS::remove(const char* path) {
    return !root_.empty() && ::remove(joinPath(root_, path).c_str()) == 0;
  }
  bool FS::remove(const String& path) { return remove(path.c_str()); }

  bool FS::rename(const char* from, const char* to) {
    if (root_.empty()) return false;
    const std::string target = joinPath(root_, to);
    ensureParentDirs(target);
    return ::rename(joinPath(root_, from).c_str(), target.c_str()) == 0;
  }
  bool FS::rename(const String& from, const String& to) { return rename(from.c_str(), to.c_str()); }

  bool FS::mkdir(const char* path) {
    if (root_.empty()) return false;
    const std::string host = joinPath(root_, path);
    if (hostIsDir(host)) return true;
    ensureParentDirs(host + "/x");
    return ::mkdir(host.c_str(), 0777) == 0;
  }
  bool FS::mkdir(const String& path) { return mkdir(path.c_str()); }

  bool FS::rmdir(const char* path) {
    return !root_.empty() && ::rmdir(joinPath(root_, path).c_str()) == 0;
  }
  bool FS::rmdir(const String& path) { return rmdir(path.c_str()); }

}  // namespace fs

// ------------------------------------------------------------------ SD_MMC

namespace {
  std::string g_cardRoot;
}

bool SDMMCFS::begin(const char* mountpoint, bool mode1bit, bool formatIfEmpty, int sdmmcFrequency,
                    uint8_t maxOpenFiles) {
  if (!present_) return false;
  if (g_cardRoot.empty()) return false;
  setRoot(g_cardRoot);
  mounted_ = true;
  return true;
}

void SDMMCFS::end() {
  mounted_ = false;
  setRoot("");
}

bool SDMMCFS::setPins(int clk, int cmd, int d0, int d1, int d2, int d3) { return true; }
bool SDMMCFS::setPins(int clk, int cmd, int d0) { return true; }

uint64_t SDMMCFS::cardSize() const { return 8ULL * 1024 * 1024 * 1024; }
uint64_t SDMMCFS::totalBytes() const { return cardSize(); }
uint64_t SDMMCFS::usedBytes() const { return 64ULL * 1024 * 1024; }
uint64_t SDMMCFS::numSectors() const { return cardSize() / 512; }
uint8_t SDMMCFS::cardType() const { return present_ ? CARD_SDHC : CARD_NONE; }

bool SDMMCFS::readRAW(uint8_t* buffer, uint32_t sector) { return false; }
bool SDMMCFS::writeRAW(uint8_t* buffer, uint32_t sector) { return false; }

void SDMMCFS::simSetPresent(bool present) {
  present_ = present;
  if (!present) end();
}

// Called by the emulator at startup to point the card at a host directory.
namespace sim {
  void setCardRoot(const std::string& root) {
    g_cardRoot = root;
    if (SD_MMC.mounted()) SD_MMC.setRoot(root);
  }
  const std::string& cardRoot() { return g_cardRoot; }
}  // namespace sim
