#pragma once
#include <stdint.h>
#include <stddef.h>
// Long, CRC-valid DF17/DF18 messages only. Software receive time in 12 MHz
// ticks is for ordering, NOT MLAT. Signal=0: no calibrated Beast amplitude.
static inline size_t beast_encode(uint8_t out[44], const uint8_t msg[14], uint64_t us) {
    uint8_t raw[21]; uint64_t ticks=us*12;
    for(int i=0;i<6;i++)raw[i]=(uint8_t)(ticks>>(8*(5-i)));
    raw[6]=0;
    for(int i=0;i<14;i++)raw[7+i]=msg[i];
    size_t n=0;out[n++]=0x1a;out[n++]=0x33;
    for(int i=0;i<21;i++){out[n++]=raw[i];if(raw[i]==0x1a)out[n++]=0x1a;}
    return n;
}
