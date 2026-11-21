/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * surface.h - serialized local shell and static application ownership.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_H
#define SURFACE_H
#include "tiku_clock.h"
#include "tiku_framebuffer.h"
#include "tiku_paged_storage.h"
#include "tiku_input.h"
#include "settings.h"
#ifndef SURFACE_DESKTOP
#define SURFACE_DESKTOP 0
#endif
#define SURFACE_BROWSER_ROWS (SURFACE_DESKTOP ? 8 : 6)
#ifndef SURFACE_TRANSFERS
#define SURFACE_TRANSFERS 0
#endif
#if SURFACE_TRANSFERS
#include "transfer_contract.h"
#endif
#ifndef SURFACE_EDITOR
#define SURFACE_EDITOR 0
#endif
#if SURFACE_EDITOR
#include "tiku_edit_control.h"
#endif
#ifndef SURFACE_CONTENT_JOBS
#define SURFACE_CONTENT_JOBS 0
#endif
#ifndef SURFACE_ARITHMETIC
#define SURFACE_ARITHMETIC 0
#endif
#ifndef SURFACE_FILE_JOBS
#define SURFACE_FILE_JOBS 0
#endif
#ifndef SURFACE_TEXT_INPUT
#define SURFACE_TEXT_INPUT 0
#endif

#ifndef SURFACE_DIRECTORIES
#define SURFACE_DIRECTORIES 0
#endif
#ifndef SURFACE_PREFERENCES
#define SURFACE_PREFERENCES 0
#endif
#ifndef SURFACE_DOCUMENTS
#define SURFACE_DOCUMENTS 0
#endif
#ifndef SURFACE_EDITOR_LINES
#define SURFACE_EDITOR_LINES 0
#endif
#ifndef SURFACE_EDITOR_DOCUMENTS
#define SURFACE_EDITOR_DOCUMENTS 0
#endif
#ifndef SURFACE_IMAGE
#define SURFACE_IMAGE 0
#endif
#ifndef SURFACE_NOTES
#define SURFACE_NOTES 0
#endif
#ifndef SURFACE_DIFF
#define SURFACE_DIFF 0
#endif
#ifndef SURFACE_HEX
#define SURFACE_HEX 0
#endif
#ifndef SURFACE_READER
#define SURFACE_READER 0
#endif
#ifndef SURFACE_VFS
#define SURFACE_VFS 0
#endif
#if SURFACE_VFS
#include "vfs_contract.h"
#endif
#ifndef SURFACE_DEVICE
#define SURFACE_DEVICE 0
#endif
#if SURFACE_DEVICE
#include "process_contract.h"
/** @brief Serialized local device observations; return copied bytes or a negative error. */
typedef struct {void *context;int (*read)(void *,const char *,char *,size_t);
    int (*process)(void *,unsigned,surface_process_info_t *);} surface_device_provider_t;
#endif
#ifndef SURFACE_BASIC
#define SURFACE_BASIC 0
#endif
#if SURFACE_BASIC
/** @brief Job results copied from a serialized local programming provider. */
enum { SURFACE_PROGRAM_NONE, SURFACE_PROGRAM_RUNNING, SURFACE_PROGRAM_DONE,
       SURFACE_PROGRAM_ERROR, SURFACE_PROGRAM_STOPPED };
/** @brief Start consumes source before return; state and stop address this owner. */
typedef struct {
    void *context;
    int (*start)(void *,const char *,size_t);
    int (*state)(void *);
    int (*stop)(void *);
} surface_program_provider_t;
#endif
#ifndef SURFACE_FILE_INFO
#define SURFACE_FILE_INFO 0
#endif
#ifndef SURFACE_TERMINAL
#define SURFACE_TERMINAL 0
#endif
#if SURFACE_TERMINAL
#include "tiku_terminal_model.h"
#endif
/** @brief Static application identities; HOME backgrounds the foreground owner. */
typedef enum { SURFACE_HOME, SURFACE_CALCULATOR, SURFACE_BROWSER, SURFACE_SETTINGS, SURFACE_ARITHMETIC_APP, SURFACE_EDITOR_APP, SURFACE_TERMINAL_APP, SURFACE_MONITOR_APP, SURFACE_VFS_APP, SURFACE_READER_APP, SURFACE_HEX_APP, SURFACE_DIFF_APP, SURFACE_NOTES_APP, SURFACE_IMAGE_APP,
    SURFACE_APP_LAST = SURFACE_IMAGE ? SURFACE_IMAGE_APP : SURFACE_NOTES ? SURFACE_NOTES_APP : SURFACE_DIFF ? SURFACE_DIFF_APP : SURFACE_HEX ? SURFACE_HEX_APP : SURFACE_READER ? SURFACE_READER_APP : SURFACE_VFS ? SURFACE_VFS_APP : SURFACE_DEVICE ? SURFACE_MONITOR_APP : SURFACE_TERMINAL ? SURFACE_TERMINAL_APP : SURFACE_EDITOR ? SURFACE_EDITOR_APP : SURFACE_ARITHMETIC ? SURFACE_ARITHMETIC_APP : SURFACE_SETTINGS } surface_app_t;
