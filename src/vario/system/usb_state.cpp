#include "system/usb_state.h"

#include <Arduino.h>
#include <USB.h>

#include "system/usb_serial.h"

#if CONFIG_IDF_TARGET_ESP32S3
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "soc/usb_pins.h"
#include "soc/usb_serial_jtag_reg.h"
#endif

namespace leaf_usb {
  namespace {
    constexpr uint32_t HOST_ENUMERATION_GRACE_MS = 3000;
    constexpr uint16_t LEAF_TINYUSB_PID = 0x0002;

    bool initialized = false;
    bool usb_started = false;
    bool host_mounted = false;
    bool host_suspended = false;
    uint32_t usb_started_ms = 0;

    void onUsbEvent(void*, esp_event_base_t, int32_t event_id, void*) {
      switch (event_id) {
        case ARDUINO_USB_STARTED_EVENT:
          host_mounted = true;
          host_suspended = false;
          break;
        case ARDUINO_USB_STOPPED_EVENT:
          host_mounted = false;
          host_suspended = false;
          break;
        case ARDUINO_USB_SUSPEND_EVENT:
          host_suspended = true;
          break;
        case ARDUINO_USB_RESUME_EVENT:
          host_suspended = false;
          break;
        default:
          break;
      }
    }

    void prepareNativeUsbTakeover() {
#if CONFIG_IDF_TARGET_ESP32S3
      CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
      CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL);
      SET_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL |
                                                   RTC_CNTL_SW_USB_PHY_SEL |
                                                   RTC_CNTL_USB_PAD_ENABLE);

      pinMode(USBPHY_DM_NUM, OUTPUT_OPEN_DRAIN);
      pinMode(USBPHY_DP_NUM, OUTPUT_OPEN_DRAIN);
      digitalWrite(USBPHY_DM_NUM, LOW);
      digitalWrite(USBPHY_DP_NUM, LOW);
      delay(25);
#endif
    }
  }  // namespace

  void init() {
    if (initialized) return;

    USB.onEvent(onUsbEvent);
    initialized = true;
  }

  bool begin() {
    init();

    if (usb_started) return true;

#if !ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
    USBSerial.begin(115200);
#endif
    USB.manufacturerName("Leaf");
    USB.productName("Leaf Vario");
    USB.PID(LEAF_TINYUSB_PID);

    prepareNativeUsbTakeover();
    const bool success = USB.begin();
    if (success && !usb_started) {
      usb_started = true;
      usb_started_ms = millis();
    }
    return success;
  }

  bool started() { return usb_started; }

  bool hostMounted() { return host_mounted; }

  bool hostSuspended() { return host_suspended; }

  bool shouldStayAwakeForHost() {
    if (!usb_started) return false;
    if (host_mounted && !host_suspended) return true;

    return millis() - usb_started_ms < HOST_ENUMERATION_GRACE_MS;
  }
}  // namespace leaf_usb
