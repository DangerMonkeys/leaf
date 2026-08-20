#include "http_server.h"

#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <sstream>

#include "display_capture.h"
#include "runtime.h"
#include "scenario.h"
#include "sim/board.h"
#include "sim/clock.h"

namespace sim {

  namespace {

    const char* BASE64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string base64(const std::string& bytes) {
      std::string out;
      out.reserve((bytes.size() + 2) / 3 * 4);
      for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t b0 = (uint8_t)bytes[i];
        const uint32_t b1 = (i + 1 < bytes.size()) ? (uint8_t)bytes[i + 1] : 0;
        const uint32_t b2 = (i + 2 < bytes.size()) ? (uint8_t)bytes[i + 2] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(BASE64_CHARS[(triple >> 18) & 0x3F]);
        out.push_back(BASE64_CHARS[(triple >> 12) & 0x3F]);
        out.push_back(i + 1 < bytes.size() ? BASE64_CHARS[(triple >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < bytes.size() ? BASE64_CHARS[triple & 0x3F] : '=');
      }
      return out;
    }

    bool sendAll(int socket, const char* data, size_t length) {
      size_t sent = 0;
      while (sent < length) {
        const ssize_t n = ::send(socket, data + sent, length - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += (size_t)n;
      }
      return true;
    }

    bool sendString(int socket, const std::string& text) {
      return sendAll(socket, text.data(), text.size());
    }

    void sendResponse(int socket, const std::string& status, const std::string& contentType,
                      const std::string& body) {
      std::ostringstream header;
      header << "HTTP/1.1 " << status << "\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Cache-Control: no-store\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Connection: close\r\n\r\n";
      sendString(socket, header.str());
      sendAll(socket, body.data(), body.size());
    }

    void sendJson(int socket, const std::string& json) {
      sendResponse(socket, "200 OK", "application/json", json);
    }

    std::string queryValue(const std::string& target, const std::string& key) {
      const size_t question = target.find('?');
      if (question == std::string::npos) return "";
      std::string query = target.substr(question + 1);
      size_t start = 0;
      while (start < query.size()) {
        size_t end = query.find('&', start);
        if (end == std::string::npos) end = query.size();
        const std::string pair = query.substr(start, end - start);
        const size_t equals = pair.find('=');
        if (equals != std::string::npos && pair.substr(0, equals) == key) {
          return pair.substr(equals + 1);
        }
        start = end + 1;
      }
      return "";
    }

    std::string pathOf(const std::string& target) {
      const size_t question = target.find('?');
      return question == std::string::npos ? target : target.substr(0, question);
    }

    std::string jsonEscape(const std::string& text) {
      std::string out;
      for (char c : text) {
        switch (c) {
          case '"':
            out += "\\\"";
            break;
          case '\\':
            out += "\\\\";
            break;
          case '\n':
            out += "\\n";
            break;
          case '\r':
            break;
          case '\t':
            out += "\\t";
            break;
          default:
            if ((unsigned char)c < 0x20) {
              char buf[8];
              snprintf(buf, sizeof(buf), "\\u%04x", c);
              out += buf;
            } else {
              out += c;
            }
        }
      }
      return out;
    }

    std::string statusJson(const Status& s) {
      std::ostringstream out;
      out.setf(std::ios::fixed);
      out << "{"
          << "\"uptimeMs\":" << s.uptimeMs << ",\"speed\":" << s.clockSpeed
          << ",\"paused\":" << (s.paused ? "true" : "false")
          << ",\"poweredOn\":" << (s.poweredOn ? "true" : "false") << ",\"page\":\""
          << jsonEscape(s.page) << "\""
          << ",\"gps\":{\"fix\":" << (s.gpsFix ? "true" : "false")
          << ",\"satellites\":" << (int)s.satellites;
      out.precision(6);
      out << ",\"lat\":" << s.latitude << ",\"lon\":" << s.longitude;
      out.precision(2);
      out << ",\"altitudeM\":" << s.gpsAltitudeM << ",\"speedKmh\":" << s.speedKmh
          << ",\"courseDeg\":" << s.courseDeg << "}"
          << ",\"baro\":{\"altitudeM\":" << s.altitudeM << ",\"climbMps\":" << s.climbRateMps
          << ",\"pressureHpa\":" << s.pressureHpa << ",\"temperatureC\":" << s.temperatureC << "}"
          << ",\"power\":{\"batteryPercent\":" << (int)s.batteryPercent
          << ",\"batteryMv\":" << s.batteryMv << ",\"charging\":" << (s.charging ? "true" : "false")
          << "}"
          << ",\"wind\":{\"valid\":" << (s.windValid ? "true" : "false")
          << ",\"speedMps\":" << s.windSpeedMps << ",\"fromDeg\":" << s.windFromDeg
          << ",\"airspeedMps\":" << s.airspeedMps << "}"
          << ",\"cardMounted\":" << (s.cardMounted ? "true" : "false")
          << ",\"clockSynced\":" << (s.clockSynced ? "true" : "false")
          << ",\"logging\":" << (s.flightLogging ? "true" : "false")
          << ",\"flightTimer\":" << (s.flightTimerRunning ? "true" : "false")
          << ",\"speaker\":{\"toneHz\":" << s.toneHz << ",\"volume\":" << (int)s.volume << "}"
          << ",\"scenario\":{\"name\":\"" << jsonEscape(s.scenarioName)
          << "\",\"positionS\":" << s.scenarioPositionS << ",\"lengthS\":" << s.scenarioLengthS
          << ",\"playing\":" << (s.scenarioPlaying ? "true" : "false") << "}";
      out << "}";
      return out.str();
    }

    std::string frameJson(const Frame& frame, uint64_t sequence) {
      std::ostringstream out;
      out << "{\"seq\":" << sequence << ",\"width\":" << frame.width
          << ",\"height\":" << frame.height << ",\"pixels\":\"" << base64(packFrame(frame))
          << "\"}";
      return out.str();
    }

    std::string readFile(const std::string& path) {
      std::ifstream in(path, std::ios::binary);
      if (!in) return "";
      std::ostringstream buffer;
      buffer << in.rdbuf();
      return buffer.str();
    }

  }  // namespace

  // ---------------------------------------------------------------- server

  HttpServer::~HttpServer() { stop(); }

  bool HttpServer::start(uint16_t port) {
    // Close-on-exec throughout: a device restart replaces the process image, and a listening
    // socket inherited into the new one would still hold the port when it tries to bind.
    listenSocket_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listenSocket_ < 0) return false;

    int reuse = 1;
    setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (::bind(listenSocket_, (sockaddr*)&address, sizeof(address)) < 0) {
      ::close(listenSocket_);
      listenSocket_ = -1;
      return false;
    }
    if (::listen(listenSocket_, 16) < 0) {
      ::close(listenSocket_);
      listenSocket_ = -1;
      return false;
    }

    running_->store(true);
    acceptThread_ = std::thread([this] { acceptLoop(); });
    return true;
  }

