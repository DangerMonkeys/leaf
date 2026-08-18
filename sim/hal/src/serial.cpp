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
  std::vector<std::string> g_lines;  // completed lines waiting for the UI
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
        g_lines.erase(g_lines.begin(), g_lines.begin() + (g_lines.size() - MAX_PENDING_LINES));
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

std::vector<std::string> HostConsole::drainLines() {
  std::lock_guard<std::mutex> lock(g_consoleMutex);
  std::vector<std::string> out;
  out.swap(g_lines);
  return out;
}

size_t HostGpsSerial::write(uint8_t c) {
  sim::board().gpsTransmit(c);
  return 1;
}

int HostGpsSerial::available() { return sim::board().gpsAvailable(); }
int HostGpsSerial::read() { return sim::board().gpsRead(); }
int HostGpsSerial::peek() { return sim::board().gpsPeek(); }
