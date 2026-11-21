/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * device_io.h - bounded local telemetry without SDK types.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_DEVICE_IO_H
#define SURFACE_DEVICE_IO_H
#include <stddef.h>
#include "process_contract.h"
/** @brief Copy an admitted local observation; owner context, at most 128 bytes. */
int surface_os_device_read(void *,const char *,char *,size_t);
/** @brief Copy slot 0 through 7; return 1 present, 0 absent, or a negative error. */
int surface_os_process_read(void *,unsigned,surface_process_info_t *);
#endif
