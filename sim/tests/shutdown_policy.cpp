#include "system/shutdown_policy.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include "utils/producer_gate.h"
using namespace shutdown_guard;
int main() {
  assert(!expired(7999999, 0));
  assert(expired(8000000, 0));
  const uint32_t wrap = 0xfffffff0;
  assert(!expired(wrap + DEADLINE_US - 1, wrap));
  assert(expired(wrap + DEADLINE_US, wrap));
  HoldPolicy hold;
  assert(!hold.update(wrap, true));
  assert(!hold.update(wrap + HOLD_US - 1, true));
  assert(hold.update(wrap + HOLD_US, true));
  hold.update(1, false);
  hold.requireRelease = true;
  assert(!hold.update(1, true));
  assert(!hold.update(HOLD_US * 2, true));
  hold.update(HOLD_US * 2 + 1, false);
  assert(!hold.update(1, true));
  assert(hold.update(1 + HOLD_US, true));

  ProducerGate gate;
  std::atomic<bool> entered{false}, release{false};
  std::thread producer([&] {
    ProducerGate::Entry entry(gate);
    assert(entry);
    entered.store(true);
    while (!release.load()) std::this_thread::yield();
  });
  while (!entered.load()) std::this_thread::yield();
  gate.requestStop();
  assert(!gate.idle());  // Resource teardown must wait for this producer.
  {
    ProducerGate::Entry rejected(gate);
    assert(!rejected);
  }
  release.store(true);
  producer.join();
  assert(gate.idle());
  gate.reopen();
  {
    ProducerGate::Entry resumed(gate);
    assert(resumed && !gate.idle());
  }
  assert(gate.idle());
  // Concurrent stops cannot admit work once the quiescence barrier is closed.
  for (int n = 0; n < 100; ++n) {
    gate.reopen();
    std::thread racing([&] {
      for (int i = 0; i < 1000; ++i) {
        ProducerGate::Entry e(gate);
      }
    });
    gate.requestStop();
    racing.join();
    assert(gate.idle());
    ProducerGate::Entry rejected(gate);
    assert(!rejected);
  }
  std::cout << "Shutdown deadline/wrap, hold/release and concurrent producer drain passed\n";
}
