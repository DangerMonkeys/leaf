#include "scenario.h"

#include <ArduinoJson.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include "sim/clock.h"

namespace sim {

  namespace {

    std::string extensionOf(const std::string& path) {
      const size_t dot = path.find_last_of('.');
      if (dot == std::string::npos) return "";
      std::string ext = path.substr(dot + 1);
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      return ext;
    }

    std::string baseNameOf(const std::string& path) {
      const size_t slash = path.find_last_of("/\\");
      return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    // Milliseconds field of a bus-log line: the digits between the type letter and the first
    // comma.  Returns false for lines that carry no timestamp (commands, bare comments).
    bool lineTimeMs(const std::string& line, uint32_t& out) {
      if (line.size() < 2) return false;
      const size_t comma = line.find(',', 1);
      if (comma == std::string::npos) return false;
      char* end = nullptr;
      const long value = strtol(line.substr(1, comma - 1).c_str(), &end, 10);
      if (!end || *end != '\0' || value < 0) return false;
      out = (uint32_t)value;
      return true;
    }

    // ---------------------------------------------------------------- NMEA synthesis

    std::string nmeaChecksummed(const std::string& body) {
      uint8_t checksum = 0;
      for (char c : body) checksum ^= (uint8_t)c;
      char buf[8];
      snprintf(buf, sizeof(buf), "*%02X", checksum);
      return "$" + body + buf;
    }

    void formatDegrees(double value, bool isLatitude, char* out, size_t outSize, char& hemisphere) {
      hemisphere = isLatitude ? (value < 0 ? 'S' : 'N') : (value < 0 ? 'W' : 'E');
      const double absolute = fabs(value);
      const int degrees = (int)absolute;
      const double minutes = (absolute - degrees) * 60.0;
      snprintf(out, outSize, isLatitude ? "%02d%07.4f" : "%03d%07.4f", degrees, minutes);
    }

    struct Fix {
      uint32_t atMs = 0;
      int hour = 12;
      int minute = 0;
      int second = 0;
      double latitude = 0;
      double longitude = 0;
      double gnssAltitudeM = 0;
      double speedKnots = 0;
      double courseDeg = 0;
      uint8_t satellites = 8;
    };

    // One fix, as the LC86G reports it.
    //
    // The talker IDs matter: the firmware binds its fix-quality field to GNGGA and its fix mode to
    // GNGSA (instruments/gps.cpp), because the receiver is multi-constellation and emits GN
    // sentences.  Synthesising GP sentences instead parses as a position but never sets
    // fixInfo.fix, and everything gated on hasUsableFix() -- glide ratio, average speed, distance
    // flown, IGC logging, flight auto-start -- silently stays blank.  Satellites in view come from
    // GPGSV, which keeps its GP talker on this receiver.
    void appendFixSentences(const Fix& fix, std::vector<std::string>& lines) {
      char time[16];
      snprintf(time, sizeof(time), "%02d%02d%02d.00", fix.hour, fix.minute, fix.second);

      char lat[16];
      char lon[16];
      char latHemisphere = 'N';
      char lonHemisphere = 'E';
      formatDegrees(fix.latitude, true, lat, sizeof(lat), latHemisphere);
      formatDegrees(fix.longitude, false, lon, sizeof(lon), lonHemisphere);

      // Position, altitude, satellite count, and fix quality 1 (GPS fix).
      char gga[160];
      snprintf(gga, sizeof(gga), "GNGGA,%s,%s,%c,%s,%c,1,%02u,0.9,%.1f,M,47.0,M,,", time, lat,
               latHemisphere, lon, lonHemisphere, fix.satellites, fix.gnssAltitudeM);
      lines.push_back(nmeaChecksummed(gga));

      // Date, ground speed and track.
      char rmc[160];
      snprintf(rmc, sizeof(rmc), "GNRMC,%s,A,%s,%c,%s,%c,%.2f,%.2f,010125,,,A", time, lat,
               latHemisphere, lon, lonHemisphere, fix.speedKnots, fix.courseDeg);
      lines.push_back(nmeaChecksummed(rmc));

      // Fix mode 3 (3D), with the DOP values the firmware's accuracy display reads.
      lines.push_back(nmeaChecksummed("GNGSA,A,3,01,02,03,04,05,06,07,08,,,,,1.8,0.9,1.5"));

      // Satellites in view, four per message, for the satellite screen.
      char gsv1[160];
      char gsv2[160];
      snprintf(gsv1, sizeof(gsv1),
               "GPGSV,2,1,%02u,01,72,035,44,02,58,120,42,03,45,210,40,04,38,290,38",
               fix.satellites);
      snprintf(gsv2, sizeof(gsv2),
               "GPGSV,2,2,%02u,05,31,015,36,06,25,175,34,07,18,255,31,08,12,330,28",
               fix.satellites);
      lines.push_back(nmeaChecksummed(gsv1));
      lines.push_back(nmeaChecksummed(gsv2));
    }

    // Motion for a synthesised flight.
    //
    // Recorded bus logs carry the real IMU's fused output; a synthesised flight has to invent it.
    // What the firmware does with these values is estimate gravity and extract vertical
    // acceleration, so the useful thing to synthesise is a level wing carrying its load factor:
    // 1g plus whatever vertical acceleration the flight path implies, with the aircraft upright.
    // Bank angle is deliberately not modelled -- inventing an orientation the accelerometer does
    // not agree with would teach the vario's fusion the wrong thing.
    std::string motionLine(uint32_t atMs, double verticalAccelG) {
      char buf[128];
      snprintf(buf, sizeof(buf), "M%u,A,0.0000,0.0000,%.4f,Q,0.0000,0.0000,0.0000", atMs,
               1.0 + verticalAccelG);
      return buf;
    }

    constexpr int MOTION_HZ = 20;  // the rate the device's DMP is configured to produce

    constexpr double RADIANS_PER_DEGREE = M_PI / 180.0;
    constexpr double METRES_PER_DEGREE_LAT = 111320.0;
    constexpr double METRES_PER_SECOND_TO_KNOTS = 1.94384;

  }  // namespace

