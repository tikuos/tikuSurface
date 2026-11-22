/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * rename.h - the public names of desktop/posix.c, prefixed for a host build.
 *
 * Included ahead of the layer and its test, so their calls reach the layer
 * and the host's C library and sanitizer keep their own definitions.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_HOST_RENAME_H
#define SURFACE_HOST_RENAME_H
#define access pxaccess
#define chdir pxchdir
#define chmod pxchmod
#define chown pxchown
#define _close px_close
#define closedir pxclosedir
#define execlp pxexeclp
#define execvp pxexecvp
#define _exit px_exit
#define fchmod pxfchmod
#define _fcntl px_fcntl
#define _fstat px_fstat
#define fstatvfs pxfstatvfs
#define fsync pxfsync
#define ftruncate pxftruncate
#define getcwd pxgetcwd
#define geteuid pxgeteuid
#define getloadavg pxgetloadavg
#define _getpid px_getpid
#define _isatty px_isatty
#define lchown pxlchown
#define _link px_link
#define _lseek px_lseek
#define lstat pxlstat
#define __malloc_lock px__malloc_lock
#define __malloc_unlock px__malloc_unlock
#define _mkdir px_mkdir
#define mkdir pxmkdir
#define mkdtemp pxmkdtemp
#define mkstemp pxmkstemp
#define mkstemps pxmkstemps
#define mount pxmount
#define nanosleep pxnanosleep
#define _open px_open
#define opendir pxopendir
#define pread pxpread
#define pthread_equal pxpthread_equal
#define pthread_mutex_destroy pxpthread_mutex_destroy
#define pthread_mutex_init pxpthread_mutex_init
#define pthread_mutex_lock pxpthread_mutex_lock
#define pthread_mutex_trylock pxpthread_mutex_trylock
#define pthread_mutex_unlock pxpthread_mutex_unlock
#define pthread_self pxpthread_self
#define pwrite pxpwrite
#define _read px_read
#define readdir pxreaddir
#define readlink pxreadlink
#define realpath pxrealpath
#define _rename px_rename
#define rewinddir pxrewinddir
#define rmdir pxrmdir
#define _sbrk_r px_sbrk_r
#define _stat px_stat
#define statvfs pxstatvfs
#define symlink pxsymlink
#define sysconf pxsysconf
#define umask pxumask
#define umount pxumount
#define _unlink px_unlink
#define utimensat pxutimensat
#define waitpid pxwaitpid
#define _write px_write
#define environ pxenviron
#endif
