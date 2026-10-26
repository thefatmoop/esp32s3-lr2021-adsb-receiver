// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from pinned lr2021-sdr radio.c for Meshnology W12.
#include "radio.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static spi_device_handle_t spi;
static SemaphoreHandle_t mutex;
static lr2021_iq_profile profile;
static bool bus_owned;
static unsigned gain_step,boost_setting=8;
static bool frontend_enabled=true;
static bool frontend_ready;
static uint16_t calibration_errors;
static radio_adsb_stats ads;
static TaskHandle_t capture_task;
static void IRAM_ATTR radio_irq(void *arg){
    (void)arg;BaseType_t wake=pdFALSE;
    if(capture_task)vTaskNotifyGiveFromISR(capture_task,&wake);
    if(wake)portYIELD_FROM_ISR();
}
void radio_adsb_wait(bool irq_wake){
    // Keep the idle task schedulable even under a continuous false-trigger storm.
    static int64_t yielded;
    int64_t now=esp_timer_get_time();
    if(!irq_wake||now-yielded>=10000){vTaskDelay(1);yielded=esp_timer_get_time();}
    else if(ulTaskNotifyTake(pdTRUE,1))ads.irq_wakes++;
    else yielded=esp_timer_get_time();
}
static int ready(void)
{
    int64_t end = esp_timer_get_time() + 200000;
    while(gpio_get_level(13)) {
        if(esp_timer_get_time() >= end) return LR2021_IQ_TIMEOUT;
        esp_rom_delay_us(1);
    }
    return 0;
}
static int frame(uint8_t *b, size_t n)
{
    int e = ready();
    if(e) return e;
    spi_transaction_t t = {.length=n*8, .tx_buffer=b, .rx_buffer=b};
    return spi_device_polling_transmit(spi, &t);
}
static int status_ok(const uint8_t *b)
{
    unsigned s = ((unsigned)b[0] << 8) | b[1];
    return s && !(s & 0xF008) && ((s >> 9) & 7) >= 2 &&
        ((s >> 9) & 7) <= 3 && ((s >> 4) & 7) <= 2 && (s & 7) <= 6
        ? 0 : LR2021_IQ_DATA;
}
static int check(void)
{
    uint8_t b[6] = {1, 0};
    int e = frame(b, sizeof(b));
    return e ? e : status_ok(b);
}
static int write_cmd(void *ctx, uint16_t op, const uint8_t *a, size_t n)
{
    ads.last_op=op;
    (void)ctx;
    if(n > 256) return LR2021_IQ_ARGUMENT;
    uint8_t b[258] = {(uint8_t)(op >> 8), (uint8_t)op};
    if(n) memcpy(b+2, a, n);
    int e = frame(b, n+2);
    return e ? e : check();
}
static int read_args(uint16_t op, const uint8_t *a, size_t n, uint8_t *out, size_t m)
{
    if(n > 256 || m > 256) return LR2021_IQ_ARGUMENT;
    uint8_t b[258] = {(uint8_t)(op >> 8), (uint8_t)op};
    if(n) memcpy(b+2, a, n);
    int e = frame(b, n+2);
    if(e) return e;
    memset(b, 0, m+2);
    e = frame(b, m+2);
    if(!e) e = status_ok(b);
    if(!e) memcpy(out, b+2, m);
    return e;
}
static int read_cmd(void *ctx, uint16_t op, uint8_t *out, size_t n)
{ (void)ctx; return read_args(op, NULL, 0, out, n); }
static int read32(void *ctx, uint32_t addr, uint32_t *value)
{
    (void)ctx;
    uint8_t a[4] = {addr >> 16, addr >> 8, addr, 1}, b[4];
    int e = read_args(0x0106, a, 4, b, 4);
    if(!e) *value = ((uint32_t)b[0]<<24) | ((uint32_t)b[1]<<16) | ((uint32_t)b[2]<<8) | b[3];
    return e;
}
static int write32(void *ctx, uint32_t addr, uint32_t value)
{
    uint8_t a[11] = {addr>>16, addr>>8, addr, 255,255,255,255,
                    value>>24, value>>16, value>>8, value};
    return write_cmd(ctx, 0x0105, a, sizeof(a));
}
static int standby(void *ctx)
{
    uint8_t a = 0;
    int e = write_cmd(ctx, 0x0128, &a, 1);
    if(!e) profile.receiving = false;
    return e;
}
static int receive(void *ctx)
{
    const uint8_t a[3] = {255,255,255};
    int e = write_cmd(ctx, 0x020C, a, 3);
    if(!e) profile.receiving = true;
    return e;
}
static int frontend(void)
{
    frontend_ready=false;
    // W12 schematic sheets 1/3: these are complete FEM supply enables,
    // not TX-only enables. Both rails must be powered for their RX LNAs.
    // The schematic and community variant disagree on IO3/4 band labels;
    // enabling both removes that ambiguity. TX control outputs stay LOW.
    const uint8_t outputs[][2]={{5,0x52},{9,0x51},{10,0x51}};
    int e=0;
    for(unsigned i=0;i<3&&!e;i++)e=write_cmd(NULL,0x0112,outputs[i],2);
    // DIO6 -> RFX2402E RXEN, DIO11 -> GC1109 CSD.
    // Bits are standby, RX_LF, TX_LF, RX_HF, TX_HF. No TX bit enabled.
    const uint8_t switches[][2]={{6,frontend_enabled?0x08:0},{11,frontend_enabled?0x02:0}};
    for(unsigned i=0;i<2&&!e;i++){
        e=write_cmd(NULL,0x0113,switches[i],2);
        const uint8_t function[2]={switches[i][0],0x21};
        if(!e)e=write_cmd(NULL,0x0112,function,2);
    }
    if(!e)e=gpio_set_level(3,1);
    if(!e)e=gpio_set_level(4,1);
    if(!e)vTaskDelay(pdMS_TO_TICKS(5));
    frontend_ready=!e;
    return e;
}
static int reset(void *ctx)
{
    profile.configured = false; profile.receiving = false;
    frontend_ready=false;
    // Remove FEM power while the radio's DIOs return to their reset pulls.
    gpio_set_level(3,0);gpio_set_level(4,0);
    int e=gpio_set_level(12,0);
    vTaskDelay(pdMS_TO_TICKS(5));
    if(!e)e=gpio_set_level(12,1);
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t v[2];
    if(!e) e = read_cmd(ctx, 0x0101, v, 2);
    if(e) return e;
    profile.fw_major=v[0]; profile.fw_minor=v[1]; profile.lr2021=v[0]==1;
    if(v[0]!=1 || v[1]!=24) return LR2021_IQ_STATE;
    if(!e) e = standby(ctx);
    if(!e) e = write_cmd(ctx, 0x0111, NULL, 0);
    /* W12 uses a crystal, no TCXO. */
    uint8_t a=0;
    if(!e) e = write_cmd(ctx, 0x0121, &a, 1); /* LDO */
    a=1;
    if(!e) e = write_cmd(ctx, 0x0206, &a, 1);
    a=0x6F;
    if(!e) e = write_cmd(ctx, 0x0122, &a, 1);
    if(!e) e = frontend();
    return e;
}
int radio_configure(uint32_t hz)
{
    if(!lr_frequency_valid(hz)) return LR2021_IQ_ARGUMENT;
    int e=standby(NULL);
    profile.configured=false;
    /* W12 controls are LR2021 DIOs, configured explicitly after each reset. */
    if(!e)e=write_cmd(NULL,0x0111,NULL,0);
    if(!e)e=frontend();
    uint8_t type=0;
    if(!e) e=write_cmd(NULL,0x0207,&type,1);
    // CalibFE encodes the RX path in bit 15 of each 4 MHz frequency.
    unsigned cell=hz/4000000 | (hz>=LR_HF_HZ?0x8000u:0);
    uint8_t cal[6]={cell>>8,cell,0,0,0,0};
    if(!e) {
        e=write_cmd(NULL,0x0123,cal,6);
        // CalibFE can report command failure as well as IMG_CALIB_ERR at
        // the experimental low end. Inspect the actual error bits before
        // allowing that one measured exception; bus/timeouts remain fatal.
        if(e==LR2021_IQ_DATA && hz>=LR_MIN_HZ && hz<106000000u) {
            uint8_t flags[2];
            int status=read_cmd(NULL,0x0110,flags,2);
            if(!status) {
                calibration_errors=((uint16_t)flags[0]<<8)|flags[1];
                if(calibration_errors==0x0080u && lr_calibration_acceptable(hz,calibration_errors))e=0;
            }
        }
    }
    uint8_t freq[4]={hz>>24,hz>>16,hz>>8,hz};
    if(!e) e=write_cmd(NULL,0x0200,freq,4);
    bool hf=hz>=LR_HF_HZ;
    const uint8_t path[2]={hf?1:0,boost_setting==8?(hf?4:0):boost_setting}, mod[2]={0x72,0x10}, pkt[4]={0,8,255,2}, sync=0x12;
    if(!e) e=write_cmd(NULL,0x0201,path,2);
    if(!e) e=write_cmd(NULL,0x0220,mod,2);
    if(!e) e=write_cmd(NULL,0x0221,pkt,4);
    if(!e) e=write_cmd(NULL,0x0223,&sync,1);
    const uint8_t gain=gain_step;
    if(!e) e=write_cmd(NULL,0x021A,&gain,1);
    uint8_t errors[2];
    if(!e) e=read_cmd(NULL,0x0110,errors,2);
    if(!e) {
        calibration_errors=((uint16_t)errors[0]<<8)|errors[1];
        if(!lr_calibration_acceptable(hz,calibration_errors))e=LR2021_IQ_DATA;
    }
    if(!e) {
        profile.frequency_hz=hz; profile.bandwidth_hz=31250;
        profile.spreading_factor=7; profile.exclusive_lora=true; profile.configured=true;
    }
    return e;
}
uint16_t radio_cal_errors(void){return calibration_errors;}
unsigned radio_gain(void){return gain_step;}
unsigned radio_boost(void){return boost_setting;}
bool radio_frontend(void){return frontend_enabled;}
int radio_controls(unsigned gain,unsigned boost,bool fem)
{
    if(gain>13||boost>8)return LR2021_IQ_ARGUMENT;
    int e=standby(NULL);
    profile.configured=false;
    if(e){frontend_ready=false;return e;}
    gain_step=gain;boost_setting=boost;frontend_enabled=fem;
    return radio_configure(profile.frequency_hz);
}
int radio_receiver_configure(uint32_t hz,unsigned gain,unsigned boost)
{
    // One stopped-stream transaction owns all P4-controlled RF settings.
    if(gain>13||boost>8)return LR2021_IQ_ARGUMENT;
    gain_step=gain;boost_setting=boost;frontend_enabled=true;
    return radio_configure(hz);
}
uint8_t radio_rf_state(uint32_t hz)
{
    if(!profile.configured||!frontend_ready||profile.frequency_hz!=hz)return 0;
    bool hf=hz>=LR_HF_HZ;
    return 0x80|(frontend_enabled?0x40:0)|(hf?0x20:0)|(!gain_step?0x10:0)|
        (boost_setting==8?(hf?4:0):boost_setting);
}
static int restore(void *ctx, const lr2021_iq_profile *saved)
{
    int e=radio_configure(saved->frequency_hz);
    if(!e && saved->receiving) e=receive(ctx);
    return e;
}
static void lock(void *ctx) { (void)ctx; xSemaphoreTake(mutex,portMAX_DELAY); }
static void unlock(void *ctx) { (void)ctx; xSemaphoreGive(mutex); }
static int snapshot(void *ctx, lr2021_iq_profile *p) { (void)ctx; *p=profile; return 0; }
static uint64_t now(void *ctx) { (void)ctx; return esp_timer_get_time(); }
static void sleep_us(void *ctx, unsigned us) { (void)ctx; esp_rom_delay_us(us); }
static int fast(void *ctx, bool on)
{
    (void)ctx;
    if(on) {
        if(bus_owned) return LR2021_IQ_STATE;
        int e=spi_device_acquire_bus(spi,portMAX_DELAY);
        if(!e) bus_owned=true;
        return e;
    }
    if(bus_owned) { spi_device_release_bus(spi); bus_owned=false; }
    return 0;
}
static int clear_fifo(void *ctx) { return write_cmd(ctx,0x011E,NULL,0); }
static int read_fifo(void *ctx, uint8_t *out, size_t n)
{
    (void)ctx;
    if(!n || n>256) return LR2021_IQ_ARGUMENT;
    uint8_t b[258]={0,1};
    int e=frame(b,n+2);
    if(!e) e=status_ok(b);
    if(!e) memcpy(out,b+2,n);
    return e;
}
const lr2021_iq_hal *radio_hal(void)
{
    static const lr2021_iq_hal h={.lock=lock,.unlock=unlock,.snapshot=snapshot,
        .read32=read32,.write32=write32,.write=write_cmd,.read=read_cmd,.standby=standby,
        .receive=receive,.reset=reset,.restore=restore,.fast=fast,.clear_fifo=clear_fifo,
        .read_fifo=read_fifo,.now_us=now,.sleep_us=sleep_us};
    return &h;
}
uint16_t radio_version(void){return (profile.fw_major<<8)|profile.fw_minor;}
int radio_info(void)
{
    uint8_t v[2],err[2];
    int e=read_cmd(NULL,0x0101,v,2);
    if(!e) e=read_cmd(NULL,0x0110,err,2);
    if(!e) printf("LR2021 ROM %u.%u frequency=%lu bandwidth=31250 configured=%d errors=%02X%02X\n",
        v[0],v[1],(unsigned long)profile.frequency_hz,profile.configured,err[0],err[1]);
    return e;
}
int radio_init(void)
{
    mutex=xSemaphoreCreateMutex();
    if(!mutex)return LR2021_IQ_IO;
    // Keep FEM rails off until radio TX controls are explicitly driven LOW.
    gpio_set_level(3,0);gpio_set_direction(3,GPIO_MODE_OUTPUT);
    gpio_set_level(4,0);gpio_set_direction(4,GPIO_MODE_OUTPUT);
    int e=gpio_set_direction(12,GPIO_MODE_OUTPUT);
    if(!e)e=gpio_set_direction(13,GPIO_MODE_INPUT);
    spi_bus_config_t sb={.mosi_io_num=10,.miso_io_num=11,.sclk_io_num=9,
        .quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=258};
    if(!e)e=spi_bus_initialize(SPI2_HOST,&sb,SPI_DMA_CH_AUTO);
    spi_device_interface_config_t sd={.clock_speed_hz=16000000,.mode=0,
        .spics_io_num=8,.queue_size=1};
    if(!e)e=spi_bus_add_device(SPI2_HOST,&sd,&spi);
    // W12 schematic sheet 3: DIO7 -> GPIO7 (INT1), DIO8 -> GPIO14 (INT2).
    // ISR only wakes the owner
    // task; all SPI and FIFO handling remain on that task, never in the ISR.
    capture_task=xTaskGetCurrentTaskHandle();
    gpio_config_t irq={.pin_bit_mask=1ULL<<7,.mode=GPIO_MODE_INPUT,
        .pull_down_en=GPIO_PULLDOWN_ENABLE,.intr_type=GPIO_INTR_POSEDGE};
    if(!e)e=gpio_config(&irq);
    if(!e){int isr=gpio_install_isr_service(ESP_INTR_FLAG_IRAM);if(isr!=ESP_ERR_INVALID_STATE)e=isr;}
    if(!e)e=gpio_isr_handler_add(7,radio_irq,NULL);
    if(!e) e=reset(NULL);
    return e;
}