  int32_t pressureFromAltitude(double metres) {
    // Inverse of Pressure::altitude(): metres = 44330 * (1 - (hPa/1013.25)^0.1903)
    constexpr double seaLevelPressure = 1013.25;
    constexpr double scaleHeight = 44330.0;
    const double ratio = 1.0 - metres / scaleHeight;
    const double hpa = seaLevelPressure * pow(ratio > 0 ? ratio : 0.0, 1.0 / 0.1903);
    return (int32_t)lround(hpa * 100.0);
  }

  Scenario& scenario() {
    static Scenario instance;
    return instance;
  }

  // ---------------------------------------------------------------- loading

  bool Scenario::load(const std::string& path, std::string& error) {
    const std::string ext = extensionOf(path);
    std::vector<Event> loaded;
    bool ok = false;
    if (ext == "log" || ext == "txt") {
      ok = loadBusLog(path, loaded, error);
    } else if (ext == "igc") {
      ok = loadIgc(path, loaded, error);
    } else if (ext == "json") {
      ok = loadSynthetic(path, loaded, error);
    } else {
      error = "unsupported recording type '." + ext + "' (expected .log, .igc or .json)";
      return false;
    }

    if (!ok) return false;
    if (loaded.empty()) {
      error = "recording contains no playable messages";
      return false;
    }
    install(path, loaded);
    return true;
  }

  // Degrees from an NMEA ddmm.mmmm / dddmm.mmmm field.
  namespace {
    double degreesFromNmea(const std::string& value, int degreeDigits) {
      if (value.size() < (size_t)degreeDigits + 1) return NAN;
      const double degrees = atof(value.substr(0, degreeDigits).c_str());
      const double minutes = atof(value.substr(degreeDigits).c_str());
      return degrees + minutes / 60.0;
    }

    std::vector<std::string> splitFields(const std::string& sentence) {
      std::vector<std::string> fields;
      std::string current;
      for (char c : sentence) {
        if (c == ',') {
          fields.push_back(current);
          current.clear();
        } else if (c == '*') {
          break;
        } else {
          current.push_back(c);
        }
      }
      fields.push_back(current);
      return fields;
    }
  }  // namespace

  std::vector<Scenario::TrackPoint> Scenario::extractTrack(const std::vector<Event>& events) {
    std::vector<TrackPoint> track;
    for (const Event& event : events) {
      // Only GPS lines carry a position, and only GGA carries an altitude with it.
      if (event.line.empty() || event.line[0] != 'G') continue;
      const size_t dollar = event.line.find('$');
      if (dollar == std::string::npos) continue;
      if (event.line.compare(dollar + 1, 5, "GNGGA") != 0 &&
          event.line.compare(dollar + 1, 5, "GPGGA") != 0) {
        continue;
      }

      const std::vector<std::string> f = splitFields(event.line.substr(dollar + 1));
      if (f.size() < 10) continue;
      if (f[6] == "0" || f[6].empty()) continue;  // no fix

      const double lat = degreesFromNmea(f[2], 2) * (f[3] == "S" ? -1.0 : 1.0);
      const double lon = degreesFromNmea(f[4], 3) * (f[5] == "W" ? -1.0 : 1.0);
      if (isnan(lat) || isnan(lon)) continue;

      TrackPoint point;
      point.atS = event.atMs / 1000.0f;
      point.latitude = lat;
      point.longitude = lon;
      point.altitudeM = (float)atof(f[9].c_str());
      track.push_back(point);
    }
    return track;
  }

