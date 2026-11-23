/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_flash_slice.c - automatic SPI1 suspension and retained controller ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <arch/esp32s31/tiku_watchdog_arch.h>
static uint32_t regs[64],sfdp[32],ident,clock_root,spi0_ctrl,spi0_fsm;
/* The core's bus timeout controls: protection on, threshold 65,535 cycles. */
#define BUS_TIMEOUT 0x1ffffu
static uint32_t ibus_timeout=BUS_TIMEOUT,dbus_timeout=BUS_TIMEOUT;
static uint64_t ticks;
static unsigned context=1,commands,busy,wel,remaining,duration;
static unsigned hold_suspended,ignore_write,stuck,protected,write_enable_fail,software_resumes,cache_misses;
static unsigned erases,programs,bytes,meter_ends,watchdogs,fatals,cache_reads;
static uint32_t operation,program_data[16],finish_data[16];
static jmp_buf fatal_jump;
static const char *fatal_reason;

/** @brief Execute short commands or start the controller's autonomous wait-idle operation. */
static void execute(void)
{
    if(busy) {
        if((regs[0x9c/4]&1u) && !hold_suspended && !stuck) {
            regs[0x9c/4]&=~1u;regs[0xa4/4]&=~1u;++software_resumes;
            unsigned interval=operation==2 ? 64u : 512u;
            ticks+=16u*interval;
            if(remaining>interval)remaining-=interval;
            else {
                busy=wel=0;regs[0]=0;
                if(operation==2)memcpy(finish_data,program_data,sizeof finish_data);
            }
        }
        return;
    }
    if(!regs[0] || stuck)return;
    unsigned code=regs[8]&255u,in=(regs[6]&(1u<<28)) ? (regs[10]+1)/8 : 0;
    unsigned out=(regs[6]&(1u<<27)) ? (regs[9]+1)/8 : 0;
    ++commands;
    assert((regs[8]>>28)==7 && !(regs[6]&0x0300f000u));
    if(code==0x9f){assert(in==3);regs[22]=ident;}
    else if(code==0x5a){
        assert(in==8 && regs[7]==((23u<<26)|7) && (regs[6]&(1u<<29)));
        assert(regs[1]<=sizeof sfdp-8 && !(regs[1]&3u));
        memcpy(regs+22,(unsigned char *)sfdp+regs[1],8);
    }else if(code==5){assert(in==1);regs[22]=wel*2u|protected;}
    else if(code==0x35){assert(in==1);regs[22]=0;}
    else if(code==6){if(!write_enable_fail)wel=1;}
    else if(code==0x20 || code==2){
        assert(wel && regs[0]==((1u<<18)|(1u<<17)) && regs[7]==(23u<<26));
        assert(regs[1]>=0xe40000u && regs[1]<0xff8000u);
        assert(out==(code==2 ? 64u : 0u));
        assert(regs[0x98/4]==0x50001 && regs[0xa0/4]==0x350075);
        assert(regs[0x9c/4]==((5u<<25)|(1u<<24)|(1u<<23)|(128u<<6)|44u));
        assert(regs[0xa4/4]==0x7a00e0 && ((regs[3]>>2)&1023)==8);
        /* At 20 MHz, require 64 us program and 512 us erase resume intervals. */
        assert(((regs[3]>>12)&1023)==(code==2 ? 10u : 80u));
        assert(((spi0_fsm>>7)&4095)==31);
        if(!ignore_write){
            operation=code;remaining=duration;busy=1;
            if(out)memcpy(program_data,regs+22,out);
            return;
        }
    }else assert(!"unexpected flash command");
    regs[0]=0;
}
/** @brief Advance the modeled clock and service explicit controller requests. */
static void advance(void)
{
    ticks+=16u*10u;
    execute();
    if(busy && !(regs[0xa4/4]&1u) && !stuck) {
        if(remaining>10)remaining-=10;
        else {
            busy=wel=0;regs[0]=0;
            if(operation==2)memcpy(finish_data,program_data,sizeof finish_data);
        }
    }
}
/** @brief Expose controller and shared-clock registers. */
static volatile uint32_t *reg(uintptr_t at)
{
    execute();
    if(at==0x20587064u)return &clock_root;
    if(at==0x20500010u)return &spi0_ctrl;
    if(at==0x20500054u)return &spi0_fsm;
    if(at==0x20586124u)return &ibus_timeout;
    if(at==0x20586128u)return &dbus_timeout;
    assert(at>=0x20501000u && at<0x20501100u && !(at&3u));
    return &regs[(at-0x20501000u)/4];
}
#define TIKU_S31_STORAGE_ASYNC_ENABLE 1
#define TIKU_S31_FLASH_REG(a) (*reg(a))
#include <arch/esp32s31/tiku_storage_async_io.c>

