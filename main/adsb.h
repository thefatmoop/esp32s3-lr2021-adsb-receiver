// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define ADSB_MAX_AIRCRAFT 96
#define ADSB_LIST_MS 600000ULL
#define ADSB_MAP_MS 120000ULL
#define ADSB_CAPTURE_BYTES 32
typedef struct {
    uint32_t icao, messages, clean_messages, corrected_messages;
    uint64_t clean_seen,previous_clean_seen;
    char callsign[9];
    uint64_t seen, position_at, altitude_at, velocity_at, callsign_at;
    double latitude, longitude;
    int altitude, vertical_rate;
    float speed, track;
    bool altitude_valid, velocity_valid, vertical_valid, position_valid;
    bool gnss_altitude, airborne, nonicao;
    float rssi;
    uint32_t cpr_lat[2],cpr_lon[2];
    uint64_t cpr_time[2];
    uint8_t cpr_type[2];
} aircraft;
typedef struct {
    aircraft aircraft[ADSB_MAX_AIRCRAFT];
    uint32_t candidates, bad_pairs, bad_crc, valid, unsupported, evictions;
    char last_frame[29];
    uint64_t last_valid;
    uint32_t near_pairs, clean_pairs, aligned_valid, quality[8];
    unsigned best_bad_pairs;
    uint32_t capture_offsets[17];
    // 0=off, 1=observe only, 2=apply conservative single-bit corrections.
    uint8_t correction_mode;
    uint32_t repair_candidates,repair_rejected,repair_eligible,corrected,repair_confirmed;
    char last_repaired[29];
    unsigned last_repair_bit;
    struct {uint8_t message[14];uint64_t at;} repair_pending[32];
    unsigned repair_pending_next;
} adsb_state;
void adsb_correction_init(void);
uint32_t adsb_crc(const uint8_t *msg,int bits);
int adsb_from_chips(const uint8_t chips[28],bool assisted,uint8_t msg[14],bool *pairs_ok);
bool adsb_ingest(adsb_state *s,const uint8_t chips[28],bool assisted,float rssi,uint64_t now);
bool adsb_ingest_capture(adsb_state *s,const uint8_t chips[ADSB_CAPTURE_BYTES],bool assisted,float rssi,uint64_t now);
bool adsb_ingest_capture_n(adsb_state *s,const uint8_t *chips,unsigned bytes,bool assisted,float rssi,uint64_t now);
bool adsb_message(adsb_state *s,const uint8_t msg[14],float rssi,uint64_t now);
bool adsb_ingest_manchester(adsb_state *s,const uint8_t capture[14],bool assisted,float rssi,uint64_t now);
bool adsb_selftest(void);
