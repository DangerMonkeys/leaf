#include "page_menu_training.h"
#ifdef LEAF_TRAINING_STUDENT
#include "training/student.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/fonts.h"
#include "ui/display/pages/dialogs/page_list_select.h"
TrainingMenuPage& TrainingMenuPage::instance() {
  static TrainingMenuPage page;
  return page;
}
void TrainingMenuPage::show() { push_page(&instance()); }
void TrainingMenuPage::draw() {
  auto& student = training::student;
  u8g2.firstPage();
  do {
    menu_ui::drawTitle("Training", menu_ui::GLYPH_LEAF_LABS);
    const char* pairLabel = student.offered() && !student.accepting() ? "Confirm pair"
                            : student.pairing()                       ? "Cancel pair"
                                                                      : "Pair";
    const char* labels[] = {"Back", "Enabled", pairLabel, "Unpair"};
    const uint8_t ys[] = {190, 38, 58, 78};
    for (uint8_t row = 0; row < 4; ++row) {
      menu_ui::beginRow(ys[row], row == cursor_position);
      menu_ui::drawLabel(2, ys[row], labels[row]);
      if (row == 0)
        menu_ui::drawBackIcon(80, ys[row]);
      else if (row == 1) {
        u8g2.setCursor(81, ys[row]);
        menu_ui::printGlyph(student.enabled() ? menu_ui::ICON_ON : menu_ui::ICON_OFF);
      } else
        menu_ui::drawEnterIcon(81, ys[row], row == cursor_position);
      menu_ui::endRow();
    }
    u8g2.setFont(leaf_5x8);
    u8g2.drawStr(2, 101, student.status());
    char line[24];
    const uint32_t peer = student.offered() ? student.candidate() : student.instructor();
    if (peer) {
      snprintf(line, sizeof(line), "Inst %08lx", (unsigned long)peer);
      u8g2.drawStr(2, 114, line);
      if (!student.offered()) {
        snprintf(line, sizeof(line), "Student %u of 4", student.slot() + 1);
        u8g2.drawStr(2, 126, line);
      }
    }
    if (student.offered() && !student.accepting()) {
      u8g2.drawStr(2, 140, "Check Inst ID");
      u8g2.drawStr(2, 152, "then Confirm pair");
    } else if (student.pairing()) {
      u8g2.drawStr(2, 140, "Add student on the");
      u8g2.drawStr(2, 152, "instructor device");
    } else {
      u8g2.drawStr(2, 140, student.enabled() ? "FANET paused" : "Normal FANET mode");
      u8g2.drawStr(2, 152, student.enabled() ? "Off keeps pairing" : "Enable to connect");
    }
    u8g2.setFont(leaf_6x12);
  } while (u8g2.nextPage());
}
void TrainingMenuPage::setting_change(Button dir, ButtonEvent state, uint8_t count) {
  if (state != ButtonEvent::CLICKED) return;
  if (cursor_position == 0) {
    speaker.playSound(fx::cancel);
    pop_page();
    return;
  }
  if (dir != Button::CENTER && dir != Button::RIGHT) return;
  auto& student = training::student;
  bool ok = true;
  switch (cursor_position) {
    case 1:
      ok = student.setEnabled(!student.enabled());
      break;
    case 2:
      if (student.offered() && !student.accepting())
        ok = student.confirmPair();
      else if (student.pairing())
        student.cancelPair();
      else
        ok = student.startPair();
      break;
    case 3: {
      static const char* choices[] = {"Keep pairing", "Unpair"};
      PageListSelect::show("Unpair?", etl::array_view<const char*>(choices), 0, [](int choice) {
        if (choice == 1) training::student.unpair();
      });
      break;
    }
  }
  speaker.playSound(ok ? fx::neutral : fx::cancel);
}
#endif