/** @brief Close admission; confirmation preserves the application until resolved. */
typedef enum { SURFACE_CLOSE_BUSY, SURFACE_CLOSE_ACCEPTED, SURFACE_CLOSE_CONFIRM } surface_close_result_t;
/** @brief Opaque owner; call operations and provider completions on one thread. */
typedef struct surface surface_t;
/** @brief Copy active task identity and status without constructing a presentation snapshot. */
void surface_activity(const surface_t *,surface_app_t *,tiku_resource_error_t *);
/** @brief Providers and their contexts outlive the shell and pending work. */
typedef struct {
#if SURFACE_TRANSFERS
    surface_transfer_provider_t transfers;
#endif
#if SURFACE_TERMINAL
    tiku_terminal_provider_t terminal;
#if SURFACE_BASIC
    surface_program_provider_t program;
#endif
#endif
#if SURFACE_DEVICE
    surface_device_provider_t device;
#if SURFACE_VFS
    surface_vfs_provider_t vfs;
#endif
#endif
    tiku_allocator_t allocator;
    tiku_clock_t clock;
    tiku_storage_provider_t storage;
    tiku_resource_caller_t caller;
    const char *platform_name, *build_identity, *media_description, *services_description;
    surface_settings_provider_t settings;
#if SURFACE_PREFERENCES
    surface_preferences_provider_t preferences;
#endif
    unsigned paint_rows; /**< rows per render call, 1..480; zero paints the full frame */
} surface_config_t;
/** @brief Copied presentation state; strings remain valid independently of apps. */
typedef struct {
#if SURFACE_IMAGE
    const void *image_frame;
    char image_path[65];
    unsigned home_page,task_page,image_width,image_height,image_rotation,image_watch;
    int image_busy,image_error; double image_zoom,image_pan_x,image_pan_y;
#endif
#if SURFACE_TRANSFERS
    surface_transfer_snapshot_t transfer;
    unsigned transfers_page,transfer_confirm,transfer_enable_pending,transfer_stop_pending,transfer_completed;
    int transfer_result;
#endif
#if SURFACE_FILE_INFO
    uint64_t file_length,file_version,file_epoch;
    int file_details,file_info_known,file_can_write;
#endif
#if SURFACE_EDITOR
    int file_open;
#endif
#if SURFACE_DEVICE || SURFACE_TERMINAL || SURFACE_EDITOR
    union {
#if SURFACE_DIFF
        struct {
            char diff_path[2][65],diff_text[2][8][80];
            unsigned diff_line[2][8],diff_kind[8],diff_first,diff_total,diff_rows,diff_column,diff_columns;
            unsigned diff_removed,diff_added,diff_loaded;
            int diff_busy,diff_ready,diff_flags,diff_dialog,diff_binary,diff_equal;
            int diff_tools,diff_export,diff_patch_result;
        };
#endif
#if SURFACE_HEX
        struct {
            char byte_path[65];
            unsigned char byte_cells[128];
            size_t byte_size,byte_cursor,byte_first,byte_count;
            int byte_dirty,byte_busy,byte_dialog,byte_loaded,byte_writable,byte_search;
            int byte_input_page,byte_ascii,byte_editing,byte_high;
        };
#endif
#if SURFACE_VFS
#if SURFACE_READER
        struct {
            const void *reader_page;
            char reader_path[65];
            int reader_scroll,reader_height,reader_busy,reader_error,reader_history;
            unsigned reader_notices;
            int reader_find_dialog,reader_searching,reader_match,reader_match_length,reader_matches,reader_ordinal;
            char reader_query[256];
            unsigned reader_tools,reader_heading_first,reader_heading_count,reader_heading_rows;
            unsigned reader_heading_level[6];
            char reader_heading_title[6][65];
            int reader_copied;
        };
#endif
        struct {
            surface_vfs_node_t inspector_rows[6],inspector_node;
            char inspector_path[64],inspector_value[1025];
            size_t inspector_total,inspector_bytes;
            unsigned inspector_count,inspector_offset;
            int inspector_more,inspector_dynamic,inspector_reading,inspector_dialog;
            int inspector_phase,inspector_error,inspector_loaded,inspector_saved;
        };
#endif
#if SURFACE_DEVICE
        struct {
            uint32_t monitor_values[7];
            float monitor_history[120];
            unsigned char monitor_gaps[120];
            unsigned monitor_valid,monitor_count;
            int monitor_paused,monitor_sampling;
            surface_process_info_t monitor_processes[8];
            unsigned monitor_process_present,monitor_process_valid,monitor_page,monitor_selected;
            int monitor_mode;
        };
#endif
#if SURFACE_TERMINAL
        struct {
    tiku_terminal_cell_t terminal_cells[TIKU_TERMINAL_ROWS][TIKU_TERMINAL_COLS];
    int terminal_row,terminal_col,terminal_cursor,terminal_view,terminal_error,terminal_lost;
    unsigned terminal_pending;
#if SURFACE_BASIC
    int program_state;
#endif
        };
#endif
#if SURFACE_EDITOR
        struct {
#if SURFACE_NOTES
    int notes_selected,notes_tools,notes_confirm,notes_loading;
    unsigned notes_page,notes_count,notes_paper,notes_important;
    char notes_titles[6][65];
    unsigned notes_state[6],notes_colors[6];
#endif
#if SURFACE_EDITOR_DOCUMENTS
    int editor_sessions,editor_list,editor_confirm;
    unsigned editor_count,editor_page,editor_states[6];
    char editor_titles[6][65];
#endif
    tiku_edit_grid_t editor_grid;
    char editor_path[65];
    size_t editor_bytes;
    int editor_dirty,editor_busy,editor_dialog,editor_error,editor_select;
    int editor_tools,editor_wrapped;
    unsigned editor_search_flags;
    size_t editor_replaced;
    char editor_query[256];
        };
#endif
    };
#endif
#if SURFACE_TEXT_INPUT
    int text_entry, text_caret, text_a, text_b, text_scroll, text_symbols, text_shift, text_error, text_pressed;
    char text_value[256];
    unsigned word_mode, bit_page, range_select, range_pending, range_high, range_low, range_copied;
    char range_hex[65];
    uint64_t word_value;
    char checksum_input[256], crc32[65], crc16[65];
    unsigned checksum_text, word_copied;
    size_t checksum_bytes;
#endif
#if SURFACE_FILE_JOBS
    unsigned file_job;
#if SURFACE_CONTENT_JOBS
    unsigned file_dialog;
    size_t copy_bytes;
#if SURFACE_DIRECTORIES
    uint64_t copy_prepared,copy_total;
#endif
    int file_create;
#endif
#if SURFACE_DIRECTORIES
    int file_selecting,file_directory,file_upgrade;
#endif
    int file_actions, media_writable;
    char file_path[65];
#endif
#if SURFACE_ARITHMETIC
    char arithmetic[24];
    int arithmetic_copied;
#endif
    surface_app_t foreground, pending;
    int retiring, stopped, pressed, clock_failed, display_failed;
    unsigned open_apps;
    int applications_menu, close_confirmation, close_enabled;
#if SURFACE_DOCUMENTS
    int document_menu,document_count;
    surface_app_t document_apps[3],document_default,document_error;
    unsigned document_relations[3];
    char document_type[48];
#endif
    tiku_resource_error_t status;
    uint64_t now;
    char platform_name[64], build_identity[96], media_description[96], services_description[96];
    unsigned word_width, word_base;
    char hex[65], bases[4][65];
    char path[TIKU_RESOURCE_PATH_MAX], rows[SURFACE_BROWSER_ROWS][64], preview[1025];
#if SURFACE_PREFERENCES
    surface_preferences_t preferences;
    unsigned preferences_device;
    int preferences_device_error;
    unsigned preferences_status,preferences_dirty,preferences_page;
    uint32_t desktop_color;
#endif
    surface_touch_map_t calibration;
    unsigned settings_status, settings_dirty;
    unsigned row_count;
    int directory[SURFACE_BROWSER_ROWS], more, previous, reading, preview_hex;
    size_t preview_bytes;
} surface_view_t;
#if SURFACE_PREFERENCES
/** @brief Refresh applied desktop choices from the provider's copied saved state. */
void surface_preferences_refresh(surface_t *);
#endif
/** @brief Construct an 800 by 480 shell; failure releases partial allocations. */
surface_t *surface_create(const surface_config_t *config);
/** @brief Coalesce a launch, invalidate queued input and reject invalid IDs/stop. */
int surface_launch(surface_t *, surface_app_t);
/** @brief Close the foreground app; unsaved settings require a discard decision. */
surface_close_result_t surface_close(surface_t *);
/** @brief Resolve a pending close; zero keeps editing, nonzero discards the draft. */
int surface_resolve_close(surface_t *, int discard);
/** @brief Enqueue input; overflow cancels the active interaction. */
int surface_post(surface_t *, const tiku_input_event_t *);
/** @brief Process at most eight shell events, four app events and one storage step. */
void surface_step(surface_t *);
/**
 * @brief Paint up to paint_rows from one copied view; BUSY includes partial work.
 * @note Changes during painting queue another frame. Accepted frames remain
 * immutable until completion; stop abandons a partial frame.
 */
tiku_present_result_t surface_render(surface_t *, tiku_presenter_t);
/** @brief Return nonzero for queued repaint or partial painting on a running shell. */
int surface_render_pending(const surface_t *);
/** @brief Copy visible state; a NULL owner yields a stopped HOME view. */
void surface_view(const surface_t *, surface_view_t *);
/** @brief Stop input and applications; provider requests retain their memory. */
void surface_stop(surface_t *);
/** @brief Destroy a stopped shell after retirement; busy leaves pointer intact. */
int surface_destroy(surface_t **);
/** @brief Repaint visible Device information after its borrowed service label changes.
 * @note Call from the serialized owner; the configured label storage outlives the shell.
 * A partial frame retains its frozen label until completion.
 */
void surface_services_changed(surface_t *);
#endif
