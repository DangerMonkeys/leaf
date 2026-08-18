// Recordings the emulator can play into the device.
//
// Three sources, one playback path: everything is normalised at load time into the bus-log line
// format (see dispatch/message_injector.h), which is the format the device itself records and the
// format sim/play_buslog.py sends.  So a synthetic flight and a captured one are replayed by the
// same code, and any loaded scenario can be written back out as a .log.
//
//   *.log   device bus logs: real captured GPS, IMU, pressure and ambient data
//   *.igc   flight tracklogs: GPS fixes and pressure altitude at 1Hz
//   *.json  synthetic scenarios: hand-authored flights (climb, circle, glide, approach)
#pragma once

#include <stdint.h>

#include <mutex>
#include <string>
#include <vector>

#include "dispatch/message_injector.h"

namespace sim {

  class Scenario {
   public:
    // One fix of the loaded recording, pulled back out for the flight-path view.  Built from the
    // normalised timeline rather than from each loader, so a bus log, an IGC and a synthetic
    // flight all yield a track by the same code path.
    struct TrackPoint {
      float atS = 0;
      double latitude = 0;
      double longitude = 0;
      float altitudeM = 0;
    };

    struct State {
      std::string name;
      double positionS = 0;
      double lengthS = 0;
      bool playing = false;
    };

    void setBus(etl::imessage_bus* bus) { injector_.setBus(bus); }
    etl::imessage_bus* injectorBus() const { return injector_.bus(); }

    // Loads and normalises a recording.  Returns false and fills `error` if the file cannot be
    // read or contains nothing playable.
    bool load(const std::string& path, std::string& error);

    void play();
    void pause();
    void seek(double seconds);

    // Called from the device loop: publishes everything due at the current device time.
    void update(uint32_t nowMs);

    State state() const;

    // Writes the normalised timeline as a .log, so a synthetic or IGC-derived scenario can be
    // replayed against real hardware with sim/play_buslog.py.
    bool exportLog(const std::string& path) const;

    // Recording files found in a directory, newest first.
    static std::vector<std::string> list(const std::string& directory);

    // The loaded recording's flight path.
    std::vector<TrackPoint> track() const;

   private:
    struct Event {
      uint32_t atMs = 0;
      std::string line;
    };

    // The loaders build into a caller-owned timeline, which only becomes the live one under the
    // lock: loading happens on the HTTP thread while the device thread is playing the old one.
    bool loadBusLog(const std::string& path, std::vector<Event>& into, std::string& error);
    bool loadIgc(const std::string& path, std::vector<Event>& into, std::string& error);
    bool loadSynthetic(const std::string& path, std::vector<Event>& into, std::string& error);
    void install(const std::string& path, std::vector<Event>& loaded);

    // Pulls the GGA sentences out of the normalised timeline into a flight path.
    static std::vector<TrackPoint> extractTrack(const std::vector<Event>& events);

    mutable std::mutex mutex_;
    std::vector<Event> events_;
    std::vector<TrackPoint> track_;
    std::string name_;
    size_t nextIndex_ = 0;
    bool playing_ = false;
    uint32_t lengthMs_ = 0;
    uint32_t positionMs_ = 0;
    // Device time corresponding to scenario time zero; shifted on seek and when paused.
    uint32_t originMs_ = 0;
    MessageInjector injector_;
  };

  Scenario& scenario();

  // Turns a pressure altitude in metres into the pressure the firmware would have measured,
  // in 100ths of hPa -- the exact inverse of Pressure::altitude().
  int32_t pressureFromAltitude(double metres);

}  // namespace sim
