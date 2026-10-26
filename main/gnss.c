// W12 L76K: vendor Demo_13_GNSS_L76K, RX39 at 9600, CTRL48 active low.
#include "gnss.h"
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"
static SemaphoreHandle_t lock;
static gnss_status state={.hdop=NAN};
typedef struct {double lat,lon,hdop;unsigned quality,sats;char utc[16];} fix_data;
static bool num(const char *s,double *v){
    if(!*s)return false;char *end;*v=strtod(s,&end);return !*end&&isfinite(*v);
}
static bool coordinate(const char *s,const char *dir,bool lat,double *out){
    double n;if(!num(s,&n)||n<0||n>(lat?9060:18060)||strlen(dir)!=1)return false;
    unsigned deg=(unsigned)(n/100);double minutes=n-deg*100;
    if(minutes>=60||deg>(lat?90:180))return false;
    double v=deg+minutes/60;if(v>(lat?90:180))return false;
    if(*dir==(lat?'S':'W'))v=-v;else if(*dir!=(lat?'N':'E'))return false;
    *out=v;return true;
}
// Returns -1 for bad framing/checksum, 0 for other sentences, 1 for GGA.
static int parse(char *line,fix_data *f){
    size_t length=strlen(line);if(length<9||line[0]!='$')return -1;
    char *star=strchr(line,'*');if(!star||strlen(star)!=3||!isxdigit((unsigned char)star[1])||!isxdigit((unsigned char)star[2]))return -1;
    unsigned checksum=0;for(char *s=line+1;s<star;s++)checksum^=(unsigned char)*s;
    char *end;unsigned expected=strtoul(star+1,&end,16);
    if(*end||end!=star+3||expected!=checksum)return -1;
    *star=0;char *fields[20];unsigned count=0;fields[count++]=line+1;
    for(char *s=line+1;*s;s++)if(*s==','){*s=0;if(count<20)fields[count++]=s+1;}
    if(strlen(fields[0])!=5||strcmp(fields[0]+2,"GGA"))return 0;
    memset(f,0,sizeof(*f));f->hdop=NAN;
    if(count<10)return 1;
    double q,sats,h;
    if(num(fields[6],&q)&&q>=0&&q<=8&&floor(q)==q)f->quality=q;
    if(num(fields[7],&sats)&&sats>=0&&sats<=99&&floor(sats)==sats)f->sats=sats;
    if(num(fields[8],&h)&&h>=0)f->hdop=h;
    if(strlen(fields[1])<sizeof(f->utc))strcpy(f->utc,fields[1]);
    if(!coordinate(fields[2],fields[3],true,&f->lat)||!coordinate(fields[4],fields[5],false,&f->lon))f->quality=0;
    return 1;
}
static double meters(double la,double lo,double lb,double lob){
    double rad=0.017453292519943295,dlat=(lb-la)*rad,dlon=remainder(lob-lo,360)*rad;
    double h=sin(dlat/2)*sin(dlat/2)+cos(la*rad)*cos(lb*rad)*sin(dlon/2)*sin(dlon/2);
    return 12742000*asin(sqrt(fmin(1,h)));
}
static bool valid_utc(const char *s){
    size_t n=strlen(s);if(n<6)return false;
    for(unsigned i=0;i<n;i++)if(i==6){if(s[i]!='.')return false;}else if(!isdigit((unsigned char)s[i]))return false;
    unsigned h=(s[0]-'0')*10+s[1]-'0',m=(s[2]-'0')*10+s[3]-'0',sec=(s[4]-'0')*10+s[5]-'0';
    return h<24&&m<60&&sec<=60;
}
static bool good(const fix_data *f){return (f->quality==1||f->quality==2||f->quality==4||f->quality==5)&&f->sats>=4&&isfinite(f->hdop)&&f->hdop>0&&f->hdop<=5&&valid_utc(f->utc);}
typedef struct {double lat,lon;unsigned count;char utc[16];uint64_t at;} stable_fix;
static bool accept_fix(gnss_status *s,stable_fix *candidate,const fix_data *f,uint64_t now){
    s->quality=f->quality;s->satellites=f->sats;s->hdop=f->hdop;
    if(!good(f)){s->fix=false;candidate->count=0;candidate->utc[0]=0;return false;}
    if(!strcmp(candidate->utc,f->utc))return false;
    strcpy(candidate->utc,f->utc);
    if(!candidate->count||now-candidate->at>5000||meters(candidate->lat,candidate->lon,f->lat,f->lon)>50){candidate->lat=f->lat;candidate->lon=f->lon;candidate->count=1;}
    else {candidate->count++;candidate->lat+=(f->lat-candidate->lat)/candidate->count;candidate->lon+=remainder(f->lon-candidate->lon,360)/candidate->count;candidate->lon=remainder(candidate->lon,360);}
    candidate->at=now;
    if(candidate->count<3)return false;
    if(!s->has_position||meters(s->latitude,s->longitude,candidate->lat,candidate->lon)>15){s->latitude=candidate->lat;s->longitude=candidate->lon;}
    s->has_position=true;s->saved_position=false;s->fix=true;s->last_fix=now;candidate->count=0;return true;
}
static bool selftest(void){
    char a[]="$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    fix_data f;if(parse(a,&f)!=1||!good(&f)||fabs(f.lat-48.1173)>1e-6||fabs(f.lon-11.5166666667)>1e-6)return false;
    char b[]="$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48";
    if(parse(b,&f)!=-1)return false;
    double v;if(coordinate("9060.000","N",true,&v)||coordinate("nan","N",true,&v)||coordinate("18100","E",false,&v))return false;
    if(!coordinate("4807.038","S",true,&v)||fabs(v+48.1173)>1e-6)return false;
    f=(fix_data){.quality=0,.sats=8,.hdop=.9,.utc="120000"};if(good(&f))return false;
    f.quality=6;if(good(&f))return false;f.quality=1;f.hdop=6;if(good(&f))return false;
    gnss_status check={0};stable_fix candidate={0};
    f=(fix_data){.lat=48.1173,.lon=11.5166666667,.quality=1,.sats=8,.hdop=1,.utc="120001"};
    if(accept_fix(&check,&candidate,&f,1000)||accept_fix(&check,&candidate,&f,1100))return false;
    strcpy(f.utc,"120002");if(accept_fix(&check,&candidate,&f,2000))return false;
    strcpy(f.utc,"120003");if(!accept_fix(&check,&candidate,&f,3000)||!check.has_position||!check.fix)return false;
    f.quality=0;accept_fix(&check,&candidate,&f,4000);
    if(check.fix||!check.has_position||check.latitude!=48.1173||check.last_fix!=3000)return false;
    f.quality=1;f.lat=41;strcpy(f.utc,"120005");accept_fix(&check,&candidate,&f,5000);
    if(check.latitude!=48.1173)return false; // An isolated indoor jump cannot move the marker.
    check.saved_position=true;strcpy(f.utc,"120006");accept_fix(&check,&candidate,&f,6000);
    strcpy(f.utc,"120007");if(!accept_fix(&check,&candidate,&f,7000)||check.saved_position||check.latitude!=41)return false;
    return meters(0,179.999,0,-179.999)<230;
}
void gnss_snapshot(gnss_status *out){
    if(!lock){*out=state;return;}
    xSemaphoreTake(lock,portMAX_DELAY);*out=state;xSemaphoreGive(lock);
    uint64_t now=esp_timer_get_time()/1000;
    out->fix=out->fix&&now>=out->last_fix&&now-out->last_fix<10000;
}
static void task(void *unused){
    (void)unused;char line[160];unsigned used=0;uint8_t buf[256];
    stable_fix candidate={0};
    uint64_t last_save=0;double saved_lat=state.latitude,saved_lon=state.longitude;bool saved=state.persisted;
    for(;;){
        int n=uart_read_bytes(UART_NUM_1,buf,sizeof(buf),pdMS_TO_TICKS(100));
        if(n<=0)continue;
        xSemaphoreTake(lock,portMAX_DELAY);state.bytes+=n;xSemaphoreGive(lock);
        for(int i=0;i<n;i++){
            char c=buf[i];if(c=='$'){used=0;line[used++]=c;continue;}
            if(!used||c=='\r')continue;
            if(c!='\n'){if(used<sizeof(line)-1)line[used++]=c;else used=0;continue;}
            line[used]=0;used=0;fix_data f;int result=parse(line,&f);uint64_t now=esp_timer_get_time()/1000;
            bool save=false;double lat=0,lon=0;
            xSemaphoreTake(lock,portMAX_DELAY);
            if(result<0)state.bad_checksum++;
            else {state.sentences++;state.last_sentence=now;}
            if(result==1&&accept_fix(&state,&candidate,&f,now)){
                lat=state.latitude;lon=state.longitude;
                // Persist the first stable fix; subsequent moved positions at most every 15 minutes.
                save=(!saved||meters(saved_lat,saved_lon,lat,lon)>50)&&(!last_save||now-last_save>=900000);
            }
            xSemaphoreGive(lock);
            if(save){
                nvs_handle_t h;double position[2]={lat,lon};int e=nvs_open("adsb_gnss",NVS_READWRITE,&h);
                if(!e){e=nvs_set_blob(h,"position",position,sizeof(position));if(!e)e=nvs_commit(h);nvs_close(h);}
                last_save=now;
                xSemaphoreTake(lock,portMAX_DELAY);state.save_error=e;if(!e)state.persisted=true;xSemaphoreGive(lock);
                if(!e){saved=true;saved_lat=lat;saved_lon=lon;}
            }
        }
    }
}
int gnss_start(void){
    lock=xSemaphoreCreateMutex();if(!lock)return state.error=-1;
    state.selftest=selftest();if(!state.selftest)return state.error=-2;
    nvs_handle_t h;double position[2];size_t size=sizeof(position);
    if(nvs_open("adsb_gnss",NVS_READONLY,&h)==ESP_OK){
        if(nvs_get_blob(h,"position",position,&size)==ESP_OK&&size==sizeof(position)&&isfinite(position[0])&&isfinite(position[1])&&fabs(position[0])<=90&&fabs(position[1])<=180){
            state.latitude=position[0];state.longitude=position[1];state.has_position=state.saved_position=state.persisted=true;
        }nvs_close(h);
    }
    // These are GNSS-only pins. Leave module reset/wake/PPS high impedance.
    gpio_set_level(48,0);gpio_set_direction(48,GPIO_MODE_OUTPUT);
    gpio_set_direction(40,GPIO_MODE_INPUT);gpio_set_direction(41,GPIO_MODE_INPUT);gpio_set_direction(42,GPIO_MODE_INPUT);
    uart_config_t config={.baud_rate=9600,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    int e=uart_param_config(UART_NUM_1,&config);
    if(!e)e=uart_set_pin(UART_NUM_1,UART_PIN_NO_CHANGE,39,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    if(!e)e=uart_driver_install(UART_NUM_1,2048,0,0,NULL,0);
    if(!e&&xTaskCreatePinnedToCore(task,"gnss",4096,NULL,2,NULL,0)!=pdPASS)e=-3;
    state.error=e;return e;
}
