/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * bridge_os.h - firmware calls the desktop runtime and thread use.
 *
 * Defined by platform/tikuos/desktop_bindings.c with TikuOS headers; this
 * header names no firmware or toolkit types, so both sides include it.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_DESKTOP_BRIDGE_OS_H
#define SURFACE_DESKTOP_BRIDGE_OS_H
#include <stddef.h>
#include <stdint.h>

/** @brief Run a short callback in kernel context and return its result. */
int surface_os_desktop_call(int (*fn)(void *), void *context);
/** @brief Start the desktop worker on a caller-owned stack; 0 on success.
 *  @note Kernel owner only. */
int surface_os_desktop_start(void (*entry)(void *), void *arg, void *stack, size_t bytes);
/** @brief On the desktop thread: wait until @p ready returns nonzero, a wake
 *         arrives or @p timeout_us passes; zero waits without a limit.
 *  @p ready runs with interrupts masked. */
void surface_os_desktop_wait(int (*ready)(void *), void *context, uint64_t timeout_us);
/** @brief Wake the desktop thread from its wait; callable from the kernel. */
void surface_os_desktop_wake(void);
/** @brief Ask the kernel runtime for an owner turn; callable from the desktop thread. */
void surface_os_desktop_kick(void);
/** @brief End the desktop thread; never returns. */
void surface_os_desktop_exit(void);
/** @brief Write console text.  @note Kernel owner only. */
void surface_os_desktop_print(const void *bytes, size_t length);
/** @brief The PSRAM section holding the desktop's zero-initialized statics. */
void surface_os_desktop_statics(void **base, size_t *bytes);
/** @brief Sample the desktop worker during a kernel dispatch. Returns the
 *  count of saved and candidate code addresses in @p out, or zero. */
size_t surface_os_desktop_sample(uintptr_t *out, size_t capacity, size_t scan_words);
/** @brief Read and hash the named file on the desktop worker. */
void surface_desktop_hash_file(const char *path);
/** @brief Mount the SD card's native media volume without writing to it;
 *  zero when mounted.  Kernel context only. */
int surface_os_card_mount(void);
/** @brief Whether the caller is the desktop thread. */
int surface_os_in_desktop(void);
/** @brief Whether the desktop thread is blocked, in its wait or on a lock;
 *         callable from the kernel. */
int surface_os_desktop_blocked(void);
/** @brief The kernel wall clock's standing: never set, set during this boot,
 *         or set before a restart and carried over it. */
enum {
    SURFACE_CLOCK_UNSET = 0,
    SURFACE_CLOCK_SET,
    SURFACE_CLOCK_RESUMED
};
/** @brief The wall clock's SURFACE_CLOCK_* standing, from tiku_rtc_state();
 *         callable from the desktop thread and the kernel. */
int surface_os_clock_state(void);
/** @brief Give the desktop's job threads @p bytes at @p memory for their
 *         stacks; 0, or -1 when refused.  Kernel owner only, before the
 *         desktop thread starts. */
int surface_os_desktop_jobs_memory(void *memory, size_t bytes);
/** @brief Give TikuOS's compat/posix store group @p bytes at @p memory for
 *         the stages of the settings files it writes; 0, or -1 when refused.
 *         Kernel owner only, before the desktop thread starts. */
int surface_os_desktop_store_memory(void *memory, size_t bytes);
/** @brief Give the desktop's calls into the kernel's VFS their authority:
 *  system settings and the file store (TIKU_VFS_CAP_SYS | TIKU_VFS_CAP_FS).
 *  @return 0, or -1 off the kernel thread */
int surface_os_desktop_authority(void);
/** @brief Make @p bytes at @p memory the C library's heap, through TikuOS's
 *         compat/posix heap group; 0, or -1 when refused.  Kernel owner
 *         only, before anything allocates. */
int surface_os_desktop_heap_memory(void *memory, size_t bytes);
/** @brief Heap bytes below the break, bytes given, growths refused and the
 *         largest of them; any argument may be NULL.  Callable from the
 *         kernel and the desktop's threads; it does not wait. */
void surface_os_desktop_heap_use(size_t *used, size_t *total, uint32_t *refused, size_t *largest);
/** @brief Heap bytes the C library's allocator has handed out and not taken
 *         back, by its own count; the desktop's threads only. */
size_t surface_os_desktop_heap_live(void);
/** @brief PSRAM's size, the bytes below the kernel's PSRAM tier, the tier's
 *         size and its bytes not reserved; 0, or -1 without a tier.  Kernel
 *         owner only. */
int surface_os_psram_extent(size_t *size, size_t *below, size_t *tier, size_t *spare);
/** @brief Mutexes and condition variables in use, and those refused for a
 *         full table; a refused lock leaves its caller unlocked. */
void surface_os_desktop_lock_use(unsigned *mutexes, unsigned *conds, unsigned *refused);
/** @brief Stack canary faults the kernel has seen on its workers, the
 *         desktop and job threads among them. */
unsigned surface_os_desktop_canary_faults(void);
/** @brief Enter and leave the interrupt-masked section; calls nest. */
void surface_os_desktop_lock(void);
void surface_os_desktop_unlock(void);
#endif
