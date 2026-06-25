#pragma once

#include <Arduino.h>

void webserver_setup();
void webserver_loop();
void webserver_enable_user_app(bool useLeafWifi);
void webserver_enable_wifi_setup();
void webserver_disable_user_app();
bool webserver_user_app_active();
String webserver_user_app_url();
