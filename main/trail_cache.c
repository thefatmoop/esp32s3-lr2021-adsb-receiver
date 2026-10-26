#include "trail_cache.h"
#include <string.h>
// Trails live in each browser. Keep the snapshot API for older clients without
// reserving a second aircraft history on the receiver.
void trail_cache_record(const aircraft *a){(void)a;}
void trail_cache_snapshot(uint32_t icao,bool nonicao,trail_record *out){
    (void)icao;(void)nonicao;memset(out,0,sizeof(*out));
}
