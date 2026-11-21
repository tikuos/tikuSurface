/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * services.h - bounded, read-only connected-service presentation.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_SERVICES_H
#define SURFACE_SERVICES_H
#include <stddef.h>
#if SURFACE_TRANSFERS
#include "../../shell/transfer_contract.h"
/** @brief Return the typed provisioned-transfer provider; configuration and credentials stay in TikuOS. */
surface_transfer_provider_t surface_os_transfers(void);
#endif
/** @brief Write a terminated Wi-Fi/transfer label from typed snapshots; foreground owner only. */
void surface_os_services(char *, size_t);
#endif
