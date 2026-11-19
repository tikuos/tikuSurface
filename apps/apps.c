/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * apps.c - static app lifecycle and asynchronous read-only media navigation.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include <string.h>

/** @brief Queue a new browser listing and retire any provider-owned request. */
static void listing(surface_app_state_t *state, uint64_t epoch, uint64_t after)
{
    tiku_paged_storage_cancel(state->store);
    state->queued = 1;
    state->reading = 0;
    state->epoch = epoch;
    state->after = after;
    state->count = state->offset = state->restore_offset = 0;
    state->preview[0] = 0;
    state->status = TIKU_RESOURCE_PENDING;
    state->shell->dirty = 1;
}

/** @brief Initialize app state and create the browser's bounded storage session. */
static tiku_resource_error_t start(void *data, tiku_static_app_t *owner, void *argument)
{
    surface_app_state_t *state = data;
    surface_t *shell = argument;
    state->shell = shell;
    state->owner = owner;
    state->id = shell->pending;
    state->status = TIKU_RESOURCE_OK;
    shell->active.state = state;
    if (state->id == SURFACE_BROWSER) {
        state->store = tiku_paged_storage_create(shell->config.allocator,
            shell->config.storage, &shell->config.caller, 16, 1024);
        if (!state->store) return TIKU_RESOURCE_UNAVAILABLE;
        memcpy(state->path, "/", 2);
        listing(state, 0, 0);
    }
    if (state->id == SURFACE_SETTINGS) {
        state->calibration = (surface_touch_map_t){0, 799, 0, 479, 0, 0, 0};
        state->settings_status = shell->config.settings.read ?
            shell->config.settings.read(shell->config.settings.context, &state->calibration) : SURFACE_SETTINGS_UNAVAILABLE;
    }
    return TIKU_RESOURCE_OK;
}

/** @brief Deliver calculator key input to a running application. */
static void dispatch(void *data, tiku_queue_result_t result, const tiku_input_event_t *event)
{
    surface_app_state_t *state = data;
    if (!state->stopped && result == TIKU_QUEUE_EVENT && event->type == TIKU_EVENT_KEY_DOWN &&
        state->id == SURFACE_CALCULATOR) surface_calculator_key(state, event->key);
}

/** @brief Close a browser session while its lease protects pending buffers. */
static void stop(void *data)
{
    surface_app_state_t *state = data;
    state->stopped = 1;
    state->queued = 0;
    tiku_paged_storage_close(state->store);
}

/** @brief Release a closed storage session after request retirement. */
static void dispose(void *data)
{
    surface_app_state_t *state = data;
    (void)tiku_paged_storage_destroy(&state->store);
}

void surface_app_start(surface_t *shell, surface_app_t id)
{
    static const tiku_static_app_ops_t ops = {
        sizeof(surface_app_state_t), start, dispatch, NULL, stop, dispose
    };
    shell->pending = id;
    shell->active.owner = tiku_static_app_create(shell->config.allocator, &ops, 4);
    if (!shell->active.owner) { shell->status = TIKU_RESOURCE_LIMIT; return; }
    shell->status = tiku_static_app_start(shell->active.owner, shell);
    if (shell->status != TIKU_RESOURCE_OK) {
        (void)tiku_static_app_destroy(&shell->active.owner);
        shell->active.state = NULL;
    }
}

void surface_app_poll(surface_app_state_t *state)
{
    if (state && state->id == SURFACE_SETTINGS && state->settings_status == SURFACE_SETTINGS_PENDING &&
        state->shell->config.settings.read) {
        surface_touch_map_t saved;
        unsigned status = state->shell->config.settings.read(state->shell->config.settings.context, &saved);
        if (status != state->settings_status) { state->settings_status = status; state->shell->dirty = 1; }
    }
    if (!state || !state->store) return;
    if (state->lease.app && !tiku_paged_storage_pending(state->store)) {
        tiku_app_lease_release(&state->lease);
        if (!state->stopped && !state->queued) {
            state->status = tiku_paged_storage_status(state->store);
            const tiku_storage_request_t *result = tiku_paged_storage_result(state->store,
                &state->count, &state->epoch, &state->eof);
            if (result && result->operation == TIKU_STORAGE_LIST) {
                state->offset = state->restore_offset < state->count ? state->restore_offset : 0;
                state->restore_offset = 0;
            }
            if (result && result->operation == TIKU_STORAGE_READ) {
                state->preview_bytes = state->count;
                state->preview_hex = 0;
                for (size_t i = 0; i < state->count; ++i) {
                    unsigned char c = result->bytes[i];
                    if ((c < 32 && c != '\n' && c != '\r' && c != '\t') || c > 126) state->preview_hex = 1;
                }
                size_t at = 0, limit = state->count;
                if (state->preview_hex && limit > 128) limit = 128;
                for (size_t i = 0; i < limit; ++i) {
                    unsigned char c = result->bytes[i];
                    if (state->preview_hex) {
                        state->preview[at++] = "0123456789ABCDEF"[c >> 4];
                        state->preview[at++] = "0123456789ABCDEF"[c & 15u];
                        state->preview[at++] = (i % 16 == 15) ? '\n' : ' ';
                    } else state->preview[at++] = c == '\t' || c == '\r' ? ' ' : (char)c;
                }
                state->preview[at] = 0;
            }
            state->shell->dirty = 1;
        }
    }
    if (state->stopped || !state->queued || tiku_paged_storage_pending(state->store)) return;
    if (!tiku_app_lease_begin(state->owner, &state->lease)) {
        state->status = TIKU_RESOURCE_LIMIT;
        state->queued = 0;
        return;
    }
    int operation = state->queued;
    state->queued = 0;
    state->status = operation == 1 ?
        tiku_paged_storage_list(state->store, state->path, state->epoch, state->after) :
        tiku_paged_storage_read(state->store, state->target, state->epoch, state->node, 0);
    if (!tiku_paged_storage_pending(state->store)) tiku_app_lease_release(&state->lease);
    state->shell->dirty = 1;
}

