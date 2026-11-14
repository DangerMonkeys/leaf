#ifdef LEAF_USB_BENCH_CONSOLE

#include "diagnostics/bench_console.h"

#include <WiFi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_random.h"
#include "mbedtls/ssl.h"

#include "comms/fanet_radio.h"
#include "comms/leaf_log_sync.h"
#include "comms/webserver.h"
#include "diagnostics/heap_monitor.h"
#include "hardware/buttons.h"
#include "instruments/baro.h"
#include "instruments/gps.h"
#include "logging/buslog.h"
#include "logging/log.h"
#include "power.h"
#include "storage/sd_card.h"
#include "system/usb_serial.h"
#include "system/version_info.h"
#include "ui/display/display.h"
#include "ui/display/pages.h"
#include "ui/settings/settings.h"
#include "utils/magic_enum.h"

namespace bench_console {
  namespace {
    // One reply fits the 64-byte CDC TX FIFO. Keep responses queued until they fit so
    // concurrent diagnostic output cannot interleave with a protocol line.
    constexpr size_t lineSize = 64;
    constexpr size_t readBudget = 32;
    char request[lineSize];
    char reply[lineSize + 1];
    size_t length = 0;
    size_t replyLength = 0;
    bool invalidLine = false;

    template <typename... Args>
    void respond(const char* format, Args... args) {
      const int count = snprintf(reply, sizeof(reply), format, args...);
      if (count > 0 && size_t(count) <= lineSize) {
        replyLength = size_t(count);
      } else {
        constexpr char error[] = "BENCH error reply_size\n";
        memcpy(reply, error, sizeof(error));
        replyLength = sizeof(error) - 1;
      }
    }

