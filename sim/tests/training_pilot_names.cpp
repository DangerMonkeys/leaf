#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include "training/instructor_link.h"
#include "training/student_link.h"
using namespace training;
static Packet wire(const Packet& p) {
  uint8_t bytes[MAX_PACKET];
  Packet decoded;
  const size_t n = encode(p, bytes, sizeof(bytes));
  assert(n && decode(bytes, n, decoded));
  for (size_t i = 0; i < n; ++i) {
    bytes[i] ^= 1;
    Packet invalid;
    assert(!decode(bytes, n, invalid));
    bytes[i] ^= 1;
  }
  return decoded;
}
int main() {
  char name[PILOT_NAME_BYTES + 1];
  copyPilotName(name, " \tAlex  Taylor\n");
  assert(!strcmp(name, "Alex Taylor"));
  copyPilotName(name, "Jos\xc3\xa9 Garc\xc3\xad" "a");
  assert(validPilotName(name));
  char display[PILOT_NAME_BYTES + 1];
  pilotNameForDisplay(display, name);
  assert(!strcmp(display, "Jose Garcia"));
  copyPilotName(name, "123456789012345678901234567890123456789\xc3\xa9");
  assert(strlen(name) == 39 && validPilotName(name));
  assert(!validPilotName("bad\nname") && !validPilotName("bad\xc0\x80"));
  StudentLink student;
  student.enabled = true;
  student.beginPair(100, 9);
  InstructorLink teacher;
  teacher.open(100);
  auto hello = wire(student.hello(77, 11, "Alex Taylor"));
  assert(teacher.hello(hello, 101));
  assert(!strcmp(teacher.candidates[0].pilotName, "Alex Taylor"));
  assert(teacher.hello(wire(student.hello(77, 11)), 102));
  assert(
      !strcmp(teacher.candidates[0].pilotName, "Alex Taylor"));  // Legacy Hello must not erase it.
  InstructorRoster roster;
  roster.group = 71;
  assert(teacher.choose(roster, 103) && !strcmp(teacher.peerName(), "Alex Taylor"));
  // Discovery must deliver the label before selection; a lost announcement is harmless.
  teacher.open(200);
  assert(!teacher.choose(roster, 201));
  hello = wire(student.hello(77, 11, "Alex Taylor"));
  assert(teacher.hello(hello, 202) && teacher.choose(roster, 203));
  Packet offer;
  assert(teacher.control(offer, 203, 42, 22));
  assert(!student.enroll(wire(offer), 204, 77, 11) && student.acceptOffer(205));
  const auto accept = wire(student.acceptance(77, 11));
  auto forged = accept;
  ++forged.header.session;
  assert(!teacher.needsCommit(forged, 206, 42, 22));
  assert(teacher.needsCommit(accept, 206, 42, 22));
  Receiver receiver;
  receiver.group = roster.group;
  auto& state = receiver.students[teacher.slot()];
  state.id = teacher.peer();
  copyPilotName(state.pilotName, teacher.peerName());  // Adapter persists this at pair commit.
  teacher.committed(207);
  Packet confirm;
  assert(teacher.control(confirm, 207, 42, 22));
  teacher.sent(207);  // Lose this Confirm and retry; no new name transfer is needed.
  assert(teacher.control(confirm, 707, 42, 22));
  assert(student.enroll(wire(confirm), 708, 77, 11));
  assert(!strcmp(state.pilotName, "Alex Taylor"));
  Packet poll;
  poll.header = {Type::Poll, roster.group, 42, 22, 101};
  poll.requestedSlot = teacher.slot();
  assert(student.poll(wire(poll), 709));
  Packet telemetry;
  telemetry.header = {Type::Telemetry, roster.group, 77, 11, 101};
  telemetry.data.slot = teacher.slot();
  receiver.expect(teacher.slot(), 101, 709);
  assert(receiver.accept(wire(telemetry), 710, false));
  assert(teacher.linked(telemetry, 710));
  assert(!strcmp(state.pilotName, "Alex Taylor"));
  assert(!teacher.control(confirm, 1207, 42, 22));  // Pairing traffic ends after linking.
  // Reconnects/session changes preserve the instructor's label; only pairing replaces it.
  telemetry.header.session = 33;
  telemetry.header.sequence = 102;
  receiver.expect(teacher.slot(), 102, 1400);
  assert(receiver.accept(wire(telemetry), 1401, false));
  assert(!strcmp(state.pilotName, "Alex Taylor"));
  assert(!receiver.fresh(3401));  // The label cannot keep flight data alive.
  student.beginPair(2300, 10);
  teacher.open(2300);
  assert(teacher.hello(wire(student.hello(77, 33, "Beth Wong")), 2301));
  assert(teacher.choose(roster, 2302) && !strcmp(teacher.peerName(), "Beth Wong"));
  teacher.open(2400);
  student.beginPair(2400, 11);
  assert(teacher.hello(wire(student.hello(77, 33)), 2401));
  assert(teacher.choose(roster, 2402) && !teacher.peerName()[0]);  // No profile clears old name.
  // Names are bounded by the room in the existing 64-byte discovery frame.
  hello = wire(student.hello(77, 33, "1234567890123456789012345678901234567890"));
  assert(strlen(hello.pilotName) == HELLO_NAME_BYTES);
  hello = wire(student.hello(77, 33, "1234567890123456789012345678901\xc3\xa9"));
  assert(strlen(hello.pilotName) == 31 && validPilotName(hello.pilotName));
  uint8_t bytes[MAX_PACKET];
  auto legacy = student.hello(77, 33);
  assert(encode(legacy, bytes, sizeof(bytes)) == 31);
  assert(encode(hello, bytes, sizeof(bytes)) == 63);
  assert(encode(telemetry, bytes, sizeof(bytes)) == 64);
  assert(encode(poll, bytes, sizeof(bytes)) == 24);
  // Removed experimental metadata types cannot generate any traffic in either role.
  for (unsigned type : {7U, 8U}) {
    auto removed = poll;
    removed.header.type = Type(type);
    assert(!encode(removed, bytes, sizeof(bytes)) && !student.poll(removed, 2500));
  }
  // Prevent a full Packet (including its name buffer) creeping back into persistent student state.
  static_assert(sizeof(StudentLink) <= 68, "Student pairing state must not retain a name buffer");
  printf("Pairing-only pilot names: codec/UTF-8, dropped-frame retries, identity checks, "
         "re-pair/reconnect labels and compact student state (%zu bytes) passed\n", sizeof(StudentLink));
}
