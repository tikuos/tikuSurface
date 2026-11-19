/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * surface.c - bounded navigation, application retirement and frame ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_word_model.h"
#include <string.h>

/** @brief Destroy a stopped instance after its request lease retires. */
static void reap(surface_instance_t *instance)
{
    if (!instance->owner) return;
    surface_app_poll(instance->state);
    if (tiku_static_app_destroy(&instance->owner)) instance->state = NULL;
}

/** @brief Move the active owner into the vacant retirement slot. */
static void retire(surface_t *shell)
{
    if (!shell->active.owner) return;
    (void)tiku_static_app_stop(shell->active.owner);
    shell->retired = shell->active;
    shell->active = (surface_instance_t){0};
    reap(&shell->retired);
}

surface_t *surface_create(const surface_config_t *config)
{
    tiku_owned_buffer_t self;
    surface_t *shell;
    if (!config || !config->clock.read || config->paint_rows > 480 ||
        !tiku_buffer_init(&self, config->allocator, _Alignof(surface_t)) ||
        !tiku_buffer_resize(&self, 1, sizeof *shell, 1)) return NULL;
    shell = self.data;
    shell->self = self;
    shell->config = *config;
    shell->events = tiku_event_queue_create(config->allocator, 8);
    shell->frame = tiku_framebuffer_create(config->allocator, 800, 480, 0);
    if (!shell->events || !shell->frame) {
        surface_stop(shell);
        (void)surface_destroy(&shell);
        return NULL;
    }
    shell->dirty = 1;
    shell->status = TIKU_RESOURCE_OK;
    return shell;
}

int surface_launch(surface_t *shell, surface_app_t id)
{
    if (!shell || shell->stopped || id < SURFACE_HOME || id > SURFACE_SETTINGS) return 0;
    shell->pending = id;
    shell->switching = 1;
    tiku_event_queue_close(shell->events);
    (void)tiku_event_queue_take(shell->events, &(tiku_input_event_t){0});
    (void)tiku_event_queue_open(shell->events);
    (void)tiku_press_release(&shell->press, 0);
    shell->dirty = 1;
    return 1;
}

int surface_post(surface_t *shell, const tiku_input_event_t *event)
{
    return shell && !shell->stopped && event && tiku_event_queue_push(shell->events, event);
}

/** @brief Apply navigation before dispatching input into a new application. */
static void navigate(surface_t *shell)
{
    reap(&shell->retired);
    if (!shell->switching) return;
    retire(shell);
    if (shell->retired.owner) return;
    shell->switching = 0;
    shell->status = TIKU_RESOURCE_OK;
    if (shell->pending != SURFACE_HOME) surface_app_start(shell, shell->pending);
    shell->pending = SURFACE_HOME;
    shell->dirty = 1;
}

/** @brief Route a release action without calling lifecycle APIs in app callbacks. */
static void action(surface_t *shell, int id)
{
    if (id == ACTION_HOME) (void)surface_launch(shell, SURFACE_HOME);
    else if (id == ACTION_BACK) {
        if (!surface_app_back(shell->active.state)) (void)surface_launch(shell, SURFACE_HOME);
    } else if (id >= ACTION_CALCULATOR && id <= ACTION_SETTINGS)
        (void)surface_launch(shell, (surface_app_t)(id - ACTION_CALCULATOR + SURFACE_CALCULATOR));
    else surface_app_action(shell->active.state, id);
}

void surface_step(surface_t *shell)
{
    if (!shell) return;
    if (shell->stopped) { reap(&shell->retired); return; }
    int failed = !tiku_clock_now(&shell->config.clock, &shell->now);
    if (failed != shell->clock_failed) shell->dirty = 1;
    shell->clock_failed = failed;
    if (tiku_framebuffer_result(shell->frame) == TIKU_PRESENT_ERROR && !shell->display_failed) {
        shell->display_failed = 1;
        shell->dirty = 1;
    }
    navigate(shell);
    for (unsigned i = 0; i < 8; ++i) {
        tiku_input_event_t event;
        tiku_queue_result_t result = tiku_event_queue_take(shell->events, &event);
        if (result == TIKU_QUEUE_EMPTY) break;
        if (result == TIKU_QUEUE_CANCEL) {
            (void)tiku_press_release(&shell->press, 0);
            if (shell->active.owner) {
                (void)tiku_static_app_foreground(shell->active.owner, 0);
                (void)tiku_static_app_foreground(shell->active.owner, 1);
            }
        } else if (event.type == TIKU_EVENT_DEACTIVATED) {
            (void)tiku_press_release(&shell->press, 0);
        } else if (event.type == TIKU_EVENT_KEY_DOWN) {
            if (event.key == 27) action(shell, ACTION_BACK);
            else if (shell->active.owner)
                (void)tiku_static_app_post(shell->active.owner, &event);
        } else if (event.type == TIKU_EVENT_POINTER_DOWN ||
                   event.type == TIKU_EVENT_POINTER_MOVE ||
                   event.type == TIKU_EVENT_POINTER_UP) {
            surface_view_t view;
            surface_view(shell, &view);
            int hit = surface_hit(&view, event.x, event.y);
            if (event.type == TIKU_EVENT_POINTER_DOWN && event.button == 1)
                (void)tiku_press_arm(&shell->press, hit);
            else if (event.type == TIKU_EVENT_POINTER_MOVE)
                (void)tiku_press_track(&shell->press, hit);
            else if (event.type == TIKU_EVENT_POINTER_UP && event.button == 1)
                action(shell, tiku_press_release(&shell->press, hit));
        }
        shell->dirty = 1;
        if (shell->switching) { navigate(shell); break; }
    }
    if (shell->active.owner) {
        (void)tiku_static_app_dispatch(shell->active.owner, 4);
        surface_app_poll(shell->active.state);
    }
}

