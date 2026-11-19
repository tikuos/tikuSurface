/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * input.c - serialized public touch provider binding.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "input.h"
#include "runtime.h"
#include <interfaces/input/tiku_touch.h>
#include <interfaces/bus/tiku_i2c_bus.h>
#include <string.h>

int surface_os_input_open(unsigned *owned)
{
    if (!owned || *owned || !surface_os_in_owner()) return 0;
    if (!tiku_i2c_get_config()) {
        const tiku_i2c_config_t config = {TIKU_I2C_SPEED_FAST};
        if (tiku_i2c_init(&config)) return 0;
        *owned = 1;
    }
    if (!tiku_touch_probe()) return 1;
    surface_os_input_close(owned);
    return 0;
}

int surface_os_input_poll(surface_touch_report_t *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    tiku_touch_report_t report;
    if (!surface_os_in_owner() || tiku_i2c_budget_begin(3500)) { out->cancel = 1; return 0; }
    int result = tiku_touch_poll(&report);
    tiku_i2c_budget_end();
    if (result) { out->cancel = 1; return 0; }
    out->cancel = report.cancel;
    out->count = report.count;
    if (out->count > SURFACE_TOUCH_CONTACTS) { out->cancel = 1; return 0; }
    for (unsigned i = 0; i < out->count; ++i) {
        out->points[i].x = report.contacts[i].x;
        out->points[i].y = report.contacts[i].y;
        out->points[i].id = report.contacts[i].id;
    }
    return 1;
}

void surface_os_input_close(unsigned *owned)
{
    if (owned && *owned && surface_os_in_owner()) { tiku_i2c_close(); *owned = 0; }
}
