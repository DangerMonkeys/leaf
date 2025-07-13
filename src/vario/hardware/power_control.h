#pragma once

// Interface supporting power control on a hardware device.
class IPowerControl {
 public:
  // Put device to sleep
  virtual void sleep() = 0;

  // Wake device up
  virtual void wake() = 0;

  virtual ~IPowerControl() = default;  // Always provide a virtual destructor
};
