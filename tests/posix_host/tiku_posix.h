/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_posix.h - TikuOS compat/posix's file calls, declared for a host build
 * of desktop/posix.c; the native build uses TikuOS's own header, and the
 * test defines these over a fixed namespace.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_HOST_TIKU_POSIX_H
#define SURFACE_HOST_TIKU_POSIX_H
#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
int tiku_posix_open(const char *path, int flags);
ssize_t tiku_posix_read(int fd, void *buf, size_t n);
off_t tiku_posix_lseek(int fd, off_t offset, int whence);
int tiku_posix_close(int fd);
int tiku_posix_fcntl(int fd, int cmd);
int tiku_posix_fstat(int fd, struct stat *st);
int tiku_posix_stat(const char *path, struct stat *st);
int tiku_posix_is_open(int fd);
DIR *tiku_posix_opendir(const char *path);
struct dirent *tiku_posix_readdir(DIR *dir);
void tiku_posix_rewinddir(DIR *dir);
int tiku_posix_closedir(DIR *dir);
int tiku_posix_is_dir_stream(const DIR *dir);
uint8_t tiku_posix_authority_set(uint8_t cap);
#endif
