/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_input_binding.c - public touch and bus ownership refusal checks.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "input.h"
#include <interfaces/bus/tiku_i2c_bus.h>
#include <interfaces/input/tiku_touch.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned budget;
int tiku_i2c_budget_begin(uint32_t us) { assert(us == 3500 && !budget); budget = 1; return 0; }
void tiku_i2c_budget_end(void) { assert(budget); budget = 0; }
static int owner = 1, opened, fail, probes, closes;
static const tiku_i2c_config_t config = {TIKU_I2C_SPEED_FAST};
int surface_os_in_owner(void) { return owner; }
const tiku_i2c_config_t *tiku_i2c_get_config(void) { return opened ? &config : NULL; }
int tiku_i2c_init(const tiku_i2c_config_t *c) { assert(c->speed == 1); if (fail == 1) return -1; opened = 1; return 0; }
void tiku_i2c_close(void) { opened = 0; ++closes; }
int tiku_i2c_write(uint8_t addr, const uint8_t *data, uint16_t length)
{ assert(addr == 0x14 && length == 3 && data[0] == 0x81 && data[1] == 0x4e && !data[2]); return fail == 4 ? -1 : 0; }
int tiku_i2c_write_read(uint8_t addr, const uint8_t *tx, uint16_t n, uint8_t *rx, uint16_t length)
{
    assert(addr == 0x14 && n == 2 && tx[0] == 0x81);
    memset(rx, 0, length);
    if (tx[1] == 0x40) { ++probes; if (fail == 2) return -1; memcpy(rx,"1158",4); return 0; }
    assert(tx[1] == 0x4e);
    if (fail == 3) return -1;
    if (length == 1) { rx[0] = 1; return 0; }
    assert(length == 11);
    const uint8_t wire[] = {1, 3, 0x34, 0x12, 0x78, 0x56, 0, 0, 0, 0, 0xe8};
    memcpy(rx,wire,sizeof wire);
    if (fail == 5) rx[10] ^= 1;
    return 0;
}
/** @brief Verify fresh/borrowed buses, identity refusal and independent wire decoding. */
int main(void)
{
    unsigned owned = 0;
    surface_touch_report_t report;
    owner = 0; assert(!surface_os_input_open(&owned) && !probes);
    assert(!surface_os_input_poll(&report) && report.cancel);
    owner = 1;
    for (fail = 1; fail <= 2; ++fail) assert(!surface_os_input_open(&owned) && !owned && !opened);
    fail = 0; assert(surface_os_input_open(&owned) && owned && opened);
    assert(surface_os_input_poll(&report) && report.count == 1 && !report.cancel);
    assert(report.points[0].x == 0x1234 && report.points[0].y == 0x5678 && report.points[0].id == 3);
    for (fail = 3; fail <= 5; ++fail) assert(!surface_os_input_poll(&report) && report.cancel && !report.count);
    surface_os_input_close(&owned); assert(!owned && !opened);
    fail = 0; opened = 1; int before = closes;
    assert(surface_os_input_open(&owned) && !owned);
    surface_os_input_close(&owned); assert(opened && closes == before);
    assert(tiku_touch_poll(NULL) < 0);
    puts("input: public reports, wire validation and bus ownership pass");
    return 0;
}
