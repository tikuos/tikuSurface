/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_i2c_budget.c - S31 shared I2C deadline across controller batches.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static uint64_t ticks;
static unsigned stuck_sync, raw, control;
/** @brief Advance the independent hardware clock by ten microseconds. */
uint64_t tiku_s31_time(void) { ticks = (ticks + 160) & ((1ULL << 52) - 1); return ticks; }
/** @brief Read only the controller fields used by deadline and completion checks. */
static uint32_t read_register(uintptr_t address)
{ return address == 0x20385020u ? raw : address == 0x20385004u ? control : 0; }
/** @brief Model controller sync acknowledgement independently of transfer completion. */
static void write_register(uintptr_t address, uint32_t value)
{ if (address == 0x20385004u) control = stuck_sync ? value : value & ~(1u << 11); }
void tiku_atomic_enter(void);
void tiku_atomic_exit(void);
#define REG_READ(a) read_register(a)
#define REG_WRITE(a,v) write_register(a,v)
#define TIKU_BOARD_I2C_SDA_PIN 7
#define TIKU_BOARD_I2C_SCL_PIN 8
#define TIKU_I2C_BUDGET_ENABLE 1
#include <arch/esp32s31/tiku_i2c_arch.c>
/** @brief Exercise success, shared exhaustion, frozen sync, nesting and clock wrap. */
int main(void)
{
    assert(tiku_i2c_arch_budget_begin(0)==TIKU_I2C_ERR_PARAM);
    assert(tiku_i2c_arch_budget_begin(50001)==TIKU_I2C_ERR_PARAM);
    assert(!tiku_i2c_arch_budget_begin(3500));
    assert(tiku_i2c_arch_budget_begin(10)==TIKU_I2C_ERR_BUSY);
    raw=1u<<7; assert(!run_batch(1));
    raw=0; assert(run_batch(1)==TIKU_I2C_ERR_TIMEOUT);
    assert(ticks>=3500u*16u && ticks<=3540u*16u);
    raw=1u<<7; assert(run_batch(1)==TIKU_I2C_ERR_TIMEOUT);
    tiku_i2c_arch_budget_end(); assert(!run_batch(1));
    ticks=0; stuck_sync=1; assert(!tiku_i2c_arch_budget_begin(100));
    assert(run_batch(1)==TIKU_I2C_ERR_TIMEOUT && ticks<=140u*16u);
    tiku_i2c_arch_budget_end(); stuck_sync=0;
    ticks=(1ULL<<52)-800; raw=0;
    assert(!tiku_i2c_arch_budget_begin(100));
    assert(run_batch(1)==TIKU_I2C_ERR_TIMEOUT && ticks<150u*16u);
    tiku_i2c_arch_budget_end();
    puts("I2C: shared deadline, sync stall, nesting and clock wrap pass");
    return 0;
}
