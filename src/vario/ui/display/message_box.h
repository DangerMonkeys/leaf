#pragma once

namespace message_box {
  // Draw inside the current u8g2 page pass. The 96x96 frame is centered on the LCD.
  void draw(const char* first, const char* second = nullptr, const char* detail = nullptr);
}  // namespace message_box
