#include "trail_cache.h"
#include <string.h>
#include <limits.h>
#include "freertos/FreeRTOS.h"
// ~19 KiB, no allocation or flash writes. 15 s spacing retains ~6 minutes
// per aircraft. The newest point is replaced between retained samples.
static trail_record tracks[TRAIL_AIRCRAFT];
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
void trail_cache_record(const aircraft *a){
    if(!a->position_valid)return;
    uint32_t now=(uint32_t)a->position_at;trail_record *t=NULL,*old=&tracks[0];
    portENTER_CRITICAL(&mux);
    for(int i=0;i<TRAIL_AIRCRAFT;i++){
        trail_record *p=&tracks[i];
        if(p->icao==a->icao&&p->nonicao==a->nonicao){t=p;break;}
        if(!p->icao){old=p;}else if(old->icao&&(uint32_t)(now-p->points[p->head].at)>(uint32_t)(now-old->points[old->head].at))old=p;
    }
    if(!t){t=old;memset(t,0,sizeof(*t));t->icao=a->icao;t->nonicao=a->nonicao;}
    if(t->count&&t->points[t->head].at==now){portEXIT_CRITICAL(&mux);return;}
    if(t->count&&(uint32_t)(now-t->points[t->head].at)>ADSB_LIST_MS){t->count=0;t->head=0;}
    if(t->count==1||(t->count>1&&(uint32_t)(now-t->points[(t->head+TRAIL_POINTS-1)%TRAIL_POINTS].at)>=15000)){
        t->head=(t->head+1)%TRAIL_POINTS;if(t->count<TRAIL_POINTS)t->count++;
    }else if(!t->count)t->count=1;
    t->points[t->head]=(trail_point){(int32_t)(a->latitude*1e6),(int32_t)(a->longitude*1e6),a->altitude_valid?a->altitude:INT32_MIN,now};
    portEXIT_CRITICAL(&mux);
}
void trail_cache_snapshot(uint32_t icao,bool nonicao,trail_record *out){
    memset(out,0,sizeof(*out));portENTER_CRITICAL(&mux);
    for(int i=0;i<TRAIL_AIRCRAFT;i++)if(tracks[i].icao==icao&&tracks[i].nonicao==nonicao){*out=tracks[i];break;}
    portEXIT_CRITICAL(&mux);
}
