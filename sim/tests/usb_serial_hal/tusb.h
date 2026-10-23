#pragma once

#include <cstddef>
#include <cstdint>

bool tud_cdc_n_connected(uint8_t);
uint32_t tud_cdc_n_write_available(uint8_t);
uint32_t tud_cdc_n_write(uint8_t, const void*, uint32_t);
uint32_t tud_cdc_n_write_flush(uint8_t);
bool tud_disconnect();
bool tud_connect();
