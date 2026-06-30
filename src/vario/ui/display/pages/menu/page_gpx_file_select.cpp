#include "ui/display/pages/menu/page_gpx_file_select.h"

#include <FS.h>
#include <SD_MMC.h>
#include <string.h>

#include "navigation/gpx.h"
#include "storage/sd_card.h"
#include "ui/audio/sound_effects.h"
#include "ui/audio/speaker.h"
#include "ui/display/display.h"
#include "ui/display/display_fields.h"
#include "ui/display/fonts.h"

namespace {
  constexpr const char* GPX_DIR = "/gpx files";
  constexpr uint8_t FILE_ROW_START_Y = 35;
  constexpr uint8_t FILE_ROW_SPACING = 15;
  constexpr uint8_t MENU_INPUT_X = 74;
  constexpr uint8_t MENU_BACK_Y = 190;
  constexpr uint8_t FILE_TEXT_MAX_WIDTH = 92;

  char lowerAscii(char c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
  }

  bool endsWithGpx(const char* name) {
    if (name == nullptr) return false;

    const size_t len = strlen(name);
    if (len < 4) return false;

    return name[len - 4] == '.' && lowerAscii(name[len - 3]) == 'g' &&
           lowerAscii(name[len - 2]) == 'p' && lowerAscii(name[len - 1]) == 'x';
  }

  int compareFileNames(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
      const char ca = lowerAscii(*a);
      const char cb = lowerAscii(*b);
      if (ca != cb) return ca < cb ? -1 : 1;
      ++a;
      ++b;
    }
    if (*a == *b) return 0;
    return *a == '\0' ? -1 : 1;
  }

  String fileNameOnly(const String& path) {
    const int slash = path.lastIndexOf('/');
    if (slash < 0) return path;
    return path.substring(slash + 1);
  }
}  // namespace

PageGpxFileSelect::PageGpxFileSelect() {
  cursor_min = CURSOR_BACK;
  cursor_position = CURSOR_BACK;
  cursor_max = CURSOR_BACK;
  status_[0] = '\0';
}

void PageGpxFileSelect::show() {
  refreshIndex();
  push_page(this);
}

bool PageGpxFileSelect::button_event(Button button, ButtonEvent state, uint8_t count) {
  if (button == Button::NONE) return false;

  if (state == ButtonEvent::CLICKED) {
    switch (button) {
      case Button::UP:
        moveCursorUp();
        break;
      case Button::DOWN:
        moveCursorDown();
        break;
      case Button::LEFT:
        close();
        break;
      case Button::RIGHT:
      case Button::CENTER:
        if (cursorOnBack()) {
          close();
        } else {
          loadSelectedFile();
        }
        break;
      default:
        break;
    }
  }

  return true;
}

void PageGpxFileSelect::draw() {
  u8g2.firstPage();
  do {
    menu_ui::drawTitle("Select File", menu_ui::GLYPH_NAV_DATA);

    if (fileCount_ == 0) {
      drawStatus();
    } else {
      ensureCursorVisible();
      const uint8_t remainingRows = fileCount_ - firstVisible_;
      const uint8_t visibleCount =
          remainingRows < VISIBLE_FILE_ROWS ? remainingRows : VISIBLE_FILE_ROWS;
      for (uint8_t i = 0; i < visibleCount; ++i) {
        const uint8_t fileIndex = firstVisible_ + i;
        drawFileRow(FILE_ROW_START_Y + i * FILE_ROW_SPACING, fileIndex);
      }
    }

    drawBackRow();
  } while (u8g2.nextPage());
}