/** @brief Model cached execution while the controller arbitrates a pending write:
 *  a read waiting on the suspend outlasts the core's bus timeout at 160 MHz, so
 *  the timeouts are off while the flash is busy and on once no operation owns it. */
static void cached_read(void)
{
    execute();
    if(busy) assert(!(ibus_timeout&1u) && !(dbus_timeout&1u));
    if(!active_flash) assert(ibus_timeout==BUS_TIMEOUT && dbus_timeout==BUS_TIMEOUT);
    if(busy){
        assert((regs[0x9c/4]&0x30u)==0x20u && regs[0xa0/4]==0x350075);
        assert((regs[0xa4/4]>>16)==0x7a && (regs[0x98/4]&1u));
        if(cache_misses)regs[0xa4/4]|=1u;
    }
    ++cache_reads;
}
/** @brief Expose the wrapping crystal-derived clock with caches enabled. */
uint64_t tiku_s31_time(void){cached_read();advance();return ticks;}
/** @brief Restrict ownership to kernel foreground. */
int tiku_s31_storage_context(void){return context;}
/** @brief Record operation submission. */
tiku_nvm_mark_t tiku_nvm_meter_begin(void){return 0;}
/** @brief Charge each accepted flash command once. */
void tiku_nvm_meter_end(tiku_nvm_mark_t mark,uint32_t n,uint32_t p,uint32_t e)
{(void)mark;bytes+=n;programs+=p;erases+=e;++meter_ends;}
/** @brief Observe completed flash progress. */
void tiku_watchdog_kick(void){++watchdogs;}
/** @brief Capture controller/progress failure without returning success. */
void tiku_s31_fatal(const char *reason){fatal_reason=reason;++fatals;longjmp(fatal_jump,1);}

