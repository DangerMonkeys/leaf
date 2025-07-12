#pragma once

class ISleepable {
 public:
  // Put device to sleep
  virtual void sleep() = 0;

  // Wake device up
  virtual void wake() = 0;

  virtual ~ISleepable() = default;  // Always provide a virtual destructor
};
