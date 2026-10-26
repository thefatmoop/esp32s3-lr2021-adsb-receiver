// SPDX-License-Identifier: GPL-3.0-or-later
#include "adsb.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
static uint32_t bits(const uint8_t *m,int start,int len) {
    uint32_t v=0;for(int i=0;i<len;i++){int p=start-1+i;v=(v<<1)|((m[p/8]>>(7-p%8))&1);}return v;
}
uint32_t adsb_crc(const uint8_t *m,int n) {
    uint32_t r=0;
    for(int i=0;i<n;i++){
        bool top=(r&0x800000)!=0;
        r=((r<<1)|((m[i/8]>>(7-i%8))&1))&0xffffff;
        if(top)r^=0xfff409;
    }
    return r;
}
int adsb_from_chips(const uint8_t c[28],bool assisted,uint8_t m[14],bool *pairs_ok) {
    memset(m,0,14);int head=assisted?4:0;
    if(assisted)m[0]=0x80;
    *pairs_ok=true;
    for(int i=head;i<112;i++) {
        int p=(i-head)*2;
        int a=(c[p/8]>>(7-p%8))&1,b=(c[(p+1)/8]>>(7-(p+1)%8))&1;
        if(a==b)*pairs_ok=false;
        m[i/8]|=a<<(7-i%8);
    }
    if(!*pairs_ok)return 0;
    int df=m[0]>>3;
    return (df==17||df==18)&&adsb_crc(m,112)==0?14:0;
}
static int nl(double lat) {
    lat=fabs(lat);
    if(lat>=87)return lat>87?1:2;
    double c=cos(lat*3.141592653589793/180.0);
    return (int)floor(2*3.141592653589793/acos(1-(1-cos(3.141592653589793/30))/(c*c)));
}
static int imod(int x,int y){int r=x%y;return r<0?r+y:r;}
static bool cpr(aircraft *a,int odd,uint64_t now) {
    if(!a->cpr_time[0]||!a->cpr_time[1]||now<a->cpr_time[0]||now<a->cpr_time[1]||
       now-a->cpr_time[0]>10000||now-a->cpr_time[1]>10000||a->cpr_type[0]!=a->cpr_type[1])return false;
    double le=a->cpr_lat[0]/131072.0,lo=a->cpr_lat[1]/131072.0;
    int j=(int)floor(59*le-60*lo+0.5);
    double lat[2]={6.0*(imod(j,60)+le),360.0/59*(imod(j,59)+lo)};
    for(int i=0;i<2;i++){if(lat[i]>=270)lat[i]-=360;if(fabs(lat[i])>90)return false;}
    if(nl(lat[0])!=nl(lat[1]))return false;
    int n=nl(lat[odd]),ni=n-odd;if(ni<1)ni=1;
    double oe=a->cpr_lon[0]/131072.0,oo=a->cpr_lon[1]/131072.0;
    int m=(int)floor(oe*(n-1)-oo*n+0.5);
    double lon=360.0/ni*(imod(m,ni)+(odd?oo:oe));if(lon>180)lon-=360;
    if(a->position_valid&&now>=a->position_at&&now-a->position_at<60000){
        double dlat=(lat[odd]-a->latitude)*111195;
        double dl=lon-a->longitude;if(dl>180)dl-=360;if(dl< -180)dl+=360;
        double dx=dl*111195*cos(lat[odd]*3.141592653589793/180);
        if(hypot(dx,dlat)>5000+1.5*(now-a->position_at))return false;
    }
    a->latitude=lat[odd];a->longitude=lon;a->position_valid=true;a->position_at=now;return true;
}
static aircraft *lookup(adsb_state *s,uint32_t addr,bool nonicao,uint64_t now) {
    aircraft *old=&s->aircraft[0];
    for(int i=0;i<ADSB_MAX_AIRCRAFT;i++){
        aircraft *a=&s->aircraft[i];
        if(a->icao==addr&&a->nonicao==nonicao&&now-a->seen<=ADSB_LIST_MS)return a;
        if(!a->icao){old=a;break;}
        if(a->seen<old->seen)old=a;
    }
    if(old->icao&&now-old->seen<=ADSB_LIST_MS)s->evictions++;
    memset(old,0,sizeof(*old));old->icao=addr;old->nonicao=nonicao;old->rssi=NAN;return old;
}
static void decode_fields(aircraft *a,const uint8_t m[14],uint64_t now){
    int tc=bits(m,33,5);
    if(tc>=1&&tc<=4){
        for(int i=0;i<8;i++){
            int c=bits(m,41+i*6,6);a->callsign[i]=c>=1&&c<=26?'A'+c-1:c>=48&&c<=57?'0'+c-48:' ';
        }
        a->callsign[8]=0;for(int i=7;i>=0&&a->callsign[i]==' ';i--)a->callsign[i]=0;
        a->callsign_at=now;
    }else if((tc>=9&&tc<=18)||(tc>=20&&tc<=22)){
        a->airborne=true;unsigned ac=bits(m,41,12);
        // Q=0 Gillham and GNSS altitude remain unknown, never use Q=1 units.
        if(tc<=18){
            a->altitude_valid=(ac&16)!=0;
            if(a->altitude_valid){a->altitude=(int)(((ac&0xfe0)>>1)|(ac&15))*25-1000;a->altitude_at=now;a->gnss_altitude=false;}
        }
        int odd=bits(m,54,1);a->cpr_lat[odd]=bits(m,55,17);a->cpr_lon[odd]=bits(m,72,17);
        a->cpr_time[odd]=now;a->cpr_type[odd]=tc<=18?0:1;cpr(a,odd,now);
    }else if(tc==19){
        unsigned sub=bits(m,38,3);
        if(sub==1||sub==2){
            int ew=bits(m,47,10),ns=bits(m,58,10),mult=sub==2?4:1;
            a->velocity_valid=ew&&ns;
            if(a->velocity_valid){
                ew=(ew-1)*mult*(bits(m,46,1)?-1:1);ns=(ns-1)*mult*(bits(m,57,1)?-1:1);
                a->speed=hypot(ew,ns);a->track=atan2(ew,ns)*180/3.141592653589793;
                if(a->track<0)a->track+=360;
            }
        }else a->velocity_valid=false;
        unsigned vr=bits(m,70,9);a->vertical_valid=vr!=0;
        if(vr)a->vertical_rate=((int)vr-1)*64*(bits(m,69,1)?-1:1);
        a->velocity_at=now;
    }else if(tc>=5&&tc<=8){
        a->airborne=false;a->position_valid=false;a->altitude_valid=false;
        a->velocity_valid=false;a->vertical_valid=false;
        a->cpr_time[0]=a->cpr_time[1]=0;
    }
}
bool adsb_message(adsb_state *s,const uint8_t m[14],float rssi,uint64_t now) {
    int df=m[0]>>3;
    if((df!=17&&df!=18)||adsb_crc(m,112))return false;
    s->valid++;s->last_valid=now;
    for(unsigned i=0;i<32;i++)if(s->repair_pending[i].at&&now>=s->repair_pending[i].at&&now-s->repair_pending[i].at<=10000&&!memcmp(s->repair_pending[i].message,m,14)){s->repair_confirmed++;s->repair_pending[i].at=0;break;}
    for(int i=0;i<14;i++)snprintf(s->last_frame+i*2,3,"%02X",m[i]);
    // DF18 CF0/1: direct ADS-B, ICAO/non-ICAO. Other CFs need relay handling.
    if(df==18&&(m[0]&7)>1){s->unsupported++;return true;}
    uint32_t addr=bits(m,9,24);if(!addr){s->unsupported++;return true;}
    aircraft *a=lookup(s,addr,df==18&&(m[0]&7)==1,now);
    a->messages++;a->seen=now;a->previous_clean_seen=a->clean_seen;a->clean_seen=now;a->clean_messages++;if(isfinite(rssi))a->rssi=rssi;
    decode_fields(a,m,now);
    return true;
}
bool adsb_ingest(adsb_state *s,const uint8_t chips[28],bool assisted,float rssi,uint64_t now) {
    s->candidates++;uint8_t m[14];bool pairs;
    if(!adsb_from_chips(chips,assisted,m,&pairs)){
        if(!pairs)s->bad_pairs++;else s->bad_crc++;return false;
    }
    return adsb_message(s,m,rssi,now);
}
// Search a bounded 8-us tail for a detector that latched early. No guessed
// payload bits, bit correction or relaxed CRC. Both bit polarities are checked
// only after every pulse pair in the candidate is valid.
static bool capture_message(const uint8_t *c,unsigned bytes,bool assisted,uint8_t *m,unsigned *best,unsigned *offset){
    const unsigned head=assisted?4:0, count=112-head;
    *best=113;*offset=0;
    if(bytes>ADSB_CAPTURE_BYTES||bytes*8<count*2)return false;
    unsigned max_shift=bytes*8-count*2;if(max_shift>16)max_shift=16;
    for(unsigned shift=0;shift<=max_shift;shift++){
        unsigned bad=0;memset(m,0,14);if(head)m[0]=0x80;
        for(unsigned bit=0;bit<count;bit++){
            unsigned p=shift+bit*2;
            unsigned hi=(c[p/8]>>(7-p%8))&1,lo=(c[(p+1)/8]>>(7-(p+1)%8))&1;
            bad+=hi==lo;
            m[(bit+head)/8]|=hi<<(7-(bit+head)%8);
        }
        if(bad<*best)*best=bad;
        if(bad)continue;
        for(unsigned inv=0;inv<2;inv++){
            if(inv){for(unsigned k=0;k<14;k++)m[k]^=255;if(head)m[0]=(m[0]&15)|0x80;}
            unsigned df=m[0]>>3;
            if((df==17||df==18)&&!adsb_crc(m,112)){*offset=shift;return true;}
        }
    }
    return false;
}
bool adsb_ingest_capture(adsb_state *s,const uint8_t *c,bool assisted,float rssi,uint64_t now){
    return adsb_ingest_capture_n(s,c,ADSB_CAPTURE_BYTES,assisted,rssi,now);
}
bool adsb_ingest_capture_n(adsb_state *s,const uint8_t *c,unsigned bytes,bool assisted,float rssi,uint64_t now){
    uint8_t m[14];unsigned bad,offset;bool found=capture_message(c,bytes,assisted,m,&bad,&offset);
    if(!s->candidates||bad<s->best_bad_pairs)s->best_bad_pairs=bad;
    s->candidates++;
    unsigned bucket=bad==0?0:bad<=2?1:bad<=8?2:bad<=16?3:bad<=32?4:bad<=64?5:bad<=96?6:7;
    s->quality[bucket]++;
    if(bad<=8)s->near_pairs++;
    if(!bad)s->clean_pairs++;
    if(!found){if(bad)s->bad_pairs++;else s->bad_crc++;return false;}
    if(offset)s->aligned_valid++;
    s->capture_offsets[offset]++;
    return adsb_message(s,m,rssi,now);
}
static uint32_t repair_syndromes[80];
static bool repair_ready;
void adsb_correction_init(void){
    if(repair_ready)return;
    for(unsigned bit=32;bit<112;bit++){uint8_t m[14]={0};m[bit/8]=1u<<(7-bit%8);repair_syndromes[bit-32]=adsb_crc(m,112);}
    repair_ready=true;
}
static bool repair_message(adsb_state *s,uint8_t m[14],float rssi,uint64_t now){
    unsigned df=m[0]>>3;if((df!=17&&df!=18)||(df==18&&(m[0]&7)>1))return false;
    adsb_correction_init();uint32_t syndrome=adsb_crc(m,112);if(!syndrome)return false;
    int bit=-1;for(unsigned i=0;i<80;i++)if(repair_syndromes[i]==syndrome){if(bit>=0)return false;bit=i+32;}
    if(bit<0)return false;
    m[bit/8]^=1u<<(7-bit%8);if(adsb_crc(m,112))return false;s->repair_candidates++;
    uint32_t addr=bits(m,9,24);bool nonicao=df==18&&(m[0]&7)==1;aircraft *a=NULL;
    for(unsigned i=0;i<ADSB_MAX_AIRCRAFT;i++)if(s->aircraft[i].icao==addr&&s->aircraft[i].nonicao==nonicao){a=&s->aircraft[i];break;}
    // Corrected packets never establish or renew trust in an aircraft address.
    if(!a||a->clean_messages<2||now<a->clean_seen||now<a->previous_clean_seen||now-a->previous_clean_seen>60000){s->repair_rejected++;return false;}
    unsigned tc=bits(m,33,5);bool position=(tc>=9&&tc<=18)||(tc>=20&&tc<=22),identity=tc>=1&&tc<=4,velocity=tc==19;
    if(!position&&!identity&&!velocity){s->repair_rejected++;return false;}
    aircraft trial=*a;decode_fields(&trial,m,now);bool plausible=true;
    if(identity&&a->callsign[0]&&strcmp(a->callsign,trial.callsign))plausible=false;
    if(position){
        if(!a->position_valid||now<a->position_at||now-a->position_at>30000||trial.position_at!=now)plausible=false;
        if(a->altitude_valid&&trial.altitude_valid&&now>=a->altitude_at&&now-a->altitude_at<=30000&&abs(trial.altitude-a->altitude)>2000+100*(now-a->altitude_at)/1000)plausible=false;
    }
    if(velocity){
        if(!trial.velocity_valid||trial.speed>1200)plausible=false;
        if(a->velocity_valid&&now>=a->velocity_at&&now-a->velocity_at<=30000&&fabsf(trial.speed-a->speed)>150+10*(now-a->velocity_at)/1000)plausible=false;
        if(trial.vertical_valid&&abs(trial.vertical_rate)>12000)plausible=false;
    }
    if(!plausible){s->repair_rejected++;return false;}
    s->repair_eligible++;s->last_repair_bit=bit+1;
    for(unsigned i=0;i<14;i++)snprintf(s->last_repaired+2*i,3,"%02X",m[i]);
    bool pending=false;for(unsigned i=0;i<32;i++)if(s->repair_pending[i].at&&now-s->repair_pending[i].at<=10000&&!memcmp(s->repair_pending[i].message,m,14)){pending=true;break;}
    if(!pending){unsigned i=s->repair_pending_next++%32;memcpy(s->repair_pending[i].message,m,14);s->repair_pending[i].at=now;}
    if(s->correction_mode!=2)return false;
    trial.messages++;trial.corrected_messages++;trial.seen=now;if(isfinite(rssi))trial.rssi=rssi;
    *a=trial;s->corrected++;memcpy(s->last_frame,s->last_repaired,sizeof(s->last_frame));return true;
}
bool adsb_ingest_manchester(adsb_state *s,const uint8_t capture[14],bool assisted,float rssi,uint64_t now){
    // The radio supplies decoded bits. In assisted mode its detector consumed
    // the first four bits (1000); discard the four trailing capture bits.
    uint8_t m[14];
    for(unsigned k=0;k<14;k++)m[k]=assisted?((k?capture[k-1]<<4:0x80)|(capture[k]>>4)):capture[k];
    s->candidates++;
    bool valid=adsb_message(s,m,rssi,now);
    if(!valid){s->bad_crc++;if(s->correction_mode)valid=repair_message(s,m,rssi,now);}
    return valid;
}
static void unhex(const char *p,uint8_t m[14]){for(int i=0;i<14;i++){unsigned v=0;sscanf(p+i*2,"%2x",&v);m[i]=v;}}
bool adsb_selftest(void) {
    static adsb_state t;memset(&t,0,sizeof(t));uint8_t m[14],c[28],out[14];bool ok;
    unhex("8D40621D58C382D690C8AC2863A7",m);
    if(adsb_crc(m,112)||!adsb_message(&t,m,NAN,1000))return false;
    memset(c,0,sizeof(c));for(int i=0;i<112;i++){unsigned p=((m[i/8]>>(7-i%8))&1)?2:1;c[i/4]|=p<<(6-2*(i%4));}
    if(adsb_from_chips(c,false,out,&ok)!=14||memcmp(m,out,14))return false;
    uint8_t extended[ADSB_CAPTURE_BYTES];unsigned bad,offset;
    for(unsigned shift=0;shift<=16;shift++){
        memset(extended,0,sizeof(extended));
        for(unsigned k=0;k<224;k++)if((c[k/8]>>(7-k%8))&1)extended[(k+shift)/8]|=1<<(7-(k+shift)%8);
        if(!capture_message(extended,32,false,out,&bad,&offset)||memcmp(m,out,14)||bad||offset!=shift)return false;
        // 30 bytes retain every supported alignment; exact-length captures
        // must reject frames whose final chips fall outside the buffer.
        if(!capture_message(extended,30,false,out,&bad,&offset)||memcmp(m,out,14)||offset!=shift)return false;
        if(capture_message(extended,28,false,out,&bad,&offset)!=(shift==0))return false;
    }
    memset(extended,255,sizeof(extended));
    if(capture_message(extended,32,false,out,&bad,&offset)||bad!=112)return false;
    uint8_t assisted[28]={0};
    for(int i=0;i<216;i++)if((c[(i+8)/8]>>(7-(i+8)%8))&1)assisted[i/8]|=1<<(7-i%8);
    if(adsb_from_chips(assisted,true,out,&ok)!=14||memcmp(m,out,14))return false;
    for(unsigned shift=0;shift<=16;shift++){
        memset(extended,0,sizeof(extended));
        for(unsigned k=0;k<216;k++)if((assisted[k/8]>>(7-k%8))&1)extended[(k+shift)/8]|=1<<(7-(k+shift)%8);
        if(!capture_message(extended,30,true,out,&bad,&offset)||memcmp(m,out,14)||offset!=shift)return false;
        if(capture_message(extended,28,true,out,&bad,&offset)!=(shift<=8))return false;
    }
    for(int i=0;i<112;i++){m[i/8]^=1<<(7-i%8);if(!adsb_crc(m,112))return false;m[i/8]^=1<<(7-i%8);}
    c[0]&=0x3f;if(adsb_from_chips(c,false,out,&ok)||ok)return false;
    unhex("8D40621D58C386435CC412692AD6",m);
    if(!adsb_message(&t,m,NAN,2000)||!t.aircraft[0].position_valid)return false;
    if(fabs(t.aircraft[0].latitude-52.2657801741)>0.00001||fabs(t.aircraft[0].longitude-3.9389125279)>0.00001||t.aircraft[0].altitude!=38000)return false;
    unhex("8D4840D6202CC371C32CE0576098",m);if(!adsb_message(&t,m,NAN,2100)||strcmp(t.aircraft[1].callsign,"KLM1023"))return false;
    unhex("8D485020994409940838175B284F",m);if(!adsb_message(&t,m,NAN,2200))return false;
    if(!t.aircraft[2].velocity_valid||fabs(t.aircraft[2].speed-159.2)>0.2||t.aircraft[2].vertical_rate!=-832)return false;
    memset(&t,0,sizeof(t));
    unhex("8D40621D58C382D690C8AC2863A7",m);adsb_message(&t,m,NAN,1000);
    unhex("8D40621D58C386435CC412692AD6",m);adsb_message(&t,m,NAN,12001);
    if(t.aircraft[0].position_valid)return false;
    // A returning aircraft keeps its identity and fields for the full list window.
    unhex("8D4840D6202CC371C32CE0576098",m);adsb_message(&t,m,NAN,20000);
    aircraft *retained=&t.aircraft[1];
    if(lookup(&t,retained->icao,false,620000)!=retained||strcmp(retained->callsign,"KLM1023"))return false;
    aircraft *expired=lookup(&t,retained->icao,false,620001);
    if(expired->messages||expired->callsign[0]||expired->position_valid)return false;
    // Hardware decoding still requires software CRC validation, including
    // after reconstructing the assisted detector's four consumed header bits.
    memset(&t,0,sizeof(t));unhex("8D40621D58C382D690C8AC2863A7",m);
    if(!adsb_ingest_manchester(&t,m,false,NAN,1000)||t.valid!=1)return false;
    uint8_t hw[14];
    for(unsigned i=0;i<14;i++)hw[i]=(m[i]<<4)|(i<13?m[i+1]>>4:15);
    if(!adsb_ingest_manchester(&t,hw,true,NAN,1001)||t.valid!=2)return false;
    for(unsigned i=0;i<112;i++){
        m[i/8]^=1<<(7-i%8);
        if(adsb_ingest_manchester(&t,m,false,NAN,1002))return false;
        m[i/8]^=1<<(7-i%8);
    }
    for(unsigned i=0;i<108;i++){
        hw[i/8]^=1<<(7-i%8);
        if(adsb_ingest_manchester(&t,hw,true,NAN,1003))return false;
        hw[i/8]^=1<<(7-i%8);
    }
    if(t.valid!=2||t.candidates!=222||t.bad_crc!=220)return false;
    memset(&t,0,sizeof(t));return true;
}
