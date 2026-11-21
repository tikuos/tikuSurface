/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * surface_internal.h - product state shared by ownership, apps and painting.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SURFACE_INTERNAL_H
#define SURFACE_INTERNAL_H
#include "surface.h"
#include "tiku_static_app.h"
#include "tiku_press.h"
#if SURFACE_ARITHMETIC
#include "tiku_calc_model.h"
#endif
#if SURFACE_TEXT_INPUT
#include "tiku_field_event.h"
#endif

/** @brief Product action IDs shared by layout and input routing. */
enum { ACTION_HOME = 1, ACTION_BACK, ACTION_APPLICATIONS, ACTION_CLOSE,
    ACTION_KEEP, ACTION_DISCARD, ACTION_LAUNCH_PREVIOUS=60,ACTION_LAUNCH_NEXT,ACTION_TASK = 40, ACTION_CALCULATOR = 10,
    ACTION_BROWSER, ACTION_SETTINGS, ACTION_ARITHMETIC, ACTION_EDITOR, ACTION_TERMINAL, ACTION_MONITOR, ACTION_INSPECTOR, ACTION_READER, ACTION_BYTES, ACTION_DIFF, ACTION_NOTES, ACTION_IMAGE, ACTION_HEX = 100, ACTION_ERASE = 116,
    ACTION_CLEAR, ACTION_WORD_WIDTH, ACTION_WORD_BASE, ACTION_WORD_SWAP,
    ACTION_WORD_EDIT, ACTION_WORD_TAB, ACTION_BITS_TAB, ACTION_CHECKSUM_TAB,
    ACTION_BIT_PAGE, ACTION_RANGE_SELECT, ACTION_RANGE_COPY,
    ACTION_CHECKSUM_MODE, ACTION_CHECKSUM_COPY, ACTION_WORD_COPY, ACTION_BIT = 400,
    ACTION_ARITHMETIC_KEY = 500, ACTION_ARITHMETIC_COPY = 700, ACTION_FILE_EDIT,
    ACTION_FILE_INFO=702,ACTION_FOLDER_NEW,ACTION_FOLDER_SELECT,ACTION_FOLDER_ENABLE_CONFIRMED,
    ACTION_DIFF_LEFT=2700,ACTION_DIFF_RIGHT,ACTION_DIFF_SPACE,ACTION_DIFF_CR,ACTION_DIFF_UP,ACTION_DIFF_DOWN,ACTION_DIFF_PREVIOUS,ACTION_DIFF_NEXT,ACTION_DIFF_CANCEL,ACTION_DIFF_TEXT_LEFT,ACTION_DIFF_TEXT_RIGHT,ACTION_DIFF_SWAP,ACTION_DIFF_TOOLS,ACTION_DIFF_DOCUMENT,ACTION_DIFF_SAVE_PATCH,ACTION_DIFF_COPY_PATCH,
    ACTION_NOTES_NEW=2900,ACTION_NOTES_SAVE_ALL,ACTION_NOTES_LIST,ACTION_NOTES_PREVIOUS,ACTION_NOTES_NEXT,ACTION_NOTES_TOOLS,ACTION_NOTES_DOCUMENT,ACTION_NOTES_IMPORTANT,ACTION_NOTES_DELETE,ACTION_NOTES_DELETE_CONFIRMED,ACTION_NOTES_KEEP,ACTION_NOTES_REFRESH,ACTION_NOTES_DUPLICATE,ACTION_NOTES_ROW=2920,ACTION_NOTES_PAPER=2930,
    ACTION_TRANSFERS_OPEN=2800,ACTION_TRANSFERS_ENABLE,ACTION_TRANSFERS_STOP,ACTION_TRANSFERS_REFRESH,ACTION_TRANSFERS_CONFIRM,ACTION_TRANSFERS_KEEP,
    ACTION_INSPECTOR_PATH=1700,ACTION_INSPECTOR_REFRESH,ACTION_INSPECTOR_PREVIOUS,ACTION_INSPECTOR_NEXT,ACTION_INSPECTOR_READ,ACTION_INSPECTOR_EDIT,ACTION_INSPECTOR_COPY,ACTION_INSPECTOR_ROW=1720,
    ACTION_READER_PATH=1800,ACTION_READER_HELP,ACTION_READER_UP,ACTION_READER_DOWN,ACTION_READER_CANCEL,ACTION_FILE_READ,ACTION_READER_FIND,ACTION_READER_PREVIOUS,ACTION_READER_NEXT,ACTION_READER_TOOLS,ACTION_READER_OUTLINE,ACTION_READER_COPY,ACTION_READER_DOCUMENT,ACTION_READER_OUTLINE_PREVIOUS,ACTION_READER_OUTLINE_NEXT,ACTION_READER_HEADING=1830,ACTION_READER_LINK=1900,
    ACTION_MONITOR_REFRESH=1600,ACTION_MONITOR_PAUSE,ACTION_MONITOR_CLEAR,
    ACTION_MONITOR_PROCESSES,ACTION_MONITOR_OVERVIEW,ACTION_MONITOR_PREVIOUS,ACTION_MONITOR_NEXT,ACTION_MONITOR_ROW=1620,
    ACTION_TERM_COMMAND=900,ACTION_TERM_CANCEL,ACTION_TERM_UP,ACTION_TERM_DOWN,ACTION_TERM_COPY,ACTION_TERM_RESET,
    ACTION_EDITOR_SAVE=800,ACTION_EDITOR_SAVE_AS,ACTION_EDITOR_TYPE,ACTION_EDITOR_FIND,ACTION_EDITOR_UNDO,ACTION_EDITOR_REDO,ACTION_EDITOR_SELECT,ACTION_EDITOR_COPY,ACTION_EDITOR_CUT,ACTION_EDITOR_PASTE,ACTION_EDITOR_NEWLINE,ACTION_EDITOR_RUN,ACTION_EDITOR_CELL=1000,
    ACTION_EDITOR_LINES=3040,ACTION_EDITOR_DUPLICATE,ACTION_EDITOR_MOVE_UP,ACTION_EDITOR_MOVE_DOWN,ACTION_EDITOR_DELETE_LINES,ACTION_EDITOR_INDENT,ACTION_EDITOR_UNINDENT,
    ACTION_EDITOR_SESSIONS=3000,ACTION_EDITOR_SESSION_NEW,ACTION_EDITOR_SESSION_CLOSE,ACTION_EDITOR_SESSION_PREVIOUS,ACTION_EDITOR_SESSION_NEXT,ACTION_EDITOR_KEEP,ACTION_EDITOR_DISCARD,ACTION_EDITOR_SESSION_ROW=3020,
    ACTION_EDITOR_TOOLS=820,ACTION_EDITOR_NEXT,ACTION_EDITOR_PREVIOUS,ACTION_EDITOR_REPLACE,ACTION_EDITOR_REPLACE_ALL,ACTION_EDITOR_GOTO,ACTION_EDITOR_CASE,ACTION_EDITOR_WORD,ACTION_EDITOR_ALL,ACTION_EDITOR_DOCUMENT,
    ACTION_PREFS_OPEN=2500,ACTION_PREFS_TOUCH,ACTION_PREFS_SAVE,ACTION_PREFS_DEFAULTS,ACTION_PREFS_TEXT,ACTION_PREFS_HTML,ACTION_PREFS_MARKDOWN,ACTION_PREFS_DEVICE=2510,ACTION_PREFS_DEVICE_LIST,ACTION_PREFS_COLOR=2520,ACTION_PREFS_DEVICE_ROW=2530,
    ACTION_DOCUMENT_OPEN=2400,ACTION_DOCUMENT_WITH,ACTION_DOCUMENT_CANCEL,ACTION_DOCUMENT_COMPARE,ACTION_DOCUMENT_CHOICE=2410,
    ACTION_BYTE_INPUT=2130,ACTION_BYTE_NAVIGATION,ACTION_BYTE_HEX_MODE,ACTION_BYTE_ASCII_MODE,ACTION_BYTE_DOCUMENT,
    ACTION_BYTE_OPEN=2100,ACTION_BYTE_SAVE,ACTION_BYTE_SAVE_AS,ACTION_BYTE_EDIT,ACTION_BYTE_UNDO,ACTION_BYTE_REDO,ACTION_BYTE_GOTO,ACTION_BYTE_FIND,ACTION_BYTE_PREVIOUS,ACTION_BYTE_NEXT,ACTION_BYTE_COPY,ACTION_BYTE_CANCEL,ACTION_FILE_BYTES,ACTION_BYTE_CELL=2200,
    ACTION_ROW = 200, ACTION_NEXT = 210, ACTION_REFRESH, ACTION_PREVIOUS, ACTION_FILE_RENAME, ACTION_FILE_DELETE, ACTION_FILE_DELETE_CONFIRMED, ACTION_FILE_NEW, ACTION_FILE_COPY, ACTION_SWAP = 300, ACTION_INVERT_X, ACTION_INVERT_Y, ACTION_SAVE, ACTION_DEFAULTS };