  void HttpServer::stop() {
    if (!running_->load()) return;
    running_->store(false);
    if (listenSocket_ >= 0) {
      ::shutdown(listenSocket_, SHUT_RDWR);
      ::close(listenSocket_);
      listenSocket_ = -1;
    }
    if (acceptThread_.joinable()) acceptThread_.join();
  }

  void HttpServer::acceptLoop() {
    while (running_->load()) {
      const int client = ::accept4(listenSocket_, nullptr, nullptr, SOCK_CLOEXEC);
      if (client < 0) continue;
      // The flag goes with the thread rather than being read back off the server, which the
      // detached thread can outlive.
      std::thread([client, running = running_] {
        handleConnection(client, *running);
        ::close(client);
      }).detach();
    }
  }

  void HttpServer::handleConnection(int client, std::atomic<bool>& running) {
    // Read the request head, then the body if the headers promised one.
    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos) {
      const ssize_t n = ::recv(client, buffer, sizeof(buffer), 0);
      if (n <= 0) return;
      request.append(buffer, (size_t)n);
      if (request.size() > 1 << 20) return;
    }

    const size_t headEnd = request.find("\r\n\r\n") + 4;
    std::string head = request.substr(0, headEnd);
    std::string body = request.substr(headEnd);

    size_t contentLength = 0;
    const size_t lengthHeader = head.find("Content-Length:");
    if (lengthHeader != std::string::npos) {
      contentLength = (size_t)strtoul(head.c_str() + lengthHeader + 15, nullptr, 10);
    }
    while (body.size() < contentLength) {
      const ssize_t n = ::recv(client, buffer, sizeof(buffer), 0);
      if (n <= 0) break;
      body.append(buffer, (size_t)n);
    }

