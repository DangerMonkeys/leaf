#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ESP_MAC_WIFI_STA = 0,
  ESP_MAC_WIFI_SOFTAP,
  ESP_MAC_BT,
  ESP_MAC_ETH,
} esp_mac_type_t;

// The emulator reports a fixed, obviously-fake MAC so screens that show it stay stable across
// runs and can never be mistaken for a real device's identity.
esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t type);
esp_err_t esp_efuse_mac_get_default(uint8_t* mac);

#ifdef __cplusplus
}
#endif
