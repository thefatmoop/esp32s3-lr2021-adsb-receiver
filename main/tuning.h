// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {uint64_t next,hold_until;uint32_t valid;unsigned index;} rx_tuner;
// A candidate, plausible pulse train or RSSI peak never locks the tuner.
// Only the caller's exact-CRC message count can do that.
static inline bool rx_tuner_tick(rx_tuner *t,uint64_t now,uint32_t valid,unsigned count){
    if(valid!=t->valid){t->valid=valid;t->hold_until=now+120000;}
    if(now<t->hold_until||now<t->next)return false;
    t->index=(t->index+1)%count;t->next=now+10000;t->hold_until=0;return true;
}
static inline bool rx_tuner_selftest(void){
    rx_tuner t={.index=7};
    if(!rx_tuner_tick(&t,1,0,8)||t.index!=0)return false;
    if(rx_tuner_tick(&t,9999,0,8))return false;
    if(rx_tuner_tick(&t,10000,1,8)||t.hold_until!=130000)return false;
    if(rx_tuner_tick(&t,129999,1,8))return false;
    return rx_tuner_tick(&t,130000,1,8)&&t.index==1;
}
