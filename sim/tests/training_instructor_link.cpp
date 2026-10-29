#include <assert.h>
#include <stdio.h>
#include "training/instructor_link.h"
#include "training/student_link.h"
using namespace training;
static Packet wire(const Packet& p) {
  uint8_t bytes[MAX_PACKET];
  Packet result;
  const auto n = encode(p, bytes, sizeof(bytes));
  assert(n && decode(bytes, n, result));
  return result;
}
static Packet hello(uint32_t id, uint32_t session = 11, uint32_t nonce = 123) {
  Packet p;
  p.header = {Type::Hello, DISCOVERY_GROUP, id, session, nonce};
  return wire(p);
}
int main() {
  InstructorRoster roster;
  roster.group = 7101;
  roster.ids[0] = 41;
  InstructorLink instructor;
  StudentLink student;
  student.enabled = true;
  student.binding = {99, 12, 3};
  student.beginPair(100, 123);
  instructor.open(100);
  auto h = wire(student.hello(77, 11));
  assert(!instructor.hello(h, 99));  // before the window opened, unsigned rollover age is expired
  assert(instructor.hello(h, 101));
  auto malformed = h;
  malformed.header.group = 1;
  assert(!instructor.hello(malformed, 102));
  malformed = h;
  malformed.target = 88;
  assert(!instructor.hello(malformed, 102));
  assert(instructor.choose(roster, 102));
  assert(instructor.slot() == 1 && instructor.peer() == 77);
  Packet control;
  assert(instructor.control(control, 102, 42, 22) && control.header.type == Type::Offer);
  instructor.sent(102);
  assert(!instructor.control(control, 601, 42, 22));
  assert(instructor.control(control, 602, 42, 22));  // lost Offer is retried
  control = wire(control);
  assert(!student.enroll(control, 603, 77, 11) && student.offered(603));
  assert(student.binding.instructor == 12 && roster.ids[1] == 0);
  assert(student.acceptOffer(604));
  const auto accept = wire(student.acceptance(77, 11));
  for (unsigned field = 0; field < 8; ++field) {
    auto wrong = accept;
    switch (field) {
      case 0:
        ++wrong.header.sender;
        break;
      case 1:
        ++wrong.header.session;
        break;
      case 2:
        ++wrong.header.sequence;
        break;
      case 3:
        ++wrong.header.group;
        break;
      case 4:
        ++wrong.target;
        break;
      case 5:
        ++wrong.targetSession;
        break;
      case 6:
        ++wrong.assignedSlot;
        break;
      case 7:
        wrong.header.type = Type::Confirm;
        break;
    }
    assert(!instructor.needsCommit(wrong, 605, 42, 22));
  }
  assert(instructor.needsCommit(accept, 605, 42, 22));
  // Until the adapter reports a successful atomic NVS save, no Confirm is generated.
  assert(instructor.control(control, 606, 42, 22) && control.header.type == Type::Offer);
  const auto original = roster;
  roster.ids[instructor.slot()] = instructor.peer();  // emulate successful persistent commit
  instructor.committed(606);
  assert(!instructor.needsCommit(accept, 607, 42, 22));  // duplicate Accept never allocates twice
  assert(instructor.matchesAccept(accept, 607, 42, 22));
  assert(instructor.control(control, 607, 42, 22) && control.header.type == Type::Confirm);
  instructor.sent(607);  // first confirmation is lost
  assert(student.binding.instructor == 12);
  assert(!instructor.control(control, 1106, 42, 22));
  assert(instructor.control(control, 1107, 42, 22));
  control = wire(control);
  assert(student.enroll(control, 1108, 77, 11));
  assert(student.binding.group == 7101 && student.binding.instructor == 42 &&
         student.binding.slot == 1);
  assert(!student.enroll(control, 1109, 77, 11));  // duplicate Confirm is harmless
  Receiver receiver;
  receiver.group = roster.group;
  for (uint8_t i = 0; i < STUDENTS; ++i) receiver.students[i].id = roster.ids[i];
  Packet poll;
  poll.header = {Type::Poll, 7101, 42, 22, 500};
  poll.requestedSlot = 1;
  assert(student.poll(wire(poll), 1110));
  Packet telemetry;
  telemetry.header = {Type::Telemetry, 7101, 77, 11, 500};
  telemetry.data.slot = 1;
  receiver.expect(1, 500, 1110);
  auto wrongTelemetry = telemetry;
  wrongTelemetry.header.sequence = 499;
  assert(!receiver.accept(wire(wrongTelemetry), 1111, false));
  assert(instructor.phase(1111) == InstructorLink::Phase::Confirming);
  assert(receiver.accept(wire(telemetry), 1112, false));
  assert(instructor.linked(telemetry, 1112) && !instructor.active(1112));
  assert(roster.ids[0] == original.ids[0] && roster.ids[1] == 77 && !roster.ids[2]);
  // A known student reuses its existing slot even when all four slots are occupied.
  InstructorRoster full;
  full.group = 9001;
  for (uint8_t i = 0; i < STUDENTS; ++i) full.ids[i] = 101 + i;
  instructor.open(2000);
  assert(instructor.hello(hello(999), 2001));
  assert(!instructor.choose(full, 2002));
  assert(instructor.phase(2002) == InstructorLink::Phase::Discovering);
  assert(instructor.hello(hello(103), 2003));
  instructor.move(1, 2003);
  assert(instructor.choose(full, 2004) && instructor.slot() == 2);
  instructor.move(-1, 2005);
  assert(instructor.peer() == 103);  // selection locked during an offer
  assert(!instructor.hello(hello(888), 2005));
  instructor.cancel();
  assert(!instructor.control(control, 2006, 42, 22));
  // Four bounded candidates; a fifth replaces only a stale candidate.
  instructor.open(3000);
  for (uint8_t i = 0; i < STUDENTS; ++i) assert(instructor.hello(hello(200 + i), 3001));
  assert(!instructor.hello(hello(999), 3002));
  assert(instructor.hello(hello(999), 7001));
  assert(instructor.candidates[0].id == 999);
  assert(!instructor.choose(roster, 11001));  // candidate freshness is half-open
  assert(instructor.expire(63000) == InstructorLink::Phase::Discovering);
  assert(!instructor.active(63000));
  // An interrupted/expired confirmation retains its saved roster. A retry reuses that slot.
  instructor.open(10000);
  assert(instructor.hello(hello(77, 11, 555), 10001));
  assert(instructor.choose(roster, 10002) && instructor.slot() == 1);
  instructor.committed(10003);
  assert(instructor.expire(70000) == InstructorLink::Phase::Confirming && roster.ids[1] == 77);
  instructor.open(70001);
  assert(instructor.hello(hello(77, 11, 556), 70002));
  assert(instructor.choose(roster, 70003));
  assert(!instructor.needsCommit(accept, 70004, 42, 22));  // prior nonce
  InstructorLink restarted;
  assert(!restarted.needsCommit(accept, 70004, 42, 23));  // reset invalidates the old transaction
  // Window, candidate and retry arithmetic cross millis rollover.
  instructor.open(0xfffffff0);
  assert(instructor.hello(hello(77), 0xfffffff1) && instructor.choose(roster, 0xfffffff2));
  assert(instructor.control(control, 0x10, 42, 22));
  instructor.sent(0xfffffff2);
  assert(!instructor.control(control, 0x10, 42, 22));
  assert(instructor.control(control, 0x200, 42, 22));
  assert(instructor.expire(uint32_t(0xfffffff0U + PAIR_WINDOW_MS)) ==
         InstructorLink::Phase::Offering);
  instructor.open(80000);
  assert(instructor.hello(hello(77), 80001) && instructor.choose(roster, 80002));
  instructor.sent(80003, 700);
  assert(!instructor.control(control, 80702, 42, 22));
  assert(instructor.control(control, 80703, 42, 22));
  // Clicking a specific empty row must pair there, even if an earlier row is also free.
  InstructorRoster empty; empty.group = 9001;
  instructor.open(90000, 3);
  assert(instructor.hello(hello(555), 90001));
  assert(instructor.choose(empty, 90002) && instructor.slot() == 3);
  instructor.open(91000, 3);
  empty.ids[3] = 777; // Occupancy changed during discovery: never overwrite it.
  assert(instructor.hello(hello(555), 91001));
  assert(!instructor.choose(empty, 91002));
  empty.ids[1] = 555;
  assert(instructor.choose(empty, 91003) && instructor.slot() == 1); // Existing binding wins.
  auto invalid = full;
  invalid.ids[3] = invalid.ids[0];
  assert(!invalid.valid());
  invalid = full;
  invalid.group = 0;
  assert(!invalid.valid());
  puts(
      "Instructor/student enrollment: codec interoperability, two-sided approval, packet "
      "loss/retries, commit ordering, slot reuse/full roster, freshness, cancellation, restart and "
      "rollover passed");
}