    void handleRequest() {
      char* cursor = nullptr;
      char* command = strtok_r(request, " ", &cursor);
      if (!command) return;
      const bool hold = !strcmp(command, "hold");
      const bool buttonCommand = hold || !strcmp(command, "press");
      const char* name = strtok_r(nullptr, " ", &cursor);
      if (name && !buttonCommand) {
        respond("BENCH error arguments\n");
        return;
      }
      if (!strcmp(command, "status")) {
        const auto page = magic_enum::enum_name(display.getPage());
        const auto state = magic_enum::enum_name(power.info().onState);
        const auto* modal = mainMenuPage.get_modal_page();
        const int cursorPosition = modal ? modal->benchCursor() : mainMenuPage.benchCursor();
        respond("BENCH status %s %s %d %u %d %d %d %d %d\n", state.data(), page.data(),
                cursorPosition, (unsigned)mainMenuPage.benchModalDepth(),
                settings.system_showWarning && display.displayingWarning(),
                baro.climbRateFilteredValid(), gps.hasUsableFix(), flightTimer_isRunning(),
                sdcard.firmwareCanAccessFilesystem());
      } else if (!strcmp(command, "heap")) {
        heap_monitor::Snapshot sample{};
        heap_monitor::snapshot(sample);
        respond("BENCH heap %lu %lu %lu %lu\n", (unsigned long)sample.internalFree,
                (unsigned long)sample.internalMin, (unsigned long)sample.internalLargest,
                (unsigned long)sample.loopStack);
      } else if (!strcmp(command, "stacks")) {
        heap_monitor::Snapshot sample{};
        heap_monitor::snapshot(sample);
        respond("BENCH stacks %lu %lu %lu %lu %lu\n", (unsigned long)sample.loopStack,
                (unsigned long)sample.bleStack, (unsigned long)sample.fanetTxStack,
                (unsigned long)sample.fanetRxStack, (unsigned long)leafLogSync.uploadStackUnused());
      } else if (!strcmp(command, "tlsheap")) {
        mbedtls_ssl_context ssl;
        mbedtls_ssl_config config;
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&config);
        int result =
            mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        mbedtls_ssl_conf_rng(
            &config,
            [](void*, unsigned char* output, size_t size) {
              esp_fill_random(output, size);
              return 0;
            },
            nullptr);
        constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        const size_t before = heap_caps_get_free_size(caps);
        if (!result) result = mbedtls_ssl_setup(&ssl, &config);
        const size_t after = heap_caps_get_free_size(caps);
        if (!result) {
          respond("BENCH tlsheap %d %d %ld\n", mbedtls_ssl_get_max_in_record_payload(&ssl),
                  mbedtls_ssl_get_max_out_record_payload(&ssl), long(before) - long(after));
        } else {
          respond("BENCH error tls_setup %d\n", result);
        }
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&config);
      } else if (!strcmp(command, "ip")) {
        const IPAddress station = WiFi.status() == WL_CONNECTED ? WiFi.localIP() : IPAddress();
        const wifi_mode_t mode = WiFi.getMode();
        const IPAddress ap = mode == WIFI_AP || mode == WIFI_AP_STA ? WiFi.softAPIP() : IPAddress();
        respond("BENCH ip %u.%u.%u.%u %u.%u.%u.%u\n", unsigned(station[0]), unsigned(station[1]),
                unsigned(station[2]), unsigned(station[3]), unsigned(ap[0]), unsigned(ap[1]),
                unsigned(ap[2]), unsigned(ap[3]));
      } else if (!strcmp(command, "web")) {
        const String url = webserver_user_app_url();
        respond("BENCH web %s\n", url.isEmpty() ? "off" : url.c_str());
      } else if (!strcmp(command, "wifi")) {
        respond("BENCH wifi %u\n", unsigned(WiFi.getMode()));
      } else if (!strcmp(command, "routeheap")) {
        uint32_t metrics[4];
        webserver_route_heap(metrics);
        respond("BENCH routeheap %lu %lu %lu %lu\n", (unsigned long)metrics[0],
                (unsigned long)metrics[1], (unsigned long)metrics[2], (unsigned long)metrics[3]);
      } else if (!strcmp(command, "navheap")) {
        uint32_t metrics[4];
        webserver_navigation_heap(metrics);
        respond("BENCH navheap %lu %lu %lu %lu\n", (unsigned long)metrics[0],
                (unsigned long)metrics[1], (unsigned long)metrics[2], (unsigned long)metrics[3]);
      } else if (!strcmp(command, "fanet")) {
        heap_monitor::Snapshot sample{};
        heap_monitor::snapshot(sample);
        respond("BENCH fanet %d %s %lu %lu\n", fanetRadio.isDetected(),
                fanetRadio.getState().c_str(), (unsigned long)sample.fanetTxStack,
                (unsigned long)sample.fanetRxStack);
      } else if (!strcmp(command, "buslog")) {
        respond("BENCH buslog %d %d\n", busLog.isLogging(), settings.dev_startLogAtBoot);
      } else if (!strcmp(command, "version")) {
        respond("BENCH version %s\n", LeafVersionInfo::firmwareVersion());
      } else if (!strcmp(command, "config")) {
        // The retired vario CSV flag remains a reserved zero for older bench clients.
        respond("BENCH config %d %u %d %d %d %d %d %d\n", settings.system_bluetoothOn,
                unsigned(settings.fanet_region), settings.dev_mode, settings.diag_systemEvents,
                settings.diag_networkEvents, settings.diag_webRequests, 0,
                settings.diag_cpuUtilization);
      } else if (buttonCommand) {
        const char* time = strtok_r(nullptr, " ", &cursor);
        const char* extra = strtok_r(nullptr, " ", &cursor);
        const Button button =
            name ? magic_enum::enum_cast<Button>(name).value_or(Button::NONE) : Button::NONE;
        char* end = nullptr;
        const unsigned long duration = time ? strtoul(time, &end, 10) : 0;
        if (button == Button::NONE || extra || bool(time) != hold ||
            (hold && (*time < '0' || *time > '9' || *end || duration < 850 || duration > 5000))) {
          respond("BENCH error button_or_duration\n");
        } else if (buttons.pressForBench(button, hold ? duration : 80)) {
          respond("BENCH ok\n");
        } else {
          respond("BENCH error button_busy\n");
        }
      } else {
        respond("BENCH error unknown_command\n");
      }
    }
  }  // namespace

  void poll() {
    if (!Serial) {
      length = 0;
      replyLength = 0;
      invalidLine = false;
      return;
    }
    if (replyLength) {
      if (Serial.writeBenchLine(reply, replyLength)) replyLength = 0;
      return;
    }
    for (size_t i = 0; i < readBudget; ++i) {
      const int byte = Serial.read();
      if (byte < 0) return;
      if (byte == '\n') {
        if (invalidLine) {
          respond("BENCH error invalid_line\n");
        } else {
          request[length] = '\0';
          handleRequest();
        }
        length = 0;
        invalidLine = false;
        return;
      }
      if (byte == '\r') continue;
      if (byte < ' ' || byte > '~' || length == sizeof(request) - 1)
        invalidLine = true;
      else if (!invalidLine)
        request[length++] = char(byte);
    }
  }
}  // namespace bench_console

#endif
