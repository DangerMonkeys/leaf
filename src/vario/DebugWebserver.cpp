#include "DebugWebserver.h"
#include <WiFi.h>
#include "SDcard.h"
#include "WebServer.h"
#include "display.h"

WebServer server;
String send_buffer = "";

void writeScreenshotBuffer(const char* buffer) {
  Serial.println("Writing screenshot buffer");
  send_buffer += buffer;
}

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
          </ul>
        </body>
      </html>
    )");
  });

  server.on("/screenshot", HTTP_GET, []() {
    send_buffer = "";
    u8g2_WriteBufferXBM(u8g2.getU8g2(), writeScreenshotBuffer);
    server.send(200, "image/x-xbitmap", send_buffer);
  });

  server.on("/mass_storage", HTTP_GET, []() {
    // Serial.end();
    SDCard_SetupMassStorage();
    server.send(200, "text/html", "OK!");
  });

  // Give it a chance to connect so this debug message means something
  delay(1000);
  server.begin();
  Serial.println("Webserver started: http://" + WiFi.localIP().toString());
}

void webserver_loop() {
  server.handleClient();
}