/** @brief Copy a provider label into a terminated presentation buffer. */
static void label(char *destination, size_t capacity, const char *source)
{
    if (!source) source = "Unavailable";
    size_t n = 0;
    while (n + 1 < capacity && source[n]) { destination[n] = source[n]; ++n; }
    destination[n] = 0;
}

void surface_view(const surface_t *shell, surface_view_t *view)
{
    if (!view) return;
    *view = (surface_view_t){0};
    if (!shell) { view->stopped = 1; return; }
    label(view->platform_name, sizeof view->platform_name, shell->config.platform_name);
    label(view->media_description, sizeof view->media_description, shell->config.media_description);
    label(view->services_description, sizeof view->services_description, shell->config.services_description);
    view->pending = shell->pending;
    view->retiring = !!shell->retired.owner;
    view->stopped = shell->stopped;
    view->pressed = shell->press.inside ? shell->press.id : 0;
    view->clock_failed = shell->clock_failed;
    view->display_failed = shell->display_failed;
    view->now = shell->now;
    view->status = shell->status;
    const surface_app_state_t *state = shell->active.state;
    if (!state) return;
    view->foreground = state->id;
    if (state->id == SURFACE_SETTINGS) {
        view->calibration = state->calibration;
        view->settings_status = state->settings_status;
    }
    if (state->id == SURFACE_CALCULATOR) {
        view->status = state->status;
        uint64_t word = 0;
        static const unsigned bases[] = {16, 10, 8, 2};
        memcpy(view->hex, state->hex, sizeof view->hex);
        (void)tiku_word_parse(state->hex[0] ? state->hex : "0", 16, 64, &word);
        for (unsigned i = 0; i < 4; ++i) (void)tiku_word_format(word, bases[i], view->bases[i]);
    } else if (state->id == SURFACE_BROWSER) {
        view->status = state->status;
        memcpy(view->path, state->path, sizeof view->path);
        memcpy(view->preview, state->preview, sizeof view->preview);
        view->reading = state->reading;
        view->preview_hex = state->preview_hex;
        view->preview_bytes = state->preview_bytes;
        view->previous = state->offset || state->history_count;
        const tiku_storage_request_t *request = tiku_paged_storage_result(state->store,
            &(size_t){0}, &(uint64_t){0}, &(int){0});
        if (request && request->operation == TIKU_STORAGE_LIST && !state->queued) {
            for (size_t i = state->offset; i < state->count && view->row_count < 6; ++i) {
                unsigned row = view->row_count++;
                const tiku_resource_entry_t *entry = &request->entries[i];
                const char *name = strrchr(entry->path, '/');
                name = name ? name + 1 : entry->path;
                size_t n = strlen(name);
                if (n > 63) n = 63;
                memcpy(view->rows[row], name, n);
                view->directory[row] = entry->kind == TIKU_RESOURCE_DIRECTORY;
            }
            view->more = state->offset + view->row_count < state->count || !state->eof;
        }
    }
}

tiku_present_result_t surface_render(surface_t *shell, tiku_presenter_t presenter)
{
    if (!shell || shell->stopped) return TIKU_PRESENT_ERROR;
    if (!shell->painting) {
        if (!shell->dirty) return TIKU_PRESENT_OK;
        shell->painting = tiku_framebuffer_begin(shell->frame, NULL);
        if (!shell->painting) return TIKU_PRESENT_BUSY;
        surface_view(shell, &shell->painted);
        shell->paint_y = 0;
        shell->dirty = 0;
    }
    unsigned rows = shell->config.paint_rows ? shell->config.paint_rows : 480;
    if (rows > 480 - shell->paint_y) rows = 480 - shell->paint_y;
    tiku_clip_set(shell->painting, (tiku_rect_t){0, (int)shell->paint_y, 800, (int)rows});
    surface_paint(shell->painting, &shell->painted);
    shell->paint_y += rows;
    if (shell->paint_y < 480) return TIKU_PRESENT_BUSY;
    tiku_clip_reset(shell->painting);
    shell->painting = NULL;
    tiku_present_result_t result = tiku_framebuffer_present(shell->frame, presenter);
    if (result == TIKU_PRESENT_OK) {
        shell->dirty |= shell->display_failed;
        shell->display_failed = 0;
    }
    else {
        (void)tiku_framebuffer_abandon(shell->frame);
        shell->dirty = 1;
        if (result == TIKU_PRESENT_ERROR) shell->display_failed = 1;
    }
    return result;
}

int surface_render_pending(const surface_t *shell)
{ return shell && !shell->stopped && (shell->dirty || shell->painting); }

void surface_stop(surface_t *shell)
{
    if (!shell || shell->stopped) return;
    shell->stopped = 1;
    if (shell->painting) {
        (void)tiku_framebuffer_abandon(shell->frame);
        shell->painting = NULL;
    }
    shell->switching = 0;
    shell->pending = SURFACE_HOME;
    tiku_event_queue_close(shell->events);
    (void)tiku_press_release(&shell->press, 0);
    retire(shell);
}

int surface_destroy(surface_t **pointer)
{
    if (!pointer || !*pointer) return 1;
    surface_t *shell = *pointer;
    if (!shell->stopped) return 0;
    reap(&shell->retired);
    if (shell->retired.owner || !tiku_framebuffer_destroy(&shell->frame)) return 0;
    if (!tiku_event_queue_destroy(&shell->events)) return 0;
    tiku_owned_buffer_t self = shell->self;
    tiku_buffer_dispose(&self);
    *pointer = NULL;
    return 1;
}

void surface_services_changed(surface_t *shell)
{
    if (shell && !shell->stopped && shell->active.state &&
        shell->active.state->id==SURFACE_SETTINGS) shell->dirty=1;
}
