/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * surface.c - bounded navigation, application retirement and frame ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_word_model.h"
#include "tiku_event.h"
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

/** @brief Hide the foreground application while retaining its model and leases. */
static void background(surface_t *shell)
{
    if (!shell->active.owner) return;
    (void)tiku_static_app_foreground(shell->active.owner, 0);
    shell->background[shell->active.state->id] = shell->active;
    shell->active = (surface_instance_t){0};
}

/** @brief Invalidate input and the pressed control across a navigation boundary. */
static void cancel_input(surface_t *shell)
{
#if SURFACE_DESKTOP
    surface_desktop_cancel(shell);
#endif
#if SURFACE_IMAGE
    if(shell->active.state&&shell->active.state->id==SURFACE_IMAGE_APP)surface_image_cancel_input(shell->active.state);
#endif
    tiku_event_queue_close(shell->events);
    (void)tiku_event_queue_take(shell->events, &(tiku_input_event_t){0});
    (void)tiku_event_queue_open(shell->events);
    (void)tiku_press_release(&shell->press, 0);
    if (shell->active.owner) {
        (void)tiku_static_app_foreground(shell->active.owner, 0);
        (void)tiku_static_app_foreground(shell->active.owner, 1);
    }
    shell->dirty = 1;
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
#if SURFACE_PREFERENCES
    surface_preferences_refresh(shell);
#endif
    shell->events = tiku_event_queue_create(config->allocator, 8);
    shell->frame = tiku_framebuffer_create(config->allocator, 800, 480, 0);
    if (!shell->events || !shell->frame
#if SURFACE_DESKTOP
        || !surface_desktop_create(shell)
#endif
        ) {
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
#if SURFACE_DESKTOP
    if(id!=SURFACE_HOME&&id!=SURFACE_BROWSER&&id!=SURFACE_SETTINGS)return 0;
#endif
#if SURFACE_DOCUMENTS
    if(shell&&shell->document_menu)return 0;
#endif
    if (!shell || shell->stopped || shell->close_confirmation ||
        id < SURFACE_HOME || id > SURFACE_APP_LAST) return 0;
#if SURFACE_TEXT_INPUT
    if (shell->text_field.active) return 0;
#endif
#if SURFACE_DOCUMENTS
    shell->document_error=SURFACE_HOME;
#endif
    shell->pending = id;
#if SURFACE_TRANSFERS
    shell->transfer_confirm=0;
#endif
    shell->switching = 1;
    shell->applications_menu = 0;
    cancel_input(shell);
    return 1;
}

surface_close_result_t surface_close(surface_t *shell)
{
#if SURFACE_DOCUMENTS
    if(shell&&shell->document_menu)return SURFACE_CLOSE_BUSY;
#endif
    if (shell && shell->close_confirmation) return SURFACE_CLOSE_CONFIRM;
#if SURFACE_TEXT_INPUT
    if (shell && shell->text_field.active) return SURFACE_CLOSE_BUSY;
#endif
    if (!shell || shell->stopped || shell->switching || shell->retired.owner ||
        !shell->active.state || (shell->active.state->id == SURFACE_SETTINGS &&
        (shell->active.state->settings_status == SURFACE_SETTINGS_PENDING
#if SURFACE_PREFERENCES
        ||shell->active.state->preferences_status==SURFACE_SETTINGS_PENDING
#endif
        ))) return SURFACE_CLOSE_BUSY;
    shell->applications_menu = 0;
    cancel_input(shell);
#if SURFACE_EDITOR
#if SURFACE_HEX
    if(surface_hex_modified(shell->active.state)){shell->close_confirmation=3;return SURFACE_CLOSE_CONFIRM;}
#endif
#if SURFACE_NOTES
    if(surface_notes_modified(shell->active.state)){shell->close_confirmation=3;return SURFACE_CLOSE_CONFIRM;}
#endif
    if(surface_editor_modified(shell->active.state)){shell->close_confirmation=3;return SURFACE_CLOSE_CONFIRM;}
#endif
    if (shell->active.state->settings_dirty
#if SURFACE_PREFERENCES
        ||shell->active.state->preferences_dirty
#endif
        ) {
        shell->close_confirmation = 1;
        return SURFACE_CLOSE_CONFIRM;
    }
    retire(shell);
    shell->status = TIKU_RESOURCE_OK;
    return SURFACE_CLOSE_ACCEPTED;
}

int surface_resolve_close(surface_t *shell, int discard)
{
    if (!shell || shell->stopped || !shell->close_confirmation) return 0;
#if SURFACE_DIRECTORIES
    if(shell->close_confirmation==4){shell->close_confirmation=0;cancel_input(shell);
        if(discard)surface_app_action(shell->active.state,ACTION_FOLDER_ENABLE_CONFIRMED);
        return 1;
    }
#endif
#if SURFACE_FILE_JOBS
    if (shell->close_confirmation==2) {
        shell->close_confirmation=0;cancel_input(shell);
        if(discard) surface_app_action(shell->active.state,ACTION_FILE_DELETE_CONFIRMED);
        return 1;
    }
#endif
    shell->close_confirmation = 0;
    cancel_input(shell);
    if (discard) {
        retire(shell);
        shell->status = TIKU_RESOURCE_OK;
    }
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
    if (shell->retired.state && shell->retired.state->id == shell->pending) return;
    if (shell->active.state && shell->active.state->id == shell->pending) {
        shell->switching = 0;
        shell->pending = SURFACE_HOME;
#if SURFACE_DESKTOP
        surface_desktop_sync(shell,1);
#endif
        return;
    }
    background(shell);
    shell->switching = 0;
    shell->status = TIKU_RESOURCE_OK;
    if (shell->pending != SURFACE_HOME) {
        surface_instance_t *saved = &shell->background[shell->pending];
        if (saved->owner) {
            shell->active = *saved;
            *saved = (surface_instance_t){0};
            (void)tiku_static_app_foreground(shell->active.owner, 1);
        } else surface_app_start(shell, shell->pending);
    }
#if SURFACE_DOCUMENTS
    surface_document_deliver(shell);
#else
#if SURFACE_HEX
    if(shell->open_path[0]&&shell->active.state&&shell->active.state->id==SURFACE_HEX_APP){
        (void)surface_hex_open(shell->active.state,shell->open_path,shell->open_epoch,shell->open_node);
        shell->open_path[0]=0;
    }
#endif
#if SURFACE_READER
    if(shell->open_path[0]&&shell->active.state&&shell->active.state->id==SURFACE_READER_APP){
        (void)surface_reader_open(shell->active.state,shell->open_path,shell->open_epoch,shell->open_node);
        shell->open_path[0]=0;
    }
#endif
#if SURFACE_EDITOR
    if(shell->open_path[0]&&shell->active.state&&shell->active.state->id==SURFACE_EDITOR_APP){
        if(!surface_editor_open(shell->active.state,shell->open_path,shell->open_epoch,shell->open_node))shell->active.state->status=TIKU_RESOURCE_BUSY;
        shell->open_path[0]=0;
    }
#endif
#endif
#if SURFACE_BASIC
    if(shell->run_basic){
        shell->run_basic=0;surface_app_state_t *editor=shell->background[SURFACE_EDITOR_APP].state;
        size_t length=0;const char *source=surface_editor_program(editor,&length);
        if(!source||!shell->active.state||shell->active.state->id!=SURFACE_TERMINAL_APP||
           !surface_terminal_program(shell->active.state,source,length)){
            if(editor)editor->status=TIKU_RESOURCE_BUSY;
        }
    }
#endif
    shell->pending = SURFACE_HOME;
#if SURFACE_DESKTOP
    surface_desktop_sync(shell,1);
#endif
    shell->dirty = 1;
}

/** @brief Route a release action without calling lifecycle APIs in app callbacks. */
void surface_action(surface_t *shell, int id)
{
#if SURFACE_DOCUMENTS
    if(surface_document_action(shell,id))return;
    shell->document_error=SURFACE_HOME;
#endif
#if SURFACE_HEX
    if(id==ACTION_FILE_BYTES&&shell->active.state&&shell->active.state->id==SURFACE_BROWSER){
        surface_app_state_t *b=shell->active.state,*h=shell->background[SURFACE_HEX_APP].state;
        if(surface_hex_modified(h)||surface_hex_busy(h)){b->status=TIKU_RESOURCE_BUSY;shell->dirty=1;return;}
        memcpy(shell->open_path,b->browser->target,strlen(b->browser->target)+1);shell->open_epoch=b->epoch;shell->open_node=b->node;
        if(!surface_launch(shell,SURFACE_HEX_APP))shell->open_path[0]=0;
        return;
    }
#endif
#if SURFACE_READER
    if(id==ACTION_FILE_READ&&shell->active.state&&shell->active.state->id==SURFACE_BROWSER){
        surface_app_state_t *b=shell->active.state;
        memcpy(shell->open_path,b->browser->target,strlen(b->browser->target)+1);shell->open_epoch=b->epoch;shell->open_node=b->node;
        if(!surface_launch(shell,SURFACE_READER_APP))shell->open_path[0]=0;
        return;
    }
#endif
#if SURFACE_BASIC
    if(id==ACTION_EDITOR_RUN&&shell->active.state&&shell->active.state->id==SURFACE_EDITOR_APP){
        if(surface_editor_busy(shell->active.state))return;
        size_t length=0;if(!surface_editor_program(shell->active.state,&length)){shell->dirty=1;return;}
        if(surface_launch(shell,SURFACE_TERMINAL_APP))shell->run_basic=1;
        return;
    }
#endif
#if SURFACE_EDITOR
    if(id==ACTION_FILE_EDIT&&shell->active.state&&shell->active.state->id==SURFACE_BROWSER){
        surface_app_state_t *b=shell->active.state,*e=shell->background[SURFACE_EDITOR_APP].state;
        if(!surface_editor_can_open(e,b->browser->target)){b->status=TIKU_RESOURCE_BUSY;return;}
        memcpy(shell->open_path,b->browser->target,strlen(b->browser->target)+1);shell->open_epoch=b->epoch;shell->open_node=b->node;
        (void)surface_launch(shell,SURFACE_EDITOR_APP);return;
    }
#endif
#if SURFACE_TEXT_INPUT
    if (id == ACTION_WORD_EDIT) { cancel_input(shell); surface_text_open(shell); return; }
#endif
    if (shell->close_confirmation) {
        if (id == ACTION_KEEP || id == ACTION_DISCARD)
            (void)surface_resolve_close(shell, id == ACTION_DISCARD);
        return;
    }
#if SURFACE_IMAGE
    if(id==ACTION_LAUNCH_PREVIOUS||id==ACTION_LAUNCH_NEXT){
        if(shell->applications_menu||!shell->active.state){
            unsigned *page=shell->applications_menu?&shell->task_page:&shell->home_page;
            unsigned size=shell->applications_menu?10:12;
            if(id==ACTION_LAUNCH_PREVIOUS&&*page)--*page;
            if(id==ACTION_LAUNCH_NEXT&&(*page+1)*size<SURFACE_APP_LAST)++*page;
            cancel_input(shell);shell->dirty=1;
        }
        return;
    }
#endif
    if (id == ACTION_APPLICATIONS) {
        shell->applications_menu ^= 1;
        cancel_input(shell);
    } else if (shell->applications_menu) {
        if (id >= ACTION_TASK && id < ACTION_TASK + SURFACE_APP_LAST)
            (void)surface_launch(shell, (surface_app_t)(id - ACTION_TASK + 1));
    } else if (id == ACTION_CLOSE) (void)surface_close(shell);
    else if (id == ACTION_HOME) (void)surface_launch(shell, SURFACE_HOME);
    else if (id == ACTION_BACK) {
        if (!surface_app_back(shell->active.state)) (void)surface_launch(shell, SURFACE_HOME);
    } else if (id >= ACTION_CALCULATOR && id <= ACTION_CALCULATOR + SURFACE_APP_LAST - 1)
        (void)surface_launch(shell, (surface_app_t)(id - ACTION_CALCULATOR + SURFACE_CALCULATOR));
    else surface_app_action(shell->active.state, id);
}

void surface_step(surface_t *shell)
{
    if (!shell) return;
#if SURFACE_TRANSFERS
    surface_transfers_poll(shell);
#endif
    if (shell->stopped) {
        reap(&shell->retired);
        reap(&shell->active);
        for (unsigned i = 1; i <= SURFACE_APP_LAST; ++i) reap(&shell->background[i]);
        return;
    }
    int failed = !tiku_clock_now(&shell->config.clock, &shell->now);
    if (failed != shell->clock_failed) shell->dirty = 1;
    shell->clock_failed = failed;
    if (tiku_framebuffer_result(shell->frame) == TIKU_PRESENT_ERROR && !shell->display_failed) {
        shell->display_failed = 1;
        shell->dirty = 1;
    }
    navigate(shell);
#if SURFACE_DESKTOP
    surface_desktop_sync(shell,0);
#endif
    for (unsigned i = 0; i < 8; ++i) {
        tiku_input_event_t event;
        tiku_queue_result_t result = tiku_event_queue_take(shell->events, &event);
        if (result == TIKU_QUEUE_EMPTY) break;
#if SURFACE_TEXT_INPUT
        if (shell->text_field.active) {
            if (result == TIKU_QUEUE_CANCEL) event = (tiku_input_event_t){.type=TIKU_EVENT_DEACTIVATED};
            (void)surface_text_event(shell, &event);
            shell->dirty = 1;
            if (!shell->text_field.active) { cancel_input(shell); break; }
            continue;
        }
#endif
#if SURFACE_DESKTOP
        if(result==TIKU_QUEUE_CANCEL)surface_desktop_cancel(shell);
        if(result!=TIKU_QUEUE_CANCEL&&surface_desktop_event(shell,&event)){
            shell->dirty=1;
            if(shell->switching){navigate(shell);break;}
            continue;
        }
#endif
        if (result == TIKU_QUEUE_CANCEL) {
#if SURFACE_IMAGE
            if(shell->active.state&&shell->active.state->id==SURFACE_IMAGE_APP)surface_image_cancel_input(shell->active.state);
#endif
            (void)tiku_press_release(&shell->press, 0);
            if (shell->active.owner) {
                (void)tiku_static_app_foreground(shell->active.owner, 0);
                (void)tiku_static_app_foreground(shell->active.owner, 1);
            }
        } else if (event.type == TIKU_EVENT_DEACTIVATED) {
            cancel_input(shell);
        } else if (event.type == TIKU_EVENT_KEY_DOWN || event.type==TIKU_EVENT_TEXT_INPUT) {
            if (shell->close_confirmation) {
                if (event.key == TIKU_KEY_ESCAPE) surface_action(shell, ACTION_KEEP);
#if SURFACE_DOCUMENTS
            } else if(shell->document_menu){
                if(event.key==TIKU_KEY_ESCAPE)surface_action(shell,ACTION_DOCUMENT_CANCEL);
                else if(event.key>='1'&&event.key<='3')surface_action(shell,ACTION_DOCUMENT_CHOICE+(int)(event.key-'1'));
            } else if(!shell->applications_menu&&shell->active.state&&shell->active.state->id==SURFACE_BROWSER&&
                (event.modifiers&TIKU_MOD_CMD)&&event.key=='o'){
                surface_action(shell,event.modifiers&TIKU_MOD_SHIFT?ACTION_DOCUMENT_WITH:ACTION_DOCUMENT_OPEN);
#endif
            } else if (shell->applications_menu) {
                if (event.key == TIKU_KEY_ESCAPE) surface_action(shell, ACTION_APPLICATIONS);
#if SURFACE_IMAGE
                else if(event.key==TIKU_KEY_PAGE_DOWN)surface_action(shell,ACTION_LAUNCH_NEXT);
                else if(event.key==TIKU_KEY_PAGE_UP)surface_action(shell,ACTION_LAUNCH_PREVIOUS);
#endif
                else if (event.key >= '1'  && event.key <= '9' && event.key <= '0'+SURFACE_APP_LAST)
                    surface_action(shell, ACTION_TASK + (int)(event.key - '1')
#if SURFACE_IMAGE
                        +(int)shell->task_page*10
#endif
                    );
#if SURFACE_HEX
                else if(event.key=='0')surface_action(shell,ACTION_TASK+9
#if SURFACE_IMAGE
                    +(int)shell->task_page*10
#endif
                    );
#endif
#if SURFACE_ARITHMETIC
            } else if (event.key==TIKU_KEY_ESCAPE && shell->active.state && shell->active.state->id==SURFACE_ARITHMETIC_APP) {
                (void)tiku_static_app_post(shell->active.owner,&event);
#endif
#if SURFACE_IMAGE
            } else if(!shell->active.state&&(event.key==TIKU_KEY_PAGE_DOWN||event.key==TIKU_KEY_PAGE_UP)){
                surface_action(shell,event.key==TIKU_KEY_PAGE_DOWN?ACTION_LAUNCH_NEXT:ACTION_LAUNCH_PREVIOUS);
#endif
            } else if (event.key == TIKU_KEY_ESCAPE) surface_action(shell, ACTION_BACK);
            else if (shell->active.owner)
                (void)tiku_static_app_post(shell->active.owner, &event);
#if SURFACE_IMAGE
        } else if(!shell->applications_menu&&!shell->close_confirmation
#if SURFACE_DOCUMENTS
            &&!shell->document_menu
#endif
            &&shell->active.state&&shell->active.state->id==SURFACE_IMAGE_APP&&
            surface_image_pointer(shell->active.state,&event)) {
            (void)tiku_press_release(&shell->press,0);
#endif
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
                surface_action(shell, tiku_press_release(&shell->press, hit));
        }
        shell->dirty = 1;
        if (shell->switching) {
            navigate(shell);
            if (!shell->switching) break;
        }
    }
    if (shell->active.owner) {
        if (!shell->applications_menu && !shell->close_confirmation
#if SURFACE_DOCUMENTS
            &&!shell->document_menu
#endif
            )
            (void)tiku_static_app_dispatch(shell->active.owner, 4);
        surface_app_poll(shell->active.state);
    }
    for (unsigned i = 1; i <= SURFACE_APP_LAST; ++i)
        surface_app_poll(shell->background[i].state);
}

/** @brief Copy a provider label into a terminated presentation buffer. */
static void label(char *destination, size_t capacity, const char *source)
{
    if (!source) source = "Unavailable";
    size_t n = 0;
    while (n + 1 < capacity && source[n]) { destination[n] = source[n]; ++n; }
    destination[n] = 0;
}

void surface_view_state(const surface_t *shell, surface_app_state_t *state, surface_view_t *view)
{
    if (!view) return;
    *view = (surface_view_t){0};
    if (!shell) { view->stopped = 1; return; }
    label(view->build_identity, sizeof view->build_identity, shell->config.build_identity);
    label(view->platform_name, sizeof view->platform_name, shell->config.platform_name);
    label(view->media_description, sizeof view->media_description, shell->config.media_description);
    label(view->services_description, sizeof view->services_description, shell->config.services_description);
#if SURFACE_TRANSFERS
    view->transfer=shell->transfer;view->transfer_confirm=shell->transfer_confirm;
    view->transfer_enable_pending=shell->transfer_enable!=0;view->transfer_stop_pending=shell->transfer_stop!=0;
    view->transfer_completed=shell->transfer_completed;view->transfer_result=shell->transfer_result;
    if(state&&state->id==SURFACE_SETTINGS)view->transfers_page=state->transfers_page;
#endif
#if SURFACE_TEXT_INPUT
    view->text_entry = shell->text_field.active;
    view->text_caret = shell->text_field.caret;
    view->text_a = shell->text_field.sel_a;
    view->text_b = shell->text_field.sel_b;
    view->text_scroll = shell->text_input.scroll;
    view->text_symbols = shell->text_symbols;
    view->text_shift = shell->text_shift;
    view->text_error = shell->text_error;
    view->text_pressed = shell->text_press.inside ? shell->text_press.id : 0;
    memcpy(view->text_value, shell->text_field.text, sizeof view->text_value);
#endif
    view->pending = shell->pending;
    for (unsigned i = 1; i <= SURFACE_APP_LAST; ++i)
        if (shell->background[i].owner) view->open_apps |= 1u << i;
    if (shell->active.state) view->open_apps |= 1u << shell->active.state->id;
    view->applications_menu = shell->applications_menu;
#if SURFACE_IMAGE
    view->home_page=shell->home_page;view->task_page=shell->task_page;
#endif
    view->close_confirmation = shell->close_confirmation;
    view->close_enabled = shell->active.owner && !shell->retired.owner && !shell->switching &&
        !(shell->active.state->id == SURFACE_SETTINGS &&
          (shell->active.state->settings_status == SURFACE_SETTINGS_PENDING
#if SURFACE_PREFERENCES
        ||shell->active.state->preferences_status==SURFACE_SETTINGS_PENDING
#endif
        ));
    view->retiring = !!shell->retired.owner;
    view->stopped = shell->stopped;
    view->pressed = shell->press.inside ? shell->press.id : 0;
    view->clock_failed = shell->clock_failed;
    view->display_failed = shell->display_failed;
    view->now = shell->now;
    view->status = shell->status;
#if SURFACE_DOCUMENTS
    surface_document_view(shell,view);
#endif
#if SURFACE_PREFERENCES
    surface_preferences_view(shell,view);
#endif
    if (!state) return;
    view->foreground = state->id;
#if SURFACE_IMAGE
    if(state->id==SURFACE_IMAGE_APP){surface_image_view(state,view);return;}
#endif
#if SURFACE_TERMINAL
    if(state->id==SURFACE_TERMINAL_APP)surface_terminal_view(state,view);
#endif
#if SURFACE_EDITOR
#if SURFACE_NOTES
    if(state->id==SURFACE_NOTES_APP){surface_notes_view(state,view);return;}
#endif
    if(state->id==SURFACE_EDITOR_APP)surface_editor_view(state,view);
#if SURFACE_HEX
#if SURFACE_DIFF
    if(state->id==SURFACE_DIFF_APP){surface_diff_view(state,view);return;}
#endif
    if(state->id==SURFACE_HEX_APP){surface_hex_view(state,view);return;}
#endif
#endif
#if SURFACE_ARITHMETIC
    if (state->id == SURFACE_ARITHMETIC_APP) {
        memcpy(view->arithmetic,state->arithmetic.entry,sizeof view->arithmetic);
        view->arithmetic_copied=state->arithmetic_copied;
    }
#endif
    if (state->id == SURFACE_SETTINGS) {
        view->calibration = state->calibration;
        view->settings_status = state->settings_status;
        view->settings_dirty = state->settings_dirty;
    }
#if SURFACE_DEVICE
    if(state->id==SURFACE_MONITOR_APP){surface_device_view(state,view);return;}
#if SURFACE_VFS
    if(state->id==SURFACE_VFS_APP){surface_inspector_view(state,view);return;}
#if SURFACE_READER
    if(state->id==SURFACE_READER_APP){surface_reader_view(state,view);return;}
#endif
#endif
#endif
    if (state->id == SURFACE_CALCULATOR) {
        view->status = state->status;
        uint64_t word = 0;
        static const unsigned bases[] = {16, 10, 8, 2};
#if SURFACE_TEXT_INPUT
        view->word_mode = state->word_mode;
        view->bit_page = state->bit_page;
        view->range_select=state->range_select;view->range_pending=state->range_pending;
        view->range_high=state->range_high;view->range_low=state->range_low;view->range_copied=state->range_copied;
        surface_calculator_checksums(state,view);
#endif
        memcpy(view->hex, state->hex, sizeof view->hex);
        view->word_width = state->word_width;
        view->word_base = state->word_base;
        (void)tiku_word_parse(state->hex[0] ? state->hex : "0", state->word_base, (int)state->word_width, &word);
#if SURFACE_TEXT_INPUT
        view->word_value = word;
        (void)tiku_word_format(tiku_word_extract(word,state->range_high,state->range_low),16,view->range_hex);
#endif
        for (unsigned i = 0; i < 4; ++i) (void)tiku_word_format(word, bases[i], view->bases[i]);
    } else if (state->id == SURFACE_BROWSER) {
        view->status = state->status;
        memcpy(view->path, state->browser->path, sizeof view->path);
        memcpy(view->preview, state->browser->preview, sizeof view->preview);
        view->reading = state->reading;
#if SURFACE_DIRECTORIES
        view->file_directory=state->browser->directory;view->file_selecting=state->browser->selecting;
#endif
#if SURFACE_FILE_INFO
        view->file_details=state->file_details;view->file_info_known=state->file_info_known;
        view->file_length=state->file_length;view->file_version=state->file_version;view->file_epoch=state->epoch;
        view->file_can_write=!!(shell->config.caller.authenticated&&(shell->config.caller.scopes&TIKU_RESOURCE_WRITE)&&
            (shell->config.storage.capabilities&TIKU_RESOURCE_FEATURE_CHANGE));
#endif
#if SURFACE_EDITOR
        view->file_open=state->reading&&
#if SURFACE_DIRECTORIES
            !state->browser->directory&&
#endif
            state->epoch&&state->node&&!state->queued&&
            state->status==TIKU_RESOURCE_OK&&!tiku_paged_storage_pending(state->store);
#endif
#if SURFACE_FILE_JOBS
        int writable=shell->config.caller.authenticated&&
            (shell->config.caller.scopes&TIKU_RESOURCE_WRITE)&&
            (shell->config.storage.capabilities&TIKU_RESOURCE_FEATURE_CHANGE);
        view->file_job=state->file_job;
#if SURFACE_CONTENT_JOBS
        view->file_dialog=state->file_dialog;view->copy_bytes=state->copy_bytes;
#if SURFACE_DIRECTORIES
        view->copy_prepared=state->copy_prepared;view->copy_total=state->copy_total;
#endif
        view->file_create=!state->reading&&!state->queued&&state->epoch&&state->status==TIKU_RESOURCE_OK&&
            !tiku_paged_storage_pending(state->store)&&strlen(state->browser->path)<=60&&
            writable;
#endif
#if SURFACE_DIRECTORIES
        view->file_upgrade=state->browser->upgrade&&!state->reading&&!state->queued&&state->epoch&&
            !tiku_paged_storage_pending(state->store)&&writable&&(shell->config.caller.scopes&TIKU_RESOURCE_HOST_CONTROL);
#endif
        view->media_writable=!!writable;
        view->file_actions=state->reading && state->epoch && state->node && !state->queued && !tiku_paged_storage_pending(state->store) && writable;
        if (state->reading) label(view->file_path,sizeof view->file_path,state->browser->target);
#endif
        view->preview_hex = state->preview_hex;
        view->preview_bytes = state->preview_bytes;
        view->previous = state->offset || state->history_count;
        const tiku_storage_request_t *request = tiku_paged_storage_result(state->store,
            &(size_t){0}, &(uint64_t){0}, &(int){0});
        if (request && request->operation == TIKU_STORAGE_LIST && !state->queued) {
            for (size_t i = state->offset; i < state->count && view->row_count < SURFACE_BROWSER_ROWS; ++i) {
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

void surface_view(const surface_t *shell,surface_view_t *view)
{ surface_view_state(shell,shell?shell->active.state:NULL,view); }

tiku_present_result_t surface_render(surface_t *shell, tiku_presenter_t presenter)
{
    if (!shell || shell->stopped) return TIKU_PRESENT_ERROR;
    if (!shell->painting) {
        if (!shell->dirty) return TIKU_PRESENT_OK;
        shell->painting = tiku_framebuffer_begin(shell->frame, NULL);
        if (!shell->painting) return TIKU_PRESENT_BUSY;
        surface_view(shell, &shell->painted);
#if SURFACE_DESKTOP
        surface_desktop_snapshot(shell);
#endif
#if SURFACE_READER
        if(shell->painted.foreground==SURFACE_READER_APP)surface_reader_hold(shell->painted.reader_page);
#endif
#if SURFACE_IMAGE
        if(shell->painted.foreground==SURFACE_IMAGE_APP)surface_image_hold(shell->painted.image_frame);
#endif
        shell->paint_y = 0;
        shell->dirty = 0;
    }
    unsigned rows = shell->config.paint_rows ? shell->config.paint_rows : 480;
    if (rows > 480 - shell->paint_y) rows = 480 - shell->paint_y;
    tiku_clip_set(shell->painting, (tiku_rect_t){0, (int)shell->paint_y, 800, (int)rows});
#if SURFACE_DESKTOP
    tiku_clip_damage(shell->painting,&(tiku_rect_t){0,(int)shell->paint_y,800,(int)rows});
    surface_desktop_paint(shell,shell->painting);
    tiku_clip_damage(shell->painting,NULL);
#else
    surface_paint(shell->painting, &shell->painted);
#endif
    shell->paint_y += rows;
    if (shell->paint_y < 480) return TIKU_PRESENT_BUSY;
    tiku_clip_reset(shell->painting);
    shell->painting = NULL;
#if SURFACE_READER
    if(shell->painted.foreground==SURFACE_READER_APP){surface_reader_drop(shell->painted.reader_page);shell->painted.reader_page=NULL;}
#endif
#if SURFACE_IMAGE
    if(shell->painted.foreground==SURFACE_IMAGE_APP){surface_image_drop(shell->painted.image_frame);shell->painted.image_frame=NULL;}
#endif
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
#if SURFACE_TEXT_INPUT
    tiku_field_stop(&shell->text_field);
#endif
    if (shell->painting) {
        (void)tiku_framebuffer_abandon(shell->frame);
        shell->painting = NULL;
#if SURFACE_READER
        if(shell->painted.foreground==SURFACE_READER_APP){surface_reader_drop(shell->painted.reader_page);shell->painted.reader_page=NULL;}
#endif
#if SURFACE_IMAGE
    if(shell->painted.foreground==SURFACE_IMAGE_APP){surface_image_drop(shell->painted.image_frame);shell->painted.image_frame=NULL;}
#endif
    }
    shell->switching = 0;
    shell->applications_menu = shell->close_confirmation = 0;
#if SURFACE_DOCUMENTS
    shell->document_menu=0;shell->document_target=SURFACE_HOME;shell->open_path[0]=0;
#endif
    shell->pending = SURFACE_HOME;
    tiku_event_queue_close(shell->events);
    (void)tiku_press_release(&shell->press, 0);
    if (shell->active.owner) (void)tiku_static_app_stop(shell->active.owner);
    reap(&shell->active);
    for (unsigned i = 1; i <= SURFACE_APP_LAST; ++i) {
        if (shell->background[i].owner) (void)tiku_static_app_stop(shell->background[i].owner);
        reap(&shell->background[i]);
    }
}

int surface_destroy(surface_t **pointer)
{
    if (!pointer || !*pointer) return 1;
    surface_t *shell = *pointer;
    if (!shell->stopped) return 0;
#if SURFACE_TRANSFERS
    if(shell->transfer_enable||shell->transfer_stop)return 0;
#endif
    reap(&shell->retired);
    reap(&shell->active);
    for (unsigned i = 1; i <= SURFACE_APP_LAST; ++i) {
        reap(&shell->background[i]);
        if (shell->background[i].owner) return 0;
    }
#if SURFACE_EDITOR
    if(shell->editor_context)return 0;
#endif
    if (shell->active.owner || shell->retired.owner || !tiku_framebuffer_destroy(&shell->frame)) return 0;
    if (!tiku_event_queue_destroy(&shell->events)) return 0;
#if SURFACE_DESKTOP
    surface_desktop_destroy(shell);
#endif
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

void surface_activity(const surface_t *s,surface_app_t *app,tiku_resource_error_t *status)
{
    if(app)*app=s&&s->active.state?s->active.state->id:SURFACE_HOME;
    if(status)*status=s?(s->active.state?s->active.state->status:s->status):TIKU_RESOURCE_UNAVAILABLE;
}
