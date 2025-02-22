#include "page_message.h"

PageMessage& PageMessage::instance() {
  static PageMessage instance;
  return instance;
}