/** @brief Text storage allocated by the file browser. */
typedef struct {
    char path[TIKU_RESOURCE_PATH_MAX],target[TIKU_RESOURCE_PATH_MAX],preview[1025];
#if SURFACE_DIRECTORIES
    int selecting,directory,upgrade;
#endif
} surface_browser_text_t;
/** @brief Owned app state; a request lease retains it through cancellation. */
typedef struct {
#if SURFACE_IMAGE
    struct surface_image *image;
#endif
    struct surface *shell;
    tiku_static_app_t *owner;
    surface_app_t id;
    int stopped, queued, reading;
#if SURFACE_TRANSFERS
    unsigned transfers_page;
#endif
    tiku_paged_storage_t *store;
    tiku_app_lease_t lease;
    tiku_resource_error_t status;
#if SURFACE_EDITOR
    struct surface_editor *editor;
#if SURFACE_EDITOR_DOCUMENTS
    struct surface_editor_documents *documents;
#endif
#if SURFACE_NOTES
    struct surface_notes *notes;
#endif
#if SURFACE_HEX
    struct surface_hex *byte_editor;
#if SURFACE_DIFF
    struct surface_diff *comparison;
#endif
#endif
#endif
#if SURFACE_TERMINAL
    struct surface_terminal *terminal;
#endif
#if SURFACE_DEVICE
    struct surface_device *device;
#if SURFACE_VFS
    struct surface_inspector *inspector;
#if SURFACE_READER
    struct surface_reader *reader;
#endif
#endif
#endif
#if SURFACE_PREFERENCES
    surface_preferences_t preferences;
    unsigned preferences_device;
    int preferences_device_error;
    unsigned preferences_status,preferences_dirty,preferences_page;
#endif
    surface_touch_map_t calibration;
    unsigned settings_status, settings_dirty;
#if SURFACE_ARITHMETIC
    tiku_calc_model_t arithmetic;
    int arithmetic_copied;
#endif
    unsigned word_width, word_base;
#if SURFACE_TEXT_INPUT
    unsigned word_mode, bit_page, range_select, range_pending, range_high, range_low, range_copied;
    char checksum_input[256], checksum_literal[256];
    unsigned checksum_text, word_copied;
#endif
#if SURFACE_FILE_JOBS
#if SURFACE_FILE_INFO
    uint64_t file_length,file_version;
    int file_details,file_info_known;
#endif
    char destination[65];
    unsigned file_job;
#if SURFACE_CONTENT_JOBS
    unsigned file_dialog;
    tiku_owned_buffer_t copy_buffer;
    size_t copy_bytes;
#if SURFACE_DIRECTORIES
    uint64_t copy_prepared,copy_total;
#endif
#endif
#endif
    char hex[65];
    tiku_owned_buffer_t browser_memory;
    surface_browser_text_t *browser;
    uint64_t epoch, after, node;
    size_t count, offset, restore_offset;
    struct { uint64_t after; size_t offset; } history[8];
    unsigned history_count;
    int eof, preview_hex;
    size_t preview_bytes;
} surface_app_state_t;
/** @brief An owned application instance and its retained model. */
typedef struct {
    tiku_static_app_t *owner;
    surface_app_state_t *state;
} surface_instance_t;
/** @brief Serialized shell storage, providers and pending navigation. */
struct surface {
#if SURFACE_DESKTOP
    struct surface_desktop *desktop;
#endif
#if SURFACE_TRANSFERS
    surface_transfer_snapshot_t transfer;
    uint64_t transfer_enable,transfer_stop,transfer_next,transfer_confirm_generation;
    unsigned transfer_phase,transfer_confirm,transfer_completed;
    int transfer_result;
#endif
#if SURFACE_PREFERENCES
    surface_preferences_t preferences;
#endif
    tiku_owned_buffer_t self;
    surface_config_t config;
#if SURFACE_EDITOR
    struct surface_editor_context *editor_context;
#endif
    tiku_event_queue_t *events;
    tiku_framebuffer_t *frame;
    tiku_surface_t *painting;
    surface_view_t painted;
    unsigned paint_y;
    surface_instance_t active, retired;
    surface_instance_t background[SURFACE_APP_LAST + 1];
    surface_app_t pending;
    int applications_menu, close_confirmation;
#if SURFACE_IMAGE
    unsigned home_page, task_page;
#endif
#if SURFACE_DOCUMENTS
    int document_menu;
#if SURFACE_DIFF
    unsigned document_side;
#endif
    surface_app_t document_target,document_error;
#endif
    int switching, stopped, dirty, clock_failed, display_failed;
#if SURFACE_EDITOR
#if SURFACE_BASIC
    int run_basic;
#endif
    char open_path[65];
    uint64_t open_epoch,open_node;
#endif
    tiku_resource_error_t status;
    uint64_t now;
    tiku_press_t press;
#if SURFACE_TEXT_INPUT
    tiku_field_t text_field;
    tiku_field_input_t text_input;
    char clipboard[256];
    int text_symbols, text_shift, text_error;
    tiku_press_t text_press;
#endif
};
#if SURFACE_TRANSFERS
/** @brief Advance one copied observation or retained command result, including during shell retirement. */
void surface_transfers_poll(surface_t *);
/** @brief Handle transfer-page navigation and generation-checked commands. */
int surface_transfers_action(surface_app_state_t *,int);
/** @brief Hit-test service controls without invoking the provider. */
int surface_transfers_hit(const surface_view_t *,int,int);
/** @brief Paint copied service state with the shared native controls. */
void surface_transfers_paint(tiku_surface_t *,const surface_view_t *);
#endif
#if SURFACE_IMAGE
/** @brief Construct and retire an image viewer through the product allocator. */
int surface_image_start(surface_app_state_t *);
void surface_image_dispose(surface_app_state_t *);
/** @brief Queue a canonical local path or report a busy viewer. */
int surface_image_open(surface_app_state_t *,const char *,uint64_t,uint64_t);
int surface_image_busy(const surface_app_state_t *);
/** @brief Advance one storage, decoding or rendering turn. */
void surface_image_poll(surface_app_state_t *);
/** @brief Dispatch viewer controls or native keyboard navigation. */
void surface_image_action(surface_app_state_t *,int);
void surface_image_key(surface_app_state_t *,const tiku_input_event_t *);
/** @brief Handle captured drag or wheel input and release capture on cancellation. */
int surface_image_pointer(surface_app_state_t *,const tiku_input_event_t *);
void surface_image_cancel_input(surface_app_state_t *);
/** @brief Copy immutable viewer state and retain its published paint frame. */
void surface_image_view(const surface_app_state_t *,surface_view_t *);
void surface_image_hold(const void *);
void surface_image_drop(const void *);
/** @brief Hit-test and paint the viewer's native controls and cached pixels. */
int surface_image_hit(const surface_view_t *,int,int);
void surface_image_paint(tiku_surface_t *,const surface_view_t *);
/** @brief Edit and accept an image pathname through the shell field. */
const char *surface_image_draft(const surface_app_state_t *);
int surface_image_accept(surface_app_state_t *,const char *);
enum { ACTION_IMAGE_OPEN=3100,ACTION_IMAGE_RELOAD,ACTION_IMAGE_FIT,ACTION_IMAGE_ACTUAL,
    ACTION_IMAGE_OUT,ACTION_IMAGE_IN,ACTION_IMAGE_ROTATE,ACTION_IMAGE_WATCH,
    ACTION_IMAGE_CANCEL,ACTION_IMAGE_LEFT,ACTION_IMAGE_RIGHT,ACTION_IMAGE_UP,ACTION_IMAGE_DOWN };
