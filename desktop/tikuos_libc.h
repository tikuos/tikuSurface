/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tikuos_libc.h - declarations the desktop sources use that newlib omits.
 *
 * Included ahead of every desktop source compiled for TikuOS.  The functions
 * are defined by desktop/posix.c; newlib's own headers stay unchanged.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_TIKUOS_LIBC_H
#define SURFACE_TIKUOS_LIBC_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

struct stat;
int lstat(const char *path, struct stat *st);
char *strcasestr(const char *haystack, const char *needle);
int getloadavg(double loadavg[], int count);
char *realpath(const char *path, char *resolved);
int clock_gettime(clockid_t clock, struct timespec *now);
int nanosleep(const struct timespec *request, struct timespec *remain);
#endif
