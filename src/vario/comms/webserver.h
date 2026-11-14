#pragma once

#include <Arduino.h>

void webserver_setup();
void webserver_loop();
void webserver_enable_user_app(bool useLeafWifi);
void webserver_enable_wifi_setup();
void webserver_disable_user_app();
bool webserver_user_app_active();
bool webserver_listening();
bool webserver_user_app_always_on();
bool webserver_wifi_setup_active();
bool webserver_wifi_setup_ready_to_finish();
bool webserver_user_app_using_leaf_wifi();
String webserver_user_app_url();
String webserver_leaf_ap_ssid();
String webserver_leaf_ap_password();
String webserver_leaf_ap_wifi_qr();

#ifdef LEAF_USB_BENCH_CONSOLE
void webserver_navigation_heap(uint32_t (&metrics)[4]);
void webserver_route_heap(uint32_t (&metrics)[4]);
#endif
