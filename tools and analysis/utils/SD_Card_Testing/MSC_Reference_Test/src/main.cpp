#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include <driver/sdmmc_host.h>
#include <esp_err.h>
#include <sdmmc_cmd.h>

#if CONFIG_IDF_TARGET_ESP32S3
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>
#include <soc/usb_pins.h>
#include <soc/usb_serial_jtag_reg.h>
#endif

namespace {
  constexpr gpio_num_t SD_CLK = GPIO_NUM_36;
  constexpr gpio_num_t SD_CMD = GPIO_NUM_35;
  constexpr gpio_num_t SD_D0 = GPIO_NUM_37;
  constexpr gpio_num_t SD_D1 = GPIO_NUM_38;
  constexpr gpio_num_t SD_D2 = GPIO_NUM_33;
  constexpr gpio_num_t SD_D3 = GPIO_NUM_34;
  constexpr uint32_t SD_SECTOR_SIZE = 512;
  constexpr uint32_t MSC_BUFFER_SIZE = 4096;
  constexpr uint8_t STATUS_LED_PIN = 47;
  constexpr uint16_t REFERENCE_TEST_PID = 0x4002;

  static_assert(MSC_BUFFER_SIZE % SD_SECTOR_SIZE == 0);

  USBMSC msc;
  sdmmc_card_t card = {};
  bool ready = false;
  bool failed = false;

  // Match the production MSC path: one DMA-capable staging buffer and one multi-sector SDMMC
  // transaction for each complete USB MSC request, capped at 4 KiB.
  DMA_ATTR uint8_t mscBuffer[MSC_BUFFER_SIZE] __attribute__((aligned(4)));

  void indicateFailure() {
    failed = true;
    ready = false;
    digitalWrite(STATUS_LED_PIN, LOW);
  }

  bool requestIsValid(uint32_t lba, uint32_t offset, uint32_t bufsize) {
    if (offset % SD_SECTOR_SIZE != 0 || bufsize == 0 || bufsize % SD_SECTOR_SIZE != 0 ||
        bufsize > MSC_BUFFER_SIZE) {
      return false;
    }

    const uint64_t firstSector = static_cast<uint64_t>(lba) + offset / SD_SECTOR_SIZE;
    const uint64_t sectorCount = bufsize / SD_SECTOR_SIZE;
    return firstSector + sectorCount <= card.csd.capacity;
  }

  int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (!ready || !requestIsValid(lba, offset, bufsize)) return -1;

    const uint32_t firstSector = lba + offset / SD_SECTOR_SIZE;
    const size_t sectorCount = bufsize / SD_SECTOR_SIZE;
    const esp_err_t result = sdmmc_read_sectors(&card, mscBuffer, firstSector, sectorCount);
    if (result != ESP_OK) {
      indicateFailure();
      return -1;
    }

    memcpy(buffer, mscBuffer, bufsize);
    return static_cast<int32_t>(bufsize);
  }

  int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    if (!ready || !requestIsValid(lba, offset, bufsize)) return -1;

    memcpy(mscBuffer, buffer, bufsize);
    const uint32_t firstSector = lba + offset / SD_SECTOR_SIZE;
    const size_t sectorCount = bufsize / SD_SECTOR_SIZE;
    const esp_err_t result = sdmmc_write_sectors(&card, mscBuffer, firstSector, sectorCount);
    if (result != ESP_OK) {
      indicateFailure();
      return -1;
    }

    return static_cast<int32_t>(bufsize);
  }

  bool onStartStop(uint8_t, bool, bool) { return true; }

  void prepareNativeUsbTakeover() {
#if CONFIG_IDF_TARGET_ESP32S3
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL);
    SET_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL |
                                                 RTC_CNTL_USB_PAD_ENABLE);

    pinMode(USBPHY_DM_NUM, OUTPUT_OPEN_DRAIN);
    pinMode(USBPHY_DP_NUM, OUTPUT_OPEN_DRAIN);
    digitalWrite(USBPHY_DM_NUM, LOW);
    digitalWrite(USBPHY_DP_NUM, LOW);
    delay(25);
#endif
  }

  esp_err_t initializeSdCard() {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    host.flags = SDMMC_HOST_FLAG_4BIT;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = SD_CLK;
    slot.cmd = SD_CMD;
    slot.d0 = SD_D0;
    slot.d1 = SD_D1;
    slot.d2 = SD_D2;
    slot.d3 = SD_D3;
    slot.width = 4;
    // Leaf has external pull-ups. Enabling the internal pull-ups as well matches Espressif's
    // reference example and is harmless for this isolated test.
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t result = sdmmc_host_init();
    if (result != ESP_OK) return result;

    result = sdmmc_host_init_slot(host.slot, &slot);
    if (result != ESP_OK) {
      sdmmc_host_deinit();
      return result;
    }

    result = sdmmc_card_init(&host, &card);
    if (result != ESP_OK) {
      sdmmc_host_deinit();
      return result;
    }

    return ESP_OK;
  }
}  // namespace

void setup() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  if (initializeSdCard() != ESP_OK || card.csd.sector_size != SD_SECTOR_SIZE ||
      card.csd.capacity == 0) {
    indicateFailure();
    return;
  }

  msc.vendorID("Leaf");
  msc.productID("MSC_Reference");
  msc.productRevision("1.0");
  msc.onRead(onRead);
  msc.onWrite(onWrite);
  msc.onStartStop(onStartStop);
  msc.isWritable(true);
  msc.mediaPresent(true);
  if (!msc.begin(card.csd.capacity, card.csd.sector_size)) {
    indicateFailure();
    return;
  }

  USB.manufacturerName("Leaf");
  USB.productName("Leaf MSC Reference");
  USB.PID(REFERENCE_TEST_PID);
  prepareNativeUsbTakeover();
  ready = true;
  if (!USB.begin()) {
    ready = false;
    indicateFailure();
    return;
  }

  digitalWrite(STATUS_LED_PIN, HIGH);
}

void loop() {
  if (!failed) {
    delay(1000);
    return;
  }

  digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
  delay(250);
}
