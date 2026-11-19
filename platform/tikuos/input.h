/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * input.h - firmware touch lifecycle without toolkit types.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_OS_INPUT_H
#define SURFACE_OS_INPUT_H
#include "../../shell/touch_report.h"
/** @brief Open or borrow the shared bus and probe touch; return 1 on success.
 * @note bus_owned is caller storage initialized to zero; close releases an owned bus.
 */
int surface_os_input_open(unsigned *bus_owned);
/** @brief Copy a public touch sample; errors return zero and set cancellation. */
int surface_os_input_poll(surface_touch_report_t *);
/** @brief Release a bus opened by this input session, then clear ownership. */
void surface_os_input_close(unsigned *bus_owned);
#endif