    std::istringstream requestLine(head);
    std::string method;
    std::string target;
    requestLine >> method >> target;
    const std::string path = pathOf(target);

    Runtime& device = runtime();

    // ---------------------------------------------------------------- static
    if (method == "GET" && (path == "/" || path == "/index.html")) {
      std::string page = readFile("sim/web/index.html");
      if (page.empty()) page = "<h1>leafsim</h1><p>sim/web/index.html not found.</p>";
      sendResponse(client, "200 OK", "text/html; charset=utf-8", page);
      return;
    }

    // ---------------------------------------------------------------- observation
    if (method == "GET" && path == "/api/state") {
      sendJson(client, statusJson(device.status()));
      return;
    }

    if (method == "GET" && path == "/api/frame") {
      sendJson(client, frameJson(device.frame(), device.frameSequence()));
      return;
    }

    if (method == "GET" && path == "/api/screenshot.png") {
      const std::string scaleText = queryValue(target, "scale");
      const int scale = scaleText.empty() ? 3 : atoi(scaleText.c_str());
      sendResponse(client, "200 OK", "image/png", device.screenshotPng(scale));
      return;
    }

    // The loaded recording's flight path, for the 3D view.  Sent once per load rather than
    // streamed: it is the same points every frame, and a five-minute flight is a few hundred of
    // them.
    if (method == "GET" && path == "/api/track") {
      const auto track = scenario().track();
      std::ostringstream out;
      out.setf(std::ios::fixed);
      out << "{\"count\":" << track.size() << ",\"points\":[";
      for (size_t i = 0; i < track.size(); i++) {
        if (i) out << ",";
        out.precision(1);
        out << "[" << track[i].atS << ",";
        out.precision(7);
        out << track[i].latitude << "," << track[i].longitude << ",";
        out.precision(1);
        out << track[i].altitudeM << "]";
      }
      out << "]}";
      sendJson(client, out.str());
      return;
    }

    if (method == "GET" && path == "/api/scenarios") {
      const auto files = Scenario::list(device.options().recordingsPath);
      std::ostringstream out;
      out << "{\"directory\":\"" << jsonEscape(device.options().recordingsPath) << "\",\"files\":[";
      for (size_t i = 0; i < files.size(); i++) {
        if (i) out << ",";
        out << "\"" << jsonEscape(files[i]) << "\"";
      }
      out << "]}";
      sendJson(client, out.str());
      return;
    }

