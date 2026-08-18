#pragma once

#include <stdint.h>
#include <string.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { WIFI_IF_STA = 0, WIFI_IF_AP = 1 } wifi_interface_t;

typedef struct {
  uint8_t ssid[32];
  uint8_t password[64];
} wifi_sta_config_t;

typedef struct {
  uint8_t ssid[32];
  uint8_t password[64];
  uint8_t ssid_len;
  uint8_t channel;
  uint8_t max_connection;
} wifi_ap_config_t;

typedef union {
  wifi_sta_config_t sta;
  wifi_ap_config_t ap;
} wifi_config_t;

esp_err_t esp_wifi_get_config(wifi_interface_t iface, wifi_config_t* conf);
esp_err_t esp_wifi_set_config(wifi_interface_t iface, wifi_config_t* conf);

#ifdef __cplusplus
}
#endif
