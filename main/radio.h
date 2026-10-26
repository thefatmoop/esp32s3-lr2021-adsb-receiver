// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lr2021_iq.h"
#include "radio_range.h"
int radio_init(void);
int radio_configure(uint32_t hz);
int radio_info(void);
const lr2021_iq_hal *radio_hal(void);

uint16_t radio_version(void);
unsigned radio_gain(void);
uint16_t radio_cal_errors(void);
unsigned radio_boost(void);
bool radio_frontend(void);
int radio_controls(unsigned gain,unsigned boost,bool fem);
int radio_receiver_configure(uint32_t hz,unsigned gain,unsigned boost);
uint8_t radio_rf_state(uint32_t hz);
typedef struct {
    uint32_t polls, frames, overflows, recoveries, errors;
    uint32_t fifo_full, partial_resets, irq_wakes;
    uint32_t fifo_overflow_flags, fifo_underflow_flags;
    unsigned fifo_peak;
    uint16_t status, chip_errors, last_op;
    int last_error;
    float noise_dbm;
    int threshold_raw;
    float rssi_now, rssi_peak;
} radio_adsb_stats;
int radio_adsb_start(unsigned gain,unsigned boost,int threshold,bool assisted,unsigned bandwidth,bool hf,unsigned capture_bytes,bool hardware_manchester);
void radio_adsb_wait(bool irq_wake);
int radio_adsb_poll(uint8_t chips[32],float *rssi);
int radio_adsb_stop(void);
void radio_adsb_get_stats(radio_adsb_stats *out);
