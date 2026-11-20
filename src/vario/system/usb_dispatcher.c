#ifdef PIO_BUILD_SYSTEM
// This object supplies the dispatcher before the linker searches libarduino_tinyusb.
// The SDK archive still supplies the hardware, control, CDC and MSC objects.
#define CFG_TUSB_MCU OPT_MCU_ESP32S3
#define CFG_TUSB_DEBUG 0
#define CFG_TUSB_CONFIG_FILE "system/leaf_tinyusb_config.h"
#include <leaf_tinyusb_usbd.inc>
#endif
