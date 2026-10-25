#include <assert.h>
#include <stdio.h>
#include <initializer_list>
#include "training/student_link.h"
using namespace training;
int main() {
  StudentLink link;
  link.binding = {7101, 42, 2};
  link.setEnabled(false);
  link.beginPair(100, 123);
  assert(!link.pairing(100) && link.binding.valid());
  link.setEnabled(true);
  Packet poll;
  poll.header = {Type::Poll, 7101, 42, 9, 1};
  poll.requestedSlot = 2;
  assert(link.poll(poll, 100) && link.connected(101));
  assert(!link.poll(poll, 102));
  link.setEnabled(false);
  assert(!link.poll(poll, 103) && !link.connected(103) && link.binding.instructor == 42);
  link.setEnabled(true);
  assert(link.poll(poll, 104));
  assert(!link.connected(3104));
  poll.header.sender = 43;
  assert(!link.poll(poll, 105));
  link.beginPair(200, 123);
  assert(link.pairing(200) && !link.poll(poll, 201));
  Packet offer;
  offer.header = {Type::Offer, 8123, 77, 456, 123};
  offer.target = 99;
  offer.targetSession = 11;
  offer.assignedSlot = 3;
  auto wrong = offer;
  wrong.target = 98;
  assert(!link.enroll(wrong, 201, 99, 11) && !link.offered(201));
  wrong = offer;
  wrong.targetSession = 12;
  assert(!link.enroll(wrong, 201, 99, 11) && !link.offered(201));
  wrong = offer;
  wrong.header.sequence = 124;
  assert(!link.enroll(wrong, 201, 99, 11) && !link.offered(201));
  assert(!link.enroll(offer, 201, 99, 11) && link.offered(201));
  auto confirm = offer;
  confirm.header.type = Type::Confirm;
  assert(!link.enroll(confirm, 202, 99, 11));  // no user approval
  wrong = offer;
  wrong.header.sender = 88;
  link.enroll(wrong, 202, 99, 11);
  assert(link.candidate(202) == 77);  // first candidate cannot be silently replaced
  assert(link.acceptOffer(203));
  auto accept = link.acceptance(99, 11);
  assert(accept.header.type == Type::Accept && accept.target == 77 && accept.targetSession == 456);
  wrong = confirm;
  wrong.assignedSlot = 1;
  assert(!link.enroll(wrong, 204, 99, 11));
  wrong = confirm;
  wrong.header.group = 8124;
  assert(!link.enroll(wrong, 204, 99, 11));
  wrong = confirm;
  wrong.header.session = 457;
  assert(!link.enroll(wrong, 204, 99, 11));
  assert(link.binding.instructor == 42);  // retain old binding until confirmation
  assert(link.enroll(confirm, 204, 99, 11));
  assert(link.binding.instructor == 77 && link.binding.slot == 3 && !link.pairing(204));
  poll.header = {Type::Poll, 8123, 77, 456, 1};
  poll.requestedSlot = 3;
  assert(link.poll(poll, 205));
  link.beginPair(300, 999);
  link.cancelPair();
  assert(link.binding.instructor == 77 && !link.pairing(301));
  link.beginPair(0xfffffff0, 1000);
  assert(link.pairing(0x10));
  assert(!link.pairing(uint32_t(0xfffffff0 + PAIR_WINDOW_MS)));
  assert(!link.acceptOffer(uint32_t(0xfffffff0 + PAIR_WINDOW_MS)));
  link.setEnabled(false);
  link.setEnabled(true);
  assert(!link.pairing(400) && link.binding.instructor == 77);
  link.unpair();
  assert(!link.binding.valid() && !link.poll(poll, 401));
  assert(!link.enroll(confirm, 402, 99, 11));
  uint8_t bytes[MAX_PACKET];
  Packet decoded;
  for (Type type : {Type::Hello, Type::Offer, Type::Accept, Type::Confirm}) {
    Packet frame = type == Type::Hello ? link.hello(99, 11) : offer;
    frame.header.type = type;
    assert(encode(frame, bytes, sizeof(bytes)) == 31);
    assert(decode(bytes, 31, decoded) && decoded.header.type == type &&
           decoded.target == frame.target);
    for (size_t i = 0; i < 31; ++i) {
      bytes[i] ^= 1;
      assert(!decode(bytes, 31, decoded));
      bytes[i] ^= 1;
    }
    assert(!decode(bytes, 30, decoded) && !encode(frame, bytes, 30));
  }
  offer.target = 0;
  assert(!encode(offer, bytes, sizeof(bytes)));
  puts(
      "Student enrollment: explicit approval, identity/session/nonce isolation, replay, timeout, "
      "enable retention and codec passed");
}
