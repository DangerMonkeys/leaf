#include <assert.h>
#include <stdio.h>
#include "training/instructor_controls.h"
using namespace training;
int main() {
  InstructorButtons b;
  b.begin(0, 0);
  b.update(16, 100);
  b.update(0, 110);
  assert(!b.update(0, 190).click); // Contact bounce never became a press.
  b.update(16, 200);
  assert(b.update(16, 220).pressed == 16);
  assert(!b.update(16, 3219).power && b.holding(3219) == 2999);
  assert(b.update(16, 3220).power);
  assert(!b.update(16, 9000).power); // Continuing the shutdown hold cannot wake on USB.
  b.update(0, 9010);
  b.update(16, 9040); // A momentary low while still holding must not rearm.
  assert(!b.update(16, 13000).power);
  b.update(0, 13010);
  assert(!b.update(0, 13090).click);
  b.update(16, 13100);
  b.update(16, 13120);
  assert(b.update(16, 16120).power);
  b.begin(16, 20);
  assert(!b.update(16, 8000).power && b.holding(8000) == 0); // Boot press is consumed.
  b.update(0, 8010);
  assert(!b.update(0, 8090).click);
  b.update(16, 8100); b.update(16, 8120);
  b.update(0, 8300);
  assert(b.update(0, 8380).click);
  assert(!b.update(0, 8390).click);
  b.update(1, 8400);
  assert(b.update(1, 8420).pressed == 1);
  b.update(16, 8500); b.update(16, 8520);
  assert(b.holding(10520) == 2000);
  b.update(0, 10520);
  const auto cancelled = b.update(0, 10600);
  assert(!cancelled.click && !cancelled.power);
  // Regression: the preview can reach full between polls, while release becomes debounced.
  // Completion must win over release; the old implementation discarded this power event.
  b.begin(0, 0);
  b.update(16, 100); b.update(16, 120);
  b.update(0, 3110);
  assert(b.holding(3120) == InstructorButtons::HOLD_MS);
  const auto complete = b.update(0, 3190);
  assert(complete.power && !complete.click);
  assert(!b.update(0, 3200).power);
  // Each direction may chatter throughout a center press without restarting its debounce.
  b.begin(0, 0);
  for (uint32_t now = 100; now <= 130; ++now) b.update(16 | (now % 2), now);
  assert(b.centerDown());
  assert(b.update(16, 3120).power);
  b.begin(0, 0xfffff000);
  b.update(16, 0xffffff00); b.update(16, 0xffffff14);
  assert(!b.update(16, 0xacb).power);
  assert(b.update(16, 0xacc).power);
  b.begin(0, 0);
  b.update(16, 100); b.update(16, 120); b.consumePress();
  assert(!b.update(16, 5000).power); // An automatic low-battery shutdown consumes the hold too.
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
  assert(nav.select(false) == InstructorAction::Pair);
  assert(nav.select(true) == InstructorAction::Focus);
  puts("Instructor buttons: debounce, short press, power hold, boot release and rollover passed");
}
