/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * apps.c - static application lifetime and queued media operations.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_event.h"
#include <string.h>

/** @brief Queue a new browser listing and retire any provider-owned request. */
static void listing(surface_app_state_t *state, uint64_t epoch, uint64_t after)
{
    tiku_paged_storage_cancel(state->store);
    state->queued = 1;
    state->reading = 0;
#if SURFACE_DIRECTORIES
    state->browser->selecting=state->browser->directory=0;
#endif
#if SURFACE_FILE_INFO
    state->file_details=state->file_info_known=0;
#endif
#if SURFACE_FILE_JOBS
    state->file_job = 0;
#if SURFACE_CONTENT_JOBS
    if(!tiku_paged_storage_pending(state->store))tiku_buffer_dispose(&state->copy_buffer);
#endif
#endif
    state->epoch = epoch;
    state->after = after;
    state->count = state->offset = state->restore_offset = 0;
    state->browser->preview[0] = 0;
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
#if SURFACE_IMAGE
    if(state->id==SURFACE_IMAGE_APP&&!surface_image_start(state))return TIKU_RESOURCE_UNAVAILABLE;
#endif
#if SURFACE_NOTES
    if(state->id==SURFACE_NOTES_APP&&!surface_notes_start(state))return TIKU_RESOURCE_UNAVAILABLE;
#endif
#if SURFACE_HEX
#if SURFACE_DIFF
    if(state->id==SURFACE_DIFF_APP&&!surface_diff_start(state))return TIKU_RESOURCE_UNAVAILABLE;
#endif
    if(state->id==SURFACE_HEX_APP&&!surface_hex_start(state))return TIKU_RESOURCE_UNAVAILABLE;
#endif
#if SURFACE_READER
    if(state->id==SURFACE_READER_APP&&!surface_reader_start(state))return TIKU_RESOURCE_UNAVAILABLE;
#endif
#if SURFACE_VFS
    if(state->id==SURFACE_VFS_APP&&!surface_inspector_start(state))return TIKU_RESOURCE_UNAVAILABLE;
#endif
#if SURFACE_DEVICE
    if(state->id==SURFACE_MONITOR_APP&&!surface_device_start(state))return TIKU_RESOURCE_UNAVAILABLE;
#endif
#if SURFACE_TERMINAL
    if(state->id==SURFACE_TERMINAL_APP&&!surface_terminal_start(state))return TIKU_RESOURCE_UNAVAILABLE;
#endif
#if SURFACE_EDITOR
    if(state->id==SURFACE_EDITOR_APP&&!
#if SURFACE_EDITOR_DOCUMENTS
       surface_editor_documents_start(state)
#else
       surface_editor_start(state)
#endif
       )return TIKU_RESOURCE_UNAVAILABLE;
#endif
#if SURFACE_ARITHMETIC
    if(state->id==SURFACE_ARITHMETIC_APP)tiku_calc_clear(&state->arithmetic);
#endif
    state->word_width = 64;
    state->word_base = 16;
#if SURFACE_TEXT_INPUT
    state->range_high = 63;
#endif
    shell->active.state = state;
    if (state->id == SURFACE_BROWSER) {
        if(!tiku_buffer_init(&state->browser_memory,shell->config.allocator,_Alignof(surface_browser_text_t))||
           !tiku_buffer_resize(&state->browser_memory,1,sizeof(surface_browser_text_t),1))return TIKU_RESOURCE_UNAVAILABLE;
        state->browser=state->browser_memory.data;
        state->store = tiku_paged_storage_create(shell->config.allocator,
            shell->config.storage, &shell->config.caller, 16, 1024);
        if (!state->store) return TIKU_RESOURCE_UNAVAILABLE;
        memcpy(state->browser->path, "/", 2);
        listing(state, 0, 0);
    }
    if (state->id == SURFACE_SETTINGS) {
#if SURFACE_PREFERENCES
        surface_preferences_start(state);
#endif
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
#if SURFACE_IMAGE
    if(!state->stopped&&result==TIKU_QUEUE_EVENT&&state->id==SURFACE_IMAGE_APP)surface_image_key(state,event);
#endif
#if SURFACE_NOTES
    if(!state->stopped&&result==TIKU_QUEUE_EVENT&&state->id==SURFACE_NOTES_APP)surface_notes_key(state,event);
#endif
#if SURFACE_READER
    if(!state->stopped&&state->id==SURFACE_READER_APP)surface_reader_key(state,result==TIKU_QUEUE_EVENT?event:NULL);
#endif
#if SURFACE_HEX
#if SURFACE_DIFF
    if(!state->stopped&&state->id==SURFACE_DIFF_APP)surface_diff_key(state,result==TIKU_QUEUE_EVENT?event:NULL);
#endif
    if(!state->stopped&&state->id==SURFACE_HEX_APP)surface_hex_key(state,result==TIKU_QUEUE_EVENT?event:NULL);
#endif
#if SURFACE_TERMINAL
    if(!state->stopped&&result==TIKU_QUEUE_EVENT&&state->id==SURFACE_TERMINAL_APP)surface_terminal_key(state,event);
#endif
#if SURFACE_EDITOR
    if(!state->stopped&&result==TIKU_QUEUE_EVENT&&state->id==SURFACE_EDITOR_APP)surface_editor_key(state,event);
#endif
#if SURFACE_ARITHMETIC
    if (!state->stopped && result==TIKU_QUEUE_EVENT && event->type==TIKU_EVENT_KEY_DOWN &&
        state->id==SURFACE_ARITHMETIC_APP) surface_arithmetic_key(state,event->key,event->modifiers);
#endif
#if SURFACE_TEXT_INPUT
    if(!state->stopped&&result==TIKU_QUEUE_EVENT&&event->type==TIKU_EVENT_KEY_DOWN&&state->id==SURFACE_CALCULATOR&&event->key=='c'&&(event->modifiers&TIKU_MOD_CMD))
        surface_calculator_action(state,state->word_mode==2?ACTION_CHECKSUM_COPY:ACTION_WORD_COPY);
#endif
    if (!state->stopped && result == TIKU_QUEUE_EVENT && event->type == TIKU_EVENT_KEY_DOWN &&
        state->id == SURFACE_CALCULATOR && !(event->modifiers & (TIKU_MOD_CMD | TIKU_MOD_OPTION)))
        surface_calculator_key(state, event->key);
}

/** @brief Close a browser session while its lease protects pending buffers. */
static void stop(void *data)
{
    surface_app_state_t *state = data;
    state->stopped = 1;
#if SURFACE_EDITOR_DOCUMENTS
    surface_editor_documents_stop(state);
#endif
#if SURFACE_NOTES
    surface_notes_stop(state);
#endif
#if SURFACE_TERMINAL
    if(state->id==SURFACE_TERMINAL_APP)surface_terminal_poll(state);
#endif
    state->queued = 0;
    tiku_paged_storage_close(state->store);
}

/** @brief Release a closed storage session after request retirement. */
static void dispose(void *data)
{
    surface_app_state_t *state = data;
    (void)tiku_paged_storage_destroy(&state->store);
#if SURFACE_IMAGE
    surface_image_dispose(state);
#endif
#if SURFACE_NOTES
    surface_notes_dispose(state);
#endif
#if SURFACE_HEX
    surface_hex_dispose(state);
#if SURFACE_DIFF
    surface_diff_dispose(state);
#endif
#endif
    tiku_buffer_dispose(&state->browser_memory);state->browser=NULL;
#if SURFACE_DEVICE
    surface_device_dispose(state);
#if SURFACE_READER
    surface_reader_dispose(state);
#endif
#if SURFACE_VFS
    surface_inspector_dispose(state);
#endif
#endif
#if SURFACE_TERMINAL
    surface_terminal_dispose(state);
#endif
#if SURFACE_EDITOR
    surface_editor_dispose(state);
#endif
#if SURFACE_CONTENT_JOBS
    tiku_buffer_dispose(&state->copy_buffer);
#endif
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
#if SURFACE_NOTES
    if(state&&state->id==SURFACE_NOTES_APP){surface_notes_poll(state);return;}
#endif
#if SURFACE_IMAGE
    if(state&&state->id==SURFACE_IMAGE_APP){surface_image_poll(state);return;}
#endif
#if SURFACE_HEX
#if SURFACE_DIFF
    if(state&&state->id==SURFACE_DIFF_APP){surface_diff_poll(state);return;}
#endif
    if(state&&state->id==SURFACE_HEX_APP){surface_hex_poll(state);return;}
#endif
#if SURFACE_VFS
    if(state&&state->id==SURFACE_VFS_APP){surface_inspector_poll(state);return;}
#if SURFACE_READER
    if(state&&state->id==SURFACE_READER_APP){surface_reader_poll(state);return;}
#endif
#endif
#if SURFACE_DEVICE
    if(state&&state->id==SURFACE_MONITOR_APP){surface_device_poll(state);return;}
#endif
#if SURFACE_TERMINAL
    if(state&&state->id==SURFACE_TERMINAL_APP){surface_terminal_poll(state);return;}
#endif
#if SURFACE_EDITOR
    if(state&&state->id==SURFACE_EDITOR_APP){surface_editor_poll(state);return;}
#endif
#if SURFACE_PREFERENCES
    if(state&&state->id==SURFACE_SETTINGS){surface_preferences_poll(state);surface_inspector_poll(state);}
#endif
    if (state && state->id == SURFACE_SETTINGS && state->settings_status == SURFACE_SETTINGS_PENDING &&
        state->shell->config.settings.read) {
        surface_touch_map_t saved;
        unsigned status = state->shell->config.settings.read(state->shell->config.settings.context, &saved);
        if (status != state->settings_status) {
            state->settings_status = status;
            if (status == SURFACE_SETTINGS_SAVED) state->settings_dirty = 0;
            state->shell->dirty = 1;
        }
    }
    if (!state || !state->store) return;
#if SURFACE_DIRECTORIES
    if(state->file_job==10){uint64_t prepared,total;
        if(tiku_paged_storage_progress(state->store,&prepared,&total)&&
           (prepared!=state->copy_prepared||total!=state->copy_total)){
            state->copy_prepared=prepared;state->copy_total=total;state->shell->dirty=1;}
    }
#endif
    if (state->lease.app && !tiku_paged_storage_pending(state->store)) {
        tiku_app_lease_release(&state->lease);
#if SURFACE_CONTENT_JOBS
        if(!state->file_job || state->stopped)tiku_buffer_dispose(&state->copy_buffer);
#endif
        if (!state->stopped && !state->queued) {
            state->status = tiku_paged_storage_status(state->store);
            const tiku_storage_request_t *result = tiku_paged_storage_result(state->store,
                &state->count, &state->epoch, &state->eof);
            if (result && result->operation == TIKU_STORAGE_LIST) {
                state->offset = state->restore_offset < state->count ? state->restore_offset : 0;
                state->restore_offset = 0;
            }
#if SURFACE_CONTENT_JOBS
            if(state->file_job==6) {
                if(result && state->status==TIKU_RESOURCE_OK) {
                    if(state->count>65536-state->copy_bytes || (!state->eof&&!state->count))
                        state->status=TIKU_RESOURCE_LIMIT;
                    else {
                        memcpy((unsigned char *)state->copy_buffer.data+state->copy_bytes,result->bytes,state->count);
                        state->copy_bytes+=state->count;
                        if(state->eof)state->file_job=state->queued=7;
                        else if(state->copy_bytes==65536)state->status=TIKU_RESOURCE_LIMIT;
                        else state->queued=6;
                    }
                }
                if(!state->queued){state->file_job=0;tiku_buffer_dispose(&state->copy_buffer);}
                else state->status=TIKU_RESOURCE_PENDING;
                state->shell->dirty=1;return;
            }
#endif
            if (result && result->operation == TIKU_STORAGE_READ) {
#if SURFACE_FILE_INFO
                state->file_info_known=tiku_paged_storage_info(state->store,&state->file_length,&state->file_version);
#endif
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
                        state->browser->preview[at++] = "0123456789ABCDEF"[c >> 4];
                        state->browser->preview[at++] = "0123456789ABCDEF"[c & 15u];
                        state->browser->preview[at++] = (i % 16 == 15) ? '\n' : ' ';
                    } else state->browser->preview[at++] = c == '\t' || c == '\r' ? ' ' : (char)c;
                }
                state->browser->preview[at] = 0;
            }
#if SURFACE_FILE_JOBS
            if (state->file_job) {
#if SURFACE_CONTENT_JOBS
                tiku_buffer_dispose(&state->copy_buffer);
#endif
#if SURFACE_DIRECTORIES
                if(state->file_job==8&&state->status==TIKU_RESOURCE_UNSUPPORTED)state->browser->upgrade=1;
                if(state->file_job==9&&state->status==TIKU_RESOURCE_OK)state->browser->upgrade=0;
#endif
                state->file_job=0;
                if (state->status==TIKU_RESOURCE_OK) {state->history_count=0;listing(state,0,0);}
            }
#endif
            state->shell->dirty = 1;
        }
    }
    if (state->stopped || !state->queued || tiku_paged_storage_pending(state->store)) return;
    if (!tiku_app_lease_begin(state->owner, &state->lease)) {
        state->status = TIKU_RESOURCE_LIMIT;
        state->queued = 0;
#if SURFACE_CONTENT_JOBS
        state->file_job=0;tiku_buffer_dispose(&state->copy_buffer);
#endif
        return;
    }
    int operation = state->queued;
    state->queued = 0;
