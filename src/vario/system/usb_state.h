#pragma once

namespace leaf_usb {
  void init();
  bool begin();

  bool started();
  bool hostMounted();
  bool hostSuspended();
  bool shouldStayAwakeForHost();
}  // namespace leaf_usb
