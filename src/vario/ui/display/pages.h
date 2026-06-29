#ifndef PAGES_H
#define PAGES_H

#include "ui/display/pages/menu/page_menu_altimeter.h"
#include "ui/display/pages/menu/page_menu_developer.h"
#include "ui/display/pages/menu/page_menu_display.h"
#include "ui/display/pages/menu/page_menu_flight_tools.h"
#include "ui/display/pages/menu/page_menu_gps.h"
#include "ui/display/pages/menu/page_menu_log.h"
#include "ui/display/pages/menu/page_menu_nav_data.h"
#include "ui/display/pages/menu/page_menu_settings.h"
#include "ui/display/pages/menu/page_menu_system.h"
#include "ui/display/pages/menu/page_menu_units.h"
#include "ui/display/pages/menu/page_menu_vario.h"
#include "ui/display/pages/menu/page_menu_wifi.h"
#include "ui/display/pages/primary/page_menu_main.h"

extern MainMenuPage mainMenuPage;

extern AltimeterMenuPage altimeterMenuPage;
extern VarioMenuPage varioMenuPage;
extern DisplayMenuPage displayMenuPage;
extern FlightToolsMenuPage flightToolsMenuPage;
extern NavDataMenuPage navDataMenuPage;
extern UnitsMenuPage unitsMenuPage;
extern GPSMenuPage gpsMenuPage;
extern LogMenuPage logMenuPage;
extern SettingsRootMenuPage settingsMenuPage;
extern SystemMenuPage systemMenuPage;
extern DeveloperMenuPage developerMenuPage;
extern WifiMenuPage wifiMenuPage;

#endif
