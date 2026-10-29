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
      changed_ = centerChanged_ = centerAt_ = now;
      consumed_ =
          (value & CENTER) != 0;  // Release the button used to boot before accepting a hold.
    }
    Event update(uint8_t value, uint32_t now) {
      Event event;
      // Debounce center independently: direction contact chatter must not cancel a power hold.
      if ((value ^ raw_) & ~CENTER) changed_ = now;
      if ((value ^ raw_) & CENTER) centerChanged_ = now;
      raw_ = value;
      const uint8_t directions = value & ~CENTER;
      if (directions != (stable_ & ~CENTER) && uint32_t(now - changed_) >= 20) {
        event.pressed = directions & ~stable_;
        stable_ = (stable_ & CENTER) | directions;
      }
      // Commit a completed hold before handling release. The preview uses this same debounced
      // state; handling release first could cancel a hold after its progress bar reached full.
      if ((stable_ & CENTER) && !consumed_ && uint32_t(now - centerAt_) >= HOLD_MS) {
        consumed_ = true;
        event.power = true;
      }
      const bool down = value & CENTER;
      if (down != bool(stable_ & CENTER) && uint32_t(now - centerChanged_) >= (down ? 20U : 80U)) {
        if (down) {
          stable_ |= CENTER;
          event.pressed |= CENTER;
          centerAt_ = now;
          consumed_ = false;
        } else {
          stable_ &= ~CENTER;
          event.click = !consumed_ && uint32_t(now - centerAt_) < PREVIEW_MS;
          consumed_ = false;
        }
      }
      return event;
    }
    void consumePress() { consumed_ = true; }
    bool centerDown() const { return stable_ & CENTER; }
    uint32_t holding(uint32_t now) const {
      return centerDown() && !consumed_ ? uint32_t(now - centerAt_) : 0;
    }

   private:
    uint8_t raw_ = 0, stable_ = 0;
    uint32_t changed_ = 0, centerChanged_ = 0, centerAt_ = 0;
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
    InstructorAction select(bool assigned = true) {
      if (page == InstructorPage::Roster) {
        if (row < SETTINGS_ROW) return assigned ? InstructorAction::Focus : InstructorAction::Pair;
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
