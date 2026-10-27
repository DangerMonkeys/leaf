#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "training/instructor_link.h"
#include "training/student_link.h"
using namespace training;
static Packet wire(const Packet& input) {
  uint8_t bytes[MAX_PACKET];
  Packet output;
  const auto size = encode(input, bytes, sizeof(bytes));
  assert(size && decode(bytes, size, output));
  for (size_t i = 0; i < size; ++i) {
    bytes[i] ^= 1;
    Packet broken;
    assert(!decode(bytes, size, broken));
    bytes[i] ^= 1;
  }
  return output;
}
static Packet data(uint8_t slot, uint32_t sequence, uint32_t session = 11) {
  Packet p;
  p.header = {Type::Broadcast, 71, uint32_t(100 + slot), session, sequence};
  p.data.slot = slot;
  p.data.valid = Baro | Climb;
  p.data.pressurePa = 101325;
  p.data.pressureAge = 0;
  p.data.climbCms = 500 + slot;
  return wire(p);
}
int main() {
  Receiver receiver;
  receiver.group = 71;
  auto p = data(0, 100);
  assert(!receiver.accept(p, 1000, true));  // No autonomous enrollment, even during pairing.
  for (uint8_t i = 0; i < STUDENTS; ++i) receiver.students[i].id = 100 + i;
  for (uint8_t i = 0; i < STUDENTS; ++i) {
    // No expect()/poll or 125-ms deadline, and each student has an independent sequence.
    assert(receiver.accept(data(i, 100 + i), 4000 + i * 1000, false));
    assert(receiver.students[i].data.climbCms == 500 + i);
  }
  const uint32_t at = receiver.students[0].receivedAt;
  assert(!receiver.accept(p, at + LINK_STALE_MS + 100, false));
  assert(receiver.students[0].receivedAt == at && !receiver.fresh(at + LINK_STALE_MS));
  auto wrong = data(0, 101);
  ++wrong.header.sender;
  assert(!receiver.accept(wrong, 7000, false));
  wrong = data(0, 101); ++wrong.header.group;
  assert(!receiver.accept(wrong, 7000, false));
  wrong = data(0, 101); wrong.data.slot = 1;
  assert(!receiver.accept(wrong, 7000, false));
  assert(receiver.accept(data(0, 101), 7000, false));
  assert(!receiver.accept(data(0, 100), 7001, false));
  assert(receiver.climbFresh(8999) && !receiver.climbFresh(9000));
  assert(receiver.accept(data(0, 1, 22), 9001, false));  // Student reboot, no handshake needed.
  assert(!receiver.accept(data(0, 102, 11), 9002, false));  // Late previous-boot packet.
  assert(receiver.students[0].session == 22 && receiver.students[0].receivedAt == 9001);
  receiver.students[1] = {}; receiver.students[1].id = 101;
  assert(receiver.accept(data(1, 0xffffffff), 0xfffffff0, false));
  assert(receiver.accept(data(1, 0), 0x10, false));
  assert(!receiver.accept(data(1, 0xffffffff), 0x11, false));
  receiver.focus = 1;
  assert(receiver.fresh(0x10 + LINK_STALE_MS - 1));
  assert(!receiver.fresh(0x10 + LINK_STALE_MS));
  // The age of the source pressure sample also bounds the two-second hold.
  receiver.focus = 0;
  p = data(0, 2, 22); p.data.pressureAge = 1500;
  assert(receiver.accept(wire(p), 10000, false));
  assert(receiver.climbFresh(10499) && !receiver.climbFresh(10500));
  char nmeaLine[96];
  assert(nmea(receiver, 10400, 2, nmeaLine, sizeof(nmeaLine)));
  assert(strstr(nmeaLine, "$LK8EX1,101325,"));
  assert(nmea(receiver, 10500, 2, nmeaLine, sizeof(nmeaLine)));
  assert(strstr(nmeaLine, "$LK8EX1,999999,99999,9999,"));
  // Instructor controls select a rate, never grant permission to transmit.
  StudentLink student;
  student.binding = {71, 42, 2}; student.setEnabled(true);
  assert(student.fast(1));
  Packet focus;
  focus.header = {Type::Focus, 71, 42, 20, 1}; focus.focusSlot = 0;
  focus = wire(focus);
  assert(student.control(focus, 100) && !student.fast(101));
  assert(!student.control(focus, 101));
  wrong = focus; ++wrong.header.sender; ++wrong.header.sequence;
  assert(!student.control(wrong, 102));
  wrong = focus; ++wrong.header.group; ++wrong.header.sequence;
  assert(!student.control(wrong, 102));
  assert(!student.fast(100 + FOCUS_LEASE_MS - 1));
  assert(student.fast(100 + FOCUS_LEASE_MS));
  focus.header.sequence = 2; focus.focusSlot = 2;
  assert(student.control(wire(focus), 4000) && student.fast(4001));
  focus.header.session = 21; focus.header.sequence = 1; focus.focusSlot = 0;
  assert(student.control(wire(focus), 5000) && !student.fast(5001));
  student.setEnabled(false);
  assert(!student.control(focus, 5002));
  student.setEnabled(true);
  assert(student.fast(5003));
  student.beginPair(6000, 99);
  assert(!student.control(focus, 6001));
  student.cancelPair();
  assert(!student.control(Packet{}, 6002));
  focus.requestedSlot = 1;
  uint8_t bytes[MAX_PACKET];
  assert(!encode(focus, bytes, sizeof(bytes)));
  focus.requestedSlot = 0; focus.focusSlot = STUDENTS;
  assert(!encode(focus, bytes, sizeof(bytes)));
  for (uint32_t entropy = 0; entropy < 10000; ++entropy) {
    assert(broadcastInterval(true, entropy) >= 225 && broadcastInterval(true, entropy) <= 275);
    assert(broadcastInterval(false, entropy) >= 900 && broadcastInterval(false, entropy) <= 1100);
  }
  // Button pairing finishes on the first autonomous telemetry packet, without a poll.
  StudentLink peer; peer.setEnabled(true); peer.beginPair(100, 99);
  InstructorLink instructor; instructor.open(100);
  assert(instructor.hello(wire(peer.hello(100, 11, "Alex")), 101));
  InstructorRoster roster; roster.group = 71;
  assert(instructor.choose(roster, 102));
  Packet control;
  assert(instructor.control(control, 102, 42, 20));
  assert(!peer.enroll(wire(control), 103, 100, 11) && peer.acceptOffer(104));
  assert(instructor.needsCommit(wire(peer.acceptance(100, 11)), 105, 42, 20));
  instructor.committed(105);
  assert(instructor.control(control, 106, 42, 20));
  assert(peer.enroll(wire(control), 107, 100, 11));
  Receiver paired; paired.group = 71; paired.students[0].id = 100;
  const auto first = data(0, 501);
  assert(paired.accept(first, 1000, false) && instructor.linked(first, 1000));
  assert(!instructor.active(1001));
  puts("Autonomous training: four-source isolation, no poll deadline, replay/session/wrap guards, focus leases, 2s freshness and pairing completion passed");
}
