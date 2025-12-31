#pragma once

#include <cstdint>

#include "utils/state_assert_mixin.h"

// DiagnosticNetwork checks for the presence of a diagnostic network at
// appropriate times, connects to it if found, and manages diagnostic tasks
// related to this network.
class DiagnosticNetwork : private StateAssertMixin<DiagnosticNetwork> {
 public:
  enum class State : uint8_t {
    Ready,
    LookingForNetwork,
    NoNetworkFound,
    ConnectingToNetwork,
    ConnectedToNetwork,
    Error
  };

  State state() const { return state_; }

  void update();

  const char* error_msg() const { return error_msg_; }

 private:
  void onUnexpectedState(const char* action, State actual) const;
  friend struct StateAssertMixin<DiagnosticNetwork>;

  State state_ = State::LookingForNetwork;
  uint32_t t0_;
  const char* error_msg_ = "No error";

  bool printed_end_state_ = false;

  void maybeLookForNetwork();
  void checkForDiagnosticNetwork();
  void checkForConnection();
};

extern DiagnosticNetwork diagnostic_network;
