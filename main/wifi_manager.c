// SPDX-License-Identifier: GPL-3.0-or-later
#include "wifi_manager.h"
#include "wifi_policy.h"
#include <string.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
typedef struct {char ssid[33],password[64];} credentials;
typedef struct {credentials value;bool temporary;} request;
static SemaphoreHandle_t net_lock;
static QueueHandle_t queue;
static network_info net;
static uint64_t ap_deadline;
static uint64_t ms(void){return esp_timer_get_time()/1000;}
void web_network(network_info *out){
 if(!net_lock){memset(out,0,sizeof(*out));return;}
 xSemaphoreTake(net_lock,portMAX_DELAY);*out=net;
 out->reboot_seconds=net.ap?wifi_seconds_left(ms(),ap_deadline):0;xSemaphoreGive(net_lock);
}
static void message(const char *text,int error){xSemaphoreTake(net_lock,portMAX_DELAY);snprintf(net.message,sizeof(net.message),"%s",text);net.error=error;xSemaphoreGive(net_lock);}
static void event(void *arg,esp_event_base_t base,int32_t id,void *data){
 (void)arg;xSemaphoreTake(net_lock,portMAX_DELAY);
 if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){wifi_event_sta_disconnected_t *e=data;net.disconnects++;net.last_reason=e->reason;}
 if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP){ip_event_got_ip_t *e=data;net.connected=true;snprintf(net.ip,sizeof(net.ip),IPSTR,IP2STR(&e->ip_info.ip));}
 if((base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED)||(base==IP_EVENT&&id==IP_EVENT_STA_LOST_IP)){net.connected=false;net.ip[0]=0;net.rssi=0;}
 xSemaphoreGive(net_lock);
}
bool network_submit(const char *ssid,const char *pass,bool temporary){
 if(!ssid||!pass||!queue)return false;size_t sn=strlen(ssid),pn=strlen(pass);
 if(!sn||sn>32||pn>63||(pn&&pn<8))return false;
 request r={.temporary=temporary};memcpy(r.value.ssid,ssid,sn);memcpy(r.value.password,pass,pn);
 bool ok=xQueueSend(queue,&r,0)==pdTRUE;memset(&r,0,sizeof(r));return ok;
}
bool web_wifi(const char *ssid,const char *pass){return network_submit(ssid,pass,false);}
static int setup_ap(bool on){
 int e=esp_wifi_set_mode(on?WIFI_MODE_APSTA:WIFI_MODE_STA);
 if(!e&&on){wifi_config_t c={0};strcpy((char *)c.ap.ssid,SETUP_SSID);strcpy((char *)c.ap.password,SETUP_PASSWORD);
  c.ap.ssid_len=strlen(SETUP_SSID);c.ap.max_connection=3;c.ap.authmode=WIFI_AUTH_WPA2_PSK;e=esp_wifi_set_config(WIFI_IF_AP,&c);}
 if(!e){xSemaphoreTake(net_lock,portMAX_DELAY);net.ap=on;ap_deadline=on?ms()+WIFI_SETUP_TIMEOUT_MS:0;xSemaphoreGive(net_lock);}
 else message("Wi-Fi mode change failed; retrying",e);return e;
}
static int connect_to(const credentials *c){
 esp_wifi_disconnect();vTaskDelay(pdMS_TO_TICKS(100));
 xSemaphoreTake(net_lock,portMAX_DELAY);net.connected=false;net.ip[0]=0;net.rssi=0;strcpy(net.ssid,c->ssid);xSemaphoreGive(net_lock);
 if(!c->ssid[0])return 0;
 wifi_config_t cfg={0};memcpy(cfg.sta.ssid,c->ssid,strlen(c->ssid));memcpy(cfg.sta.password,c->password,strlen(c->password));cfg.sta.pmf_cfg.capable=true;
 int e=esp_wifi_set_config(WIFI_IF_STA,&cfg);memset(&cfg,0,sizeof(cfg));if(!e)e=esp_wifi_connect();return e;
}
static int save(const credentials *c){
 nvs_handle_t h;int e=nvs_open("adsb_wifi",NVS_READWRITE,&h);if(e)return e;
 // A single committed blob prevents an SSID/password mismatch after power loss.
 e=nvs_set_blob(h,"verified",c,sizeof(*c));if(!e)e=nvs_commit(h);nvs_close(h);return e;
}
static void network_task(void *arg){
 (void)arg;credentials saved={0},active={0};nvs_handle_t h;
 if(nvs_open("adsb_wifi",NVS_READONLY,&h)==ESP_OK){size_t n=sizeof(saved);
  if(nvs_get_blob(h,"verified",&saved,&n)!=ESP_OK||n!=sizeof(saved)){
   memset(&saved,0,sizeof(saved));size_t sn=sizeof(saved.ssid),pn=sizeof(saved.password);
   if(nvs_get_str(h,"ssid",saved.ssid,&sn)!=ESP_OK)saved.ssid[0]=0;
   if(nvs_get_str(h,"password",saved.password,&pn)!=ESP_OK)saved.password[0]=0;
  }nvs_close(h);saved.ssid[32]=0;saved.password[63]=0;}
 active=saved;int e=connect_to(&active);message(active.ssid[0]?"Connecting to saved Wi-Fi":"Connect to setup AP",e);
 uint64_t offline=ms(),retry=ms()+10000,test_started=0,stable=0;bool testing=false,temporary=false,was_connected=false;
 if(!active.ssid[0])setup_ap(true);
 for(;;){
  request r={0};if(xQueueReceive(queue,&r,pdMS_TO_TICKS(250))==pdTRUE){
   active=r.value;temporary=r.temporary;memset(&r,0,sizeof(r));testing=true;test_started=ms();stable=0;was_connected=false;
   xSemaphoreTake(net_lock,portMAX_DELAY);net.testing=true;if(net.ap)ap_deadline=ms()+WIFI_SETUP_TIMEOUT_MS;xSemaphoreGive(net_lock);
   // Let the provisioning HTTP response reach the client before switching APs.
   // Otherwise an accepted update looks like a timeout to the submitting browser.
   vTaskDelay(pdMS_TO_TICKS(750));
   e=connect_to(&active);offline=ms();retry=ms()+10000;message("Testing Wi-Fi; previous network retained",e);
  }
  network_info info;web_network(&info);uint64_t now=ms();
  if(info.connected){
   if(!stable)stable=now;
   if(testing&&now-stable>=3000){
    e=temporary?0:save(&active);if(!e&&!temporary)saved=active;
    message(e?"Connected, but saving failed; retry setup":temporary?"Connected for test; saved Wi-Fi unchanged":"Connected; Wi-Fi saved",e);
    testing=false;xSemaphoreTake(net_lock,portMAX_DELAY);net.testing=false;xSemaphoreGive(net_lock);
   }
   if(!testing&&info.ap)setup_ap(false);
   if(!testing&&!was_connected&&!info.error)message("Wi-Fi connected",0);
   wifi_ap_record_t ap;if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK){xSemaphoreTake(net_lock,portMAX_DELAY);net.rssi=ap.rssi;xSemaphoreGive(net_lock);}
   offline=now;was_connected=true;
  }else{
   stable=0;if(was_connected){message("Wi-Fi lost; reconnecting",0);was_connected=false;}
   if(testing&&now-test_started>=WIFI_CONNECT_TIMEOUT_MS){
    active=saved;testing=false;xSemaphoreTake(net_lock,portMAX_DELAY);net.testing=false;xSemaphoreGive(net_lock);
    e=connect_to(&active);retry=ms()+10000;message("Connection failed; previous Wi-Fi restored",e);
    if(!info.ap)setup_ap(true);
   }else if(!info.ap&&now-offline>=WIFI_CONNECT_TIMEOUT_MS)setup_ap(true);
   if(active.ssid[0]&&now>=retry){e=esp_wifi_connect();retry=now+10000;if(e!=ESP_OK&&e!=ESP_ERR_WIFI_CONN)message("Retrying Wi-Fi",e);}
   web_network(&info);uint64_t deadline;xSemaphoreTake(net_lock,portMAX_DELAY);deadline=ap_deadline;xSemaphoreGive(net_lock);
   if(wifi_setup_expired(info.ap,info.connected,now,deadline))esp_restart();
  }
 }
}
int network_start(void){
 net_lock=xSemaphoreCreateMutex();queue=xQueueCreate(1,sizeof(request));if(!net_lock||!queue)return ESP_ERR_NO_MEM;
 int e=nvs_flash_init();if(e)return e;
 if((e=esp_netif_init()))return e;if((e=esp_event_loop_create_default()))return e;
 if(!esp_netif_create_default_wifi_sta()||!esp_netif_create_default_wifi_ap())return ESP_ERR_NO_MEM;
 wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
 // Bound transient Wi-Fi copies during map downloads and multiple clients.
 // RX stays above the default ten static buffers; this is not a bulk router.
 cfg.dynamic_rx_buf_num=16;cfg.dynamic_tx_buf_num=16;
 if((e=esp_wifi_init(&cfg)))return e;
 if((e=esp_wifi_set_storage(WIFI_STORAGE_RAM)))return e;
 if((e=esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,NULL)))return e;
 if((e=esp_event_handler_register(IP_EVENT,ESP_EVENT_ANY_ID,event,NULL)))return e;
 if((e=esp_wifi_set_mode(WIFI_MODE_STA)))return e;if((e=esp_wifi_start()))return e;
 if((e=esp_wifi_set_ps(WIFI_PS_NONE)))return e;
 return xTaskCreate(network_task,"network",4096,NULL,3,NULL)==pdPASS?0:ESP_ERR_NO_MEM;
}
