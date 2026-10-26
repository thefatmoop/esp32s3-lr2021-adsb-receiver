"""Exercise the firmware's actual chunk writer with a full aircraft table.

Run: python tests/test_web_serialization.py <C compiler> [compiler arguments]
For Zig, pass: /path/to/zig cc
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main/web.c').read_text(encoding='utf-8')
writer = source[source.index('typedef struct {httpd_req_t *r;char data['):source.index('static esp_err_t state_finish(httpd_req_t *r,cJSON *metadata,adsb_state *s,const rx_status *st){')]
harness = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "adsb.h"
typedef int esp_err_t;
#define ESP_FAIL -1
typedef struct {int calls,fail_at;size_t largest;FILE *out;} httpd_req_t;
typedef struct {uint64_t now;} rx_status;
static int httpd_resp_send_chunk(httpd_req_t *r,const char *p,size_t n){
    if(++r->calls==r->fail_at)return ESP_FAIL;
    if(n>r->largest)r->largest=n;
    if(n)fwrite(p,1,n,r->out);
    return 0;
}
'''
harness += writer
harness += r'''
int main(int argc,char **argv){
    adsb_state s={0};rx_status st={.now=700000};
    for(int i=0;i<ADSB_MAX_AIRCRAFT;i++){
        aircraft *a=&s.aircraft[i];a->icao=0xABC000+i;
        strcpy(a->callsign,"TEST123");a->seen=699000;a->position_at=698000;
        a->altitude_at=a->velocity_at=699000;a->position_valid=a->velocity_valid=true;
        a->altitude_valid=a->vertical_valid=true;a->latitude=40.123456;a->longitude=-74.123456;
        a->altitude=35000;a->speed=499.5;a->track=180.5;a->rssi=-85.5;
    }
    s.aircraft[0].rssi=NAN;s.aircraft[1].position_valid=false;
    s.aircraft[2].velocity_at=0;s.aircraft[2].altitude_at=0;
    if(argc>2&&strcmp(argv[2],"empty")==0)memset(&s,0,sizeof(s));
    httpd_req_t r={.out=fopen(argv[1],"wb")};
    if(argc>2&&strcmp(argv[2],"fail")==0)r.fail_at=2;
    json_writer w={.r=&r};write_json(&w,"{\"aircraft\":[");write_aircraft(&w,&s,&st);
    int result=finish_aircraft(&w);fclose(r.out);
    if(r.fail_at)return result==ESP_FAIL&&r.calls==2?0:1;
    return result==0&&r.largest<=sizeof(w.data)?0:2;
}
'''
with tempfile.TemporaryDirectory() as directory:
    temp=Path(directory);c=temp/'test.c';exe=temp/'test.exe';out=temp/'result.json'
    c.write_text(harness,encoding='utf-8')
    compiler=sys.argv[1:] or ['cc']
    subprocess.run(compiler+['-std=c11', '-I'+str(root/'main'),str(c),'-o',str(exe)],check=True)
    for mode in ['full','empty','fail']:
        subprocess.run([str(exe),str(out),mode],check=True)
        if mode=='fail':continue
        aircraft=json.loads(out.read_text())['aircraft']
        assert len(aircraft)==(96 if mode=='full' else 0)
        if aircraft:
            assert aircraft[0]['rssi'] is None
            assert aircraft[1]['lat'] is None and aircraft[1]['positionAge'] is None
            assert aircraft[2]['speed'] is None and aircraft[2]['altitude'] is None
            assert aircraft[-1]['address']=='ABC05F'
            assert aircraft[-1]['callsign']=='TEST123' and aircraft[-1]['age']==1
    print('PASS: 96 aircraft, empty table, stale/null fields, chunk boundaries, send failure')