#if SURFACE_FILE_JOBS
#if SURFACE_CONTENT_JOBS
#if SURFACE_DIRECTORIES
    if((operation==3||operation==4)&&state->browser->directory)
        state->status=operation==3?tiku_paged_storage_tree_rename(state->store,state->browser->target,state->epoch,state->node,state->destination):
            tiku_paged_storage_tree_delete(state->store,state->browser->target,state->epoch,state->node);
    else if(operation==10)state->status=tiku_paged_storage_tree_copy(state->store,state->browser->target,state->epoch,state->node,
        state->destination,state->copy_buffer.data,state->copy_buffer.bytes);
    else if(operation==9)state->status=tiku_paged_storage_directory_upgrade(state->store,state->epoch);
    else if(operation==8)state->status=tiku_paged_storage_mkdir(state->store,state->destination,state->epoch);
    else
#endif
    if(operation==6)state->status=tiku_paged_storage_read(state->store,state->browser->target,state->epoch,state->node,state->copy_bytes);
    else if(operation==7)state->status=tiku_paged_storage_replace(state->store,state->destination,state->epoch,0,state->copy_buffer.data,state->copy_bytes);
    else if(operation==5)state->status=tiku_paged_storage_replace(state->store,state->destination,state->epoch,0,NULL,0);
    else
