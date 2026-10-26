#pragma once
#include "adsb.h"
#include "radio.h"
typedef struct {unsigned gain,boost;int threshold;bool assisted;unsigned bandwidth;bool temporary,hunt,hf;unsigned capture_bytes;bool irq_wake,hardware_manchester;} rx_config;
typedef struct {rx_config config;radio_adsb_stats radio;int error;bool selftest;uint32_t generation;uint64_t now,hold_until;unsigned tune_profile;} rx_status;
void receiver_snapshot(adsb_state *out,rx_status *status);
bool receiver_configure(const rx_config *config);
bool receiver_correction_mode(unsigned mode);