  std::vector<Scenario::TrackPoint> Scenario::track() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return track_;
  }

  void Scenario::install(const std::string& path, std::vector<Event>& loaded) {
    std::stable_sort(loaded.begin(), loaded.end(),
                     [](const Event& a, const Event& b) { return a.atMs < b.atMs; });
    std::vector<TrackPoint> track = extractTrack(loaded);

    std::lock_guard<std::mutex> lock(mutex_);
    events_.swap(loaded);
    track_.swap(track);
    name_ = baseNameOf(path);
    lengthMs_ = events_.empty() ? 0 : events_.back().atMs;
    nextIndex_ = 0;
    positionMs_ = 0;
    playing_ = false;
    originMs_ = clock().millis();
  }

  bool Scenario::loadBusLog(const std::string& path, std::vector<Event>& into, std::string& error) {
    std::ifstream in(path);
    if (!in) {
      error = "cannot open " + path;
      return false;
    }
    std::string line;
    while (std::getline(in, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
      if (line.empty()) continue;
      // 'V' is the version header BusLogger writes; '#' lines are its comments.
      if (line[0] == 'V' || line[0] == '#') continue;
      uint32_t atMs = 0;
      if (!lineTimeMs(line, atMs)) continue;
      into.push_back({atMs, line});
    }
    return true;
  }

  bool Scenario::loadIgc(const std::string& path, std::vector<Event>& into, std::string& error) {
    std::ifstream in(path);
    if (!in) {
      error = "cannot open " + path;
      return false;
    }

    std::string line;
    bool haveFirst = false;
    int firstSecondOfDay = 0;
    double previousLat = 0;
    double previousLon = 0;
    int previousSecond = 0;

    while (std::getline(in, line)) {
      // B HHMMSS DDMMmmm N DDDMMmmm E A PPPPP GGGGG
      if (line.size() < 35 || line[0] != 'B') continue;

      const auto digits = [&line](size_t offset, size_t count) {
        return atoi(line.substr(offset, count).c_str());
      };

      const int hour = digits(1, 2);
      const int minute = digits(3, 2);
      const int second = digits(5, 2);
      const int secondOfDay = hour * 3600 + minute * 60 + second;

      const double latitude =
          (digits(7, 2) + digits(9, 5) / 60000.0) * (line[14] == 'S' ? -1.0 : 1.0);
      const double longitude =
          (digits(15, 3) + digits(18, 5) / 60000.0) * (line[23] == 'W' ? -1.0 : 1.0);
      const double pressureAltitude = digits(25, 5);
      const double gnssAltitude = digits(30, 5);

      if (!haveFirst) {
        haveFirst = true;
        firstSecondOfDay = secondOfDay;
        previousLat = latitude;
        previousLon = longitude;
        previousSecond = secondOfDay;
      }

      const uint32_t atMs = (uint32_t)((secondOfDay - firstSecondOfDay) * 1000);

      // IGC has no speed or heading, so derive them from consecutive fixes: that is what the
      // receiver reports on a real flight, and the firmware's wind and navigation code needs it.
      const int dt = secondOfDay - previousSecond;
      double speedKnots = 0;
      double courseDeg = 0;
      if (dt > 0) {
        const double dLat = (latitude - previousLat) * METRES_PER_DEGREE_LAT;
        const double dLon =
            (longitude - previousLon) * METRES_PER_DEGREE_LAT * cos(latitude * RADIANS_PER_DEGREE);
        const double distance = sqrt(dLat * dLat + dLon * dLon);
        speedKnots = (distance / dt) * METRES_PER_SECOND_TO_KNOTS;
        courseDeg = atan2(dLon, dLat) / RADIANS_PER_DEGREE;
        if (courseDeg < 0) courseDeg += 360.0;
      }

      Fix fix;
      fix.atMs = atMs;
      fix.hour = hour;
      fix.minute = minute;
      fix.second = second;
      fix.latitude = latitude;
      fix.longitude = longitude;
      fix.gnssAltitudeM = gnssAltitude;
      fix.speedKnots = speedKnots;
      fix.courseDeg = courseDeg;

      std::vector<std::string> sentences;
      appendFixSentences(fix, sentences);
      for (const auto& sentence : sentences) {
        into.push_back({atMs, "G" + std::to_string(atMs) + "," + sentence});
      }

      // Pressure at 4Hz between fixes, interpolated from the tracklog's pressure altitude: the
      // vario reads climb from pressure, and 1Hz steps would make it read as a staircase.
      for (int sub = 0; sub < 4; sub++) {
        const uint32_t subMs = atMs + (uint32_t)(sub * 250);
        char buf[48];
        snprintf(buf, sizeof(buf), "P%u,%d", subMs, pressureFromAltitude(pressureAltitude));
        into.push_back({subMs, buf});
      }

      // A tracklog has no IMU data, so the emulator supplies level flight at 1g.  Without it the
      // firmware never finishes its startup checks and sits on the splash screen, exactly as a
      // device with a dead IMU would.
      for (int sub = 0; sub < MOTION_HZ; sub++) {
        const uint32_t subMs = atMs + (uint32_t)(sub * (1000 / MOTION_HZ));
        into.push_back({subMs, motionLine(subMs, 0.0)});
      }

      previousLat = latitude;
      previousLon = longitude;
      previousSecond = secondOfDay;
    }

    if (!haveFirst) {
      error = "no B records found in " + path;
      return false;
    }
    return true;
  }

  bool Scenario::loadSynthetic(const std::string& path, std::vector<Event>& into,
                               std::string& error) {
    std::ifstream in(path);
    if (!in) {
      error = "cannot open " + path;
      return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    JsonDocument doc;
    const DeserializationError parseError = deserializeJson(doc, text);
    if (parseError) {
      error = std::string("cannot parse ") + path + ": " + parseError.c_str();
      return false;
    }

    // A synthetic scenario is a start state plus a list of legs.  Each leg holds a heading, speed
    // and climb rate for a duration; "turnRate" makes it a circle, which is how thermals and
    // circuits get authored.
    double latitude = doc["start"]["lat"] | 47.5;
    double longitude = doc["start"]["lon"] | 11.2;
    double altitude = doc["start"]["altitudeM"] | 1500.0;
    double heading = doc["start"]["headingDeg"] | 0.0;
    const double temperature = doc["ambient"]["temperatureC"] | 15.0;
    const double humidity = doc["ambient"]["humidity"] | 50.0;
    const double windSpeed = doc["wind"]["speedMps"] | 0.0;
    const double windFrom = doc["wind"]["fromDeg"] | 0.0;

    uint32_t atMs = 0;
    int elapsedSeconds = 0;
    const int startHour = doc["start"]["hour"] | 12;

    JsonArray legs = doc["legs"].as<JsonArray>();
    if (legs.isNull() || legs.size() == 0) {
      error = "scenario has no legs";
      return false;
    }

    // Carried across legs so the transition between them shows up as vertical acceleration, and
    // started at the first leg's climb rate so the flight does not begin with a phantom jolt.
    double previousClimbRate = legs[0]["climbMps"] | 0.0;

    // Wind is applied as drift: the aircraft flies its airspeed through a moving air mass, which
    // is what makes wind estimation do anything interesting.
    const double windToRad = (windFrom + 180.0) * RADIANS_PER_DEGREE;
    const double windNorth = windSpeed * cos(windToRad);
    const double windEast = windSpeed * sin(windToRad);

    for (JsonObject leg : legs) {
      const double durationS = leg["durationS"] | 60.0;
      const double airspeed = leg["airspeedMps"] | 10.0;
      const double climbRate = leg["climbMps"] | 0.0;
      const double turnRate = leg["turnRateDegPerS"] | 0.0;
      if (leg["headingDeg"].is<double>()) heading = leg["headingDeg"].as<double>();

      const int steps = (int)(durationS * 4);  // 4Hz integration, matching the pressure rate
      for (int step = 0; step < steps; step++) {
        const double dt = 0.25;
        heading += turnRate * dt;
        while (heading >= 360.0) heading -= 360.0;
        while (heading < 0.0) heading += 360.0;

        const double headingRad = heading * RADIANS_PER_DEGREE;
        const double groundNorth = airspeed * cos(headingRad) + windNorth;
        const double groundEast = airspeed * sin(headingRad) + windEast;

        latitude += (groundNorth * dt) / METRES_PER_DEGREE_LAT;
        longitude +=
            (groundEast * dt) / (METRES_PER_DEGREE_LAT * cos(latitude * RADIANS_PER_DEGREE));
        altitude += climbRate * dt;

        char pressureLine[48];
        snprintf(pressureLine, sizeof(pressureLine), "P%u,%d", atMs,
                 pressureFromAltitude(altitude));
        into.push_back({atMs, pressureLine});

        // Motion at the IMU's own rate, carrying the vertical acceleration this step's change in
        // climb rate implies.  Constant-rate legs are 1g; the transitions between them are not.
        const double verticalAccelG = (climbRate - previousClimbRate) / (dt * 9.80665);
        previousClimbRate = climbRate;
        for (int sub = 0; sub < MOTION_HZ / 4; sub++) {
          const uint32_t motionMs = atMs + (uint32_t)(sub * (1000 / MOTION_HZ));
          into.push_back({motionMs, motionLine(motionMs, sub == 0 ? verticalAccelG : 0.0)});
        }

        // One GPS fix per second, as the receiver produces.
        if (step % 4 == 0) {
          const double groundSpeed = sqrt(groundNorth * groundNorth + groundEast * groundEast);
          double track = atan2(groundEast, groundNorth) / RADIANS_PER_DEGREE;
          if (track < 0) track += 360.0;

          Fix fix;
          fix.atMs = atMs;
          fix.hour = (startHour + elapsedSeconds / 3600) % 24;
          fix.minute = (elapsedSeconds / 60) % 60;
          fix.second = elapsedSeconds % 60;
          fix.latitude = latitude;
          fix.longitude = longitude;
          fix.gnssAltitudeM = altitude;
          fix.speedKnots = groundSpeed * METRES_PER_SECOND_TO_KNOTS;
          fix.courseDeg = track;

          std::vector<std::string> sentences;
          appendFixSentences(fix, sentences);
          for (const auto& sentence : sentences) {
            into.push_back({atMs, "G" + std::to_string(atMs) + "," + sentence});
          }
          elapsedSeconds++;
        }

        // Ambient every 10 seconds, as the AHT20 task produces it.
        if (atMs % 10000 == 0) {
          char ambientLine[64];
          snprintf(ambientLine, sizeof(ambientLine), "A%u,%.1f,%.1f", atMs, temperature, humidity);
          into.push_back({atMs, ambientLine});
        }

        atMs += 250;
      }
    }

    return true;
  }

  // ---------------------------------------------------------------- playback

  void Scenario::play() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.empty()) return;
    playing_ = true;
    // Anchor scenario time to device time so playback resumes where it paused.
    originMs_ = clock().millis() - positionMs_;
    injector_.resetReferenceTime();
  }