#endif
#if SURFACE_DESKTOP
/** @brief Allocate and retire the desktop's workspaces and copied paint state. */
int surface_desktop_create(surface_t *);
void surface_desktop_destroy(surface_t *);
/** @brief Synchronize task windows and focus after owner navigation. */
void surface_desktop_sync(surface_t *, int focus);
/** @brief Copy a task window frame and minimization state; zero means closed. */
int surface_desktop_geometry(const surface_t *,surface_app_t,tiku_rect_t *,int *);
/** @brief Cancel captured gestures without changing window geometry. */
void surface_desktop_cancel(surface_t *);
/** @brief Handle window, desktop, folder and Deskbar input on the owner thread. */
int surface_desktop_event(surface_t *, const tiku_input_event_t *);
/** @brief Freeze window geometry and content for a complete incremental frame. */
void surface_desktop_snapshot(surface_t *);
/** @brief Paint one damage strip from frozen desktop state. */
void surface_desktop_paint(surface_t *, tiku_surface_t *);
#endif
/** @brief Composite selected native product artwork within the paint clip. */
void surface_icon_paint(tiku_surface_t *,int x,int y,unsigned index);
/** @brief Composite native artwork at a positive destination size. */
void surface_icon_scaled(tiku_surface_t *,int x,int y,unsigned index,int size);
/** @brief Copy the selected task's presentation state without changing focus. */
void surface_view_state(const surface_t *, surface_app_state_t *, surface_view_t *);
/** @brief Dispatch a shell action outside application callbacks. */
void surface_action(surface_t *, int);
/** @brief Start a static product app; status is reported in the shell. */
void surface_app_start(surface_t *, surface_app_t);
/** @brief Retire provider work and advance one queued browser operation. */
void surface_app_poll(surface_app_state_t *);
/** @brief Deliver a product action to a live app. */
void surface_app_action(surface_app_state_t *, int);
/** @brief Request the parent browser view, returning zero at its root. */
int surface_app_back(surface_app_state_t *);
/** @brief Edit the selected unsigned word with digit, erase or clear input. */
void surface_calculator_key(surface_app_state_t *, unsigned);
/** @brief Change word width/radix or byte order using the shared word model. */
void surface_calculator_action(surface_app_state_t *, int);
#if SURFACE_TEXT_INPUT
const char *surface_calculator_draft(const surface_app_state_t *);
int surface_calculator_checksum_accept(surface_app_state_t *,const char *);
void surface_calculator_checksums(const surface_app_state_t *,surface_view_t *);
#endif
#if SURFACE_TEXT_INPUT
/** @brief Open the word dialog; edits stay private until successful acceptance. */
void surface_text_open(surface_t *);
/** @brief Route a modal event; return nonzero while it owns input. */
int surface_text_event(surface_t *, const tiku_input_event_t *);
/** @brief Paint a copied text dialog and its touch keyboard. */
void surface_text_paint(tiku_surface_t *, const surface_view_t *);
#endif
#if SURFACE_FILE_JOBS
/** @brief Queue a checked absolute destination from the private filename draft. */
int surface_file_rename(surface_app_state_t *, const char *);
#endif
/** @brief Find the enabled control under one logical pointer position. */
int surface_hit(const surface_view_t *, int, int);
/** @brief Paint one copied shell view into the acquired surface. */
void surface_paint(tiku_surface_t *, const surface_view_t *);
#if SURFACE_ARITHMETIC
/** @brief Apply arithmetic keys or copy a result into the owned clipboard. */
void surface_arithmetic_key(surface_app_state_t *,unsigned,unsigned);
/** @brief Deliver one arithmetic touch action. */
void surface_arithmetic_action(surface_app_state_t *,int);
/** @brief Return the arithmetic control at a logical coordinate. */
int surface_arithmetic_hit(int,int);
/** @brief Paint the copied arithmetic state and keypad. */
void surface_arithmetic_paint(tiku_surface_t *,const surface_view_t *);
#endif
#if SURFACE_CONTENT_JOBS
/** @brief Queue an empty file at an unused validated absolute path. */
int surface_file_create(surface_app_state_t *,const char *);
/** @brief Copy a selected file to an unused path with bounded reads and publication. */
int surface_file_copy(surface_app_state_t *,const char *);
#endif
#if SURFACE_EDITOR
/** @brief Retained Editor model and queued provider lifetime. */
/** @brief Retain the shell's document allocator until the last Editor session closes. */
int surface_editor_context_acquire(surface_t *);
/** @brief Release one document session after its model allocations have retired. */
void surface_editor_context_release(surface_t *);
#if SURFACE_NOTES
/** @brief A retained note snapshot; title is a complete UTF-8 prefix of its first line. */
typedef struct { uint64_t revision,metadata; unsigned paper,important; int exists,deleted,busy,dirty; size_t bytes; char title[65]; } surface_note_info_t;
int surface_notes_start(surface_app_state_t *);
void surface_notes_stop(surface_app_state_t *);
void surface_notes_dispose(surface_app_state_t *);
void surface_notes_poll(surface_app_state_t *);
int surface_notes_modified(const surface_app_state_t *);
int surface_notes_back(surface_app_state_t *);
void surface_notes_key(surface_app_state_t *,const tiku_input_event_t *);
void surface_notes_action(surface_app_state_t *,int);
void surface_notes_view(surface_app_state_t *,surface_view_t *);
int surface_notes_hit(const surface_view_t *,int,int);
void surface_notes_paint(tiku_surface_t *,const surface_view_t *);
surface_app_state_t *surface_notes_editor(surface_app_state_t *);
int surface_editor_note(surface_app_state_t *,const char *);
int surface_editor_note_save(surface_app_state_t *);
int surface_editor_note_delete(surface_app_state_t *);
int surface_editor_note_copy(surface_app_state_t *,surface_app_state_t *);
void surface_editor_note_metadata(surface_app_state_t *,unsigned,unsigned);
void surface_editor_note_info(surface_app_state_t *,surface_note_info_t *);
#endif
/** @brief Admit a new file without replacing a dirty retained session. */
int surface_editor_can_open(const surface_app_state_t *,const char *);
#if SURFACE_EDITOR_DOCUMENTS
/** @brief Document identity and model state without rendering a frame. */
typedef struct { char path[65];size_t bytes;int dirty,busy,error; } surface_editor_info_t;
/** @brief Read one unit session's identity and state. */
void surface_editor_info(const surface_app_state_t *,surface_editor_info_t *);
/** @brief Resolve the selected child, or return a unit session unchanged. */
surface_app_state_t *surface_editor_session(surface_app_state_t *);
/** @brief Allocate the session manager and its initial document. */
int surface_editor_documents_start(surface_app_state_t *);
/** @brief Close child storage sessions while leases retain borrowed buffers. */
void surface_editor_documents_stop(surface_app_state_t *);
/** @brief Release child models after provider retirement. */
void surface_editor_documents_dispose(surface_app_state_t *);
/** @brief Advance one active child job, including its intermediate requests. */
void surface_editor_documents_poll(surface_app_state_t *);
/** @brief Report unsaved state across retained documents. */
int surface_editor_documents_modified(const surface_app_state_t *);
/** @brief Select an existing path or load a separate retained session. */
int surface_editor_documents_open(surface_app_state_t *,const char *,uint64_t,uint64_t);
/** @brief Handle document selection and close confirmation. */
int surface_editor_documents_action(surface_app_state_t *,int);
/** @brief Handle document shortcuts and overview input. */
int surface_editor_documents_key(surface_app_state_t *,const tiku_input_event_t *);
/** @brief Leave the document overview or its close confirmation. */
int surface_editor_documents_back(surface_app_state_t *);
/** @brief Snapshot the selected document and overview rows. */
void surface_editor_documents_view(surface_app_state_t *,surface_view_t *);
/** @brief Hit-test document overview controls. */
int surface_editor_documents_hit(const surface_view_t *,int,int);
/** @brief Paint document overview and close confirmation. */
void surface_editor_documents_paint(tiku_surface_t *,const surface_view_t *);
#endif
int surface_editor_start(surface_app_state_t *);
void surface_editor_dispose(surface_app_state_t *);
void surface_editor_poll(surface_app_state_t *);
int surface_editor_modified(const surface_app_state_t *);
int surface_editor_busy(const surface_app_state_t *);
#if SURFACE_BASIC
/** @brief Read the current draft into Editor storage valid until its next edit/save. */
const char *surface_editor_program(surface_app_state_t *,size_t *);
/** @brief Start a copied program through the active Terminal provider. */
int surface_terminal_program(surface_app_state_t *,const char *,size_t);
#endif
int surface_editor_open(surface_app_state_t *,const char *,uint64_t,uint64_t);
void surface_editor_key(surface_app_state_t *,const tiku_input_event_t *);
void surface_editor_action(surface_app_state_t *,int);
void surface_editor_dialog(surface_app_state_t *,int);
int surface_editor_back(surface_app_state_t *);
const char *surface_editor_draft(surface_app_state_t *);
int surface_editor_accept(surface_app_state_t *,const char *);
void surface_editor_view(surface_app_state_t *,surface_view_t *);
int surface_editor_hit(const surface_view_t *,int,int);
void surface_editor_paint(tiku_surface_t *,const surface_view_t *);
#endif
#if SURFACE_TERMINAL
/** @brief Create a retained console model and acquire its session lease. */
int surface_terminal_start(surface_app_state_t *);
/** @brief Poll bounded copied input/output and retire a stopped console. */
void surface_terminal_poll(surface_app_state_t *);
/** @brief Release a retired console model. */
void surface_terminal_dispose(surface_app_state_t *);
/** @brief Queue a complete touch command including its newline. */
int surface_terminal_command(surface_app_state_t *,const char *);
/** @brief Route physical keys to the console's bounded input queue. */
void surface_terminal_key(surface_app_state_t *,const tiku_input_event_t *);
/** @brief Apply console commands, scrollback and clipboard actions. */
void surface_terminal_action(surface_app_state_t *,int);
/** @brief Copy visible cells and status into the presentation snapshot. */
void surface_terminal_view(const surface_app_state_t *,surface_view_t *);
/** @brief Resolve one terminal touch action. */
int surface_terminal_hit(int,int);
/** @brief Paint copied terminal cells and controls. */
void surface_terminal_paint(tiku_surface_t *,const surface_view_t *);
#endif
#if SURFACE_DEVICE
/** @brief Retained device observations and shared bounded history. */
int surface_device_start(surface_app_state_t *);
void surface_device_dispose(surface_app_state_t *);
void surface_device_poll(surface_app_state_t *);
void surface_device_action(surface_app_state_t *,int);
void surface_device_view(const surface_app_state_t *,surface_view_t *);
int surface_device_back(surface_app_state_t *);
int surface_device_hit(const surface_view_t *,int,int);
void surface_device_paint(tiku_surface_t *,const surface_view_t *);
#endif
#if SURFACE_VFS
/** @brief Local namespace navigation and deferred value operations. */
int surface_inspector_start(surface_app_state_t *);
/** @brief Queue a copied path for metadata; refuse a pending operation. */
int surface_inspector_open(surface_app_state_t *,const char *);
void surface_inspector_dispose(surface_app_state_t *);
void surface_inspector_poll(surface_app_state_t *);
void surface_inspector_action(surface_app_state_t *,int);
void surface_inspector_view(const surface_app_state_t *,surface_view_t *);
int surface_inspector_back(surface_app_state_t *);
int surface_inspector_accept(surface_app_state_t *,const char *);
const char *surface_inspector_draft(surface_app_state_t *);
int surface_inspector_hit(const surface_view_t *,int,int);
void surface_inspector_paint(tiku_surface_t *,const surface_view_t *);
#endif
#if SURFACE_READER
int surface_reader_start(surface_app_state_t *);
void surface_reader_dispose(surface_app_state_t *);
void surface_reader_poll(surface_app_state_t *);
void surface_reader_action(surface_app_state_t *,int);
/** @brief Route Find, repeat-search and page-navigation keys to Reader. */
void surface_reader_key(surface_app_state_t *,const tiku_input_event_t *);
void surface_reader_view(const surface_app_state_t *,surface_view_t *);
int surface_reader_back(surface_app_state_t *);
int surface_reader_open(surface_app_state_t *,const char *,uint64_t,uint64_t);
int surface_reader_accept(surface_app_state_t *,const char *);
const char *surface_reader_draft(surface_app_state_t *);
int surface_reader_hit(const surface_view_t *,int,int);
void surface_reader_paint(tiku_surface_t *,const surface_view_t *);
void surface_reader_hold(const void *);
void surface_reader_drop(const void *);
#endif
#if SURFACE_HEX
/** @brief Retained binary document, checked file jobs and touch controls. */
int surface_hex_start(surface_app_state_t *);
void surface_hex_dispose(surface_app_state_t *);
void surface_hex_poll(surface_app_state_t *);
int surface_hex_modified(const surface_app_state_t *);
int surface_hex_busy(const surface_app_state_t *);
int surface_hex_open(surface_app_state_t *,const char *,uint64_t,uint64_t);
void surface_hex_action(surface_app_state_t *,int);
int surface_hex_accept(surface_app_state_t *,const char *);
const char *surface_hex_draft(surface_app_state_t *);
void surface_hex_view(const surface_app_state_t *,surface_view_t *);
int surface_hex_hit(const surface_view_t *,int,int);
void surface_hex_paint(tiku_surface_t *,const surface_view_t *);
#endif
#if SURFACE_DOCUMENTS
/** @brief Classify a canonical file name through the static product type table. */
const char *surface_document_type(const char *);
/** @brief Copy sorted installed handler choices and matching strength. */
int surface_document_choices(const char *,surface_app_t *,unsigned *);
/** @brief Resolve an application identity to its static product label. */
const char *surface_document_name(surface_app_t);
/** @brief Handle document/modal commands outside application callbacks. */
int surface_document_action(surface_t *,int);
/** @brief Deliver and clear one identity-bound pending document after navigation. */
void surface_document_deliver(surface_t *);
/** @brief Copy modal, matching and refused-target state for presentation. */
void surface_document_view(const surface_t *,surface_view_t *);
/** @brief Resolve modal and file-preview document controls. */
int surface_document_hit(const surface_view_t *,int,int);
/** @brief Paint preview controls or the active Open-with dialog. */
void surface_document_paint(tiku_surface_t *,const surface_view_t *);
/** @brief Whether Reader retains a pending open or parsing job. */
int surface_reader_busy(const surface_app_state_t *);
#endif
#if SURFACE_PREFERENCES
void surface_preferences_start(surface_app_state_t *);
void surface_preferences_poll(surface_app_state_t *);
int surface_preferences_action(surface_app_state_t *,int);
int surface_preferences_hit(const surface_view_t *,int,int);
void surface_preferences_paint(tiku_surface_t *,const surface_view_t *);
void surface_preferences_view(const surface_t *,surface_view_t *);
surface_app_t surface_preferences_handler(const surface_t *,const char *,surface_app_t);
#endif
#if SURFACE_HEX
void surface_hex_key(surface_app_state_t *,const tiku_input_event_t *);
int surface_hex_back(surface_app_state_t *);
#endif
#if SURFACE_DIFF
/** @brief Retained comparison lifecycle, input and copied presentation. */
int surface_diff_start(surface_app_state_t *);
void surface_diff_dispose(surface_app_state_t *);
void surface_diff_poll(surface_app_state_t *);
int surface_diff_open(surface_app_state_t *,unsigned,const char *);
/** @brief Open a Tracker-selected snapshot without resolving a newer catalog identity. */
int surface_diff_selected(surface_app_state_t *,unsigned,const char *,uint64_t,uint64_t);
/** @brief Report pending comparison or file-read ownership. */
int surface_diff_busy(const surface_app_state_t *);
int surface_diff_back(surface_app_state_t *);
int surface_diff_export(surface_app_state_t *,const char *);
void surface_diff_action(surface_app_state_t *,int);
void surface_diff_key(surface_app_state_t *,const tiku_input_event_t *);
int surface_diff_accept(surface_app_state_t *,const char *);
const char *surface_diff_draft(surface_app_state_t *);
void surface_diff_view(const surface_app_state_t *,surface_view_t *);
int surface_diff_hit(const surface_view_t *,int,int);
void surface_diff_paint(tiku_surface_t *,const surface_view_t *);
#endif
#endif
