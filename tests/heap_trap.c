/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * heap_trap.c - refuse heap calls outside the host allocation provider.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host.h"
#include <assert.h>
#include <stddef.h>
/** @brief Linker entry points for the actual host heap implementation. */
void *__real_malloc(size_t);
void __real_free(void *);
/** @brief Permit allocations inside the explicit host provider. */
void *__wrap_malloc(size_t bytes) { assert(surface_host_allocating); return __real_malloc(bytes); }
/** @brief Permit releases inside the explicit host provider. */
void __wrap_free(void *pointer) { assert(surface_host_allocating); __real_free(pointer); }
/** @brief Refuse unbound zeroed allocations. */
void *__wrap_calloc(size_t count, size_t bytes) { (void)count; (void)bytes; assert(0); return NULL; }
/** @brief Refuse unbound resizing. */
void *__wrap_realloc(void *pointer, size_t bytes) { (void)pointer; (void)bytes; assert(0); return NULL; }
/** @brief Refuse unbound string allocation. */
char *__wrap_strdup(const char *text) { (void)text; assert(0); return NULL; }