  void Scenario::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    playing_ = false;
  }

  void Scenario::seek(double seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (seconds < 0) seconds = 0;
    positionMs_ = (uint32_t)(seconds * 1000);
    originMs_ = clock().millis() - positionMs_;
    nextIndex_ = 0;
    while (nextIndex_ < events_.size() && events_[nextIndex_].atMs < positionMs_) nextIndex_++;
    injector_.resetReferenceTime();
  }

  void Scenario::update(uint32_t nowMs) {
    std::vector<std::string> due;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!playing_ || events_.empty()) return;

      positionMs_ = nowMs - originMs_;
      while (nextIndex_ < events_.size() && events_[nextIndex_].atMs <= positionMs_) {
        due.push_back(events_[nextIndex_].line);
        nextIndex_++;
      }
      if (nextIndex_ >= events_.size()) playing_ = false;
    }

    // Published outside the lock: handlers run the whole firmware pipeline, and a scenario
    // control request arriving from the HTTP thread must not block behind them.
    for (const std::string& line : due) {
      injector_.handleLine(line.c_str(), line.size());
    }
  }

  Scenario::State Scenario::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    State s;
    s.name = name_;
    s.positionS = positionMs_ / 1000.0;
    s.lengthS = lengthMs_ / 1000.0;
    s.playing = playing_;
    return s;
  }

  bool Scenario::exportLog(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << "V" << name_ << " (exported by leafsim)\n";
    for (const Event& event : events_) out << event.line << "\n";
    return true;
  }

  std::vector<std::string> Scenario::list(const std::string& directory) {
    std::vector<std::string> found;
    DIR* dir = opendir(directory.c_str());
    if (!dir) return found;
    while (struct dirent* entry = readdir(dir)) {
      const std::string name = entry->d_name;
      const std::string ext = extensionOf(name);
      if (ext == "log" || ext == "igc" || ext == "json") found.push_back(name);
    }
    closedir(dir);
    std::sort(found.begin(), found.end());
    return found;
  }

}  // namespace sim
