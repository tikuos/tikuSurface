/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_transport.c - CID-bound asynchronous media sessions and extent checks.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define TIKU_S31_SD_ASYNC_ENABLE 1
#include <arch/esp32s31/tiku_mfs_block_arch.c>
static unsigned opened,closed,begins,reads,refuse,fail_io,wrong_card;
static uint32_t physical;
static void *destination;
int tiku_s31_sd_open_begin(tiku_s31_sd_async_t *op,tiku_s31_sdcard_t *card)
{
    assert(!opened);opened=1;++begins;*op=(tiku_s31_sd_async_t){.state=1,.card=card};
    *card=(tiku_s31_sdcard_t){.sectors=32768};card->cid[0]=(uint8_t)(wrong_card?2:1);return 1;
}
int tiku_s31_sd_clock_begin(tiku_s31_sd_async_t *op,tiku_s31_sdcard_t *card,uint32_t hz)
{ assert(opened && card==op->card && hz==4000000);op->state=2;return 1; }
int tiku_s31_sd_read_begin(tiku_s31_sd_async_t *op,tiku_s31_sdcard_t *card,uint32_t lba,void *out)
{ assert(opened && card==op->card);op->state=3;physical=lba;destination=out;++reads;return 1; }
int tiku_s31_sd_poll(tiku_s31_sd_async_t *op)
{
    assert(opened && op->state);
    if(refuse)return TIKU_S31_SD_INVALID;
    if(fail_io){op->state=opened=0;++closed;return TIKU_S31_SD_IO;}
    if(op->state==3)memset(destination,(uint8_t)physical,512);
    op->state=0;return 0;
}
int tiku_s31_sdcard_close(tiku_s31_sdcard_t *card)
{ (void)card;assert(opened);opened=0;++closed;return 0; }
int tiku_s31_sd_cancel(tiku_s31_sd_async_t *op)
{ assert(opened && op->state);opened=op->state=0;++closed;return 0; }
int tiku_s31_sdcard_read(uint32_t lba,void *out,void *card)
{ (void)lba;(void)out;(void)card;assert(0);return -1; }
int tiku_s31_sdcard_write(uint32_t lba,const void *in,tiku_s31_sdcard_t *card)
{ (void)lba;(void)in;(void)card;assert(0);return -1; }
int tiku_s31_sdcard_sync(tiku_s31_sdcard_t *card) { (void)card;assert(0);return -1; }
void tiku_watchdog_arch_kick(void) { assert(0); }
int main(void)
{
    tiku_s31_mfs_block_t backend;tiku_s31_mfs_async_t op;
    const uint32_t cid[4]={0,0,0,0x01000000};
    assert(!tiku_s31_mfs_block_init(&backend,100,1000,cid,4000000));
    uint8_t output[4096];memset(output,0xaa,sizeof output);
    assert(tiku_s31_mfs_async_acquire(&op,&backend)==1 && backend.active==2);
    refuse=1;assert(tiku_s31_mfs_async_poll(&op)==TIKU_MFS_ARG && backend.active==2 && opened);refuse=0;
    assert(tiku_s31_mfs_async_poll(&op)==1 && !tiku_s31_mfs_async_poll(&op));
    assert(backend.block.read(&backend,0,1,output)==TIKU_MFS_ARG);
    assert(tiku_s31_mfs_block_release(&backend)==TIKU_MFS_ARG);
    assert(tiku_s31_mfs_async_read(&op,995,8,output)==TIKU_MFS_ARG);
    assert(tiku_s31_mfs_async_read(&op,0,9,output)==TIKU_MFS_ARG);
    assert(tiku_s31_mfs_async_read(&op,7,8,output)==1 && physical==107);
    for(unsigned i=0;i<8;i++)assert(tiku_s31_mfs_async_poll(&op)==(i==7?0:1));
    for(unsigned i=0;i<4096;i++)assert(output[i]==107+i/512);
    assert(begins==1 && reads==8 && opened && !closed);
    assert(tiku_s31_mfs_async_read(&op,999,1,output)==1 && physical==1099);
    assert(!tiku_s31_mfs_async_poll(&op) && !tiku_s31_mfs_async_close(&op) && !backend.active && !opened);
    wrong_card=1;
    assert(tiku_s31_mfs_async_acquire(&op,&backend)==1);
    assert(tiku_s31_mfs_async_poll(&op)==TIKU_MFS_STALE && !backend.active && !opened);wrong_card=0;
    assert(tiku_s31_mfs_async_acquire(&op,&backend)==1 && !tiku_s31_mfs_async_close(&op));
    assert(!backend.active && !opened);
    assert(tiku_s31_mfs_async_acquire(&op,&backend)==1);fail_io=1;
    assert(tiku_s31_mfs_async_poll(&op)==TIKU_MFS_IO && !backend.active && !opened);fail_io=0;
    assert(tiku_s31_mfs_async_acquire(&op,&backend)==1);
    assert(tiku_s31_mfs_async_poll(&op)==1 && !tiku_s31_mfs_async_poll(&op));
    assert(tiku_s31_mfs_async_read(&op,1,1,output)==1 && !tiku_s31_mfs_async_close(&op));
    assert(!backend.active && !opened);
    backend.first=32767;
    assert(tiku_s31_mfs_async_acquire(&op,&backend)==1);
    assert(tiku_s31_mfs_async_poll(&op)==TIKU_MFS_STALE && !backend.active && !opened);
    puts("Media transport: retained read session, CID/extent checks, cancellation and refusal ownership pass");
}