    // ---------------------------------------------------------------- event stream
    if (method == "GET" && path == "/api/events") {
      const std::string header =
          "HTTP/1.1 200 OK\r\n"
          "Content-Type: text/event-stream\r\n"
          "Cache-Control: no-store\r\n"
          "Access-Control-Allow-Origin: *\r\n"
          "Connection: keep-alive\r\n\r\n";
      if (!sendString(client, header)) return;

      uint64_t lastFrame = UINT64_MAX;
      // Cursors of this connection's own, so several open tabs each see the whole stream
      // instead of dividing it between them, and a script's expect-serial keeps its lines.
      // The console starts at zero -- a tab opened late still gets the boot log -- while tones
      // start at the present, because replaying an hour of beeps at someone is not useful.
      uint64_t serialCursor = 0;
      uint64_t toneCursor = board().toneEventCount();
      while (running.load()) {
        const uint64_t sequence = device.frameSequence();
        if (sequence != lastFrame) {
          lastFrame = sequence;
          if (!sendString(client,
                          "event: frame\ndata: " + frameJson(device.frame(), sequence) + "\n\n")) {
            return;
          }
        }

        if (!sendString(client, "event: status\ndata: " + statusJson(device.status()) + "\n\n")) {
          return;
        }

        std::vector<std::string> lines;
        serialCursor = device.serialSince(serialCursor, lines);
        for (const std::string& line : lines) {
          if (!sendString(client, "event: serial\ndata: \"" + jsonEscape(line) + "\"\n\n")) return;
        }

        std::vector<ToneEvent> tones;
        toneCursor = board().toneEventsSince(toneCursor, tones);
        for (const ToneEvent& tone : tones) {
          std::ostringstream out;
          out << "event: tone\ndata: {\"atMs\":" << tone.atMs << ",\"hz\":" << tone.frequencyHz
              << ",\"volume\":" << (int)tone.volume << "}\n\n";
          if (!sendString(client, out.str())) return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      return;
    }

    // ---------------------------------------------------------------- control
    if (method == "POST") {
      JsonDocument request;
      const DeserializationError parseError = deserializeJson(request, body);
      if (parseError && !body.empty()) {
        sendJson(client, "{\"ok\":false,\"error\":\"invalid JSON body\"}");
        return;
      }

      if (path == "/api/button") {
        const std::string button = request["button"] | "";
        const std::string action = request["action"] | "click";
        // Applied here rather than queued: a button is a pin write, and a device halted in the
        // firmware's fatal-error handler is not draining the queue but is still reading pins.
        device.pressButton(button, action);
        sendJson(client, "{\"ok\":true}");
        return;
      }

      if (path == "/api/clock") {
        if (request["speed"].is<double>()) device.setSpeed(request["speed"].as<double>());
        if (request["paused"].is<bool>()) device.setPaused(request["paused"].as<bool>());
        if (request["stepMs"].is<uint32_t>())
          device.stepMilliseconds(request["stepMs"].as<uint32_t>());
        sendJson(client, "{\"ok\":true}");
        return;
      }

      if (path == "/api/scenario") {
        if (request["load"].is<const char*>()) {
          const std::string file = request["load"].as<const char*>();
          const std::string full = file.find('/') == std::string::npos
                                       ? device.options().recordingsPath + "/" + file
                                       : file;
          std::string error;
          if (!scenario().load(full, error)) {
            sendJson(client, "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}");
            return;
          }
        }
        if (request["seek"].is<double>()) scenario().seek(request["seek"].as<double>());
        if (request["play"].is<bool>()) {
          if (request["play"].as<bool>()) {
            scenario().play();
          } else {
            scenario().pause();
          }
        }
        sendJson(client, "{\"ok\":true}");
        return;
      }

      if (path == "/api/inject") {
        const std::string line = request["line"] | "";
        if (line.empty()) {
          sendJson(client, "{\"ok\":false,\"error\":\"no line\"}");
          return;
        }
        device.inject(line);
        sendJson(client, "{\"ok\":true}");
        return;
      }

      if (path == "/api/board") {
        if (request["batteryPercent"].is<int>()) {
          // power.cpp reads a divided battery voltage; work back from percent through the same
          // thresholds it uses, so the displayed percentage matches what was asked for.
          const int percent = request["batteryPercent"].as<int>();
          const uint32_t mv = 3250 + (uint32_t)((4080 - 3250) * (percent / 100.0));
          board().setBatteryMilliVolts(mv);
        }
        if (request["charging"].is<bool>()) {
          board().setCharging(request["charging"].as<bool>());
        }
        if (request["cardPresent"].is<bool>()) {
          const bool present = request["cardPresent"].as<bool>();
          device.post([present] { SD_MMC.simSetPresent(present); });
        }
        sendJson(client, "{\"ok\":true}");
        return;
      }

      if (path == "/api/restart") {
        device.post([&device] { device.restart(); });
        sendJson(client, "{\"ok\":true}");
        return;
      }
    }

    sendResponse(client, "404 Not Found", "text/plain", "no such endpoint\n");
  }

}  // namespace sim
