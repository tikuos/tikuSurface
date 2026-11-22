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
/** @brief What surface_os_input_poll() found. */
#define SURFACE_OS_INPUT_FAILED 0   /* an error: the report is empty and cancelled */
#define SURFACE_OS_INPUT_REPORT 1   /* a report, copied */
#define SURFACE_OS_INPUT_NONE   2   /* no report since the poll before */
/**
 * @brief Copy a public touch sample.
 * @return SURFACE_OS_INPUT_REPORT with the controller's report, whose count
 * is 0 once the last finger has lifted; SURFACE_OS_INPUT_NONE with an empty
 * report when the controller made none since the poll before, and the caller
 * keeps the contacts it has; SURFACE_OS_INPUT_FAILED with cancellation set.
 */
int surface_os_input_poll(surface_touch_report_t *);
/** @brief Release a bus opened by this input session, then clear ownership. */
void surface_os_input_close(unsigned *bus_owned);
#endif
