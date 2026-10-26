#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "adsb.h"
static void hex(const char *s,uint8_t m[14]){for(int i=0;i<14;i++){unsigned v;sscanf(s+2*i,"%2x",&v);m[i]=v;}}
static void flip(uint8_t *m,unsigned bit){m[bit/8]^=1u<<(7-bit%8);}
int main(void){
 assert(adsb_selftest());adsb_correction_init();static adsb_state seed,s;uint8_t clean[14],bad[14];
 hex("8D4840D6202CC371C32CE0576098",clean);assert(adsb_message(&seed,clean,-80,1000));assert(adsb_message(&seed,clean,-80,1100));
 for(unsigned bit=0;bit<112;bit++){
  s=seed;s.correction_mode=1;memcpy(bad,clean,14);flip(bad,bit);assert(!adsb_ingest_manchester(&s,bad,false,-85,1200));
  assert(s.valid==2&&!s.corrected&&!memcmp(s.aircraft,seed.aircraft,sizeof(s.aircraft)));assert(!strcmp(s.last_frame,seed.last_frame));
  assert(s.repair_eligible==(bit>=32));
  s=seed;s.correction_mode=2;bool accepted=adsb_ingest_manchester(&s,bad,false,-85,1200);assert(accepted==(bit>=32));assert(s.valid==2);
  if(accepted){assert(s.corrected==1&&s.aircraft[0].messages==3&&s.aircraft[0].corrected_messages==1);assert(s.aircraft[0].clean_seen==1100&&s.aircraft[0].clean_messages==2);assert(!strcmp(s.last_frame,seed.last_frame));assert(adsb_message(&s,clean,-80,1300));assert(s.repair_confirmed==1&&s.valid==3);}
 }
 for(unsigned a=0;a<112;a++)for(unsigned b=a+1;b<112;b++){s=seed;s.correction_mode=2;memcpy(bad,clean,14);flip(bad,a);flip(bad,b);assert(!adsb_ingest_manchester(&s,bad,false,-85,1200));assert(!s.corrected);}
 memcpy(bad,clean,14);flip(bad,60);
 memset(&s,0,sizeof(s));s.correction_mode=2;assert(!adsb_ingest_manchester(&s,bad,false,-85,1200));assert(s.repair_rejected==1&&!s.aircraft[0].icao);
 s=seed;s.correction_mode=2;s.aircraft[0].clean_messages=1;assert(!adsb_ingest_manchester(&s,bad,false,-85,1200));
 s=seed;s.correction_mode=2;assert(!adsb_ingest_manchester(&s,bad,false,-85,61101));
 s=seed;s.correction_mode=2;s.aircraft[0].clean_seen=61099;assert(!adsb_ingest_manchester(&s,bad,false,-85,61101)); // One fresh clean frame is insufficient.
 s=seed;s.correction_mode=0;assert(!adsb_ingest_manchester(&s,bad,false,-85,1200));assert(!s.repair_candidates);
 // Assisted Manchester input must be aligned BEFORE the syndrome lookup.
 uint8_t assisted[14];for(int i=0;i<14;i++)assisted[i]=(bad[i]<<4)|(i==13?0:bad[i+1]>>4);
 s=seed;s.correction_mode=2;assert(adsb_ingest_manchester(&s,assisted,true,-85,1200));assert(s.corrected==1);
 memset(&s,0,sizeof(s));hex("8D40621D58C382D690C8AC2863A7",clean);adsb_message(&s,clean,-80,1000);hex("8D40621D58C386435CC412692AD6",clean);adsb_message(&s,clean,-80,2000);assert(s.aircraft[0].position_valid);s.correction_mode=2;
 memcpy(bad,clean,14);flip(bad,111);assert(adsb_ingest_manchester(&s,bad,false,-85,2100));assert(s.corrected==1&&s.valid==2);
 s.aircraft[0].latitude=0;s.aircraft[0].longitude=0;assert(!adsb_ingest_manchester(&s,bad,false,-85,2200));assert(s.corrected==1&&s.repair_rejected==1);
 printf("PASS decoder selftest; all 112 single-bit cases, all 6216 two-bit cases; observation isolation, clean counters, trust expiry, unknown addresses, assisted input and CPR plausibility\n");
}
