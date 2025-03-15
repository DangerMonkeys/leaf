#pragma once

#include "etl/span.h"
#include "etl/string.h"
#include "menu_page.h"

#define PAGE_LIST_SELECTED_TEXT_WIDTH 20

/// @brief Shows a list of options, allows the user to select one
class PageListSelect : public SimpleSettingsMenuPage {
 public:
  /// @brief Shows a list of options to the user and calls the callback with the selected
  /// @param title Title of the page
  /// @param entries List of entries to show
  /// @param selected Index of selected entry
  /// @param callback Callback with function to selected entry
  static void show(const char* title, const etl::array_view<const char*> entries,
                   const int& selected, void (*callback)(int));

  const char* get_title() const override { return title; }
  etl::array_view<const char*> get_labels() const override { return entries; }

  // Menu item button icons need to be selected/unselected
  virtual void draw_menu_input(int8_t cursor_position) override;

 protected:
  void setting_change(Button dir, ButtonState state, uint8_t count) override;

 private:
  PageListSelect() {}
  static PageListSelect& getInstance();

  const char* title;
  etl::array_view<const char*> entries;
  int selected;
  void (*callback)(int);
};