// Receive-only OOK packet path, adapted from LakeShark ls_lora_lr20xx.c.
// Keep all radio calls on the receiver task; snapshots are copied by that task.
static uint8_t aq[256], aq_n, aq_pos;
static unsigned ads_frame_bytes=32,raw_capture_bytes=32;
static unsigned partial;
static int64_t partial_since;
static float aq_rssi;
static unsigned ag=13,ab=7,abw;
static int ath=100;
static bool ahead,ahf,ahw;
static int irq_clear(void) {
    const uint8_t all[4]={255,255,255,255};
    return write_cmd(NULL,0x0116,all,4);
}
static int ads_arm(void) {
    aq_n=aq_pos=0;
    partial=0;partial_since=0;
    int e=standby(NULL);
    if(!e)e=clear_fifo(NULL);
    const uint8_t flags[2]={255,255};
    if(!e)e=write_cmd(NULL,0x0114,flags,2);
    if(!e)e=irq_clear();
    if(!e)e=receive(NULL);
    return e;
}
int radio_adsb_stop(void){aq_n=aq_pos=0;return standby(NULL);}
int radio_adsb_start(unsigned gain,unsigned boost,int threshold,bool assisted,unsigned bandwidth,bool hf,unsigned capture_bytes,bool hardware_manchester) {
    if(!capture_bytes)capture_bytes=32;
    if(gain>13||boost>7||bandwidth>3||capture_bytes<28||capture_bytes>32||((threshold!=100&&threshold!=101)&&(threshold< -64||threshold>63)))return -1;
    ag=gain;ab=boost;ath=threshold;ahead=assisted;abw=bandwidth;ahf=hf;
    ahw=hardware_manchester;raw_capture_bytes=capture_bytes;
    ads_frame_bytes=hardware_manchester?14:capture_bytes;
    ads.fifo_peak=0;
    int e=reset(NULL);
    uint8_t type=10;
    if(!e)e=write_cmd(NULL,0x0207,&type,1);
    uint32_t hz=1090000000u;
    uint8_t freq[4]={hz>>24,hz>>16,hz>>8,hz},path[2]={hf?1:0,boost};
    if(!e)e=write_cmd(NULL,0x0200,freq,4);
    if(!e)e=write_cmd(NULL,0x0201,path,2);
    // W12 ROM 1.24 reports NO_CALIB_ERR (0x0200) with all-zero CalibFe.
    // Explicit LF calibration cells succeed on this board.
    // CalibFE bit 15 selects the same physical RX input as SetRxPath.
    unsigned cell=hz/4000000u | (hf?0x8000u:0);
    const uint8_t cal[6]={cell>>8,cell,(cell+1)>>8,cell+1,(cell-1)>>8,cell-1};
    if(!e)e=write_cmd(NULL,0x0123,cal,6);
    const uint8_t mod[7]={0,0x1e,0x84,0x80,0,bandwidth*64,0}; // 2 Mchip/s, 3.077 .. 2.222 MHz
    if(!e)e=write_cmd(NULL,0x0281,mod,7);
    ads.noise_dbm=NAN;ads.threshold_raw=-1;
    ads.rssi_now=ads.rssi_peak=NAN;
    if(!e&&threshold!=100&&threshold!=101) {
        uint32_t v=0;
        e=read32(NULL,0xF30E14,&v);
        if(!e)e=write32(NULL,0xF30E14,(v&~(0x7fu<<20))|((uint32_t)(threshold+64)<<20));
    }
    // 28 bytes = 112 us of data; 30 also preserves the decoder's full 8-us
    // alignment search. 32 is retained as an A/B control (16-us extra tail).
    // ADSBee: inverted Manchester (9), hardware CRC off, 14 decoded bytes.
    // Raw mode retains MSB-first packed chips for the S3 alignment search.
    const uint8_t pkt[6]={0,16,0,0,ads_frame_bytes,hardware_manchester?9:0},sync[5]={0,0,0,0,hardware_manchester?0:0x80};
    // Assisted detector: last 8 preamble chips + first 4 message bits (1000).
    // Detector pattern is LSB-first: preamble 0x0285 -> 0x02, data -> 0xa9.
    const uint8_t det[5]={assisted?0xa9:0x02,assisted?0x02:0x85,15,0,0};
    if(!e)e=write_cmd(NULL,0x0282,pkt,6);
    if(!e)e=write_cmd(NULL,0x0284,sync,5);
    if(!e)e=write_cmd(NULL,0x0288,det,5);
    // Latch real FIFO overflow/underflow separately from a stalled partial
    // capture. GetRxFifoLevel alone can miss a full FIFO between polls.
    const uint8_t fifoirq[4]={0x30,0,0,128};
    if(!e)e=write_cmd(NULL,0x011a,fifoirq,4);
    const uint8_t irqfn[2]={7,0x10},irqmask[5]={7,0,0xe7,0,1};
    if(!e)e=write_cmd(NULL,0x0112,irqfn,2);
    if(!e)e=write_cmd(NULL,0x0115,irqmask,5);
    uint8_t g=gain;
    if(!e)e=write_cmd(NULL,0x021a,&g,1);
    if(!e&&threshold==101){
        // TheClams lr2021-apps auto_thr: CCA minimum + 15 dB, biased by 64.
        // Repeat short measurements and use the quietest result. CCA uses AGC
        // as in that example; restore the requested fixed gain afterwards.
        unsigned quiet=0;
        for(int i=0;i<3&&!e;i++){
            e=write_cmd(NULL,0x0129,NULL,0);
            const uint8_t duration[3]={0,1,64}; // 320 * 31.25 ns = 10 us
            if(!e)e=write_cmd(NULL,0x0218,duration,3);
            esp_rom_delay_us(50);
            uint8_t cca[4];
            if(!e)e=read_cmd(NULL,0x0219,cca,4);
            if(!e){unsigned min=((unsigned)cca[0]<<1)|((cca[3]>>2)&1);if(min>quiet)quiet=min;}
        }
        if(!e){
            ads.noise_dbm=-(float)quiet*.5f;
            // Explicit modulo matches the reference's signed i8 masked write,
            // including calibrated RSSI values below -79 dBm.
            unsigned raw=(unsigned)(64+15-(int)(quiet>>1))&127u;
            uint32_t v=0;e=read32(NULL,0xF30E14,&v);
            if(!e)e=write32(NULL,0xF30E14,(v&~(127u<<20))|(raw<<20));
            if(!e)e=write_cmd(NULL,0x021a,&g,1);
        }
    }
    uint32_t thr_read=0;
    if(!e)e=read32(NULL,0xF30E14,&thr_read);
    if(!e)ads.threshold_raw=(thr_read>>20)&127;
    // ADSBee 1421's bench-tested fixed-gain Mode S workaround: prevent ADC
    // saturation blanking. This undocumented register is restored on reset.
    // Reference: research/adsbee/lr2021_ook_adsb.hh (GPL-3.0).
    if(!e&&gain&&!assisted){
        uint32_t v=0;e=read32(NULL,0xF30150,&v);
        if(!e)e=write32(NULL,0xF30150,(v&~255u)|0x18u);
    }
    if(!e){
        uint32_t v=0;e=read32(NULL,0xF30844,&v);
        if(!e)e=write32(NULL,0xF30844,v&~0x01000000u);
    }
    uint8_t err[2]={0};
    if(!e)e=read_cmd(NULL,0x0110,err,2);
    ads.chip_errors=((uint16_t)err[0]<<8)|err[1];
    if(!e&&ads.chip_errors)e=LR2021_IQ_DATA;
    if(!e)e=ads_arm();
    ads.last_error=e;
    if(e)standby(NULL);
    return e;
}
int radio_adsb_poll(uint8_t chips[32],float *rssi) {
    if(aq_pos<aq_n)goto deliver;
    uint8_t s[6]={1,0};
    int e=frame(s,6);
    ads.polls++;
    ads.status=((uint16_t)s[0]<<8)|s[1];
    if(!e)e=status_ok(s);
    if(!e&&((ads.status>>4)&7)){
        ads.recoveries++;
        e=radio_adsb_start(ag,ab,ath,ahead,abw,ahf,raw_capture_bytes,ahw);
        return e?e:0;
    }
    uint32_t irq=((uint32_t)s[2]<<24)|((uint32_t)s[3]<<16)|((uint32_t)s[4]<<8)|s[5];
    if(!e&&(irq&((1u<<16)|(1u<<17)))) {
        uint8_t er[2];e=read_cmd(NULL,0x0110,er,2);
        if(!e){ads.chip_errors=((uint16_t)er[0]<<8)|er[1];e=LR2021_IQ_DATA;}
    }
    if(!e&&(irq&1u)){
        uint8_t flags[2]={0};e=read_cmd(NULL,0x012e,flags,2);
        if(!e&&(flags[0]&0x30)){
            if(flags[0]&0x10)ads.fifo_overflow_flags++;
            if(flags[0]&0x20)ads.fifo_underflow_flags++;
            ads.overflows++;e=ads_arm();
            if(!e)return 0;
        }
    }
    if(!e&&irq)e=irq_clear();
    if(!e&&ads.polls%16==0){
        uint8_t r[2];e=read_cmd(NULL,0x020b,r,2);
        if(!e){ads.rssi_now=-(float)(((unsigned)r[0]<<1)|(r[1]&1))*.5f;if(!isfinite(ads.rssi_peak)||ads.rssi_now>ads.rssi_peak)ads.rssi_peak=ads.rssi_now;}
    }
    uint8_t level[2]={0};
    if(!e)e=read_cmd(NULL,0x011c,level,2);
    unsigned n=((unsigned)level[0]<<8)|level[1];
    if(!e&&n>ads.fifo_peak)ads.fifo_peak=n;
    if(!e&&n>256)e=LR2021_IQ_DATA;
    // Never trust frame alignment after saturation. Clear in standby.
    if(!e&&n==256){ads.overflows++;ads.fifo_full++;e=ads_arm();n=0;}
    if(!e&&n&&n<ads_frame_bytes){
        int64_t now=esp_timer_get_time();
        if(n!=partial){partial=n;partial_since=now;}
        // Interrupt-driven polls can be microseconds apart. A poll-count
        // timeout would incorrectly reset an otherwise healthy partial frame.
        if(now-partial_since>=3000){ads.overflows++;ads.partial_resets++;e=ads_arm();n=0;}
    }else {partial=0;partial_since=0;}
    if(!e&&n>=ads_frame_bytes) {
        aq_n=n/ads_frame_bytes;aq_pos=0;
        e=read_fifo(NULL,aq,aq_n*ads_frame_bytes);
        aq_rssi=NAN;
        uint8_t ps[6];
        if(!e&&!read_cmd(NULL,0x0287,ps,6))aq_rssi=-(float)(((unsigned)ps[3]<<1)|(ps[4]&1))*0.5f;
    }
    if(!e&&(ads.status&7)!=4&&!aq_n){ads.recoveries++;e=ads_arm();}
    if(e){ads.errors++;ads.last_error=e;aq_n=aq_pos=0;return e<0?e:-e;}
    if(!aq_n)return 0;
deliver:
    memcpy(chips,aq+aq_pos*ads_frame_bytes,ads_frame_bytes);
    ++aq_pos;
    if(rssi)*rssi=aq_pos==aq_n?aq_rssi:NAN;
    if(aq_pos==aq_n)aq_pos=aq_n=0;
    ads.frames++;
    return ads_frame_bytes;
}
void radio_adsb_get_stats(radio_adsb_stats *out){*out=ads;}