#endif
    if (operation >= 3) state->status = operation == 3 ?
        tiku_paged_storage_rename(state->store,state->browser->target,state->epoch,state->node,state->destination) :
        tiku_paged_storage_delete(state->store,state->browser->target,state->epoch,state->node);
    else
#endif
    state->status = operation == 1 ?
        tiku_paged_storage_list(state->store, state->browser->path, state->epoch, state->after) :
        tiku_paged_storage_read(state->store, state->browser->target, state->epoch, state->node, 0);
    if (!tiku_paged_storage_pending(state->store)) {
        tiku_app_lease_release(&state->lease);
#if SURFACE_FILE_JOBS
        state->file_job=0;
#if SURFACE_CONTENT_JOBS
        tiku_buffer_dispose(&state->copy_buffer);
#endif
#endif
    }
    state->shell->dirty = 1;
}

#if SURFACE_FILE_JOBS
/** @brief Admit file actions on a retained preview through an advertised write provider. */
static int file_ready(surface_app_state_t *state)
{
    return state && !state->stopped && state->id==SURFACE_BROWSER && state->reading &&
        state->browser->target[0] && state->epoch && state->node && !state->queued &&
        !tiku_paged_storage_pending(state->store) &&
        (state->shell->config.storage.capabilities&TIKU_RESOURCE_FEATURE_CHANGE);
}
/** @brief Validate a bounded canonical absolute filename. */
static int valid_file_path(const char *target)
{
    if (!target || target[0]!='/') return 0;
    size_t length=strlen(target);if(length<2 || length>=65 || target[length-1]=='/') return 0;
    const char *part=target+1;
    for(size_t i=1;i<=length;++i) {
        if(i<length && ((unsigned char)target[i]<32 || (unsigned char)target[i]==127 || target[i]=='\\')) return 0;
        if(!target[i] || target[i]=='/') {
            size_t n=(size_t)(target+i-part);
            if(!n || (n==1 && part[0]=='.') || (n==2 && part[0]=='.' && part[1]=='.')) return 0;
            part=target+i+1;
        }
    }
    return 1;
}
int surface_file_rename(surface_app_state_t *state,const char *target)
{
    if(!file_ready(state)||!valid_file_path(target))return 0;
    memcpy(state->destination,target,strlen(target)+1);state->file_job=state->queued=3;
    state->status=TIKU_RESOURCE_PENDING;state->shell->dirty=1;return 1;
}
#endif

