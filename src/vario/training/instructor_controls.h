#pragma once
#include <stdint.h>
namespace training {
  // Debounced direct buttons. A power hold consumes the whole press, including its release.
  class InstructorButtons {
   public:
    static constexpr uint8_t CENTER = 16;
    static constexpr uint32_t HOLD_MS = 3000, PREVIEW_MS = 500;
    struct Event {
      uint8_t pressed = 0;
      bool click = false, power = false;
    };
    void begin(uint8_t value, uint32_t now) {
      raw_ = stable_ = value;
      changed_ = centerAt_ = now;
      consumed_ =
          (value & CENTER) != 0;  // Release the button used to boot before accepting a hold.
    }
    Event update(uint8_t value, uint32_t now) {
      Event event;
      if (value != raw_) {
        raw_ = value;
        changed_ = now;
      }
      if (value != stable_ && uint32_t(now - changed_) >= 20) {
        event.pressed = value & ~stable_;
        const bool released = (stable_ & CENTER) && !(value & CENTER);
        stable_ = value;
        if (event.pressed & CENTER) {
          centerAt_ = now;
          consumed_ = false;
        }
        if (released) {
          // Releasing an interrupted power hold cancels it; do not select a menu item.
          event.click = !consumed_ && uint32_t(now - centerAt_) < PREVIEW_MS;
          consumed_ = false;
        }
      }
      if ((stable_ & CENTER) && !consumed_ && uint32_t(now - centerAt_) >= HOLD_MS) {
        consumed_ = true;
        event.power = true;
      }
      return event;
    }
    bool centerDown() const { return stable_ & CENTER; }
    uint32_t holding(uint32_t now) const {
      return centerDown() && !consumed_ ? uint32_t(now - centerAt_) : 0;
    }

   private:
    uint8_t raw_ = 0, stable_ = 0;
    uint32_t changed_ = 0, centerAt_ = 0;
    bool consumed_ = false;
  };
  enum class InstructorPage : uint8_t { Roster, Settings, Remove };
  struct InstructorUnits {
    bool altitudeFeet = false, speedMph = false, climbFpm = false;
  };
  enum class InstructorAction : uint8_t {
    None,
    Focus,
    Volume,
    AltitudeUnits,
    SpeedUnits,
    ClimbUnits,
    Pair,
    AskRemove,
    Remove
  };
  class InstructorNavigation {
   public:
    static constexpr uint8_t SETTINGS_ROW = 4, SETTINGS_ITEMS = 7;
    InstructorPage page = InstructorPage::Roster;
    uint8_t row = 0, setting = 0, confirmation = 0;
    void move(int8_t direction) {
      if (page == InstructorPage::Roster)
        row = (row + 5 + direction) % 5;
      else if (page == InstructorPage::Settings)
        setting = (setting + SETTINGS_ITEMS + direction) % SETTINGS_ITEMS;
      else
        confirmation = (confirmation + 2 + direction) % 2;
    }
    void back() {
      page = page == InstructorPage::Remove ? InstructorPage::Settings : InstructorPage::Roster;
    }
    void askRemove() {
      page = InstructorPage::Remove;
      confirmation = 0;
    }
    InstructorAction select() {
      if (page == InstructorPage::Roster) {
        if (row < SETTINGS_ROW) return InstructorAction::Focus;
        page = InstructorPage::Settings;
        return InstructorAction::None;
      }
      if (page == InstructorPage::Remove) {
        if (confirmation) return InstructorAction::Remove;
        back();
        return InstructorAction::None;
      }
      static constexpr InstructorAction actions[] = {
          InstructorAction::Volume,     InstructorAction::AltitudeUnits,
          InstructorAction::SpeedUnits, InstructorAction::ClimbUnits,
          InstructorAction::Pair,       InstructorAction::AskRemove};
      if (setting < SETTINGS_ITEMS - 1) return actions[setting];
      back();
      return InstructorAction::None;
    }
  };
}  // namespace training
