#include <assert.h>
#include <stdio.h>
#include "training/instructor_controls.h"
using namespace training;
int main() {
  InstructorButtons b;
  b.begin(0, 0);
  assert(!b.update(16, 100).pressed);
  assert(!b.update(0, 110).click);  // Contact bounce is ignored.
  assert(!b.update(0, 130).pressed);
  b.update(16, 200);
  assert(b.update(16, 220).pressed == 16);
  assert(!b.update(16, 3219).power);
  assert(b.holding(3219) == 2999);
  assert(b.update(16, 3220).power);
  assert(!b.update(16, 9000).power);  // A continuing hold cannot wake after powering off.
  b.update(0, 9010);
  assert(!b.update(0, 9030).click);  // Release cannot open a menu or remove a student.
  b.update(16, 9100);
  b.update(16, 9120);
  assert(b.update(16, 12120).power);  // A separate hold can power back on.
  b.begin(16, 20);  // Button is already down at boot, even if BLE startup takes >3 seconds.
  assert(!b.update(16, 8000).power && b.holding(8000) == 0);
  b.update(0, 8010);
  assert(!b.update(0, 8030).click);
  b.update(16, 8100);
  b.update(16, 8120);
  b.update(0, 8300);
  assert(b.update(0, 8320).click);  // Ordinary short center press opens the menu on release.
  assert(!b.update(0, 8350).click);
  b.update(1, 8400);
  assert(b.update(1, 8420).pressed == 1);
  b.update(16, 8500);
  b.update(16, 8520);
  assert(b.holding(10520) == 2000);
  b.update(0, 10520);
  const auto cancelled = b.update(0, 10540);
  assert(!cancelled.click &&
         !cancelled.power);  // Cancelling a power hold cannot select a menu item.
  b.begin(0, 0xfffff000);
  b.update(16, 0xffffff00);
  b.update(16, 0xffffff14);
  assert(!b.update(16, 0xacb).power);
  assert(b.update(16, 0xacc).power);  // millis rollover during the hold.
  InstructorNavigation nav;
  nav.move(-1);
  assert(nav.row == 4 && nav.page == InstructorPage::Roster);
  assert(nav.select() == InstructorAction::None && nav.page == InstructorPage::Settings);
  assert(nav.select() == InstructorAction::Volume);
  nav.move(1);
  assert(nav.select() == InstructorAction::AltitudeUnits);
  nav.move(1);
  assert(nav.select() == InstructorAction::SpeedUnits);
  nav.move(1);
  assert(nav.select() == InstructorAction::ClimbUnits);
  nav.move(1);
  assert(nav.select() == InstructorAction::Pair);
  nav.move(1);
  assert(nav.select() == InstructorAction::AskRemove);
  nav.askRemove();
  assert(nav.confirmation == 0);
  assert(nav.select() == InstructorAction::None && nav.page == InstructorPage::Settings);
  nav.askRemove();
  nav.move(-1);
  assert(nav.select() == InstructorAction::Remove);
  nav.back();
  assert(nav.page == InstructorPage::Settings);
  nav.move(1);
  assert(nav.select() == InstructorAction::None && nav.page == InstructorPage::Roster);
  nav.move(1);
  assert(nav.row == 0 && nav.select() == InstructorAction::Focus);
  nav.move(1);
  assert(nav.row == 1 && nav.select() == InstructorAction::Focus);
  puts("Instructor buttons: debounce, short press, power hold, boot release and rollover passed");
}
