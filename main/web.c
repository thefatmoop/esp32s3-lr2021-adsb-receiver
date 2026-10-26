// SPDX-License-Identifier: GPL-3.0-or-later
#include "web.h"
#include "receiver.h"
#include "gnss.h"
#include "tcp_stream.h"
#include "trail_cache.h"
#include "map_version.h"
#include <stdarg.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
static char token[33];
// One HTTP task serializes these handlers; reserve its snapshot once.
static adsb_state web_snapshot;
extern const uint8_t setup_start[] asm("_binary_setup_html_start"),setup_end[] asm("_binary_setup_html_end");
extern const uint8_t setup_js_start[] asm("_binary_setup_js_start"),setup_js_end[] asm("_binary_setup_js_end");
extern const uint8_t ui_start[] asm("_binary_ui_html_start"),ui_end[] asm("_binary_ui_html_end");
extern const uint8_t js_start[] asm("_binary_ui_js_start"),js_end[] asm("_binary_ui_js_end");
extern const uint8_t live_js_start[] asm("_binary_live_js_start"),live_js_end[] asm("_binary_live_js_end");
extern const uint8_t css_start[] asm("_binary_style_css_start"),css_end[] asm("_binary_style_css_end");
extern const uint8_t world_start[] asm("_binary_world_json_gz_start"),world_end[] asm("_binary_world_json_gz_end");
static esp_err_t asset(httpd_req_t *r){
    const uint8_t *p=ui_start,*end=ui_end;const char *type="text/html; charset=utf-8";
    network_info ni;web_network(&ni);
    if(!strcmp(r->uri,"/setup")||(!strcmp(r->uri,"/")&&ni.ap)){p=setup_start;end=setup_end;}
    if(!strcmp(r->uri,"/setup.js")){p=setup_js_start;end=setup_js_end;type="text/javascript";}
    if(!strcmp(r->uri,"/ui.js")){p=js_start;end=js_end;type="text/javascript";}
    if(!strcmp(r->uri,"/live.js")){p=live_js_start;end=live_js_end;type="text/javascript";}
    if(!strcmp(r->uri,"/style.css")){p=css_start;end=css_end;type="text/css";}
    httpd_resp_set_type(r,type);httpd_resp_set_hdr(r,"Cache-Control","no-cache");
    if(!strcmp(r->uri,"/world.json")){
        p=world_start;end=world_end;httpd_resp_set_type(r,"application/json");
        httpd_resp_set_hdr(r,"Content-Encoding","gzip");httpd_resp_set_hdr(r,"ETag",MAP_ETAG);
        char etag[40];if(httpd_req_get_hdr_value_str(r,"If-None-Match",etag,sizeof(etag))==ESP_OK&&!strcmp(etag,MAP_ETAG)){
            httpd_resp_set_status(r,"304 Not Modified");return httpd_resp_send(r,NULL,0);
        }
    }
    httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'self'; style-src 'self'; script-src 'self'; connect-src 'self' https://api.adsbdb.com; img-src 'self' data: https://tile.openstreetmap.org https://airport-data.com https://*.airport-data.com; frame-ancestors 'none'");
    return httpd_resp_send(r,(const char *)p,end-p);
}
static esp_err_t json_send(httpd_req_t *r,cJSON *v){
    char *p=cJSON_PrintUnformatted(v);cJSON_Delete(v);
    if(!p)return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");
    httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");
    esp_err_t e=httpd_resp_sendstr(r,p);cJSON_free(p);return e;
}
static esp_err_t network_get(httpd_req_t *r){
    network_info n;web_network(&n);cJSON *j=cJSON_CreateObject();
    cJSON_AddStringToObject(j,"token",token);cJSON_AddBoolToObject(j,"connected",n.connected);cJSON_AddBoolToObject(j,"ap",n.ap);
    cJSON_AddBoolToObject(j,"testing",n.testing);cJSON_AddStringToObject(j,"ip",n.ip);cJSON_AddStringToObject(j,"ssid",n.ssid);
    cJSON_AddNumberToObject(j,"rssi",n.rssi);cJSON_AddNumberToObject(j,"rebootSeconds",n.reboot_seconds);
    cJSON_AddStringToObject(j,"message",n.message);cJSON_AddNumberToObject(j,"error",n.error);
    cJSON_AddStringToObject(j,"apName",n.ap?SETUP_SSID:"");return json_send(r,j);
}
static void number(cJSON *o,const char *k,double v,bool valid){if(valid&&isfinite(v))cJSON_AddNumberToObject(o,k,v);else cJSON_AddNullToObject(o,k);}
static void tcp_json(cJSON *j){
    tcp_status t;tcp_stream_snapshot(&t);cJSON *o=cJSON_AddObjectToObject(j,"tcp");
    cJSON_AddBoolToObject(o,"enabled",t.config.enabled);cJSON_AddNumberToObject(o,"port",t.config.port);
    cJSON_AddStringToObject(o,"format",t.config.avr?"avr":"beast");cJSON_AddNumberToObject(o,"clients",t.clients);
    cJSON_AddNumberToObject(o,"queued",t.queued);cJSON_AddNumberToObject(o,"bytes",t.sent);
    cJSON_AddNumberToObject(o,"dropped",t.dropped);cJSON_AddNumberToObject(o,"slowClients",t.slow_clients);cJSON_AddNumberToObject(o,"error",t.error);
}
static esp_err_t state_finish(httpd_req_t *r,cJSON *metadata,adsb_state *s,const rx_status *st);
static esp_err_t state_get(httpd_req_t *r){
    adsb_state *s=&web_snapshot;
    rx_status st;receiver_snapshot(s,&st);network_info ni;web_network(&ni);
    cJSON *j=cJSON_CreateObject();
    tcp_json(j);cJSON_AddNumberToObject(j,"trailCapacity",0);cJSON_AddNumberToObject(j,"liveHz",2);
    cJSON_AddStringToObject(j,"token",token);cJSON_AddNumberToObject(j,"uptime",st.now/1000.0);
    cJSON_AddNumberToObject(j,"listSeconds",ADSB_LIST_MS/1000);cJSON_AddNumberToObject(j,"mapSeconds",ADSB_MAP_MS/1000);
    cJSON_AddBoolToObject(j,"selftest",st.selftest);cJSON_AddNumberToObject(j,"error",st.error);
    cJSON_AddBoolToObject(j,"hardwareManchester",st.config.hardware_manchester);
    cJSON_AddBoolToObject(j,"hf",st.config.hf);
    cJSON_AddNumberToObject(j,"generation",st.generation);cJSON_AddNumberToObject(j,"frequency",1090000000);
    cJSON_AddNumberToObject(j,"candidates",s->candidates);cJSON_AddNumberToObject(j,"badPairs",s->bad_pairs);
    cJSON_AddNumberToObject(j,"badCrc",s->bad_crc);cJSON_AddNumberToObject(j,"valid",s->valid);
    cJSON_AddNumberToObject(j,"unsupported",s->unsupported);cJSON_AddNumberToObject(j,"evictions",s->evictions);
    cJSON_AddNumberToObject(j,"correctionMode",s->correction_mode);cJSON_AddNumberToObject(j,"corrected",s->corrected);
    cJSON_AddNumberToObject(j,"repairCandidates",s->repair_candidates);cJSON_AddNumberToObject(j,"repairRejected",s->repair_rejected);
    cJSON_AddNumberToObject(j,"repairEligible",s->repair_eligible);cJSON_AddNumberToObject(j,"repairConfirmed",s->repair_confirmed);
    cJSON_AddStringToObject(j,"lastRepaired",s->last_repaired);cJSON_AddNumberToObject(j,"lastRepairBit",s->last_repair_bit);
    cJSON_AddStringToObject(j,"lastFrame",s->last_frame);number(j,"lastValidAge",(st.now-s->last_valid)/1000.0,s->valid);
    cJSON_AddBoolToObject(j,"hunt",st.config.hunt);cJSON_AddNumberToObject(j,"tuneProfile",st.tune_profile);
    cJSON_AddStringToObject(j,"tuning",!st.config.hunt?"manual":st.hold_until>st.now?"locked":"searching");
    cJSON_AddNumberToObject(j,"holdSeconds",st.hold_until>st.now?(st.hold_until-st.now)/1000.0:0);
    cJSON_AddNumberToObject(j,"bandwidth",st.config.bandwidth);
    number(j,"bestBadPairs",s->best_bad_pairs,s->best_bad_pairs<=112);cJSON_AddNumberToObject(j,"nearPairs",s->near_pairs);cJSON_AddNumberToObject(j,"cleanPairs",s->clean_pairs);cJSON_AddNumberToObject(j,"alignedValid",s->aligned_valid);
    cJSON *quality=cJSON_AddArrayToObject(j,"quality");for(int i=0;i<8;i++)cJSON_AddItemToArray(quality,cJSON_CreateNumber(s->quality[i]));
    number(j,"rssiNow",st.radio.rssi_now,true);number(j,"rssiPeak",st.radio.rssi_peak,true);
    cJSON_AddNumberToObject(j,"gain",st.config.gain);cJSON_AddNumberToObject(j,"boost",st.config.boost);
    cJSON_AddNumberToObject(j,"threshold",st.config.threshold);cJSON_AddBoolToObject(j,"assisted",st.config.assisted);
    cJSON_AddNumberToObject(j,"overflows",st.radio.overflows);cJSON_AddNumberToObject(j,"radioErrors",st.radio.errors);
    cJSON_AddNumberToObject(j,"fifoFull",st.radio.fifo_full);cJSON_AddNumberToObject(j,"partialResets",st.radio.partial_resets);
    cJSON_AddNumberToObject(j,"fifoOverflowFlags",st.radio.fifo_overflow_flags);cJSON_AddNumberToObject(j,"fifoUnderflowFlags",st.radio.fifo_underflow_flags);
    cJSON_AddNumberToObject(j,"fifoPeak",st.radio.fifo_peak);cJSON_AddNumberToObject(j,"irqWakes",st.radio.irq_wakes);
    cJSON_AddNumberToObject(j,"captureBytes",st.config.capture_bytes);cJSON_AddBoolToObject(j,"irqWake",st.config.irq_wake);
    cJSON *offsets=cJSON_AddArrayToObject(j,"captureOffsets");for(unsigned i=0;i<17;i++)cJSON_AddItemToArray(offsets,cJSON_CreateNumber(s->capture_offsets[i]));
    number(j,"noiseDbm",st.radio.noise_dbm,st.config.threshold==101);cJSON_AddNumberToObject(j,"thresholdRaw",st.radio.threshold_raw);
    cJSON_AddNumberToObject(j,"recoveries",st.radio.recoveries);cJSON_AddNumberToObject(j,"polls",st.radio.polls);
    cJSON_AddNumberToObject(j,"heap",esp_get_free_heap_size());
    cJSON_AddNumberToObject(j,"heapMinimum",esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(j,"heapLargest",heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(j,"resetReason",esp_reset_reason());
    cJSON_AddNumberToObject(j,"wifiDisconnects",ni.disconnects);cJSON_AddNumberToObject(j,"wifiLastReason",ni.last_reason);cJSON_AddStringToObject(j,"ip",ni.ip);
    cJSON_AddNumberToObject(j,"wifiRssi",ni.rssi);cJSON_AddNumberToObject(j,"apRebootSeconds",ni.reboot_seconds);cJSON_AddStringToObject(j,"wifiMessage",ni.message);cJSON_AddBoolToObject(j,"wifiTesting",ni.testing);
    cJSON_AddStringToObject(j,"ssid",ni.ssid);cJSON_AddBoolToObject(j,"connected",ni.connected);cJSON_AddBoolToObject(j,"ap",ni.ap);
    gnss_status gps;gnss_snapshot(&gps);cJSON *g=cJSON_AddObjectToObject(j,"gnss");
    cJSON_AddBoolToObject(g,"hasPosition",gps.has_position);cJSON_AddBoolToObject(g,"fix",gps.fix);
    cJSON_AddBoolToObject(g,"saved",gps.saved_position);cJSON_AddBoolToObject(g,"persisted",gps.persisted);
    cJSON_AddBoolToObject(g,"selftest",gps.selftest);cJSON_AddNumberToObject(g,"error",gps.error);cJSON_AddNumberToObject(g,"saveError",gps.save_error);
    number(g,"lat",gps.latitude,gps.has_position);number(g,"lon",gps.longitude,gps.has_position);
    number(g,"age",st.now>=gps.last_fix?(st.now-gps.last_fix)/1000.0:0,gps.has_position&&!gps.saved_position);
    number(g,"sentenceAge",st.now>=gps.last_sentence?(st.now-gps.last_sentence)/1000.0:0,gps.sentences>0);
    number(g,"hdop",gps.hdop,true);cJSON_AddNumberToObject(g,"satellites",gps.satellites);cJSON_AddNumberToObject(g,"quality",gps.quality);
    cJSON_AddNumberToObject(g,"bytes",gps.bytes);cJSON_AddNumberToObject(g,"sentences",gps.sentences);cJSON_AddNumberToObject(g,"badChecksum",gps.bad_checksum);
    return state_finish(r,j,s,&st);
}
static cJSON *body(httpd_req_t *r){
    char auth[40];if(httpd_req_get_hdr_value_str(r,"X-ADSB-Token",auth,sizeof(auth))!=ESP_OK||strcmp(auth,token)){httpd_resp_send_err(r,HTTPD_403_FORBIDDEN,"Reload the page");return NULL;}
    if(r->content_len<=0||r->content_len>512){httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid request size");return NULL;}
    char b[513];int n=0;while(n<r->content_len){int got=httpd_req_recv(r,b+n,r->content_len-n);if(got<=0){httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Incomplete request");return NULL;}n+=got;}b[n]=0;
    const char *end=NULL;cJSON *j=cJSON_ParseWithLengthOpts(b,n+1,&end,true);
    if(!cJSON_IsObject(j)){cJSON_Delete(j);httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid JSON object");return NULL;}return j;
}
static bool integer(cJSON *j,const char *key,int min,int max,int *out){cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||floor(v->valuedouble)!=v->valuedouble||v->valuedouble<min||v->valuedouble>max)return false;*out=v->valueint;return true;}
static esp_err_t radio_post(httpd_req_t *r){
    cJSON *j=body(r);if(!j)return ESP_OK;int g=0,b=0,t=100;bool ok=integer(j,"gain",0,13,&g)&&integer(j,"boost",0,7,&b)&&integer(j,"threshold",-64,101,&t)&&(t<=63||t==100||t==101);
    cJSON *a=cJSON_GetObjectItemCaseSensitive(j,"assisted");ok=ok&&cJSON_IsBool(a);
    int bw=0;if(cJSON_HasObjectItem(j,"bandwidth"))ok=ok&&integer(j,"bandwidth",0,3,&bw);
    cJSON *temp=cJSON_GetObjectItemCaseSensitive(j,"temporary");if(temp&&!cJSON_IsBool(temp))ok=false;
    cJSON *hunt=cJSON_GetObjectItemCaseSensitive(j,"hunt");if(hunt&&!cJSON_IsBool(hunt))ok=false;
    cJSON *hf=cJSON_GetObjectItemCaseSensitive(j,"hf");if(hf&&!cJSON_IsBool(hf))ok=false;
    // Existing browser forms omit capture controls; preserve the tested values.
    rx_status prior;receiver_snapshot(NULL,&prior);int bytes=prior.config.capture_bytes?prior.config.capture_bytes:32;
    if(cJSON_HasObjectItem(j,"captureBytes"))ok=ok&&integer(j,"captureBytes",28,32,&bytes);
    cJSON *wake=cJSON_GetObjectItemCaseSensitive(j,"irqWake");if(wake&&!cJSON_IsBool(wake))ok=false;
    cJSON *hw=cJSON_GetObjectItemCaseSensitive(j,"hardwareManchester");if(hw&&!cJSON_IsBool(hw))ok=false;
    rx_config c={.hf=cJSON_IsTrue(hf),.hunt=cJSON_IsTrue(hunt),.gain=g,.boost=b,.threshold=t,.assisted=cJSON_IsTrue(a),.bandwidth=bw,.temporary=cJSON_IsTrue(temp),.capture_bytes=bytes,.irq_wake=wake?cJSON_IsTrue(wake):prior.config.irq_wake,.hardware_manchester=hw?cJSON_IsTrue(hw):prior.config.hardware_manchester};cJSON_Delete(j);
    if(!ok)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid receiver settings");
    if(!receiver_configure(&c)){httpd_resp_set_status(r,"503 Service Unavailable");return httpd_resp_sendstr(r,"Receiver busy");}
    httpd_resp_set_status(r,"202 Accepted");httpd_resp_set_type(r,"application/json");return httpd_resp_sendstr(r,"{\"accepted\":true}");
}
static esp_err_t wifi_post(httpd_req_t *r){
    cJSON *j=body(r);if(!j)return ESP_OK;
    cJSON *s=cJSON_GetObjectItemCaseSensitive(j,"ssid"),*p=cJSON_GetObjectItemCaseSensitive(j,"password");
    cJSON *tmp=cJSON_GetObjectItemCaseSensitive(j,"temporary");
    bool ok=cJSON_IsString(s)&&cJSON_IsString(p)&&(!tmp||cJSON_IsBool(tmp))&&network_submit(s->valuestring,p->valuestring,cJSON_IsTrue(tmp));cJSON_Delete(j);
    if(!ok)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid Wi-Fi details or busy");
    httpd_resp_set_type(r,"application/json");return httpd_resp_sendstr(r,"{\"accepted\":true}");
}
static esp_err_t tcp_post(httpd_req_t *r){
    cJSON *j=body(r);if(!j)return ESP_OK;int port;
    cJSON *on=cJSON_GetObjectItemCaseSensitive(j,"enabled"),*format=cJSON_GetObjectItemCaseSensitive(j,"format");
    bool ok=cJSON_IsBool(on)&&integer(j,"port",1024,65535,&port)&&cJSON_IsString(format)&&(!strcmp(format->valuestring,"beast")||!strcmp(format->valuestring,"avr"));
    tcp_config cfg={0};if(ok)cfg=(tcp_config){port,cJSON_IsTrue(on),!strcmp(format->valuestring,"avr")};cJSON_Delete(j);
    if(!ok)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Use Beast or AVR and port 1024-65535");
    if(!tcp_stream_configure(cfg))return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Could not save TCP settings");
    httpd_resp_set_status(r,"202 Accepted");httpd_resp_set_type(r,"application/json");return httpd_resp_sendstr(r,"{\"accepted\":true}");
}
static esp_err_t correction_post(httpd_req_t *r){
    cJSON *j=body(r);if(!j)return ESP_OK;int mode;bool ok=integer(j,"mode",0,2,&mode);cJSON_Delete(j);
    if(!ok)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Mode must be 0=off, 1=observe, or 2=apply");
    if(!receiver_correction_mode(mode))return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Could not save correction mode");
    httpd_resp_set_type(r,"application/json");return httpd_resp_sendstr(r,"{\"accepted\":true}");
}
// Live snapshots use a fixed output buffer rather than a cJSON allocation per
// field. The HTTP task serializes clients; RF lock is held only during copy.
typedef struct {httpd_req_t *r;char data[3072];size_t used;esp_err_t error;} json_writer;
static void write_json(json_writer *w,const char *format,...){
    if(w->error)return;va_list ap;va_start(ap,format);int n=vsnprintf(w->data+w->used,sizeof(w->data)-w->used,format,ap);va_end(ap);
    if(n<0){w->error=ESP_FAIL;return;}
    if((size_t)n>=sizeof(w->data)-w->used){
        w->error=httpd_resp_send_chunk(w->r,w->data,w->used);w->used=0;if(w->error)return;
        va_start(ap,format);n=vsnprintf(w->data,sizeof(w->data),format,ap);va_end(ap);
        if(n<0||(size_t)n>=sizeof(w->data)){w->error=ESP_FAIL;return;}
    }w->used+=n;
}
// The server runs handlers serially. Stream aircraft in bounded chunks instead
// of constructing a cJSON tree and a second full-sized response in the heap.
static void write_aircraft(json_writer *w,const adsb_state *s,const rx_status *st){
    bool first=true;
    for(int i=0;i<ADSB_MAX_AIRCRAFT&&!w->error;i++){
        const aircraft *a=&s->aircraft[i];if(!a->icao||st->now-a->seen>ADSB_LIST_MS)continue;
        // Decoder callsigns are strictly A-Z/0-9/space, requiring no escaping.
        write_json(w,"%s{\"address\":\"%06lX\",\"nonIcao\":%s,\"callsign\":\"%s\",\"messages\":%lu,\"age\":%.3f",first?"":",",(unsigned long)a->icao,a->nonicao?"true":"false",a->callsign,(unsigned long)a->messages,(st->now-a->seen)/1000.0);first=false;
        write_json(w,",\"cleanMessages\":%lu,\"correctedMessages\":%lu",(unsigned long)a->clean_messages,(unsigned long)a->corrected_messages);
        if(a->position_valid)write_json(w,",\"lat\":%.6f,\"lon\":%.6f,\"positionAge\":%.3f",a->latitude,a->longitude,(st->now-a->position_at)/1000.0);
        else write_json(w,",\"lat\":null,\"lon\":null,\"positionAge\":null");
        if(a->altitude_valid&&st->now-a->altitude_at<=30000)write_json(w,",\"altitude\":%d",a->altitude);else write_json(w,",\"altitude\":null");
        if(a->velocity_valid&&st->now-a->velocity_at<=30000)write_json(w,",\"speed\":%.1f,\"track\":%.1f",a->speed,a->track);else write_json(w,",\"speed\":null,\"track\":null");
        if(a->vertical_valid&&st->now-a->velocity_at<=30000)write_json(w,",\"verticalRate\":%d",a->vertical_rate);else write_json(w,",\"verticalRate\":null");
        if(isfinite(a->rssi))write_json(w,",\"rssi\":%.1f}",a->rssi);else write_json(w,",\"rssi\":null}");
    }
}
static esp_err_t finish_aircraft(json_writer *w){
    write_json(w,"]}");
    if(!w->error)w->error=httpd_resp_send_chunk(w->r,w->data,w->used);
    if(!w->error)w->error=httpd_resp_send_chunk(w->r,NULL,0);
    return w->error;
}
static esp_err_t state_finish(httpd_req_t *r,cJSON *metadata,adsb_state *s,const rx_status *st){
    char *header=cJSON_PrintUnformatted(metadata);cJSON_Delete(metadata);
    if(!header){return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory");}
    size_t n=strlen(header);
    if(n<2||header[n-1]!='}'){cJSON_free(header);return ESP_FAIL;}
    httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");
    json_writer w={.r=r};w.error=httpd_resp_send_chunk(r,header,n-1);cJSON_free(header);
    write_json(&w,",\"aircraft\":[");write_aircraft(&w,s,st);
    return finish_aircraft(&w);
}
static esp_err_t live_get(httpd_req_t *r){
    adsb_state *s=&web_snapshot;
    rx_status st;receiver_snapshot(s,&st);httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");
    json_writer w={.r=r};
    write_json(&w,"{\"uptime\":%.3f,\"valid\":%lu,\"candidates\":%lu,\"error\":%d,\"overflows\":%lu,\"radioErrors\":%lu,\"heap\":%lu,\"lastValidAge\":",st.now/1000.0,(unsigned long)s->valid,(unsigned long)s->candidates,st.error,(unsigned long)st.radio.overflows,(unsigned long)st.radio.errors,(unsigned long)esp_get_free_heap_size());
    if(s->valid)write_json(&w,"%.3f",(st.now-s->last_valid)/1000.0);else write_json(&w,"null");
    write_json(&w,",\"correctionMode\":%u,\"corrected\":%lu,\"repairCandidates\":%lu,\"repairRejected\":%lu,\"repairEligible\":%lu,\"repairConfirmed\":%lu",s->correction_mode,(unsigned long)s->corrected,(unsigned long)s->repair_candidates,(unsigned long)s->repair_rejected,(unsigned long)s->repair_eligible,(unsigned long)s->repair_confirmed);
    write_json(&w,",\"aircraft\":[");write_aircraft(&w,s,&st);
    return finish_aircraft(&w);
}

static esp_err_t trails_get(httpd_req_t *r){
    char query[64],address[8],non[3];if(httpd_req_get_url_query_str(r,query,sizeof(query))!=ESP_OK||httpd_query_key_value(query,"address",address,sizeof(address))!=ESP_OK||strlen(address)!=6)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Six-digit address required");
    char *end;uint32_t icao=strtoul(address,&end,16);if(*end||!icao)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid address");
    bool nonicao=httpd_query_key_value(query,"nonIcao",non,sizeof(non))==ESP_OK&&!strcmp(non,"1");
    trail_record t;trail_cache_snapshot(icao,nonicao,&t);uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
    cJSON *j=cJSON_CreateObject(),*points=cJSON_AddArrayToObject(j,"points");cJSON_AddNumberToObject(j,"uptime",esp_timer_get_time()/1000000.0);
    for(int i=0;i<t.count;i++){trail_point *p=&t.points[(t.head+TRAIL_POINTS+1-t.count+i)%TRAIL_POINTS];uint32_t age=now-p->at;if(age>ADSB_LIST_MS)continue;
        cJSON *a=cJSON_CreateArray();cJSON_AddItemToArray(points,a);cJSON_AddItemToArray(a,cJSON_CreateNumber(p->lat/1e6));cJSON_AddItemToArray(a,cJSON_CreateNumber(p->lon/1e6));cJSON_AddItemToArray(a,p->alt==INT32_MIN?cJSON_CreateNull():cJSON_CreateNumber(p->alt));cJSON_AddItemToArray(a,cJSON_CreateNumber(age/1000.0));
    }return json_send(r,j);
}
static esp_err_t connection_open(httpd_handle_t server,int fd){
    (void)server;int yes=1;
    return setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes))==0?ESP_OK:ESP_FAIL;
}
int web_start(void){
    esp_err_t e=network_start();if(e)return e;
    if(tcp_stream_start())return ESP_ERR_NO_MEM;
    for(int i=0;i<4;i++)snprintf(token+8*i,9,"%08lx",(unsigned long)esp_random());
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.stack_size=8192;cfg.max_uri_handlers=16;cfg.max_open_sockets=8;cfg.lru_purge_enable=true;cfg.recv_wait_timeout=2;cfg.send_wait_timeout=3;
    cfg.open_fn=connection_open;cfg.keep_alive_enable=true;cfg.keep_alive_idle=5;cfg.keep_alive_interval=3;cfg.keep_alive_count=3;
    httpd_handle_t server;if((e=httpd_start(&server,&cfg)))return e;
    const char *paths[]={"/","/ui.js","/style.css","/world.json","/api/state","/api/radio","/api/wifi"};
    for(int i=0;i<7;i++){httpd_uri_t u={.uri=paths[i],.method=i>=5?HTTP_POST:HTTP_GET,.handler=i<4?asset:i==4?state_get:i==5?radio_post:wifi_post};if((e=httpd_register_uri_handler(server,&u)))return e;}

    const char *extra[]={"/setup","/setup.js","/api/network","/map"};
    for(int i=0;i<4;i++){httpd_uri_t u={.uri=extra[i],.method=HTTP_GET,.handler=i==2?network_get:asset};if((e=httpd_register_uri_handler(server,&u)))return e;}
    httpd_uri_t live_js={.uri="/live.js",.method=HTTP_GET,.handler=asset};if((e=httpd_register_uri_handler(server,&live_js)))return e;
    httpd_uri_t live={.uri="/api/live",.method=HTTP_GET,.handler=live_get},traces={.uri="/api/trails",.method=HTTP_GET,.handler=trails_get},tcp={.uri="/api/tcp",.method=HTTP_POST,.handler=tcp_post};
    if((e=httpd_register_uri_handler(server,&live))||(e=httpd_register_uri_handler(server,&traces))||(e=httpd_register_uri_handler(server,&tcp)))return e;
    httpd_uri_t correction={.uri="/api/correction",.method=HTTP_POST,.handler=correction_post};if((e=httpd_register_uri_handler(server,&correction)))return e;
    return 0;
}
