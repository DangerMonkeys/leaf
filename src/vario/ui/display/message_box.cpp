#include "ui/display/message_box.h"

#include "ui/display/display.h"
#include "ui/display/fonts.h"

namespace message_box {
  void draw(const char* first, const char* second, const char* detail) {
    constexpr int WIDTH = 96;
    constexpr int HEIGHT = 96;
    constexpr int LINE_HEIGHT = 14;
    constexpr int DETAIL_GAP = 8;
    const int x = (u8g2.getDisplayWidth() - WIDTH) / 2;
    const int y = (u8g2.getDisplayHeight() - HEIGHT) / 2 - 3;

    u8g2.setDrawColor(0);
    u8g2.drawRBox(x, y, WIDTH, HEIGHT, 5);
    u8g2.setDrawColor(1);
    u8g2.drawRFrame(x, y, WIDTH, HEIGHT, 5);
    u8g2.setFont(leaf_6x12);

    const char* lines[] = {first, second, detail};
    int count = 0;
    for (const char* line : lines)
      if (line && *line) ++count;
    const int height = u8g2.getAscent() - u8g2.getDescent() + (count - 1) * LINE_HEIGHT +
                       (detail && *detail ? DETAIL_GAP : 0);
    int baseline = y + (HEIGHT - height) / 2 + u8g2.getAscent();
    for (unsigned i = 0; i < 3; ++i) {
      if (!lines[i] || !*lines[i]) continue;
      if (i == 2) baseline += DETAIL_GAP;
      u8g2.drawStr(x + (WIDTH - u8g2.getStrWidth(lines[i])) / 2, baseline, lines[i]);
      baseline += LINE_HEIGHT;
    }
  }
}  // namespace message_box
