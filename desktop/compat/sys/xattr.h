/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * sys/xattr.h - extended attributes for the desktop on TikuOS.
 *
 * desktop/posix.c answers them, with Linux's signatures, for Stomme's state
 * kit built with TIKU_STATE_XATTR.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_COMPAT_SYS_XATTR_H
#define SURFACE_COMPAT_SYS_XATTR_H
#include <sys/types.h>
#define XATTR_CREATE  1
#define XATTR_REPLACE 2
ssize_t getxattr(const char *path, const char *name, void *value, size_t size);
int setxattr(const char *path, const char *name, const void *value, size_t size, int flags);
ssize_t listxattr(const char *path, char *list, size_t size);
int removexattr(const char *path, const char *name);
ssize_t fgetxattr(int fd, const char *name, void *value, size_t size);
int fsetxattr(int fd, const char *name, const void *value, size_t size, int flags);
int fremovexattr(int fd, const char *name);
#endif
