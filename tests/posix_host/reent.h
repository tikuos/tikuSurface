/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * reent.h - newlib's reentrancy names, for a host build of desktop/posix.c.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_HOST_REENT_H
#define SURFACE_HOST_REENT_H
#include <sys/types.h>
/** @brief The layer reads only the error field of a reentrancy structure. */
struct _reent { int _errno; };
/** @brief newlib's signed size, which the layer's system calls return. */
typedef ssize_t _ssize_t;
#endif