#if SURFACE_CONTENT_JOBS
/** @brief Admit a new file from a completed writable folder view. */
static int create_ready(surface_app_state_t *state)
{
    return state&&!state->stopped&&state->id==SURFACE_BROWSER&&!state->reading&&!state->queued&&
        state->epoch&&state->status==TIKU_RESOURCE_OK&&!tiku_paged_storage_pending(state->store)&&
        (state->shell->config.storage.capabilities&TIKU_RESOURCE_FEATURE_CHANGE);
}
int surface_file_copy(surface_app_state_t *state,const char *target)
{
    if(!file_ready(state)||!valid_file_path(target)||!strcmp(state->browser->target,target)
       )return 0;
    if(!tiku_buffer_init(&state->copy_buffer,state->shell->config.allocator,1)||
        !tiku_buffer_resize(&state->copy_buffer,65536,1,0)) {
        state->status=TIKU_RESOURCE_LIMIT;state->shell->dirty=1;return 0;
    }
    memcpy(state->destination,target,strlen(target)+1);state->copy_bytes=0;
#if SURFACE_DIRECTORIES
    state->copy_prepared=state->copy_total=0;
    state->file_job=state->queued=state->browser->directory?10:6;
#else
    state->file_job=state->queued=6;
#endif
    state->status=TIKU_RESOURCE_PENDING;state->shell->dirty=1;return 1;
}
int surface_file_create(surface_app_state_t *state,const char *target)
{
    if(!create_ready(state)||!valid_file_path(target))return 0;
    memcpy(state->destination,target,strlen(target)+1);state->file_job=state->queued=
#if SURFACE_DIRECTORIES
        state->file_dialog==4?8:
#endif
        5;
    state->status=TIKU_RESOURCE_PENDING;state->shell->dirty=1;return 1;
}
#endif

