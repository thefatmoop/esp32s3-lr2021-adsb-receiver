#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    double latitude,longitude,hdop;
    uint64_t last_sentence,last_fix;
    uint32_t bytes,sentences,bad_checksum;
    unsigned satellites,quality;
    bool has_position,saved_position,fix,selftest,persisted;
    int error,save_error;
} gnss_status;
int gnss_start(void);
void gnss_snapshot(gnss_status *out);
