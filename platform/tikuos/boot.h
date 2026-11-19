/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * boot.h - application composition entry points without SDK or kernel types.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_BOOT_H
#define SURFACE_BOOT_H
/** @brief Start the PSRAM UI runtime and its timer process; zero means refusal. */
int tiku_application_start(void);
/** @brief Stop the UI runtime; timer work continues until ownership retires. */
void tiku_application_stop(void);
struct tiku_vfs_node;
/** @brief Return the static product diagnostic subtree for boot assembly. */
const struct tiku_vfs_node *tiku_application_vfs_get(void);
#endif
