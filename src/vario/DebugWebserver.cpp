#include "DebugWebserver.h"
#include <WiFi.h>
#include "SDcard.h"
#include "WebServer.h"
#include "etl/string_stream.h"
#include "fanet_radio.h"
#include "lock_guard.h"
#include "ui/display.h"

WebServer server;
String send_buffer = "";

constexpr auto endl = "\n";

void writeScreenshotBuffer(const char* buffer) {
  Serial.println("Writing screenshot buffer");
  send_buffer += buffer;
}

class UnsafeManager : public Fanet::FanetManager {
  friend void webserver_setup();
};

void webserver_setup() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", R"(
      <!DOCTYPE html>
      <html>
        <head>
          <title>ESP32 Vario</title>
          <style>
            body { font-family: Arial, sans-serif; margin: 2em auto; max-width: 800px; }
          </style>
        </head>
        <body>
          <h1>ESP32 Vario Debug Webserver</h1>
          <ul>
            <li><a href="/screenshot" target="_blank">Download Screenshot</a></li>
            <li><a href="/mass_storage" target="_blank">Start Mass Storage</a></li>
            <li><a href="/fanet" target="_blank">FANet Message Stats</a></li>
          </ul>
        </body>
      </html>
    )");
  });

  server.on("/raw-xbm", HTTP_GET, []() {
    send_buffer = "";
    u8g2_WriteBufferXBM(u8g2.getU8g2(), writeScreenshotBuffer);
    server.send(200, "image/x-xbitmap", send_buffer);
  });

  server.on("/fanet", HTTP_GET, []() {
    etl::string<1024> str;
    etl::string_stream ss(str);
    auto& radio = FanetRadio::getInstance();
    auto radioStats = radio.getStats();

    // Lock the radio while we poke around their private parts
    LockGuard lock(radio.x_fanet_manager_mutex);
    UnsafeManager* manager = (UnsafeManager*)radio.manager;
    auto ms = millis();

    ss << "<!DOCTYPE html>\n"
       << "<html>\n"
       << "<script>\n"
       << "setTimeout(() => location.reload(), 1000);\n"
       << "</script>\n"
       << "<body><pre>\n"

       << "Current Time: " << ms << endl
       << endl
       << "-- STATS -- " << endl
       << "State: " << FanetRadio::getInstance().getState().c_str() << "\n"
       << "rx: " << radioStats.rx << "\n"
       << "txSuccess: " << radioStats.txSuccess << "\n"
       << "txFailed: " << radioStats.txFailed << "\n"
       << "processed: " << radioStats.processed << "\n"
       << "forwarded: " << radioStats.forwarded << "\n"
       << "fwdMinRssiDrp: " << radioStats.fwdMinRssiDrp << "\n"
       << "fwdNeighborDrp: " << radioStats.fwdNeighborDrp << "\n"
       << "fwdEnqueuedDrp: " << radioStats.fwdEnqueuedDrop << "\n"
       << "fwdDbBoostDrop: " << radioStats.fwdDbBoostDrop << "\n"
       << "rxFromUsDrp: " << radioStats.rxFromUsDrp << "\n"
       << "txAck: " << radioStats.txAck << "\n"
       << "neighbors: " << radioStats.neighborTableSize << "\n"
       << "-- Manager --" << endl
       << "Manager Queue Length: " << manager->txQueue.size() << endl
       << "Next TX Time: "
       << (manager->txQueue.size() ? (manager->nextTxTime(ms).value() - ms) / 1000.0 : 0) << endl
       << "Next allowed tracking time: " << (manager->nextAllowedTrackingTime - ms) / 1000.0 << endl
       << "Last Location Sent Ago: " << (ms - manager->lastLocationSentMs) / 1000.0 << endl

       << "</pre></body>\n"
       << "</html>\n";

    server.send(200, "text/html", str.c_str());
  });

  server.on("/screenshot", HTTP_GET, []() {
    server.send(200, "text/html", R"(
        <!DOCTYPE html>
        <html>
        <head>
        <title>Screenshot</title>
        </head>
        <body>

        <canvas id="screenshotCanvas" width="200" height="200" style="border:0px solid #000;"></canvas>

        <script>
        function drawXBM(xbmData, canvasId) {
          const canvas = document.getElementById(canvasId);
          const ctx = canvas.getContext('2d');

          // Extract width and height (using regex, adjust as needed)
          const widthMatch = xbmData.match(/width (\d+)/);
          const heightMatch = xbmData.match(/height (\d+)/);
          const width = parseInt(widthMatch[1]);
          const height = parseInt(heightMatch[1]);

          // Extract bitmap data (assuming hex format)
          const dataMatch = xbmData.match(/bits\[\] = {(.*?)}/s); // Use /s for multiline matching
          const dataStr = dataMatch[1].replace(/,/g, ''); // Remove commas
          const data = new Uint8Array(dataStr.match(/[\da-f]{2}/gi).map(byte => parseInt(byte, 16)));

          let index = 0;
          for (let y = 0; y < height; y++) {
            for (let x = 0; x < width; x++) {
              const byteIndex = Math.floor(index / 8);
              const bitIndex = index % 8;
              const pixelValue = (data[byteIndex] >> bitIndex) & 1;

              if (pixelValue) {
                ctx.fillStyle = 'black'; // Or any color you want
              } else {
                ctx.fillStyle = 'white'; // Background color
              }

              // Data is rotated and mirrored, so this should rotate it again
              ctx.fillRect(y, width - x - 1, 1, 1);
              index++;
            }
          }
        }


        fetch('/raw-xbm')
          .then(response => response.text())
          .then(xbmData => {
            drawXBM(xbmData, 'screenshotCanvas');
          })
          .catch(error => {
            console.error('Error fetching XBM data:', error);
          });

        </script>

        </body>
        </html>
    )");
  });

  server.on("/mass_storage", HTTP_GET, []() {
    // Serial.end();
    SDCard_SetupMassStorage();
    server.send(200, "text/html", "OK!");
  });

  // Give it a chance to connect so this debug message means something.
  delay(250);
  // The captive portal belongs on port 80 for setting up WiFi.  Keep the debug
  // webserver on port 81.
  server.begin(81);
  Serial.printf("Webserver started: http://%s:81/\n", WiFi.localIP().toString());
}

void webserver_loop() { server.handleClient(); }