#pragma once

#include "logbook/logbook_store.h"

namespace logbook_card {
void drawFlightCard(const LogbookEntrySummary& summary);
String dateString(const LogbookEntrySummary& summary);
String timeString(const LogbookEntrySummary& summary);
}  // namespace logbook_card