/** @brief Reset all model state and install a JEDEC SFDP basic-parameter table. */
static void reset(void)
{
    memset(regs,0,sizeof regs);memset(sfdp,0,sizeof sfdp);
    sfdp[0]=0x50444653;sfdp[1]=0xff000106;sfdp[2]=0x10010600;sfdp[3]=0xff000030;
    sfdp[(0x30+44)/4]=0x3576a1cc;sfdp[(0x30+48)/4]=0x757a757a;
    ident=0x184046;context=1;busy=wel=0;active_flash=NULL;profile_valid=0;
    duration=2400;remaining=0;hold_suspended=ignore_write=stuck=protected=write_enable_fail=0;
    commands=erases=programs=bytes=meter_ends=watchdogs=cache_reads=software_resumes=0;
    cache_misses=1;clock_root=0;spi0_ctrl=0x1234;spi0_fsm=0x5678;
    ibus_timeout=dbus_timeout=BUS_TIMEOUT;
    regs[1]=0x123456;regs[2]=0x4321;regs[6]=0x200;regs[7]=3;regs[8]=4;regs[9]=5;regs[10]=6;
}
/** @brief Drain a flash operation while requiring cached reads and bounded callback work. */
static int drain(tiku_s31_flash_t *op,unsigned kind,void *data)
{
    unsigned turns=0;int rc;
    do{
        uint64_t start=ticks;cached_read();
        rc=tiku_s31_storage_slice(op,kind,0xe40000,data,kind==2?64:0,1);
        assert(ticks-start<16000u && ++turns<2000);
    }while(rc==1);
    return rc;
}
/** @brief Exercise admitted commands, automatic suspension, ownership, faults and restoration. */
int main(void)
{
    uint32_t data[16];for(unsigned i=0;i<16;i++)data[i]=0x12340000u+i;
    for(unsigned kind=1;kind<=2;kind++){
        reset();ticks=UINT32_MAX*16ULL-2000;tiku_s31_flash_t op={0},other={0};
        uint32_t saved[15];for(unsigned i=0;i<15;i++)saved[i]=TIKU_S31_FLASH_REG(saved_registers[i]);
        uint64_t start=ticks;
        assert(tiku_s31_storage_slice(&op,kind,0xe40000,data,kind==2?64:0,0)==1);
        assert(ticks-start<16000 && tiku_s31_storage_io_busy());
        assert(tiku_s31_storage_slice(&other,kind,0xe40000,data,kind==2?64:0,0)==-1);
        assert(tiku_s31_storage_slice(&op,kind,0xe41000,data,kind==2?64:0,1)==-1);
        other=op;
        assert(tiku_s31_storage_slice(&other,kind,0xe40000,data,kind==2?64:0,1)==-1);
        cached_read();
        unsigned before=remaining;
        for(unsigned i=0;i<5;i++){cached_read();advance();}
        assert(remaining==before && !software_resumes);
        hold_suspended=1;
        for(unsigned i=0;i<5;i++)assert(tiku_s31_storage_slice(&op,kind,0xe40000,data,kind==2?64:0,1)==1);
        hold_suspended=0;assert(!drain(&op,kind,data));
        assert(!tiku_s31_storage_io_busy() && !busy && !wel && !op.operation);
        for(unsigned i=0;i<15;i++)assert(saved[i]==TIKU_S31_FLASH_REG(saved_registers[i]));
        assert(erases==(kind==1) && programs==(kind==2) && bytes==(kind==2?64u:0u));
        if(kind==2)assert(!memcmp(data,finish_data,sizeof data));
        assert(cache_reads && software_resumes && watchdogs==1);
    }
    static const uint32_t roots[]={0,1u<<5,2u<<5};
    static const uint32_t clocks[]={0x10001,0x170b17,0xf070f};
    for(unsigned i=0;i<3;i++){
        reset();clock_root=roots[i];tiku_s31_flash_t op={0};
        assert(tiku_s31_storage_slice(&op,1,0xe40000,NULL,0,0)==1);
        assert(regs[0x14/4]==clocks[i]);assert(!drain(&op,1,NULL));
    }
    for(unsigned fault=0;fault<13;fault++){
        reset();tiku_s31_flash_t op={0};
        if(fault==0)ident=0x184020;
        if(fault==1)sfdp[0]^=1;
        if(fault==2)sfdp[1]|=0x080000;
        if(fault==3)sfdp[2]&=~0xff000000u;
        if(fault==4)sfdp[(0x30+44)/4]|=0x80000000;
        if(fault==5)sfdp[(0x30+48)/4]^=1;
        if(fault==6)sfdp[(0x30+44)/4]|=3u<<29;
        if(fault==7)sfdp[(0x30+44)/4]|=15u<<20;
        if(fault==8)clock_root=3u<<5;
        if(fault==9)sfdp[(0x30+44)/4]=(sfdp[(0x30+44)/4]&~(127u<<24))|(2u<<29)|(7u<<24);
        if(fault==10)sfdp[(0x30+44)/4]=(sfdp[(0x30+44)/4]&~(127u<<13))|(2u<<18)|(7u<<13);
        if(fault==11)sfdp[(0x30+44)/4]|=1u<<9;
        if(fault==12)sfdp[(0x30+44)/4]=(sfdp[(0x30+44)/4]&~(15u<<20))|(8u<<20);
        assert(tiku_s31_storage_slice(&op,1,0xe40000,NULL,0,0)==-1 && !erases && !active_flash);
    }
    reset();tiku_s31_flash_t op={0};protected=4;
    assert(tiku_s31_storage_slice(&op,1,0xe40000,NULL,0,0)==-1 && !erases);
    reset();write_enable_fail=1;
    assert(tiku_s31_storage_slice(&op,1,0xe40000,NULL,0,0)==-1 && !erases);
    reset();memset(&op,0,sizeof op);
    assert(tiku_s31_storage_slice(&op,1,0xe40000,NULL,0,0)==1);
    cache_misses=0;
    if(setjmp(fatal_jump))assert(!"active flash poll issued an overlapping command");
    assert(tiku_s31_storage_slice(&op,1,0xe40000,NULL,0,1)==1 && busy && !software_resumes);
    cache_misses=1;assert(!drain(&op,1,NULL));
    reset();ignore_write=1;
    assert(tiku_s31_storage_slice(&op,1,0xe40000,NULL,0,0)==1 && drain(&op,1,NULL)==-1);
    reset();context=0;
    assert(tiku_s31_storage_slice(&op,1,0xe40000,NULL,0,0)==-1 && !commands);
    reset();assert(tiku_s31_storage_slice(&op,1,0xe3f000,NULL,0,0)==-1 && !commands);
    assert(tiku_s31_storage_slice(&op,2,0xe400fc,data,64,0)==-1 && !commands);
    assert(tiku_s31_storage_slice(&op,2,0xe40000,data,68,0)==-1 && !commands);
    reset();stuck=1;
    if(!setjmp(fatal_jump)){tiku_s31_storage_slice(&op,1,0xe40000,NULL,0,0);assert(!"unsafe return");}
    assert(!strcmp(fatal_reason,"flash command timeout"));
    reset();memset(&op,0,sizeof op);duration=1000000;
    assert(tiku_s31_storage_slice(&op,2,0xe40000,data,64,0)==1);ticks+=1600000;
    if(!setjmp(fatal_jump)){tiku_s31_storage_slice(&op,2,0xe40000,data,64,1);assert(!"unsafe return");}
    assert(!strcmp(fatal_reason,"flash progress timeout") && fatals==2);
    puts("Flash: SFDP, auto suspend, owner resume, ownership, bounded polling and controller restoration pass");
    return 0;
}