void PageGpxFileSelect::refreshIndex() {
  fileCount_ = 0;
  firstVisible_ = 0;
  tooManyFiles_ = false;
  status_[0] = '\0';

  if (!sdcard.isMounted()) {
    if (!SDCard::isCardPresent() || !sdcard.mount()) {
      snprintf(status_, sizeof(status_), "SD card not ready");
      cursor_position = CURSOR_BACK;
      cursor_max = CURSOR_BACK;
      return;
    }
  }

  if (!ensureGpxDirectory()) {
    snprintf(status_, sizeof(status_), "Folder error");
    cursor_position = CURSOR_BACK;
    cursor_max = CURSOR_BACK;
    return;
  }

  File dir = SD_MMC.open(GPX_DIR);
  if (!dir || !dir.isDirectory()) {
    snprintf(status_, sizeof(status_), "Folder error");
    cursor_position = CURSOR_BACK;
    cursor_max = CURSOR_BACK;
    return;
  }

  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      const String name = fileNameOnly(entry.name());
      if (endsWithGpx(name.c_str())) {
        addFileName(name);
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();

  if (fileCount_ == 0) {
    snprintf(status_, sizeof(status_), "No GPX files");
    cursor_position = CURSOR_BACK;
    cursor_max = CURSOR_BACK;
    return;
  }

  if (tooManyFiles_) {
    snprintf(status_, sizeof(status_), "Showing first %u", MAX_GPX_FILES);
  }

  cursor_position = 0;
  cursor_max = fileCount_ - 1;
}

bool PageGpxFileSelect::ensureGpxDirectory() {
  File existing = SD_MMC.open(GPX_DIR);
  if (existing) {
    const bool isDirectory = existing.isDirectory();
    existing.close();
    return isDirectory;
  }

  if (!SD_MMC.mkdir(GPX_DIR)) {
    return false;
  }

  File created = SD_MMC.open(GPX_DIR);
  const bool isDirectory = created && created.isDirectory();
  if (created) created.close();
  return isDirectory;
}

void PageGpxFileSelect::addFileName(const String& name) {
  if (fileCount_ >= MAX_GPX_FILES) {
    tooManyFiles_ = true;
    return;
  }

  if (name.length() > MAX_GPX_FILENAME_LENGTH) {
    Serial.print("Skipping GPX file with too-long name: ");
    Serial.println(name);
    return;
  }

  char candidate[MAX_GPX_FILENAME_LENGTH + 1];
  name.toCharArray(candidate, sizeof(candidate));

  uint8_t insertAt = fileCount_;
  while (insertAt > 0 && compareFileNames(candidate, fileNames_[insertAt - 1]) < 0) {
    strncpy(fileNames_[insertAt], fileNames_[insertAt - 1], sizeof(fileNames_[insertAt]));
    fileNames_[insertAt][MAX_GPX_FILENAME_LENGTH] = '\0';
    --insertAt;
  }

  strncpy(fileNames_[insertAt], candidate, sizeof(fileNames_[insertAt]));
  fileNames_[insertAt][MAX_GPX_FILENAME_LENGTH] = '\0';
  ++fileCount_;
}

void PageGpxFileSelect::moveCursorDown() {
  if (fileCount_ == 0) {
    cursor_position = CURSOR_BACK;
    return;
  }

  if (cursorOnBack()) {
    cursor_position = 0;
  } else if (cursor_position >= static_cast<int8_t>(fileCount_ - 1)) {
    cursor_position = CURSOR_BACK;
  } else {
    ++cursor_position;
  }

  ensureCursorVisible();
}

void PageGpxFileSelect::moveCursorUp() {
  if (fileCount_ == 0) {
    cursor_position = CURSOR_BACK;
    return;
  }

  if (cursorOnBack()) {
    cursor_position = fileCount_ - 1;
  } else if (cursor_position <= 0) {
    cursor_position = CURSOR_BACK;
  } else {
    --cursor_position;
  }

  ensureCursorVisible();
}

void PageGpxFileSelect::ensureCursorVisible() {
  if (cursorOnBack() || fileCount_ == 0) return;

  if (cursor_position < firstVisible_) {
    firstVisible_ = cursor_position;
  } else if (cursor_position >= firstVisible_ + VISIBLE_FILE_ROWS) {
    firstVisible_ = cursor_position - VISIBLE_FILE_ROWS + 1;
  }
}

void PageGpxFileSelect::close() {
  speaker.playSound(fx::cancel);
  pop_page();
}

void PageGpxFileSelect::loadSelectedFile() {
  if (cursorOnBack() || cursor_position < 0 || cursor_position >= fileCount_) return;

  const String path = String(GPX_DIR) + "/" + fileNames_[cursor_position];
  Serial.print("Loading GPX file: ");
  Serial.println(path);

  const bool success = gpx_readFile(SD_MMC, path);
  if (success) {
    speaker.playSound(fx::confirm);
    pop_page();
  } else {
    speaker.playSound(fx::bad);
    snprintf(status_, sizeof(status_), "Load failed");
  }
}

void PageGpxFileSelect::drawFileRow(uint8_t y, uint8_t fileIndex) {
  const bool selected = cursor_position == fileIndex;
  menu_ui::beginRow(y, selected);
  drawFittedText(2, y, fileNames_[fileIndex], FILE_TEXT_MAX_WIDTH);
  menu_ui::endRow();
}

void PageGpxFileSelect::drawBackRow() {
  menu_ui::beginRow(MENU_BACK_Y, cursorOnBack());
  menu_ui::drawLabel(2, MENU_BACK_Y, "Back");
  menu_ui::drawBackIcon(MENU_INPUT_X, MENU_BACK_Y);
  menu_ui::endRow();
}

void PageGpxFileSelect::drawStatus() {
  u8g2.setFont(leaf_6x12);
  u8g2.setCursor(2, 67);
  u8g2.print(status_[0] == '\0' ? "No GPX files" : status_);
}

void PageGpxFileSelect::drawFittedText(uint8_t x, uint8_t y, const char* text, uint8_t maxWidth) {
  char buffer[MAX_GPX_FILENAME_LENGTH + 1];
  strncpy(buffer, text, sizeof(buffer));
  buffer[MAX_GPX_FILENAME_LENGTH] = '\0';

  size_t len = strlen(buffer);
  while (len > 2 && u8g2.getStrWidth(buffer) > maxWidth) {
    buffer[len - 3] = '.';
    buffer[len - 2] = '.';
    buffer[len - 1] = '\0';
    --len;
  }

  u8g2.setCursor(x, y);
  u8g2.print(buffer);
}

bool PageGpxFileSelect::cursorOnBack() const { return cursor_position == CURSOR_BACK; }
