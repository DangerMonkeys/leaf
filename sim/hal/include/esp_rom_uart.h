#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The firmware redirects the ROM printf channel to capture a backtrace into a buffer.  On the
// host, esp_backtrace_print() writes to stderr instead, so these hooks record the requested
// callback and do nothing with it.
typedef void (*esp_rom_putc_t)(char c);
void esp_rom_install_channel_putc(int channel, esp_rom_putc_t putc);
void esp_rom_install_uart_printf(void);

#ifdef __cplusplus
}
#endif
