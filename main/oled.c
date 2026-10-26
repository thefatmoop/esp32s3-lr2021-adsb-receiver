// SPDX-License-Identifier: GPL-3.0-or-later
#include "oled.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static i2c_master_dev_handle_t display;
static uint8_t pixels[1024];
static uint8_t sent_pixels[1024];
static bool screen_synced;
static const char letters[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.-/ :";
static const uint8_t glyphs[][5]={
 {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},
 {39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30},
 {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},
 {127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},
 {127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},
 {127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},
 {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67},
 {0,96,96,0,0},{8,8,8,8,8},{32,16,8,4,2},{0,0,0,0,0},{0,54,54,0,0}
};
static void text(int x,int y,const char *s,int scale) {
    while(*s){char c=*s++;if(c>='a'&&c<='z')c-=32;const char *found=strchr(letters,c);unsigned k=found?(unsigned)(found-letters):39;
        for(int col=0;col<5;col++)for(int row=0;row<7;row++)if(glyphs[k][col]&(1<<row))
            for(int a=0;a<scale;a++)for(int b=0;b<scale;b++){
                int px=x+col*scale+a,py=y+row*scale+b;
                if(px>=0&&px<128&&py>=0&&py<64)pixels[(py/8)*128+px]|=1<<(py%8);
            }
        x+=6*scale;
    }
}
bool oled_init(void) {
    screen_synced=false;
    // W12 SSD1315: active-low peripheral rail and reset, 0x3c I2C.
    gpio_set_level(45,0);gpio_set_direction(45,GPIO_MODE_OUTPUT);
    gpio_set_level(47,0);gpio_set_direction(47,GPIO_MODE_OUTPUT);vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(47,1);vTaskDelay(pdMS_TO_TICKS(30));
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t cfg={.i2c_port=0,.sda_io_num=17,.scl_io_num=18,
        .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
    if(i2c_new_master_bus(&cfg,&bus)!=ESP_OK)return false;
    if(i2c_master_probe(bus,0x3c,100)!=ESP_OK)return false;
    i2c_device_config_t dev={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x3c,.scl_speed_hz=400000};
    if(i2c_master_bus_add_device(bus,&dev,&display)!=ESP_OK){display=NULL;return false;}
    const uint8_t init[]={0,0xae,0xd5,0x80,0xa8,0x3f,0xd3,0,0x40,0x8d,0x14,0x20,0,
        0xa1,0xc8,0xda,0x12,0x81,0x7f,0xd9,0xf1,0xdb,0x40,0xa4,0xa6,0xaf};
    if(i2c_master_transmit(display,init,sizeof(init),100)!=ESP_OK){display=NULL;return false;}
    return true;
}
static bool flush(void) {
    if(!display)return false;
    // Only changed columns of each 8-pixel page need an I2C transfer.
    // Invalidate on failure so a partially written frame is fully redrawn.
    for(unsigned page=0;page<8;page++){
        unsigned base=page*128,first=0,last=127;
        if(screen_synced){
            while(first<128&&pixels[base+first]==sent_pixels[base+first])first++;
            if(first==128)continue;
            while(last>first&&pixels[base+last]==sent_pixels[base+last])last--;
        }
        const uint8_t addr[]={0,0x21,first,last,0x22,page,page};
        if(i2c_master_transmit(display,addr,sizeof(addr),100)!=ESP_OK){screen_synced=false;return false;}
        uint8_t frame[129];frame[0]=0x40;unsigned size=last-first+1;
        memcpy(frame+1,pixels+base+first,size);
        if(i2c_master_transmit(display,frame,size+1,100)!=ESP_OK){screen_synced=false;return false;}
    }
    memcpy(sent_pixels,pixels,sizeof(pixels));screen_synced=true;return true;
}
bool oled_set_enabled(bool enabled) {
    if(!display)return false;
    const uint8_t cmd[]={0,enabled?0xaf:0xae};
    return i2c_master_transmit(display,cmd,sizeof(cmd),100)==ESP_OK;
}
void oled_show(uint32_t hz,const char *status) {
    if(!display)return;
    memset(pixels,0,sizeof(pixels));char freq[20];
    snprintf(freq,sizeof(freq),"%lu.%03lu",(unsigned long)(hz/1000000),(unsigned long)(hz%1000000/1000));
    text(0,0,"W12 ADSB",1);text(0,20,freq,2);text(103,27,"MHZ",1);
    text(0,43,hz>=1500000000u?"HF / 2.4G ANT":"LF / SUB-G ANT",1);text(0,56,status,1);
    flush();
}
bool oled_show_stats(const oled_stats *s) {
    if(!display)return false;
    memset(pixels,0,sizeof(pixels));char line[40];
    snprintf(line,sizeof(line),"ADSB 1090 MHZ %s",s->ready?(s->hf?"HF":"LF"):"--");text(0,0,line,1);
    // The two-color panel's top 16 rows are amber; all remaining rows blue.
    // Keep both header lines above that boundary and the large rate below it.
    text(0,8,"CRC VALID / 5S AVG",1);
    if(s->error){snprintf(line,sizeof(line),"RADIO ERROR %d",s->error);text(0,18,line,1);}
    else if(!s->ready)text(0,18,"STARTING",1);
    else {
        snprintf(line,sizeof(line),"%.1f",s->hits_per_second);
        text(0,18,line,strlen(line)<=7?2:1);text(96,24,"HIT/S",1);
    }
    if(s->ready&&isfinite(s->rssi_now))snprintf(line,sizeof(line),"RSSI %.1f DBM",s->rssi_now);
    else snprintf(line,sizeof(line),"RSSI -- DBM");
    text(0,37,line,1);
    if(s->ready&&isfinite(s->rssi_peak))snprintf(line,sizeof(line),"PEAK %.1f DBM",s->rssi_peak);
    else snprintf(line,sizeof(line),"PEAK -- DBM");
    text(0,46,line,1);
    snprintf(line,sizeof(line),"%u AIR %lu MSG",s->aircraft,(unsigned long)s->valid);text(0,56,line,1);
    return flush();
}

bool oled_show_wifi(const network_info *n){
 if(!display)return false;memset(pixels,0,sizeof(pixels));char line[40];
 text(0,0,"WI-FI / NETWORK",1);
 text(0,8,n->ap?"AP CONFIGURATION":n->connected?"CLIENT CONNECTED":"CLIENT CONNECTING",1);
 if(n->ap){
  text(0,19,SETUP_SSID,1);text(0,28,"PASS " SETUP_PASSWORD,1);text(0,37,SETUP_IP,1);
  snprintf(line,sizeof(line),"REBOOT IN %02u:%02u",n->reboot_seconds/60,n->reboot_seconds%60);text(0,46,line,1);
  text(0,56,n->testing?"TESTING NEW WI-FI":"RETRY SAVED WI-FI",1);
 }else{
  text(0,19,n->connected?n->ip:"WAITING FOR ROUTER",1);
  // SSIDs are case-sensitive; identify them in the web UI, which has full UTF-8.
  if(n->connected)snprintf(line,sizeof(line),"WI-FI RSSI %d DBM",n->rssi);else snprintf(line,sizeof(line),"WI-FI RSSI --");text(0,30,line,1);
  text(0,43,n->testing?"TESTING CONNECTION":n->connected?"OPEN IP IN BROWSER":"SETUP AP AFTER 45S",1);
  text(0,56,n->error?"NETWORK ERROR":"1090 RX CONTINUES",1);
 }
 return flush();
}
bool oled_show_gnss(const gnss_status *g,uint64_t now){
 if(!display)return false;memset(pixels,0,sizeof(pixels));char line[40];
 text(0,0,"GNSS / POSITION",1);text(0,8,g->error?"MODULE ERROR":g->fix?"LIVE FIX":g->has_position?"LAST KNOWN POSITION":"ACQUIRING FIX",1);
 snprintf(line,sizeof(line),"SAT %u  HDOP %.1f",g->satellites,g->hdop);text(0,19,line,1);
 if(g->has_position){snprintf(line,sizeof(line),"LAT %.5f",g->latitude);text(0,29,line,1);snprintf(line,sizeof(line),"LON %.5f",g->longitude);text(0,39,line,1);}
 else {text(0,29,"NO POSITION YET",1);text(0,39,"WAITING FOR GNSS",1);}
 if(g->saved_position)snprintf(line,sizeof(line),"SAVED FIX AGE UNKNOWN");
 else if(g->has_position)snprintf(line,sizeof(line),"FIX AGE %lu S",(unsigned long)((now-g->last_fix)/1000));
 else snprintf(line,sizeof(line),"FIX NOT AVAILABLE");text(0,49,line,1);
 text(0,57,g->sentences&&now-g->last_sentence<10000?"GNSS DATA ACTIVE":"GNSS DATA STALE/NONE",1);return flush();
}
