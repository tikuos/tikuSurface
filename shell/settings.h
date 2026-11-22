/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * settings.h - versioned touch settings and explicit persistence requests.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_SETTINGS_H
#define SURFACE_SETTINGS_H
#include "touch_report.h"
#define SURFACE_SETTINGS_BYTES 32u
/** @brief Settings status distinguishes defaults, pending saves and refused records. */
enum { SURFACE_SETTINGS_DEFAULT, SURFACE_SETTINGS_SAVED, SURFACE_SETTINGS_PENDING,
    SURFACE_SETTINGS_CORRUPT, SURFACE_SETTINGS_UNAVAILABLE, SURFACE_SETTINGS_FAILED,
    SURFACE_SETTINGS_INCOMPATIBLE };
/** @brief Serialized settings callbacks; save queues a copy and returns nonzero on acceptance. */
typedef struct {
    void *context;
    unsigned (*read)(void *, surface_touch_map_t *);
    int (*save)(void *, const surface_touch_map_t *);
} surface_settings_provider_t;
/** @brief Desktop palette and compatible handler choices, independent of calibration. */
typedef struct {uint8_t background,text,html,markdown;} surface_preferences_t;
/** @brief Serialized preference callbacks; save retains a copy through completion. */
typedef struct {
    void *context;
    unsigned (*read)(void *,surface_preferences_t *);
    int (*save)(void *,const surface_preferences_t *);
} surface_preferences_provider_t;
/** @brief Validate palette and installed-handler choice indexes. */
int surface_preferences_valid(const surface_preferences_t *);
/** @brief Encode a checked version-1 desktop record with CRC and reserved bytes. */
int surface_preferences_encode(const surface_preferences_t *,unsigned char out[SURFACE_SETTINGS_BYTES]);
/** @brief Decode an independent desktop record; errors preserve the destination. */
unsigned surface_preferences_decode(const unsigned char *,unsigned,surface_preferences_t *);
/** @brief Encode a validated mapping in a fixed little-endian version-1 record. */
int surface_settings_encode(const surface_touch_map_t *, unsigned char out[SURFACE_SETTINGS_BYTES]);
/**
 * @brief The status of a calibration read from the store, and its map when the record is good.
 * @param found The store's read result: 0 a record, 1 none, any other an error.
 * @return SURFACE_SETTINGS_SAVED with the record's map in @p map; else
 * SURFACE_SETTINGS_DEFAULT for no record, SURFACE_SETTINGS_UNAVAILABLE for an
 * error or the decoder's status for a bad record, and @p map is unchanged.
 */
unsigned surface_settings_load(int found, const unsigned char *record, unsigned length,
    surface_touch_map_t *map);
/** @brief The status a store write leaves: SURFACE_SETTINGS_SAVED for result 0,
 *  SURFACE_SETTINGS_FAILED for any other. */
unsigned surface_settings_stored(int result);
/**
 * @brief The map that puts the screen's edges where @p map now reports @p edge.
 * @param edge The positions @p map reports at the screen's left, right, top
 * and bottom edges, in pixels of an 800 by 480 screen.
 * @return 1 with the corrected map in @p out, its swap and inversions as in
 * @p map; 0 with @p out unchanged when @p map is invalid, an axis's two edges
 * are not in order or the result is no valid map. Bounds outside 0..65535
 * are held to that range.
 */
int surface_touch_map_refit(const surface_touch_map_t *map, const int edge[4],
    surface_touch_map_t *out);
/** @brief Decode a checksummed record; errors leave the destination unchanged. */
unsigned surface_settings_decode(const unsigned char *, unsigned length, surface_touch_map_t *);
#endif
