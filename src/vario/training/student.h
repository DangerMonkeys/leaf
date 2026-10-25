#pragma once
#ifdef LEAF_TRAINING_STUDENT
#include "console.h"
#include "dispatch/message_sink.h"
#include "dispatch/message_types.h"
#include "radio.h"
#include "student_link.h"
namespace training {
  class Student : public MessageSink<Student, GpsReading> {
   public:
    void begin();
    void update();
    void on_receive(const GpsReading& message);
    bool setEnabled(bool value);
    bool startPair();
    bool confirmPair();
    void cancelPair();
    bool unpair();
    bool enabled() const { return link_.enabled; }
    bool paired() const { return link_.binding.valid(); }
    bool pairing() const { return link_.pairing(millis()); }
    bool offered() const { return link_.offered(millis()); }
    bool accepting() const { return link_.accepted(millis()); }
    uint32_t instructor() const { return link_.binding.instructor; }
    uint32_t candidate() const { return link_.candidate(millis()); }
    uint8_t slot() const { return link_.binding.slot; }
    const char* status() const;
    void on_receive_unknown(const etl::imessage&) {}

   private:
    bool saveBinding();
    Config config_;
    StudentLink link_;
    Radio radio_;
    Console console_;
    Telemetry gps_;
    uint32_t gpsAt_ = 0, session_ = 0, lastStatus_ = 0, lastHandoff_ = 0, pairTxAt_ = 0;
    uint32_t nextPairTxMs_ = 0;
    bool reserved_ = false, storageError_ = false;
    uint8_t statusPage_ = 0;
  };
  extern Student student;
}  // namespace training
#endif