void surface_app_action(surface_app_state_t *state, int action)
{
#if SURFACE_NOTES
    if(state&&state->id==SURFACE_NOTES_APP){surface_notes_action(state,action);return;}
#endif
#if SURFACE_IMAGE
    if(state&&state->id==SURFACE_IMAGE_APP){surface_image_action(state,action);return;}
#endif
#if SURFACE_HEX
#if SURFACE_DIFF
    if(state&&state->id==SURFACE_DIFF_APP){surface_diff_action(state,action);return;}
#endif
    if(state&&state->id==SURFACE_HEX_APP){surface_hex_action(state,action);return;}
#endif
#if SURFACE_VFS
    if(state&&state->id==SURFACE_VFS_APP){surface_inspector_action(state,action);return;}
#if SURFACE_READER
    if(state&&state->id==SURFACE_READER_APP){surface_reader_action(state,action);return;}
#endif
#endif
#if SURFACE_DEVICE
    if(state&&state->id==SURFACE_MONITOR_APP){surface_device_action(state,action);return;}
#endif
#if SURFACE_FILE_INFO
    if(state&&state->id==SURFACE_BROWSER&&action==ACTION_FILE_INFO){state->file_details^=1;state->shell->dirty=1;return;}
#endif
#if SURFACE_TERMINAL
    if(state&&state->id==SURFACE_TERMINAL_APP){surface_terminal_action(state,action);return;}
#endif
    if (!state || state->stopped) return;
#if SURFACE_EDITOR
    if(state->id==SURFACE_EDITOR_APP){surface_editor_action(state,action);return;}
#endif
#if SURFACE_ARITHMETIC
    if(state->id==SURFACE_ARITHMETIC_APP){surface_arithmetic_action(state,action);return;}
#endif
    if (state->id == SURFACE_CALCULATOR) {
        if (action >= ACTION_HEX && action < ACTION_HEX + 16)
            surface_calculator_key(state, (unsigned)"0123456789ABCDEF"[action - ACTION_HEX]);
        else if (action == ACTION_ERASE) surface_calculator_key(state, TIKU_KEY_BACKSPACE);
        else if (action == ACTION_CLEAR) surface_calculator_key(state, 'X');
        else surface_calculator_action(state, action);
        return;
    }
    if (state->id == SURFACE_SETTINGS) {
#if SURFACE_TRANSFERS
        if(surface_transfers_action(state,action))return;
#endif
#if SURFACE_PREFERENCES
        if(surface_preferences_action(state,action)||state->preferences_status==SURFACE_SETTINGS_PENDING)return;
#endif
        if (state->settings_status == SURFACE_SETTINGS_PENDING ||
            state->settings_status == SURFACE_SETTINGS_UNAVAILABLE ||
            state->settings_status == SURFACE_SETTINGS_INCOMPATIBLE) return;
        if ((action >= ACTION_SWAP && action <= ACTION_INVERT_Y) || action == ACTION_DEFAULTS)
            state->settings_dirty = 1;
        if (action == ACTION_SWAP) state->calibration.swap ^= 1;
        else if (action == ACTION_INVERT_X) state->calibration.invert_x ^= 1;
        else if (action == ACTION_INVERT_Y) state->calibration.invert_y ^= 1;
        else if (action == ACTION_DEFAULTS)
            state->calibration = (surface_touch_map_t){0,799,0,479,0,0,0};
        else if (action == ACTION_SAVE && state->shell->config.settings.save)
            state->settings_status = state->shell->config.settings.save(state->shell->config.settings.context,
                &state->calibration) ? SURFACE_SETTINGS_PENDING : SURFACE_SETTINGS_FAILED;
        state->shell->dirty = 1;
        return;
    }
    if (state->id != SURFACE_BROWSER) return;
#if SURFACE_FILE_JOBS
#if SURFACE_CONTENT_JOBS
#if SURFACE_DIRECTORIES
    if(action==ACTION_FOLDER_SELECT&&create_ready(state)){state->browser->selecting^=1;state->shell->dirty=1;return;}
#endif
    if(action==ACTION_FILE_COPY && file_ready(state)
       ){state->file_dialog=3;surface_text_open(state->shell);return;}
#if SURFACE_DIRECTORIES
    if((action==ACTION_FOLDER_NEW||action==ACTION_FOLDER_ENABLE_CONFIRMED)&&state->browser->upgrade&&
        !state->reading&&!state->queued&&state->epoch&&!tiku_paged_storage_pending(state->store)&&
        state->shell->config.caller.authenticated&&(state->shell->config.caller.scopes&TIKU_RESOURCE_HOST_CONTROL)&&
        (state->shell->config.caller.scopes&TIKU_RESOURCE_WRITE)&&
        (state->shell->config.storage.capabilities&TIKU_RESOURCE_FEATURE_CHANGE)){
        if(action==ACTION_FOLDER_NEW)state->shell->close_confirmation=4;
        else {state->file_job=state->queued=9;state->status=TIKU_RESOURCE_PENDING;}
        state->shell->dirty=1;return;
    }
#endif
    if((action==ACTION_FILE_NEW
#if SURFACE_DIRECTORIES
        ||action==ACTION_FOLDER_NEW
#endif
        ) && create_ready(state) && strlen(state->browser->path)<=60){
        size_t n=strlen(state->browser->path);memcpy(state->destination,state->browser->path,n);
        if(n>1)state->destination[n++]='/';
        memcpy(state->destination+n,"new",4);state->file_dialog=
#if SURFACE_DIRECTORIES
            action==ACTION_FOLDER_NEW?4:
#endif
            2;surface_text_open(state->shell);return;
    }
#endif
    if (action==ACTION_FILE_RENAME && file_ready(state)) {
#if SURFACE_CONTENT_JOBS
        state->file_dialog=1;
#endif
        surface_text_open(state->shell);return;
    }
    if (action==ACTION_FILE_DELETE && file_ready(state)) {
        state->shell->close_confirmation=2;state->shell->dirty=1;return;
    }
    if (action==ACTION_FILE_DELETE_CONFIRMED && file_ready(state)) {
        state->file_job=state->queued=4;state->status=TIKU_RESOURCE_PENDING;state->shell->dirty=1;return;
    }
#endif
    if (action == ACTION_REFRESH) { state->history_count = 0; listing(state, 0, 0); return; }
    const tiku_storage_request_t *result = tiku_paged_storage_result(state->store,
            &(size_t){0}, &(uint64_t){0}, &(int){0});
    if (!result || result->operation != TIKU_STORAGE_LIST || state->queued) return;
    if (action == ACTION_PREVIOUS) {
        if (state->offset >= SURFACE_BROWSER_ROWS) state->offset -= SURFACE_BROWSER_ROWS;
        else if (state->history_count) {
            unsigned n = --state->history_count;
            listing(state, state->epoch, state->history[n].after);
            state->restore_offset = state->history[n].offset;
        }
    } else if (action == ACTION_NEXT) {
        if (state->offset + SURFACE_BROWSER_ROWS < state->count) state->offset += SURFACE_BROWSER_ROWS;
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
    } else if (action >= ACTION_ROW && action < ACTION_ROW + SURFACE_BROWSER_ROWS) {
        size_t index = state->offset + (unsigned)(action - ACTION_ROW);
        if (index >= state->count) return;
        const tiku_resource_entry_t *entry = &result->entries[index];
#if SURFACE_DIRECTORIES
        if(entry->kind==TIKU_RESOURCE_DIRECTORY&&state->browser->selecting){
            memcpy(state->browser->target,entry->path,sizeof state->browser->target);state->browser->preview[0]=0;
            state->browser->directory=1;state->browser->selecting=0;state->node=entry->node_id;
            state->reading=1;state->count=0;state->status=TIKU_RESOURCE_OK;
#if SURFACE_FILE_INFO
            state->file_details=state->file_info_known=0;
#endif
            state->shell->dirty=1;return;
        }
#endif
        if (entry->kind == TIKU_RESOURCE_DIRECTORY) {
            state->history_count = 0;
            memcpy(state->browser->path, entry->path, sizeof state->browser->path);
            listing(state, 0, 0);
        } else {
#if SURFACE_DIRECTORIES
            state->browser->directory=state->browser->selecting=0;
#endif
            memcpy(state->browser->target, entry->path, sizeof state->browser->target);
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
#if SURFACE_DIFF
    if(state&&state->id==SURFACE_DIFF_APP)return surface_diff_back(state);
#endif
#if SURFACE_NOTES
    if(state&&state->id==SURFACE_NOTES_APP)return surface_notes_back(state);
#endif
#if SURFACE_TRANSFERS
    if(state&&state->id==SURFACE_SETTINGS&&state->transfers_page){
        if(state->shell->transfer_confirm)state->shell->transfer_confirm=0;else state->transfers_page=0;
        state->shell->dirty=1;return 1;
    }
#endif
#if SURFACE_HEX
    if(state&&state->id==SURFACE_HEX_APP)return surface_hex_back(state);
#endif
#if SURFACE_PREFERENCES
    if(state&&state->id==SURFACE_SETTINGS&&state->preferences_page){state->preferences_page=state->preferences_page==3?2:0;state->shell->dirty=1;return 1;}
#endif
#if SURFACE_EDITOR
    if(state&&state->id==SURFACE_EDITOR_APP)return surface_editor_back(state);
#endif
#if SURFACE_DEVICE
    if(state&&state->id==SURFACE_MONITOR_APP)return surface_device_back(state);
#endif
#if SURFACE_VFS
    if(state&&state->id==SURFACE_VFS_APP)return surface_inspector_back(state);
#if SURFACE_READER
    if(state&&state->id==SURFACE_READER_APP)return surface_reader_back(state);
#endif
#endif
    if (!state || state->stopped || state->id != SURFACE_BROWSER) return 0;
    if (state->reading) { state->history_count = 0; listing(state, 0, 0); return 1; }
    if (!strcmp(state->browser->path, "/")) return 0;
    state->history_count = 0;
    char *slash = strrchr(state->browser->path, '/');
    if (slash == state->browser->path) slash[1] = 0;
    else if (slash) *slash = 0;
    listing(state, 0, 0);
    return 1;
}
