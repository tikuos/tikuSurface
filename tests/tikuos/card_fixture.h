/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * card_fixture.h - an SD card on the host: MFS on a RAM disk behind the
 * media service and the desktop's bridge, for tests of the file layer.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_CARD_FIXTURE_H
#define SURFACE_CARD_FIXTURE_H
#include <stddef.h>
#include <stdint.h>
#include "../../platform/tikuos/media_io.h"
/** @brief Format an empty card and mount it behind a fresh bridge. */
void card_fixture_reset(void);
/** @brief Run one bridge request to its end, as the kernel's turns would. */
void card_fixture_request(surface_media_io_t *io);
/** @brief Write object @p name with @p len bytes directly on the card. */
void card_fixture_put(const char *name, const void *bytes, size_t len);
/** @brief Read object @p name directly from the card into @p out; its
 *  length, or -1 when it is not there. */
long card_fixture_get(const char *name, void *out, size_t max);
/** @brief Leave an unfinished replacement of object @p name, which refuses
 *  renaming it until the upload is resumed or deleted. */
void card_fixture_pending(const char *name);
/** @brief The card's visible objects. */
unsigned card_fixture_count(void);
/** @brief Mount the card again from its sectors, as a restart does. */
void card_fixture_remount(void);
/** @brief Fail every sector write from now on (1) or none (0). */
void card_fixture_fail_writes(int on);
/** @brief Bridge requests run so far, by operation. */
unsigned card_fixture_requests(unsigned operation);
/** @brief Remove object @p name directly on the card, as another client. */
void card_fixture_delete(const char *name);
/** @brief The bridge's count of changes it did not make, after an idle
 *  turn, as the desktop reads it; a remount counts as one. */
unsigned card_fixture_foreign(void);
#endif
