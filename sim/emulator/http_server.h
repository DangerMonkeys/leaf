// The emulator's control surface: a small HTTP server for the browser panel and for scripts.
//
// Every request is handled on its own thread.  Most are queued for the device thread; buttons,
// board state and the clock are applied directly, on state guarded for it -- see runtime.h for
// why those three cannot be queued.
//
//   GET  /                      the control panel
//   GET  /api/state             device status as JSON
//   GET  /api/frame             the current screen (1bpp, base64) with a sequence number
//   GET  /api/events            server-sent events: screen, status, serial output, speaker tones
//   GET  /api/screenshot.png    PNG of the current screen (?scale=N)
//   GET  /api/scenarios         recordings available to load
//   GET  /api/track             the loaded recording as [time, lat, lon, altitude] fixes
//   POST /api/button            {"button":"CENTER","action":"click"|"down"|"up"}
//   POST /api/clock             {"speed":10} {"paused":true} {"stepMs":500}
//   POST /api/scenario          {"load":"flight.igc"} {"play":true} {"seek":42.0}
//   POST /api/inject            {"line":"P1234,92310"} -- one bus-log line
//   POST /api/board             {"batteryPercent":42,"charging":true,"cardPresent":false}
//   POST /api/restart           reboots the emulated device
#pragma once

#include <stdint.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace sim {

  class HttpServer {
   public:
    ~HttpServer();

    bool start(uint16_t port);
    void stop();

   private:
    void acceptLoop();
    static void handleConnection(int client, std::atomic<bool>& running);

    int listenSocket_ = -1;
    // Shared rather than a member flag: connection threads are detached and an event stream only
    // notices the shutdown on its next 50ms pass, which is long after the server itself is gone.
    std::shared_ptr<std::atomic<bool>> running_ = std::make_shared<std::atomic<bool>>(false);
    std::thread acceptThread_;
  };

}  // namespace sim
