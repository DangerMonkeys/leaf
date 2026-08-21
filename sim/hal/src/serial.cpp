// Serial consoles.
//
// Serial is the debug console: it goes to the host's stdout and into a ring buffer the emulator
// UI drains, so the browser panel shows the same boot log you would see over USB.
// Serial0 is the GPS UART, wired to the virtual board's byte pipe.

#include <HardwareSerial.h>
#include <stdio.h>

#include <mutex>

#include "sim/board.h"

HostConsole Serial;
HostGpsSerial Serial0;
HostConsole Serial1;
HostConsole Serial2;

namespace {
  std::mutex g_consoleMutex;
  std::string g_pending;             // partial line not yet terminated
  std::vector<std::string> g_lines;  // the retained tail of the transcript
  // Sequence number of g_lines.front(); everything older has been dropped.  Consumers hold a
  // sequence number rather than an index so that trimming the tail cannot shift them.
  uint64_t g_firstSeq = 0;
  constexpr size_t MAX_PENDING_LINES = 2000;

  void pushChar(char c) {
    if (c == '\r') return;
    if (c == '\n') {
      // The firmware announces a fatal error on the console before halting in a handler that
      // waits for a button press; the emulator surfaces that instead of appearing to hang.
      if (g_pending.rfind("FATAL ERROR", 0) == 0) sim::noteFatalError(g_pending);
      g_lines.push_back(g_pending);
      g_pending.clear();
      if (g_lines.size() > MAX_PENDING_LINES) {
        const size_t dropped = g_lines.size() - MAX_PENDING_LINES;
        g_lines.erase(g_lines.begin(), g_lines.begin() + dropped);
        g_firstSeq += dropped;
      }
      return;
    }
    g_pending.push_back(c);
  }
}  // namespace

size_t HostConsole::write(uint8_t c) {
  fputc((int)c, stdout);
  std::lock_guard<std::mutex> lock(g_consoleMutex);
  pushChar((char)c);
  return 1;
}

size_t HostConsole::write(const uint8_t* buffer, size_t size) {
  fwrite(buffer, 1, size, stdout);
  std::lock_guard<std::mutex> lock(g_consoleMutex);
  for (size_t i = 0; i < size; i++) pushChar((char)buffer[i]);
  return size;
}

void HostConsole::flush() { fflush(stdout); }

uint64_t HostConsole::linesSince(uint64_t cursor, std::vector<std::string>& into) {
  std::lock_guard<std::mutex> lock(g_consoleMutex);
  const uint64_t end = g_firstSeq + g_lines.size();
  if (cursor < g_firstSeq) cursor = g_firstSeq;  // fell behind; resume at the oldest line held
  for (uint64_t seq = cursor; seq < end; seq++) into.push_back(g_lines[(size_t)(seq - g_firstSeq)]);
  return end;
}

uint64_t HostConsole::lineCount() {
  std::lock_guard<std::mutex> lock(g_consoleMutex);
  return g_firstSeq + g_lines.size();
}

size_t HostGpsSerial::write(uint8_t c) {
  sim::board().gpsTransmit(c);
  return 1;
}

int HostGpsSerial::available() { return sim::board().gpsAvailable(); }
int HostGpsSerial::read() { return sim::board().gpsRead(); }
int HostGpsSerial::peek() { return sim::board().gpsPeek(); }
