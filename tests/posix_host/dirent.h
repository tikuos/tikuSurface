/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * dirent.h - newlib's directory stream types, for a host build of desktop/posix.c.
 *
 * The layer embeds a DIR in its own stream structure, which needs the
 * complete type newlib has; the host C library's DIR is opaque.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_HOST_DIRENT_H
#define SURFACE_HOST_DIRENT_H
#include <sys/types.h>
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
/** @brief newlib's stream header; the layer leaves it zero. */
typedef struct { int dd_fd; } DIR;
/** @brief One directory entry as readdir() returns it. */
struct dirent { ino_t d_ino; unsigned char d_type; char d_name[256]; };
DIR *opendir(const char *path);
struct dirent *readdir(DIR *dp);
void rewinddir(DIR *dp);
int closedir(DIR *dp);
#endif
