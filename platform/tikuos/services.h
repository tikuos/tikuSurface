/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * services.h - bounded, read-only connected-service presentation.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_SERVICES_H
#define SURFACE_SERVICES_H
#include <stddef.h>
/** @brief Write a terminated Wi-Fi/transfer label from typed snapshots; foreground owner only. */
void surface_os_services(char *, size_t);
#endif
