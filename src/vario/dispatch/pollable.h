#pragma once

class IPollable {
 public:
  // Call this method as frequently as desired to perform periodic maintainance/acquisition/etc
  // tasks.
  virtual void update() = 0;

  virtual ~IPollable() = default;  // Always provide a virtual destructor
};
