#pragma once
#include <stdbool.h>
#include <stdint.h>
#define SETUP_SSID "W12-ADSB-SETUP"
#define SETUP_PASSWORD "12341090"
#define SETUP_IP "192.168.4.1"
#ifndef WIFI_SETUP_TIMEOUT_MS
#define WIFI_SETUP_TIMEOUT_MS 600000ULL
#endif
#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 45000ULL
#endif
typedef struct {bool connected,ap,testing;char ip[16],ssid[33],message[80];int rssi,error;unsigned reboot_seconds,disconnects,last_reason;} network_info;
int network_start(void);
void web_network(network_info *out);
bool network_submit(const char *ssid,const char *password,bool temporary);
bool web_wifi(const char *ssid,const char *password);
