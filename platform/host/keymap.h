/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * keymap.h - X11 key symbols translated into toolkit logical keys.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_HOST_KEYMAP_H
#define SURFACE_HOST_KEYMAP_H
/** @brief Map an X11 symbol and logical modifiers; zero denotes an unsupported key. */
unsigned surface_host_key(unsigned long symbol,unsigned modifiers);
#endif