void surface_app_action(surface_app_state_t *state, int action)
{
    if (!state || state->stopped) return;
    if (state->id == SURFACE_CALCULATOR) {
        if (action >= ACTION_HEX && action < ACTION_HEX + 16)
            surface_calculator_key(state, (unsigned)"0123456789ABCDEF"[action - ACTION_HEX]);
        else if (action == ACTION_ERASE) surface_calculator_key(state, 8);
        else if (action == ACTION_CLEAR) surface_calculator_key(state, 'X');
        return;
    }
    if (state->id == SURFACE_SETTINGS) {
        if (action == ACTION_SWAP) state->calibration.swap ^= 1;
        else if (action == ACTION_INVERT_X) state->calibration.invert_x ^= 1;
        else if (action == ACTION_INVERT_Y) state->calibration.invert_y ^= 1;
        else if (action == ACTION_SAVE && state->shell->config.settings.save)
            state->settings_status = state->shell->config.settings.save(state->shell->config.settings.context,
                &state->calibration) ? SURFACE_SETTINGS_PENDING : SURFACE_SETTINGS_FAILED;
        state->shell->dirty = 1;
        return;
    }
    if (state->id != SURFACE_BROWSER) return;
    if (action == ACTION_REFRESH) { state->history_count = 0; listing(state, 0, 0); return; }
    const tiku_storage_request_t *result = tiku_paged_storage_result(state->store,
            &(size_t){0}, &(uint64_t){0}, &(int){0});
    if (!result || result->operation != TIKU_STORAGE_LIST || state->queued) return;
    if (action == ACTION_PREVIOUS) {
        if (state->offset >= 6) state->offset -= 6;
        else if (state->history_count) {
            unsigned n = --state->history_count;
            listing(state, state->epoch, state->history[n].after);
            state->restore_offset = state->history[n].offset;
        }
    } else if (action == ACTION_NEXT) {
        if (state->offset + 6 < state->count) state->offset += 6;
        else if (!state->eof && state->count) {
            if (state->history_count == 8) {
                memmove(state->history, state->history + 1, 7 * sizeof state->history[0]);
                state->history_count = 7;
            }
            state->history[state->history_count].after = state->after;
            state->history[state->history_count++].offset = state->offset;
            uint64_t after = result->entries[state->count - 1].node_id;
            listing(state, state->epoch, after);
        }
    } else if (action >= ACTION_ROW && action < ACTION_ROW + 6) {
        size_t index = state->offset + (unsigned)(action - ACTION_ROW);
        if (index >= state->count) return;
        const tiku_resource_entry_t *entry = &result->entries[index];
        if (entry->kind == TIKU_RESOURCE_DIRECTORY) {
            state->history_count = 0;
            memcpy(state->path, entry->path, sizeof state->path);
            listing(state, 0, 0);
        } else {
            memcpy(state->target, entry->path, sizeof state->target);
            state->node = entry->node_id;
            state->reading = 1;
            state->queued = 2;
            state->count = 0;
            state->status = TIKU_RESOURCE_PENDING;
        }
    }
    state->shell->dirty = 1;
}

int surface_app_back(surface_app_state_t *state)
{
    if (!state || state->stopped || state->id != SURFACE_BROWSER) return 0;
    if (state->reading) { state->history_count = 0; listing(state, 0, 0); return 1; }
    if (!strcmp(state->path, "/")) return 0;
    state->history_count = 0;
    char *slash = strrchr(state->path, '/');
    if (slash == state->path) slash[1] = 0;
    else if (slash) *slash = 0;
    listing(state, 0, 0);
    return 1;
}
