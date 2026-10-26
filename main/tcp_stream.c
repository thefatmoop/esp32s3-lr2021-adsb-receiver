#include "tcp_stream.h"
#include "beast.h"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include "lwip/sockets.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
typedef struct {uint64_t us;uint8_t msg[14];} frame;
typedef struct {int fd;size_t used;uint64_t progress;uint8_t data[2048];} client;
static QueueHandle_t frames,configs;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static tcp_status status={.config={30005,1,0}};
static atomic_bool enabled;
static void close_client(client *c){if(c->fd>=0)close(c->fd);c->fd=-1;c->used=0;}
static void task(void *arg){
    (void)arg;client clients[2]={{.fd=-1},{.fd=-1}};int listener=-1;uint64_t retry=0;
    tcp_config cfg=status.config;
    for(;;){
        uint64_t now=esp_timer_get_time()/1000;tcp_config next;
        if(xQueueReceive(configs,&next,0)==pdTRUE){
            cfg=next;if(listener>=0)close(listener);listener=-1;
            for(int i=0;i<2;i++)close_client(&clients[i]);xQueueReset(frames);retry=0;
            portENTER_CRITICAL(&mux);status.config=cfg;status.error=0;portEXIT_CRITICAL(&mux);
            atomic_store(&enabled,cfg.enabled);
        }
        if(cfg.enabled&&listener<0&&now>=retry){
            listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
            struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(cfg.port),.sin_addr.s_addr=htonl(INADDR_ANY)};
            int reuse=1;if(listener>=0)setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
            if(listener<0||bind(listener,(struct sockaddr*)&address,sizeof(address))<0||listen(listener,2)<0||fcntl(listener,F_SETFL,O_NONBLOCK)<0){
                int error=errno;if(listener>=0)close(listener);listener=-1;retry=now+2000;
                portENTER_CRITICAL(&mux);status.error=error;portEXIT_CRITICAL(&mux);
            }else{portENTER_CRITICAL(&mux);status.error=0;portEXIT_CRITICAL(&mux);}
        }
        if(listener>=0){
            int fd=accept(listener,NULL,NULL);
            if(fd>=0){int slot=-1;for(int i=0;i<2;i++)if(clients[i].fd<0){slot=i;break;}
                if(slot<0||fcntl(fd,F_SETFL,O_NONBLOCK)<0)close(fd);
                else{int yes=1;setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));clients[slot]=(client){.fd=fd,.progress=now};}
            }
        }
        frame f;for(int batch=0;batch<32&&xQueueReceive(frames,&f,0)==pdTRUE;batch++){
            uint8_t encoded[44];size_t len;
            if(cfg.avr){static const char hex[]="0123456789ABCDEF";encoded[0]='*';for(int i=0;i<14;i++){encoded[1+2*i]=hex[f.msg[i]>>4];encoded[2+2*i]=hex[f.msg[i]&15];}encoded[29]=';';encoded[30]='\n';len=31;}
            else len=beast_encode(encoded,f.msg,f.us);
            for(int i=0;i<2;i++){client *c=&clients[i];if(c->fd<0)continue;
                if(c->used+len>sizeof(c->data)){close_client(c);portENTER_CRITICAL(&mux);status.slow_clients++;portEXIT_CRITICAL(&mux);continue;}
                if(!c->used)c->progress=now;memcpy(c->data+c->used,encoded,len);c->used+=len;
            }
        }
        unsigned connected=0;
        for(int i=0;i<2;i++){client *c=&clients[i];if(c->fd<0)continue;
            char discard[64];int received=recv(c->fd,discard,sizeof(discard),MSG_DONTWAIT);
            if(received==0||(received<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)){close_client(c);continue;}
            if(c->used){int n=send(c->fd,c->data,c->used,MSG_DONTWAIT);
                if(n>0){c->used-=n;memmove(c->data,c->data+n,c->used);c->progress=now;portENTER_CRITICAL(&mux);status.sent+=n;portEXIT_CRITICAL(&mux);}
                else if((n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)||now-c->progress>5000){close_client(c);portENTER_CRITICAL(&mux);status.slow_clients++;portEXIT_CRITICAL(&mux);continue;}
            }connected++;
        }
        portENTER_CRITICAL(&mux);status.clients=connected;portEXIT_CRITICAL(&mux);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
int tcp_stream_start(void){
    nvs_handle_t n;tcp_config c;size_t len=sizeof(c);
    if(nvs_open("adsb_tcp",NVS_READONLY,&n)==ESP_OK){if(nvs_get_blob(n,"config",&c,&len)==ESP_OK&&len==sizeof(c)&&c.port>=1024&&c.enabled<=1&&c.avr<=1)status.config=c;nvs_close(n);}
    frames=xQueueCreate(64,sizeof(frame));configs=xQueueCreate(1,sizeof(tcp_config));
    if(!frames||!configs)return -1;atomic_store(&enabled,status.config.enabled);
    // Two client buffers live on this task's stack, isolated from RF capture.
    return xTaskCreatePinnedToCore(task,"adsb_tcp",8192,NULL,2,NULL,0)==pdPASS?0:-1;
}
bool tcp_stream_configure(tcp_config c){
    if(c.port<1024||c.enabled>1||c.avr>1)return false;
    nvs_handle_t n;if(nvs_open("adsb_tcp",NVS_READWRITE,&n)!=ESP_OK)return false;
    esp_err_t e=nvs_set_blob(n,"config",&c,sizeof(c));if(!e)e=nvs_commit(n);nvs_close(n);
    return e==ESP_OK&&xQueueOverwrite(configs,&c)==pdTRUE;
}
void tcp_stream_snapshot(tcp_status *out){portENTER_CRITICAL(&mux);*out=status;portEXIT_CRITICAL(&mux);}
void tcp_stream_submit(const char hex[29],uint64_t us){
    if(!frames||!atomic_load(&enabled))return;frame f={.us=us};
    for(int i=0;i<14;i++){unsigned a=hex[2*i],b=hex[2*i+1];f.msg[i]=((a<='9'?a-'0':a-'A'+10)<<4)|(b<='9'?b-'0':b-'A'+10);}
    bool ok=xQueueSend(frames,&f,0)==pdTRUE;
    portENTER_CRITICAL(&mux);if(ok)status.queued++;else status.dropped++;portEXIT_CRITICAL(&mux);
}
