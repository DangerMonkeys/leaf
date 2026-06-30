#pragma once

#include <Arduino.h>

void webserver_setup();
void webserver_loop();
void webserver_enable_user_app(bool useLeafWifi);
void webserver_enable_wifi_setup();
void webserver_disable_user_app();
bool webserver_user_app_active();
bool webserver_wifi_setup_ready_for_network_app();
bool webserver_user_app_using_leaf_wifi();
String webserver_user_app_url();
String webserver_leaf_ap_ssid();
String webserver_leaf_ap_password();
String webserver_leaf_ap_wifi_qr();
