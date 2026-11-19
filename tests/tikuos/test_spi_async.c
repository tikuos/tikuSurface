/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_spi_async.c - Independent SPI register completion, timeout and ownership checks.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t registers[256];
static uintptr_t addresses[256];
static unsigned register_count, claims;
static uint64_t ticks;
static int kernel = 1;
/** @brief Return one independent register without synthesizing hardware completion. */
static volatile uint32_t *reg(uintptr_t address)
{
    for (unsigned i=0;i<register_count;i++) if(addresses[i]==address)return &registers[i];
    assert(register_count<256); addresses[register_count]=address; return &registers[register_count++];
}
#define TIKU_S31_SPI_REG(a) (*reg(a))
#define TIKU_S31_SD_ASYNC_ENABLE 1
#include <arch/esp32s31/tiku_spi_arch.c>
volatile uint8_t tiku_s31_in_trap;
uint64_t tiku_s31_time(void) { return ticks; }
int tiku_thread_in_kernel(void) { return kernel; }
int tiku_gpio_claim(uint8_t port,uint8_t pin,const char *who)
{ (void)port;(void)pin;(void)who;++claims;return 0; }
int tiku_gpio_release(uint8_t port,uint8_t pin,const char *who)
{ (void)port;(void)pin;(void)who;assert(claims);--claims;return 0; }
/** @brief Verify asynchronous phases do not spin, leak ownership or copy unfinished input. */
int main(void)
{
    tiku_s31_spi_async_t op,other;
    const tiku_spi_config_t config={0,0,100};
    assert(tiku_s31_spi_init_begin(&op,&config)==1 && claims==3);
    for(unsigned i=0;i<100;i++)assert(tiku_s31_spi_poll(&op)==1);
    assert(!ticks && tiku_s31_spi_poll(&other)==TIKU_SPI_ERR_PARAM);
    *reg(SPI)=0; assert(!tiku_s31_spi_poll(&op));
    assert(tiku_s31_spi_clock_begin(&op,20000000)==1 && *reg(SPI+0x0c)==4097);
    assert(tiku_s31_spi_poll(&op)==1);
    *reg(SPI)=0;assert(!tiku_s31_spi_poll(&op));
    uint8_t tx[64],rx[64]; for(unsigned i=0;i<64;i++){tx[i]=(uint8_t)i;rx[i]=0xaa;}
    assert(tiku_s31_spi_begin(&op,tx,rx,65)==TIKU_SPI_ERR_PARAM);
    assert(tiku_s31_spi_begin(&op,tx,rx,64)==1);
    assert(*reg(SPI+0x98)==0x03020100 && *reg(SPI+0x1c)==511);
    assert(tiku_spi_arch_write(tx,1)==TIKU_SPI_ERR_PARAM);
    assert(tiku_s31_spi_begin(&other,tx,rx,1)==TIKU_SPI_ERR_PARAM);
    assert(tiku_s31_spi_poll(&op)==1 && rx[0]==0xaa);
    *reg(SPI)=0;assert(tiku_s31_spi_poll(&op)==1 && *reg(SPI)==(1u<<24));
    for(unsigned i=0;i<16;i++)*reg(SPI+0x98+4*i)=0x76543210;
    assert(tiku_s31_spi_poll(&op)==1 && rx[0]==0xaa);
    *reg(SPI)=0;assert(!tiku_s31_spi_poll(&op));
    for(unsigned i=0;i<64;i++)assert(rx[i]==(uint8_t)(0x76543210u>>(8*(i%4))));
    ticks=(1ULL<<52)-16000;
    assert(tiku_s31_spi_begin(&op,NULL,rx,1)==1);
    ticks=63999;assert(tiku_s31_spi_poll(&op)==1);
    ticks=64000;assert(tiku_s31_spi_poll(&op)==TIKU_SPI_ERR_TIMEOUT);
    assert(tiku_s31_spi_begin(&op,NULL,rx,1)==TIKU_SPI_ERR_PARAM);
    tiku_spi_arch_close();assert(!claims);
    assert(tiku_s31_spi_init_begin(&op,&config)==1);
    tiku_spi_arch_close();assert(!claims && tiku_s31_spi_poll(&op)==TIKU_SPI_ERR_PARAM);
    kernel=0;assert(tiku_s31_spi_init_begin(&op,&config)==TIKU_SPI_ERR_PARAM && !claims);
    kernel=1;assert(tiku_s31_spi_init_begin(&op,&config)==1);
    *reg(SPI)=0;assert(!tiku_s31_spi_poll(&op));
    assert(tiku_s31_spi_begin(&op,NULL,rx,1)==1);
    *reg(SPI)=0;assert(tiku_s31_spi_poll(&op)==1);
    ticks+=80000;assert(tiku_s31_spi_poll(&op)==TIKU_SPI_ERR_TIMEOUT);
    tiku_spi_arch_close();assert(!claims);
    puts("SPI: bounded polls, FIFO contents, exclusion, deadlines, wrap and abort pass");
}
