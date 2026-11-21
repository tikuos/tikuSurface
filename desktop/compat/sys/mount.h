/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * sys/mount.h - the mount calls the volume kit names, for TikuOS.
 *
 * TikuOS mounts its volumes itself; desktop/posix.c refuses both calls.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_COMPAT_SYS_MOUNT_H
#define SURFACE_COMPAT_SYS_MOUNT_H
int mount(const char *source, const char *target, const char *type,
          unsigned long flags, const void *data);
int umount(const char *target);
#endif
