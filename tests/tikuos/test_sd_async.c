/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_sd_async.c - SD read/write progression against an independent wire model.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t registers[256];
static uintptr_t addresses[256];
static unsigned register_count;
/** @brief Model card-pad registers independently of protocol replies. */
static volatile uint32_t *reg(uintptr_t address)
{
    for(unsigned i=0;i<register_count;i++)if(addresses[i]==address)return &registers[i];
    assert(register_count<256);addresses[register_count]=address;return &registers[register_count++];
}
#define TIKU_S31_SD_REG(a) (*reg(a))
#define TIKU_S31_SD_ASYNC_ENABLE 1
void tiku_clock_arch_delay(unsigned int us);
#include <arch/esp32s31/tiku_sdcard_arch.c>
volatile uint8_t tiku_s31_in_trap;
static uint64_t ticks, power_start;
static unsigned claims, bus_open, bus_closes, commands, polls, batch_max, acmd;
static unsigned bad_crc, no_token, no_response, bad_register, fail_spi, fail_claim, kernel=1;
static uint8_t wire[1024], sector[512];
static unsigned head,tail;
static unsigned write_stage,write_bytes,write_count,bad_write_response,bad_status,busy_forever,programming;
static uint8_t written[512];
static tiku_s31_spi_async_t *bus_op;
uint64_t tiku_s31_time(void) { return ticks; }
int tiku_thread_in_kernel(void) { return (int)kernel; }
void tiku_clock_arch_delay(unsigned int us) { (void)us; assert(0); }
int tiku_gpio_claim(uint8_t port,uint8_t pin,const char *who)
{ (void)port;(void)pin;(void)who;if(fail_claim)return -1;++claims;return 0; }
int tiku_gpio_release(uint8_t port,uint8_t pin,const char *who)
{ (void)port;(void)pin;(void)who;assert(claims);--claims;return 0; }
/** @brief Polynomial long division independent of the driver's bytewise wire CRC. */
static uint16_t wire_checksum(const uint8_t *data,unsigned length)
{
    unsigned value=0;
    for(unsigned i=0;i<length*8+16;i++){
        value=(value<<1)|(i<length*8 ? (data[i/8]>>(7-i%8))&1u : 0);
        if(value&0x10000)value^=0x11021;
    }
    return (uint16_t)value;
}
/** @brief Append a checked register or sector to the card's response stream. */
static void payload(const uint8_t *data,unsigned n)
{
    if(no_token)return;
    wire[tail++]=255;wire[tail++]=254;
    memcpy(wire+tail,data,n);tail+=n;
    unsigned sum=wire_checksum(data,n) ^ (bad_crc?1:0);
    wire[tail++]=(uint8_t)(sum>>8);wire[tail++]=(uint8_t)sum;
}
/** @brief Decode commands and provide independently selected SDHC response data. */
static void wire_command(const uint8_t *tx)
{
    unsigned cmd=tx[0]&63u;head=tail=0;++commands;
    if(!cmd)assert(((ticks-power_start)&((1ULL<<52)-1))>=640000u);
    if(no_response)return;
    wire[tail++]=(cmd==0||cmd==8||cmd==55|| (cmd==41 && acmd++<2))?1:0;
    if(cmd==8){wire[tail++]=0;wire[tail++]=0;wire[tail++]=1;wire[tail++]=0xaa;}
    if(cmd==58){wire[tail++]=0xc0;wire[tail++]=0x30;wire[tail++]=0;wire[tail++]=0;}
    if(cmd==9||cmd==10){
        /* CRC7 values are independent fixed vectors for these 15-byte records. */
        uint8_t data[16]={0};data[0]=cmd==9?0x40:0x12;data[9]=cmd==9?31:0;
        data[15]=(uint8_t)((cmd==9?0xb3:0x03) ^ (bad_register?2:0));
        payload(data,16);
    }
    if(cmd==24){assert(tx[1]==0 && tx[2]==0 && tx[3]==0 && tx[4]==7);write_stage=1;write_bytes=0;}
    if(cmd==13){wire[tail++]=(uint8_t)bad_status;programming=0;}
    if(cmd==17){assert(tx[1]==0 && tx[2]==0 && tx[3]==0 && tx[4]==7);payload(sector,512);}
}
int tiku_s31_spi_init_begin(tiku_s31_spi_async_t *op,const tiku_spi_config_t *config)
{
    assert(config->prescaler==100);
    if(bus_open)return TIKU_SPI_ERR_PARAM;
    power_start=ticks;bus_open=1;bus_op=op;*op=(tiku_s31_spi_async_t){.phase=1};return 1;
}
int tiku_s31_spi_clock_begin(tiku_s31_spi_async_t *op,uint32_t hz)
{
    assert(bus_open && !bus_op && hz==20000000);
    bus_op=op;*op=(tiku_s31_spi_async_t){.phase=1};return 1;
}
int tiku_s31_spi_begin(tiku_s31_spi_async_t *op,const uint8_t *tx,uint8_t *rx,uint16_t length)
{
    assert(bus_open && !bus_op && length && length<=64);
    if(length>batch_max)batch_max=length;
    if(tx && length==6)wire_command(tx);
    else if(tx && write_stage==1){assert(length==1 && tx[0]==254);write_stage=2;}
    else if(tx && write_stage==2){
        assert(length==64 && write_bytes+length<=512);memcpy(written+write_bytes,tx,length);write_bytes+=length;
        if(write_bytes==512)write_stage=3;
    } else if(tx && write_stage==3){
        assert(length==2 && (((unsigned)tx[0]<<8)|tx[1])==wire_checksum(written,512));
        head=tail=0;wire[tail++]=255;wire[tail++]=(uint8_t)(bad_write_response?11:5);
        wire[tail++]=0;wire[tail++]=0;wire[tail++]=0;
        if(!busy_forever)wire[tail++]=255;
        write_stage=0;programming=1;++write_count;
    }
    *op=(tiku_s31_spi_async_t){.rx=rx,.length=length,.phase=2};bus_op=op;return 1;
}
int tiku_s31_spi_poll(tiku_s31_spi_async_t *op)
{
    assert(bus_open && bus_op==op);++polls;
    if(fail_spi){bus_op=NULL;return TIKU_SPI_ERR_TIMEOUT;}
    if(op->phase++==2)return 1;
    for(unsigned i=0;op->rx && i<op->length;i++)op->rx[i]=head<tail?wire[head++]:(busy_forever && programming?0:255);
    bus_op=NULL;return 0;
}
void tiku_spi_arch_close(void) { assert(bus_open);bus_open=0;bus_op=NULL;++bus_closes; }
int tiku_spi_arch_init(const tiku_spi_config_t *config) { (void)config;assert(0);return -1; }
int tiku_spi_arch_write_read(const uint8_t *a,uint8_t *b,uint16_t n)
{ (void)a;(void)b;(void)n;assert(0);return -1; }
int tiku_spi_arch_read(uint8_t *a,uint16_t n) { (void)a;(void)n;assert(0);return -1; }
int tiku_spi_arch_write(const uint8_t *a,uint16_t n) { (void)a;(void)n;assert(0);return -1; }
int tiku_s31_spi_clock(uint32_t hz) { (void)hz;assert(0);return -1; }
/** @brief Supply elapsed time and require at most one SPI completion observation per owner turn. */
static int step(tiku_s31_sd_async_t *op)
{
    unsigned before=polls;uint64_t before_ticks=ticks;
    int rc=tiku_s31_sd_poll(op);
    assert(polls-before<=1 && ticks==before_ticks);
    ticks=(ticks+8000)&((1ULL<<52)-1);return rc;
}
/** @brief Bound the fixture loop while slow hardware makes other owner work possible. */
static int drain(tiku_s31_sd_async_t *op)
{
    for(unsigned turns=0;turns<4000;turns++){int rc=step(op);if(rc!=1)return rc;}
    assert(0);return -99;
}
/** @brief Open and validate a capacity and identity without a synchronous driver call. */
static void open_card(tiku_s31_sd_async_t *op,tiku_s31_sdcard_t *card)
{
    acmd=head=tail=write_stage=programming=0;
    assert(tiku_s31_sd_open_begin(op,card)==1);
    assert(drain(op)==0 && card->sectors==32768 && card->cid[0]==0x12 && claims==4 && bus_open);
}
int main(void)
{
    tiku_s31_sd_async_t op,other;tiku_s31_sdcard_t card;
    for(unsigned i=0;i<512;i++)sector[i]=(uint8_t)(i*37+11);
    uint8_t out[512];memset(out,0x55,sizeof out);
    open_card(&op,&card);
    assert(tiku_s31_sd_clock_begin(&op,&card,20000000)==1 && !drain(&op));
    assert(tiku_s31_sd_read_begin(&op,&card,7,out)==1);
    assert(tiku_s31_sd_read_begin(&other,&card,7,out)==TIKU_S31_SD_BUSY);
    kernel=0;assert(tiku_s31_sd_poll(&op)==TIKU_S31_SD_INVALID && claims==4 && bus_open);kernel=1;
    assert(tiku_s31_sdcard_close(&card)==TIKU_S31_SD_INVALID);
    assert(tiku_s31_sdcard_read(7,out,&card)==TIKU_S31_SD_INVALID);
    assert(!drain(&op) && !memcmp(out,sector,512) && card.reads==1 && batch_max==64);
    assert(tiku_s31_sd_poll(&op)==TIKU_S31_SD_INVALID);
    memset(out,0x55,sizeof out);bad_crc=1;
    assert(tiku_s31_sd_read_begin(&op,&card,7,out)==1 && drain(&op)==TIKU_S31_SD_IO);
    for(unsigned i=0;i<512;i++)assert(out[i]==0x55);
    assert(!claims && !bus_open);bad_crc=0;
    open_card(&op,&card);no_token=1;
    assert(tiku_s31_sd_read_begin(&op,&card,7,out)==1 && drain(&op)==TIKU_S31_SD_TIMEOUT);
    assert(!claims && !bus_open);no_token=0;
    no_response=1;assert(tiku_s31_sd_open_begin(&op,&card)==1 && drain(&op)==TIKU_S31_SD_NO_RESPONSE);
    assert(!claims && !bus_open);no_response=0;
    for(unsigned stop=0;stop<350;stop++){
        acmd=head=tail=0;assert(tiku_s31_sd_open_begin(&op,&card)==1);
        unsigned i;for(i=0;i<stop;i++)if(step(&op)!=1)break;
        if(i==stop)assert(!tiku_s31_sd_cancel(&op));else assert(!tiku_s31_sdcard_close(&card));
        assert(!claims && !bus_open);
    }
    for(unsigned stop=0;stop<70;stop++){
        open_card(&op,&card);assert(tiku_s31_sd_read_begin(&op,&card,7,out)==1);
        unsigned i;for(i=0;i<stop;i++)if(step(&op)!=1)break;
        if(i==stop)assert(!tiku_s31_sd_cancel(&op));else assert(!tiku_s31_sdcard_close(&card));
        assert(!claims && !bus_open);
    }
    bad_register=1;
    assert(tiku_s31_sd_open_begin(&op,&card)==1 && drain(&op)==TIKU_S31_SD_IO);
    assert(!claims && !bus_open);bad_register=0;
    fail_claim=1;assert(tiku_s31_sd_open_begin(&op,&card)==TIKU_S31_SD_BUSY && !claims);fail_claim=0;
    bus_open=1;unsigned closed=bus_closes;
    assert(tiku_s31_sd_open_begin(&op,&card)==TIKU_S31_SD_BUSY && bus_open && bus_closes==closed && !claims);bus_open=0;
    assert(tiku_s31_sd_open_begin(&op,&card)==1);fail_spi=1;
    assert(step(&op)==TIKU_S31_SD_IO && !claims && !bus_open);fail_spi=0;
    ticks=(1ULL<<52)-8000;open_card(&op,&card);assert(!tiku_s31_sdcard_close(&card));
    open_card(&op,&card);
    assert(tiku_s31_sd_write_begin(&op,&card,7,sector)==TIKU_S31_SD_INVALID);
    assert(!tiku_s31_sdcard_write_enable(&card,7,1));
    assert(tiku_s31_sd_write_begin(&op,&card,6,sector)==TIKU_S31_SD_INVALID);
    assert(tiku_s31_sd_write_begin(&op,&card,8,sector)==TIKU_S31_SD_INVALID);
    assert(tiku_s31_sd_write_begin(&op,&card,7,sector)==1);
    assert(tiku_s31_sd_sync_begin(&other,&card)==TIKU_S31_SD_BUSY);
    assert(!drain(&op) && write_count==1 && !memcmp(written,sector,512) && !pending);
    assert(tiku_s31_sd_sync_begin(&op,&card)==1 && !drain(&op));
    assert(!tiku_s31_sdcard_close(&card));
    for(unsigned stop=0;stop<110;++stop){
        open_card(&op,&card);assert(!tiku_s31_sdcard_write_enable(&card,7,1));
        assert(tiku_s31_sd_write_begin(&op,&card,7,sector)==1);
        unsigned i;for(i=0;i<stop;i++)if(step(&op)!=1)break;
        if(i==stop){
            unsigned turns=0;int rc;
            do{unsigned before=polls;rc=tiku_s31_sd_cancel(&op);assert(polls-before<=1);ticks+=8000;assert(++turns<4000);}while(rc==TIKU_S31_SD_BUSY);
            assert(!rc);
        }else assert(!tiku_s31_sdcard_close(&card));
        assert(!claims && !bus_open && !pending);
    }
    for(unsigned failure=0;failure<4;++failure){
        open_card(&op,&card);assert(!tiku_s31_sdcard_write_enable(&card,7,1));
        bad_write_response=failure==0;busy_forever=failure==1;bad_status=failure==2;
        assert(tiku_s31_sd_write_begin(&op,&card,7,sector)==1);
        if(failure==3){while(op.state!=33)assert(step(&op)==1);fail_spi=1;}
        if(failure==3){
            while(op.state!=40)assert(step(&op)==1);
            uint64_t reset_start=op.timer;
            while(((ticks-reset_start)&((1ULL<<52)-1))<320000u){
                assert(claims==4&&pending&&op.output==sector&&tiku_s31_sd_open_begin(&other,&card)==TIKU_S31_SD_BUSY);
                assert(step(&op)==1);
            }
        }
        int rc=drain(&op);assert(rc==(failure==1?TIKU_S31_SD_TIMEOUT:TIKU_S31_SD_IO));
        assert(!claims && !bus_open && !pending && !op.state);
        bad_write_response=busy_forever=bad_status=fail_spi=0;
    }
    kernel=0;assert(tiku_s31_sd_open_begin(&op,&card)==TIKU_S31_SD_INVALID);
    puts("SD: read/write CRC, status, authorization, bounded phases, cancellation, fault reset and wrap pass");
}
