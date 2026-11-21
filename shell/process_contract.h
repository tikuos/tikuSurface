/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * process_contract.h - copied process observations without kernel addresses.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_PROCESS_CONTRACT_H
#define SURFACE_PROCESS_CONTRACT_H
#include <stdint.h>
/** @brief One registry slot; counters are observations and may wrap independently. */
typedef struct {
    char name[32],state[16];
    uint32_t sram,nvm,uptime,wakes,events,restarts;
} surface_process_info_t;
#endif
