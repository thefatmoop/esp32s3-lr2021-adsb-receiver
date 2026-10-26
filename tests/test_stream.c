#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "beast.h"
#include "trail_cache.h"
int main(void){
 uint8_t msg[14],out[44];memset(msg,0x1a,sizeof(msg));size_t n=beast_encode(out,msg,0x1a0000/12);assert(n<=44&&out[0]==0x1a&&out[1]==0x33);
 uint8_t raw[21];unsigned r=0;for(unsigned i=2;i<n;i++){raw[r++]=out[i];if(out[i]==0x1a){i++;assert(out[i]==0x1a);}}assert(r==21&&!memcmp(raw+7,msg,14));
 uint64_t ticks=0;for(int i=0;i<6;i++)ticks=(ticks<<8)|raw[i];assert(ticks==(0x1a0000/12)*12&&raw[6]==0);
 beast_encode(out,msg,(1ULL<<48)/12+1); // 48-bit timestamp wrapping remains bounded.
 aircraft a={.icao=0x123456,.position_valid=true,.altitude_valid=true,.altitude=30000};
 for(unsigned i=0;i<800;i++){a.position_at=1000+i*1000;a.latitude=40+i*.001;a.longitude=-89;trail_cache_record(&a);trail_cache_record(&a);}
 trail_record t;trail_cache_snapshot(a.icao,false,&t);assert(t.count==TRAIL_POINTS);assert(t.points[t.head].at==800000);unsigned oldest=(t.head+TRAIL_POINTS+1-t.count)%TRAIL_POINTS;assert(t.points[t.head].at-t.points[oldest].at>=300000);
 for(unsigned i=1;i<t.count;i++)assert(t.points[(oldest+i)%TRAIL_POINTS].at>t.points[(oldest+i-1)%TRAIL_POINTS].at);
 a.nonicao=true;a.position_at+=1000;trail_cache_record(&a);trail_cache_snapshot(a.icao,true,&t);assert(t.count==1&&t.nonicao);
 a.position_at+=ADSB_LIST_MS+1;trail_cache_record(&a);trail_cache_snapshot(a.icao,true,&t);assert(t.count==1);
 for(int i=0;i<TRAIL_AIRCRAFT+10;i++){a.icao=100+i;a.position_at++;trail_cache_record(&a);}
 trail_cache_snapshot(100,false,&t);assert(t.count==0); // No ICAO/non-ICAO alias.
 trail_cache_snapshot(a.icao,true,&t);assert(t.count==1);
 printf("PASS Beast escaping/timestamps and bounded trails, sampling, expiry, identity, eviction\n");
}
