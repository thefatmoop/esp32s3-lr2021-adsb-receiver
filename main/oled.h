// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "wifi_manager.h"
#include "gnss.h"
bool oled_init(void);
void oled_show(uint32_t hz,const char *status);
typedef struct {
    bool hf,ready;
    int error;
    float hits_per_second,rssi_now,rssi_peak;
    uint32_t valid;
    unsigned aircraft;
} oled_stats;
bool oled_show_stats(const oled_stats *stats);
bool oled_set_enabled(bool enabled);
bool oled_show_wifi(const network_info *net);
bool oled_show_gnss(const gnss_status *gps,uint64_t now);
