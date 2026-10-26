// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdatomic.h>
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "radio.h"
#include "oled.h"
#include "gnss.h"
#include "adsb.h"
#include "receiver.h"
#include "web.h"
#include "cJSON.h"
#include "nvs.h"
#include "tuning.h"
#include "tcp_stream.h"
#include "trail_cache.h"
static adsb_state state={.best_bad_pairs=113,.correction_mode=1};
static rx_status current;
static SemaphoreHandle_t lock;
static QueueHandle_t config_queue,frame_queue;
static atomic_uint raw_remaining;
static oled_stats screen;
static bool oled_ok;
static unsigned oled_interval=1000;
static uint64_t oled_next;
static void send(const char *p){usb_serial_jtag_write_bytes(p,strlen(p),0);}
void receiver_snapshot(adsb_state *out,rx_status *st){
    xSemaphoreTake(lock,portMAX_DELAY);if(out)*out=state;*st=current;xSemaphoreGive(lock);
    st->now=esp_timer_get_time()/1000;
}
bool receiver_configure(const rx_config *c){
    if(c->gain>13||c->boost>7||c->bandwidth>3||(c->capture_bytes&&(c->capture_bytes<28||c->capture_bytes>32))||(c->threshold!=100&&c->threshold!=101&&(c->threshold< -64||c->threshold>63)))return false;
    return xQueueSend(config_queue,c,0)==pdTRUE;
}
bool receiver_correction_mode(unsigned mode){
    if(mode>2)return false;nvs_handle_t nv;if(nvs_open("adsb_rx",NVS_READWRITE,&nv)!=ESP_OK)return false;
    esp_err_t e=nvs_set_i32(nv,"correction",mode);if(!e)e=nvs_commit(nv);nvs_close(nv);if(e)return false;
    xSemaphoreTake(lock,portMAX_DELAY);state.correction_mode=mode;xSemaphoreGive(lock);return true;
}
static void capture(void *arg){
    static const rx_config profiles[]={
        {13,7,20,false,0,true,true},{13,7,24,false,0,true,true},
        {13,7,16,false,3,true,true},{13,0,100,false,0,true,true},
        {0,7,100,true,0,true,true},{13,7,100,true,0,true,true},
        {10,7,100,false,0,true,true},{10,0,20,false,3,true,true}
    };
    (void)arg;rx_config c={.gain=13,.boost=7,.threshold=40,.capture_bytes=32,.irq_wake=true,.hardware_manchester=true};nvs_handle_t nv;int32_t g,b,t,a,bw,hunt,hf,bytes,wake,hw;
    rx_tuner tuner={.index=7};
    if(nvs_open("adsb_rx",NVS_READONLY,&nv)==ESP_OK){
        if(nvs_get_i32(nv,"gain",&g)==ESP_OK&&nvs_get_i32(nv,"boost",&b)==ESP_OK&&nvs_get_i32(nv,"threshold",&t)==ESP_OK&&nvs_get_i32(nv,"assisted",&a)==ESP_OK&&g>=0&&g<=13&&b>=0&&b<=7&&(t==100||t==101||(t>=-64&&t<=63))&&(a==0||a==1)){c.gain=g;c.boost=b;c.threshold=t;c.assisted=a;}
        if(nvs_get_i32(nv,"bandwidth",&bw)==ESP_OK&&bw>=0&&bw<=3)c.bandwidth=bw;
        if(nvs_get_i32(nv,"hunt",&hunt)==ESP_OK)c.hunt=hunt==1;
        if(nvs_get_i32(nv,"hf",&hf)==ESP_OK)c.hf=hf==1;
        if(nvs_get_i32(nv,"capture_bytes",&bytes)==ESP_OK&&bytes>=28&&bytes<=32)c.capture_bytes=bytes;
        if(nvs_get_i32(nv,"irq_wake",&wake)==ESP_OK)c.irq_wake=wake==1;
        if(nvs_get_i32(nv,"hw_manchester",&hw)==ESP_OK)c.hardware_manchester=hw==1;
        nvs_close(nv);
    }
    int32_t correction=1;if(nvs_open("adsb_rx",NVS_READONLY,&nv)==ESP_OK){nvs_get_i32(nv,"correction",&correction);nvs_close(nv);}
    xSemaphoreTake(lock,portMAX_DELAY);state.correction_mode=correction>=0&&correction<=2?correction:1;xSemaphoreGive(lock);
    adsb_correction_init();int e=radio_init();bool test=adsb_selftest()&&rx_tuner_selftest();
    if(!test)e=-99;if(!e)e=radio_adsb_start(c.gain,c.boost,c.threshold,c.assisted,c.bandwidth,c.hf,c.capture_bytes,c.hardware_manchester);
    xSemaphoreTake(lock,portMAX_DELAY);current.config=c;current.error=e;current.selftest=test;current.generation=1;xSemaphoreGive(lock);
    unsigned fails=0;uint64_t publish=0,retry=0;
    for(;;){
        uint64_t now=esp_timer_get_time()/1000;
        if(xQueueReceive(config_queue,&c,0)==pdTRUE){
            if(!c.capture_bytes)c.capture_bytes=32;
            radio_adsb_stop();
            // Save before entering RX so a flash write cannot silently stall a live FIFO.
            if(!c.temporary&&nvs_open("adsb_rx",NVS_READWRITE,&nv)==ESP_OK){nvs_set_i32(nv,"gain",c.gain);nvs_set_i32(nv,"boost",c.boost);nvs_set_i32(nv,"threshold",c.threshold);nvs_set_i32(nv,"assisted",c.assisted);nvs_set_i32(nv,"bandwidth",c.bandwidth);nvs_set_i32(nv,"hunt",c.hunt);nvs_set_i32(nv,"hf",c.hf);nvs_set_i32(nv,"capture_bytes",c.capture_bytes);nvs_set_i32(nv,"irq_wake",c.irq_wake);nvs_set_i32(nv,"hw_manchester",c.hardware_manchester);nvs_commit(nv);nvs_close(nv);}
            tuner=(rx_tuner){.index=7,.valid=state.valid};
            e=radio_adsb_start(c.gain,c.boost,c.threshold,c.assisted,c.bandwidth,c.hf,c.capture_bytes,c.hardware_manchester);
            xSemaphoreTake(lock,portMAX_DELAY);current.config=c;state.best_bad_pairs=113;current.generation++;current.error=e;xSemaphoreGive(lock);
        }
        if(c.hunt&&test&&rx_tuner_tick(&tuner,now,state.valid,sizeof(profiles)/sizeof(profiles[0]))){
            bool hf_path=c.hf,irq_wake=c.irq_wake,hardware_manchester=c.hardware_manchester;unsigned capture_bytes=c.capture_bytes;
            c=profiles[tuner.index];c.hf=hf_path;c.irq_wake=irq_wake;c.capture_bytes=capture_bytes;c.hardware_manchester=hardware_manchester;radio_adsb_stop();
            e=radio_adsb_start(c.gain,c.boost,c.threshold,c.assisted,c.bandwidth,c.hf,c.capture_bytes,c.hardware_manchester);
            xSemaphoreTake(lock,portMAX_DELAY);current.config=c;current.generation++;state.best_bad_pairs=113;current.error=e;xSemaphoreGive(lock);
        }
        if(!e){
            for(int i=0;i<12;i++){
                uint8_t chips[ADSB_CAPTURE_BYTES];float rssi=NAN;int n=radio_adsb_poll(chips,&rssi);
                if(n<0){if(++fails>=3){e=n;retry=now+1000;fails=0;}break;}
                fails=0;if(!n)break;
                if(atomic_load(&raw_remaining)){
                    char raw[96];int p=snprintf(raw,sizeof(raw),"RAW %llu ",(unsigned long long)now);
                    for(int k=0;k<n;k++)p+=snprintf(raw+p,sizeof(raw)-p,"%02X",chips[k]);
                    snprintf(raw+p,sizeof(raw)-p," %.1f\n",rssi);
                    if(xQueueSend(frame_queue,raw,0)==pdTRUE)atomic_fetch_sub(&raw_remaining,1);
                }
                xSemaphoreTake(lock,portMAX_DELAY);
                bool valid;
                if(n==14){
                    valid=adsb_ingest_manchester(&state,chips,c.assisted,rssi,now);
                }else valid=adsb_ingest_capture_n(&state,chips,n,c.assisted,rssi,now);
                if(valid){char b[96];snprintf(b,sizeof(b),"ADSB %llu %s %.1f\n",(unsigned long long)now,state.last_frame,rssi);xQueueSend(frame_queue,b,0);
                    tcp_stream_submit(state.last_frame,esp_timer_get_time());
                    for(int k=0;k<ADSB_MAX_AIRCRAFT;k++)if(state.aircraft[k].position_valid&&state.aircraft[k].position_at==now)trail_cache_record(&state.aircraft[k]);
                }
                xSemaphoreGive(lock);
            }
        }else if(test&&now>=retry){e=radio_adsb_start(c.gain,c.boost,c.threshold,c.assisted,c.bandwidth,c.hf,c.capture_bytes,c.hardware_manchester);retry=now+5000;}
        if(now>=publish){
            radio_adsb_stats rs;radio_adsb_get_stats(&rs);
            xSemaphoreTake(lock,portMAX_DELAY);current.radio=rs;current.error=e;current.hold_until=tuner.hold_until;current.tune_profile=tuner.index;xSemaphoreGive(lock);publish=now+200;
        }
        radio_adsb_wait(c.irq_wake&&!e);
    }
}
// UI work runs on core 0, outside the capture lock. Six samples span five
// actual one-second intervals; the shorter startup window uses its real time.
static void update_oled(void){
    static struct {uint64_t at;uint32_t valid;} samples[6];
    static unsigned head,count;
    static uint32_t generation;
    uint64_t now=esp_timer_get_time()/1000;
    xSemaphoreTake(lock,portMAX_DELAY);
    screen=(oled_stats){.hf=current.config.hf,.ready=current.generation&&current.selftest,
        .error=current.error,.valid=state.valid,.rssi_now=current.radio.rssi_now,.rssi_peak=current.radio.rssi_peak};
    uint32_t gen=current.generation;
    for(unsigned i=0;i<ADSB_MAX_AIRCRAFT;i++)
        if(state.aircraft[i].icao&&now>=state.aircraft[i].seen&&now-state.aircraft[i].seen<60000)screen.aircraft++;
    xSemaphoreGive(lock);
    if(gen!=generation){head=count=0;generation=gen;}
    samples[head].at=now;samples[head].valid=screen.valid;head=(head+1)%6;
    if(count<6)count++;
    unsigned oldest=(head+6-count)%6;
    uint64_t elapsed=now-samples[oldest].at;
    if(elapsed)screen.hits_per_second=(uint32_t)(screen.valid-samples[oldest].valid)*1000.0f/elapsed;
    if(oled_interval&&now>=oled_next){
        unsigned page=(now/8000)%3;
        if(page==0){network_info n;web_network(&n);oled_ok=oled_show_wifi(&n);}
        else if(page==1)oled_ok=oled_show_stats(&screen);
        else {gnss_status g;gnss_snapshot(&g);oled_ok=oled_show_gnss(&g,now);}
        oled_next=now+oled_interval;
    }
}
static void status(void){
    static adsb_state s;rx_status st;receiver_snapshot(&s,&st);network_info net;web_network(&net);char b[620];
    snprintf(b,sizeof(b),"STATUS init=%d selftest=%d gain=%u boost=%u threshold=%d assisted=%d hf=%d candidates=%lu pairs_bad=%lu crc_bad=%lu valid=%lu polls=%lu overflow=%lu errors=%lu recoveries=%lu stat=%04X chip=%04X ip=%s ap=%d\n",
        st.error,st.selftest,st.config.gain,st.config.boost,st.config.threshold,st.config.assisted,st.config.hf,
        (unsigned long)s.candidates,(unsigned long)s.bad_pairs,(unsigned long)s.bad_crc,(unsigned long)s.valid,
        (unsigned long)st.radio.polls,(unsigned long)st.radio.overflows,(unsigned long)st.radio.errors,(unsigned long)st.radio.recoveries,
        st.radio.status,st.radio.chip_errors,net.ip,net.ap);send(b);
    snprintf(b,sizeof(b),"OLED ok=%d hf=%d hits_s=%.2f rssi=%.1f peak=%.1f aircraft=%u valid=%lu interval_ms=%u\n",
        oled_ok,screen.hf,screen.hits_per_second,screen.rssi_now,screen.rssi_peak,screen.aircraft,(unsigned long)screen.valid,oled_interval);send(b);
}
static void command(char *cmd){
    if(!strcmp(cmd,"INFO")||!strcmp(cmd,"STATUS")){send("LR2021ADSB 2 W12 frequency=1090000000\n");status();}
    else if(!strcmp(cmd,"RAW")){atomic_store(&raw_remaining,32);send("RAW ARMED 32\n");}
    else if(!strncmp(cmd,"OLED ",5)){
        unsigned ms;char extra;
        if(sscanf(cmd+5,"%u %c",&ms,&extra)==1&&(ms==0||ms==1000||ms==5000)){
            oled_interval=ms;oled_next=0;oled_ok=oled_set_enabled(ms!=0);
            send(oled_ok?"OLED APPLIED\n":"ERR OLED\n");
        }else send("ERR OLED\n");
    }
    else if(!strncmp(cmd,"RX ",3)){
        unsigned g,b;int t,a;char extra;
        if(sscanf(cmd+3,"%u %u %d %d %c",&g,&b,&t,&a,&extra)==4&&(a==0||a==1)){
            rx_config c={g,b,t,a};send(receiver_configure(&c)?"RX QUEUED\n":"ERR RX\n");
        }else send("ERR RX\n");
    }else if(!strncmp(cmd,"WIFI ",5)){
        cJSON *j=cJSON_Parse(cmd+5),*s=cJSON_GetObjectItemCaseSensitive(j,"ssid"),*p=cJSON_GetObjectItemCaseSensitive(j,"password");
        bool ok=cJSON_IsString(s)&&cJSON_IsString(p)&&web_wifi(s->valuestring,p->valuestring);
        cJSON_Delete(j);send(ok?"WIFI QUEUED\n":"ERR WIFI\n");
    }else if(*cmd)send("ERR COMMAND\n");
}
void app_main(void){
    usb_serial_jtag_driver_config_t usb={.tx_buffer_size=8192,.rx_buffer_size=1024};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    lock=xSemaphoreCreateMutex();config_queue=xQueueCreate(2,sizeof(rx_config));frame_queue=xQueueCreate(32,96);
    if(!lock||!config_queue||!frame_queue)abort();
    oled_ok=oled_init();update_oled();ESP_ERROR_CHECK(web_start());
    gnss_start();
    if(xTaskCreatePinnedToCore(capture,"adsb_rx",8192,NULL,10,NULL,1)!=pdPASS)abort();
    char cmd[512];unsigned pos=0;bool discard=false;uint64_t next=0,next_oled=0;
    for(;;){
        uint8_t ch;
        while(usb_serial_jtag_read_bytes(&ch,1,0)>0){
            if(ch=='\n'){
                if(!discard){cmd[pos]=0;command(cmd);}else send("ERR TOO LONG\n");
                memset(cmd,0,sizeof(cmd));pos=0;discard=false;
            }else if(ch!='\r'){
                if(pos<sizeof(cmd)-1&&!discard)cmd[pos++]=ch;else discard=true;
            }
        }
        char frame[96];for(int i=0;i<16&&xQueueReceive(frame_queue,frame,0)==pdTRUE;i++)send(frame);
        uint64_t now=esp_timer_get_time()/1000;
        if(now>=next_oled){update_oled();next_oled=now+1000;}
        if(now>=next){status();next=now+5000;}vTaskDelay(pdMS_TO_TICKS(10));
    }
}

