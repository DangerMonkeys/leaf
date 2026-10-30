#pragma once
#include <stdint.h>
#include <atomic>

// Quiescence barrier for producers accessing resources that will be deleted.
// Call requestStop(), wait for idle(), then free. Never free on timeout.
class ProducerGate {
 public:
  class Entry {
   public:
    explicit Entry(ProducerGate& gate) : gate_(gate) {
      if (gate_.stopping_.load()) return;
      gate_.users_.fetch_add(1);
      entered_ = !gate_.stopping_.load();
      if (!entered_) gate_.users_.fetch_sub(1);
    }
    ~Entry() {
      if (entered_) gate_.users_.fetch_sub(1);
    }
    explicit operator bool() const { return entered_; }
    Entry(const Entry&) = delete;
    Entry& operator=(const Entry&) = delete;

   private:
    ProducerGate& gate_;
    bool entered_ = false;
  };
  void requestStop() { stopping_.store(true); }
  bool stopping() const { return stopping_.load(); }
  bool idle() const { return users_.load() == 0; }
  // Only after all old producers are idle and old resources are released.
  void reopen() { stopping_.store(false); }

 private:
  std::atomic<bool> stopping_{false};
  std::atomic<uint32_t> users_{0};
};
