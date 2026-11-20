#include "page_storage_warning.h"

#include "logging/log.h"
#include "power.h"
#include "storage/sd_card.h"
#include "ui/display/display.h"
#include "ui/display/fonts.h"
#include "ui/display/menu_page.h"

namespace storage_warning {
  namespace {
    constexpr uint64_t LOW_SPACE_BYTES = 10 * 1000 * 1000;
#ifdef LEAF_USB_BENCH_CONSOLE
    uint32_t checkMicros = 0;
#endif
    enum class Problem : uint8_t { Low, Full, Unavailable };

    class StorageWarning : public MenuPage {
     public:
      void show(Problem problem, uint64_t freeBytes) {
        problem_ = problem;
        tenthsMb_ = freeBytes / 100000;
        cursor_position = CURSOR_BACK;
        push_page(this);
      }
      bool button_event(Button button, ButtonEvent state, uint8_t count) override {
        if (button == Button::CENTER && state == ButtonEvent::HELD) {
          power.shutdown();
          return false;
        }
        if (state == ButtonEvent::CLICKED &&
            (button == Button::CENTER || button == Button::LEFT || button == Button::RIGHT)) {
          pop_page();
          return true;
        }
        return false;
      }
      void draw() override {
        u8g2.firstPage();
        do {
          menu_ui::drawTitle("SD card");
          u8g2.setFont(leaf_6x12);
          line(problem_ == Problem::Low    ? "Low space"
               : problem_ == Problem::Full ? "SD card full"
                                           : "SD unavailable",
               40);
          if (problem_ != Problem::Unavailable) {
            char space[24];
            if (problem_ == Problem::Full)
              snprintf(space, sizeof(space), "0 MB free");
            else if (!tenthsMb_)
              snprintf(space, sizeof(space), "<0.1 MB free");
            else
              snprintf(space, sizeof(space), "%u.%u MB free", tenthsMb_ / 10, tenthsMb_ % 10);
            line(space, 59);
          }
          u8g2.setFont(leaf_5x8);
          if (problem_ == Problem::Unavailable) {
            line("Flight recording", 89);
            line("needs an SD card.", 101);
            line("Turn Leaf off,", 129);
            line("then check the", 141);
            line("SD card.", 153);
          } else {
            line(problem_ == Problem::Low ? "Clear space before" : "Flight recording", 89);
            line(problem_ == Problem::Low ? "your next flight." : "needs free space.", 101);
            line("Copy files to a", 129);
            line("computer, then", 141);
            line("delete old files.", 153);
          }
          u8g2.setFont(leaf_6x12);
          menu_ui::beginRow(190, true);
          menu_ui::drawLabel(2, 190, "Continue");
          menu_ui::drawEnterIcon(83, 190, true);
          menu_ui::endRow();
        } while (u8g2.nextPage());
      }

     private:
      Problem problem_ = Problem::Low;
      uint8_t tenthsMb_ = 0;
      static void line(const char* text, uint8_t y) {
        u8g2.setCursor((96 - u8g2.getStrWidth(text)) / 2, y);
        u8g2.print(text);
      }
    } warning;
  }  // namespace

  void checkAtStartup() {
    if (flightTimer_isRunning()) return;
    uint64_t total = 0, free = 0;
    const uint32_t started = micros();
    const bool available = sdcard.spaceAvailable(total, free);
#ifdef LEAF_USB_BENCH_CONSOLE
    checkMicros = micros() - started;
#endif
    Serial.printf("SD startup space: available=%d free=%llu check_us=%lu\n", available,
                  (unsigned long long)free, (unsigned long)(micros() - started));
    if (!available)
      warning.show(Problem::Unavailable, 0);
    else if (!free)
      warning.show(Problem::Full, 0);
    else if (free <= LOW_SPACE_BYTES)
      warning.show(Problem::Low, free);
  }
#ifdef LEAF_USB_BENCH_CONSOLE
  uint32_t lastCheckMicros() { return checkMicros; }
#endif
}  // namespace storage_warning
