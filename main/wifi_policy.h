#pragma once
#include <stdint.h>
#include <stdbool.h>
// Pure time policy shared with boundary tests; browsing cannot postpone reboot.
static inline unsigned wifi_seconds_left(uint64_t now,uint64_t deadline){return deadline>now?(unsigned)((deadline-now+999)/1000):0;}
static inline bool wifi_setup_expired(bool ap,bool connected,uint64_t now,uint64_t deadline){return ap&&!connected&&deadline&&now>=deadline;}
