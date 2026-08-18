// ESP-IDF and ESP class entry points.

#include <Esp.h>
#include <esp_debug_helpers.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <execinfo.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <string.h>

#include <Preferences.h>

#include "sim/clock.h"

EspClass ESP;

namespace sim {
  // Set by the emulator when a restart is requested; the run loop notices and re-runs setup().
  bool g_restartRequested = false;
}  // namespace sim

uint32_t EspClass::getFreeHeap() const { return 200 * 1024; }
uint32_t EspClass::getMinFreeHeap() const { return 150 * 1024; }
uint32_t EspClass::getMaxAllocHeap() const { return 120 * 1024; }
void EspClass::restart() { sim::g_restartRequested = true; }

extern "C" {

esp_reset_reason_t esp_reset_reason(void) { return ESP_RST_POWERON; }
uint32_t esp_get_free_heap_size(void) { return 200 * 1024; }
uint32_t esp_get_minimum_free_heap_size(void) { return 150 * 1024; }
void esp_restart(void) { sim::g_restartRequested = true; }

esp_err_t esp_sleep_enable_timer_wakeup(uint64_t timeInUs) {
  (void)timeInUs;
  return ESP_OK;
}
esp_err_t esp_sleep_enable_ext0_wakeup(int gpioNum, int level) {
  (void)gpioNum;
  (void)level;
  return ESP_OK;
}

// Light sleep in charge mode is a timed wait on device; here it is simply virtual time passing,
// which keeps the charging loop's cadence right without the emulator actually idling.
esp_err_t esp_light_sleep_start(void) {
  sim::clock().advanceUs(1000);
  return ESP_OK;
}

const char* esp_err_to_name(esp_err_t code) {
  switch (code) {
    case ESP_OK:
      return "ESP_OK";
    case ESP_FAIL:
      return "ESP_FAIL";
    case ESP_ERR_NO_MEM:
      return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG:
      return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:
      return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_NOT_FOUND:
      return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_TIMEOUT:
      return "ESP_ERR_TIMEOUT";
    default:
      return "ESP_ERR";
  }
}

size_t heap_caps_get_free_size(uint32_t caps) {
  return (caps & MALLOC_CAP_SPIRAM) ? 6 * 1024 * 1024 : 200 * 1024;
}
size_t heap_caps_get_largest_free_block(uint32_t caps) {
  return (caps & MALLOC_CAP_SPIRAM) ? 4 * 1024 * 1024 : 120 * 1024;
}
size_t heap_caps_get_total_size(uint32_t caps) {
  return (caps & MALLOC_CAP_SPIRAM) ? 8 * 1024 * 1024 : 320 * 1024;
}
size_t heap_caps_get_minimum_free_size(uint32_t caps) { return heap_caps_get_free_size(caps) / 2; }
void* heap_caps_malloc(size_t size, uint32_t caps) { return malloc(size); }
void heap_caps_free(void* ptr) { free(ptr); }

esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t type) {
  // Locally-administered, obviously synthetic: nothing here can be mistaken for a real unit.
  static const uint8_t simMac[6] = {0x02, 0x4C, 0x45, 0x41, 0x46, 0x00};
  if (!mac) return ESP_ERR_INVALID_ARG;
  memcpy(mac, simMac, sizeof(simMac));
  return ESP_OK;
}

esp_err_t esp_efuse_mac_get_default(uint8_t* mac) { return esp_read_mac(mac, ESP_MAC_WIFI_STA); }

esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) {
  Preferences::simEraseAll();
  return ESP_OK;
}

esp_err_t esp_wifi_get_config(wifi_interface_t iface, wifi_config_t* conf) {
  if (conf) memset(conf, 0, sizeof(*conf));
  return ESP_OK;
}
esp_err_t esp_wifi_set_config(wifi_interface_t iface, wifi_config_t* conf) { return ESP_OK; }

void esp_backtrace_print(int depth) {
  void* frames[64];
  if (depth > 64) depth = 64;
  const int count = backtrace(frames, depth);
  fflush(stdout);
  fprintf(stderr, "--- emulator backtrace (%d frames) ---\n", count);
  backtrace_symbols_fd(frames, count, 2);
}

void esp_rom_install_channel_putc(int channel, void (*putc)(char)) {
  (void)channel;
  (void)putc;
}

void esp_rom_install_uart_printf(void) {}

}  // extern "C"
