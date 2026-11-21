/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * paint.c - touch geometry and rendering for an 800 by 480 surface.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_font.h"
#include "tiku_text_layout.h"
#include "tiku_ui.h"
#if SURFACE_DEVICE
#include "icons_grid.h"
#define ICON_SIZE 32
#define ICON_PIXELS surface_grid_icons
#else
#include "icons.h"
#define ICON_SIZE 48
#define ICON_PIXELS surface_icons
#endif
#if SURFACE_CONTENT_JOBS || SURFACE_FILE_INFO
#include "tiku_word_model.h"
#endif
#include <string.h>

/** @brief Return the fixed rectangle for a product control. */
static tiku_rect_t box(const surface_view_t *v,int id)
{
#if SURFACE_IMAGE
    if(id==ACTION_LAUNCH_PREVIOUS||id==ACTION_LAUNCH_NEXT){
        if(v->applications_menu)return (tiku_rect_t){id==ACTION_LAUNCH_PREVIOUS?80:568,384,152,44};
        return (tiku_rect_t){id==ACTION_LAUNCH_PREVIOUS?516:648,96,120,40};
    }
    if(id>=ACTION_TASK&&id<ACTION_TASK+SURFACE_APP_LAST){
        int n=id-ACTION_TASK-(int)v->task_page*10;
        return n>=0&&n<10?(tiku_rect_t){80+(n%2)*328,104+(n/2)*56,312,44}:(tiku_rect_t){0};
    }
    if(id>=ACTION_CALCULATOR&&id<ACTION_CALCULATOR+SURFACE_APP_LAST){
        int n=id-ACTION_CALCULATOR-(int)v->home_page*12;
        return n>=0&&n<12?(tiku_rect_t){32+(n%4)*188,154+(n/4)*90,172,90}:(tiku_rect_t){0};
    }
#else
    (void)v;
#endif

#if SURFACE_DIFF
    if(id>=ACTION_TASK&&id<ACTION_TASK+SURFACE_APP_LAST){int n=id-ACTION_TASK;return (tiku_rect_t){80+(n%2)*328,104+(n/2)*56,312,44};}
#endif
#if SURFACE_HEX
    if(id==ACTION_FILE_BYTES)return (tiku_rect_t){608,340,160,42};
    if(id>=ACTION_CALCULATOR&&id<ACTION_CALCULATOR+SURFACE_APP_LAST){int n=id-ACTION_CALCULATOR;return (tiku_rect_t){32+(n%4)*188,154+(n/4)*90,172,90};}
    if(id==ACTION_TASK+9)return (tiku_rect_t){408,392,312,44};
#endif
#if SURFACE_DIRECTORIES
    if(id==ACTION_REFRESH)return (tiku_rect_t){32,390,100,44};
    if(id==ACTION_FOLDER_SELECT)return (tiku_rect_t){140,390,68,44};
    if(id==ACTION_FILE_NEW)return (tiku_rect_t){220,390,88,44};
    if(id==ACTION_FOLDER_NEW)return (tiku_rect_t){316,390,96,44};
#endif
#if SURFACE_READER
    if(id==ACTION_FILE_READ)return (tiku_rect_t){408,340,184,42};
    if(id==ACTION_TASK+8)return (tiku_rect_t){80,392,312,44};
    if(id>=ACTION_CALCULATOR&&id<ACTION_CALCULATOR+SURFACE_APP_LAST){int n=id-ACTION_CALCULATOR;return (tiku_rect_t){32+(n%3)*252,154+(n/3)*90,232,90};}
#endif
#if SURFACE_DEVICE
    if(id>=ACTION_TASK&&id<ACTION_TASK+SURFACE_APP_LAST){int n=id-ACTION_TASK;return (tiku_rect_t){80+(n%2)*328,112+(n/2)*72,312,56};}
    if(id>=ACTION_CALCULATOR&&id<ACTION_CALCULATOR+SURFACE_APP_LAST){int n=id-ACTION_CALCULATOR;return (tiku_rect_t){32+(n%4)*188,160+(n/4)*130,172,100};}
#endif
#if SURFACE_FILE_INFO
    if(id==ACTION_FILE_INFO)return (tiku_rect_t){220,340,168,42};
#endif
#if SURFACE_TERMINAL
    if(id==ACTION_TERMINAL)return (tiku_rect_t){536,356,232,56};
#endif
#if SURFACE_EDITOR
    if(id==ACTION_EDITOR)return (tiku_rect_t){284,356,232,56};
    if(id==ACTION_FILE_EDIT)return (tiku_rect_t){32,340,160,42};
#endif
#if SURFACE_TEXT_INPUT
    if(id==ACTION_CHECKSUM_MODE)return (tiku_rect_t){424,88,312,40};
    if(id==ACTION_CHECKSUM_COPY)return (tiku_rect_t){424,370,312,44};
    if(id==ACTION_WORD_COPY)return (tiku_rect_t){640,370,96,44};
    if(id==ACTION_ERASE)return (tiku_rect_t){424,370,96,44};
    if(id==ACTION_CLEAR)return (tiku_rect_t){532,370,96,44};
    if (id >= ACTION_WORD_TAB && id <= ACTION_CHECKSUM_TAB)
        return (tiku_rect_t){32+(id-ACTION_WORD_TAB)*116,88,108,28};
    if (id == ACTION_RANGE_SELECT) return (tiku_rect_t){208,370,128,44};
    if (id == ACTION_RANGE_COPY) return (tiku_rect_t){352,370,128,44};
    if (id == ACTION_BIT_PAGE) return (tiku_rect_t){32,370,160,44};
    if (id >= ACTION_BIT && id < ACTION_BIT+32) {
        int n=id-ACTION_BIT;
        return (tiku_rect_t){32+(n%8)*92,146+(n/8)*54,84,46};
    }
#endif
#if SURFACE_ARITHMETIC
    if(id==ACTION_ARITHMETIC)return (tiku_rect_t){32,356,232,56};
#endif
    if (id == ACTION_HOME) return (tiku_rect_t){16, 12, 88, 40};
    if (id == ACTION_BACK) return (tiku_rect_t){116, 12, 88, 40};
    if (id == ACTION_APPLICATIONS) return (tiku_rect_t){516, 12, 152, 40};
    if (id == ACTION_CLOSE) return (tiku_rect_t){680, 12, 104, 40};
    if (id == ACTION_KEEP) return (tiku_rect_t){240, 270, 152, 44};
    if (id == ACTION_DISCARD) return (tiku_rect_t){408, 270, 152, 44};
    if (id >= ACTION_TASK && id < ACTION_TASK + SURFACE_APP_LAST)
        return (tiku_rect_t){240, (SURFACE_TERMINAL?96:SURFACE_EDITOR?112:142) + (id - ACTION_TASK) * (SURFACE_TERMINAL?56:SURFACE_EDITOR?60:66), 320, 52};
    if (id >= ACTION_CALCULATOR && id <= ACTION_CALCULATOR + SURFACE_APP_LAST - 1)
        return (tiku_rect_t){32 + (id - ACTION_CALCULATOR) * 252, 160, 232, 144};
    if (id >= ACTION_HEX && id < ACTION_HEX + 16) {
        int n = id - ACTION_HEX;
        return (tiku_rect_t){32 + (n % 4) * 90, 194 + (n / 4) * 56, 78, 46};
    }
    if (id == ACTION_ERASE) return (tiku_rect_t){424, 370, 148, 44};
    if (id == ACTION_CLEAR) return (tiku_rect_t){588, 370, 148, 44};
    if (id >= ACTION_WORD_WIDTH && id <= ACTION_WORD_SWAP)
        return (tiku_rect_t){424 + (id - ACTION_WORD_WIDTH) * 112, 310, 104, 44};
    if (id >= ACTION_ROW && id < ACTION_ROW + 6)
        return (tiku_rect_t){32, 112 + (id - ACTION_ROW) * 44, 736, 40};
    if (id == ACTION_REFRESH) return (tiku_rect_t){32, 390, 160, 44};
    if (id == ACTION_PREVIOUS) return (tiku_rect_t){420, 390, 160, 44};
#if SURFACE_FILE_JOBS
    if (id == ACTION_FILE_RENAME) return (tiku_rect_t){420,390,160,44};
    if (id == ACTION_FILE_DELETE) return (tiku_rect_t){608,390,160,44};
#endif
#if SURFACE_CONTENT_JOBS
    if(id==ACTION_FILE_COPY)return (tiku_rect_t){220,390,168,44};
    if(id==ACTION_FILE_NEW)return (tiku_rect_t){220,390,168,44};
#endif
    if (id == ACTION_NEXT) return (tiku_rect_t){608, 390, 160, 44};
    if (id >= ACTION_SWAP && id <= ACTION_SAVE) return (tiku_rect_t){32 + (id - ACTION_SWAP) * 184, 330, 168, 44};
    if (id == ACTION_DEFAULTS) return (tiku_rect_t){32, 386, 168, 44};
    return (tiku_rect_t){0};
}

/** @brief Test logical coordinates without extending the right/bottom edge. */
static int contains(tiku_rect_t r, int x, int y)
{ return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

int surface_hit(const surface_view_t *view, int x, int y)
{
    if (view->stopped) return 0;
#if SURFACE_TEXT_INPUT
    if (view->text_entry) return 0;
#endif
#if SURFACE_DOCUMENTS
    if(view->document_menu)return surface_document_hit(view,x,y);
#endif
    if (view->close_confirmation) {
        if (contains(box(view,ACTION_KEEP), x, y)) return ACTION_KEEP;
        if (contains(box(view,ACTION_DISCARD), x, y)) return ACTION_DISCARD;
        return 0;
    }
    if (contains(box(view,ACTION_APPLICATIONS), x, y)) return ACTION_APPLICATIONS;
    if (view->applications_menu) {
#if SURFACE_IMAGE
        if(view->task_page&&contains(box(view,ACTION_LAUNCH_PREVIOUS),x,y))return ACTION_LAUNCH_PREVIOUS;
        if((view->task_page+1)*10<SURFACE_APP_LAST&&contains(box(view,ACTION_LAUNCH_NEXT),x,y))return ACTION_LAUNCH_NEXT;
#endif
        for (int id = ACTION_TASK; id < ACTION_TASK + SURFACE_APP_LAST; ++id)
            if (contains(box(view,id), x, y)) return id;
        return 0;
    }
    if (view->close_enabled && contains(box(view,ACTION_CLOSE), x, y)) return ACTION_CLOSE;
    if (contains(box(view,ACTION_HOME), x, y)) return ACTION_HOME;
    if (contains(box(view,ACTION_BACK), x, y)) return ACTION_BACK;
    if (view->foreground == SURFACE_HOME) {
#if SURFACE_IMAGE
        if(view->home_page&&contains(box(view,ACTION_LAUNCH_PREVIOUS),x,y))return ACTION_LAUNCH_PREVIOUS;
        if((view->home_page+1)*12<SURFACE_APP_LAST&&contains(box(view,ACTION_LAUNCH_NEXT),x,y))return ACTION_LAUNCH_NEXT;
#endif
        for (int id = ACTION_CALCULATOR; id <= ACTION_CALCULATOR + SURFACE_APP_LAST - 1; ++id)
            if (contains(box(view,id), x, y)) return id;
#if SURFACE_IMAGE
    } else if(view->foreground==SURFACE_IMAGE_APP){return surface_image_hit(view,x,y);
#endif
#if SURFACE_ARITHMETIC
    } else if(view->foreground==SURFACE_ARITHMETIC_APP) {
        return surface_arithmetic_hit(x,y);
#endif
#if SURFACE_TERMINAL
    } else if(view->foreground==SURFACE_TERMINAL_APP){return surface_terminal_hit(x,y);
#endif
#if SURFACE_EDITOR
#if SURFACE_NOTES
    } else if(view->foreground==SURFACE_NOTES_APP){return surface_notes_hit(view,x,y);
#endif
    } else if(view->foreground==SURFACE_EDITOR_APP){
        int hit=surface_editor_hit(view,x,y);
#if SURFACE_EDITOR_DOCUMENTS
        if(view->editor_sessions&&(view->editor_list||view->editor_confirm||hit==ACTION_EDITOR_SESSIONS))return hit;
#endif
        return view->editor_busy?0:hit;
#if SURFACE_HEX
#if SURFACE_DIFF
    } else if(view->foreground==SURFACE_DIFF_APP){return surface_diff_hit(view,x,y);
#endif
    } else if(view->foreground==SURFACE_HEX_APP){return surface_hex_hit(view,x,y);
#endif
#endif
#if SURFACE_DEVICE
#if SURFACE_READER
    } else if(view->foreground==SURFACE_READER_APP){return surface_reader_hit(view,x,y);
#endif
    } else if(view->foreground==SURFACE_MONITOR_APP){return surface_device_hit(view,x,y);
#if SURFACE_VFS
    } else if(view->foreground==SURFACE_VFS_APP){return surface_inspector_hit(view,x,y);
#endif
#endif
    } else if (view->foreground == SURFACE_CALCULATOR) {
#if SURFACE_TEXT_INPUT
        for (int id=ACTION_WORD_TAB;id<=ACTION_CHECKSUM_TAB;++id)
            if (contains(box(view,id),x,y)) return id;
        if (view->word_mode==1) {
            if (contains(box(view,ACTION_RANGE_SELECT),x,y)) return ACTION_RANGE_SELECT;
            if (contains(box(view,ACTION_RANGE_COPY),x,y)) return ACTION_RANGE_COPY;
            if (view->word_width==64 && contains(box(view,ACTION_BIT_PAGE),x,y)) return ACTION_BIT_PAGE;
            for (int id=ACTION_BIT;id<ACTION_BIT+32;++id) {
                unsigned bit=view->bit_page*32+31-(unsigned)(id-ACTION_BIT);
                if (bit<view->word_width && contains(box(view,id),x,y)) return id;
            }
            return 0;
        }
        if (view->word_mode==2){
            if(contains(box(view,ACTION_CHECKSUM_MODE),x,y))return ACTION_CHECKSUM_MODE;
            if(contains(box(view,ACTION_CHECKSUM_COPY),x,y))return ACTION_CHECKSUM_COPY;
            return contains((tiku_rect_t){32,130,736,88},x,y)?ACTION_WORD_EDIT:0;
        }
        if(contains(box(view,ACTION_WORD_COPY),x,y))return ACTION_WORD_COPY;
        if (contains((tiku_rect_t){32,120,348,52}, x, y)) return ACTION_WORD_EDIT;
#endif
        for (int id = ACTION_HEX; id <= ACTION_WORD_SWAP; ++id)
            if ((id >= ACTION_ERASE || (unsigned)(id - ACTION_HEX) < (view->word_base ? view->word_base : 16)) &&
                contains(box(view,id), x, y)) return id;
    } else if (view->foreground == SURFACE_BROWSER) {
#if SURFACE_CONTENT_JOBS
        if(view->file_create && contains(box(view,ACTION_FILE_NEW),x,y))return ACTION_FILE_NEW;
#if SURFACE_DIRECTORIES
        if((view->file_create||view->file_upgrade)&&contains(box(view,ACTION_FOLDER_NEW),x,y))return ACTION_FOLDER_NEW;
        if(view->file_create&&contains(box(view,ACTION_FOLDER_SELECT),x,y))return ACTION_FOLDER_SELECT;
#endif
#endif
        for (unsigned i = 0; i < view->row_count; ++i)
            if (contains(box(view,ACTION_ROW + (int)i), x, y)) return ACTION_ROW + (int)i;
        if (contains(box(view,ACTION_REFRESH), x, y)) return ACTION_REFRESH;
#if SURFACE_FILE_JOBS
        if(view->reading) {
#if SURFACE_DOCUMENTS
            int document_action=surface_document_hit(view,x,y);if(document_action)return document_action;
#endif
#if SURFACE_FILE_INFO
            if(view->status==TIKU_RESOURCE_OK&&
#if SURFACE_DIRECTORIES
               !view->file_directory&&
#endif
               contains(box(view,ACTION_FILE_INFO),x,y))return ACTION_FILE_INFO;
#endif
#if SURFACE_EDITOR
#if SURFACE_READER
            if(view->file_open&&contains(box(view,ACTION_FILE_READ),x,y))return ACTION_FILE_READ;
#endif
            if(view->file_open&&contains(box(view,ACTION_FILE_EDIT),x,y))return ACTION_FILE_EDIT;
#if SURFACE_HEX
            if(view->file_open&&contains(box(view,ACTION_FILE_BYTES),x,y))return ACTION_FILE_BYTES;
#endif
#endif
#if SURFACE_CONTENT_JOBS
            if(view->file_actions&&contains(box(view,ACTION_FILE_COPY),x,y))return ACTION_FILE_COPY;
#endif
            if(view->file_actions && contains(box(view,ACTION_FILE_RENAME),x,y)) return ACTION_FILE_RENAME;
            if(view->file_actions && contains(box(view,ACTION_FILE_DELETE),x,y)) return ACTION_FILE_DELETE;
            return 0;
        }
#endif
        if (view->previous && contains(box(view,ACTION_PREVIOUS), x, y)) return ACTION_PREVIOUS;
        if (view->more && contains(box(view,ACTION_NEXT), x, y)) return ACTION_NEXT;
    } else if (view->foreground == SURFACE_SETTINGS) {
#if SURFACE_TRANSFERS
        if(view->transfers_page)return surface_transfers_hit(view,x,y);
#if SURFACE_PREFERENCES
        if(!view->preferences_page)
#endif
        {int transfer=surface_transfers_hit(view,x,y);if(transfer)return transfer;}
#endif
#if SURFACE_PREFERENCES
        int action=surface_preferences_hit(view,x,y);if(action||view->preferences_page)return action;
#endif
        if (view->settings_status == SURFACE_SETTINGS_PENDING || view->settings_status == SURFACE_SETTINGS_UNAVAILABLE || view->settings_status == SURFACE_SETTINGS_INCOMPATIBLE) return 0;
        for (int id = ACTION_SWAP; id <= ACTION_DEFAULTS; ++id)
            if (contains(box(view,id), x, y)) return id;
    }
    return 0;
}

/** @brief Paint a control with its current press state. */
static void button(tiku_surface_t *s, const surface_view_t *v, int id, const char *label, int enabled)
{
    tiku_rect_t r = box(v,id);
    if (!r.w || r.y >= s->clip.y + s->clip.h || r.y + r.h <= s->clip.y) return;
    tiku_ui_button(s, r, label, enabled ? (v->pressed == id ? TIKU_S_PRESSED : 0) : TIKU_S_DISABLED);
}

/** @brief Skip frozen-font text whose ink bounds miss the current paint rows. */
static void colored_text(tiku_surface_t *s, const tiku_font_t *font, int x, int y,
                         const char *value, tiku_rgb_t color)
{
    if (y - font->ascent >= s->clip.y + s->clip.h || y + font->height - font->ascent <= s->clip.y) return;
    tiku_text(s, font, x, y, value, color);
}

/** @brief Draw scale-one text with the selected frozen font. */
static void text(tiku_surface_t *s, int x, int y, const char *value, int bold)
{ colored_text(s, bold ? tiku_font_bold() : tiku_font_plain(), x, y, value, TIKU_C_TEXT); }

/** @brief Fit media text to its column without splitting valid UTF-8 code points. */
static void fitted_text(tiku_surface_t *s,int x,int y,const char *value,int width,int bold,tiku_rgb_t color)
{
    const tiku_font_t *font=bold?tiku_font_bold():tiku_font_plain();
    if(y-font->ascent>=s->clip.y+s->clip.h||y+font->height-font->ascent<=s->clip.y)return;
    char line[128];
    (void)tiku_text_elide(bold?TIKU_MEASURE_BOLD:TIKU_MEASURE_PLAIN,value,width,line,sizeof line);
    colored_text(s,font,x,y,line,color);
}

/** @brief Composite frozen artwork within the current clipped paint rows. */
void surface_icon_scaled(tiku_surface_t *s, int x, int y, unsigned index,int size)
{
    int first = s->clip.y > y ? s->clip.y - y : 0;
    int end = s->clip.y + s->clip.h - y;
    if (end > size) end = size;
    for (int row = first; row < end; ++row) for (int col = 0; col < size; ++col) {
        uint32_t p = ICON_PIXELS[index][(row * ICON_SIZE / size) * ICON_SIZE + col * ICON_SIZE / size], a = p >> 24;
        if (!a) continue;
        uint32_t d = tiku_peek(s, x + col, y + row), c = 0;
        for (unsigned shift = 0; shift < 24; shift += 8)
            c |= (((p >> shift) & 255u) + (((d >> shift) & 255u) * (255u-a) + 127u)/255u) << shift;
        tiku_pixel(s, x + col, y + row, c);
    }
}

void surface_icon_paint(tiku_surface_t *s,int x,int y,unsigned index)
{ surface_icon_scaled(s,x,y,index,ICON_SIZE); }

/** @brief Paint a desktop icon and label inside its larger touch target. */
static void launcher(tiku_surface_t *s, const surface_view_t *v, int id, const char *label)
{
    tiku_rect_t r = box(v,id);
    if (!r.w || r.y >= s->clip.y+s->clip.h || r.y+r.h <= s->clip.y) return;
    surface_icon_paint(s, r.x+(r.w-ICON_SIZE)/2, r.y+(SURFACE_DEVICE?12:20), (unsigned)(id-ACTION_CALCULATOR));
    int width = tiku_text_width(tiku_font_bold(),label), x = r.x+(r.w-width)/2;
    int selected = v->pressed == id;
    if (selected) tiku_fill(s,(tiku_rect_t){x-6,r.y+(SURFACE_DEVICE?60:82),width+12,24},TIKU_C_SELECT);
    colored_text(s,tiku_font_bold(),x,r.y+(SURFACE_DEVICE?77:99),label,selected?TIKU_C_SELTEXT:TIKU_C_TEXT);
}

/** @brief Show storage outcomes with an explicit refresh action. */
static const char *storage_status(tiku_resource_error_t status)
{
    switch (status) {
    case TIKU_RESOURCE_OK:
        return "Read-only media";
    case TIKU_RESOURCE_BUSY: return "File busy or destination occupied. Choose another name or retry.";
    case TIKU_RESOURCE_PENDING: return "Loading media...";
    case TIKU_RESOURCE_UNAVAILABLE: return "Media unavailable. Refresh to retry.";
    case TIKU_RESOURCE_STALE_CURSOR: return "Media changed. Refresh to reload.";
    case TIKU_RESOURCE_CANCELLED: return "Request cancelled";
    default: return "Media request failed. Refresh to retry.";
    }
}

void surface_paint(tiku_surface_t *s, const surface_view_t *v)
{
#if SURFACE_TEXT_INPUT
    if (v->text_entry) { surface_text_paint(s, v); return; }
#endif
    static const char *titles[] = {"TikuSurface", "Word calculator", SURFACE_DOCUMENTS?"TikuTracker":"Media browser", SURFACE_PREFERENCES?"Settings":"Device information", "Calculator", "Editor", "Terminal"
#if SURFACE_DEVICE
        , "Device Monitor"
#if SURFACE_VFS
        , "VFS Inspector"
#if SURFACE_READER
        , "Reader"
#if SURFACE_HEX
        , "TikuHex"
#if SURFACE_DIFF
        , "TikuDiff"
#if SURFACE_NOTES
        , "Notes"
#if SURFACE_IMAGE
        , "TikuImage"
#endif
#endif
#endif
#endif
#endif
#endif
#endif
    };
    tiku_fill(s, (tiku_rect_t){0, 0, 800, 480},
#if SURFACE_PREFERENCES
        v->desktop_color
#else
        TIKU_C_BACKDROP
#endif
        );
    tiku_ui_raised(s, (tiku_rect_t){8, 8, 784, 48});
    button(s, v, ACTION_HOME, "Home", !v->applications_menu && !v->close_confirmation);
    button(s, v, ACTION_BACK, "Back", !v->applications_menu && !v->close_confirmation);
    button(s, v, ACTION_APPLICATIONS, "Applications", !v->close_confirmation);
    button(s, v, ACTION_CLOSE, "Close", v->close_enabled && !v->applications_menu && !v->close_confirmation);
    text(s, 228, 37, "TikuSurface", 1);
    (void)tiku_ui_window(s, (tiku_rect_t){16,64,768,376},
#if SURFACE_TRANSFERS
        v->foreground==SURFACE_SETTINGS&&v->transfers_page?"Transfers":
#endif
        SURFACE_PREFERENCES&&v->foreground==SURFACE_SETTINGS
#if SURFACE_PREFERENCES
        &&v->preferences_page?(v->preferences_page==1?"Desktop preferences":"Device settings"):
#else
        ?"Settings":
#endif
        titles[v->foreground], 1);
    if (v->foreground == SURFACE_HOME) {
        text(s, 32, 112, "Your applications", 1);
        text(s, 32, 139, "Open an application or return to your work.", 0);
#if SURFACE_IMAGE
        button(s,v,ACTION_LAUNCH_PREVIOUS,"Previous",v->home_page>0);
        button(s,v,ACTION_LAUNCH_NEXT,"Next",(v->home_page+1)*12<SURFACE_APP_LAST);
#endif
#if SURFACE_DEVICE
        for(int id=ACTION_CALCULATOR;id<ACTION_CALCULATOR+SURFACE_APP_LAST;++id)
            launcher(s,v,id,titles[id-ACTION_CALCULATOR+1]);
#else
        launcher(s, v, ACTION_CALCULATOR, "Word calculator");
        launcher(s, v, ACTION_BROWSER, SURFACE_DOCUMENTS?"TikuTracker":"Media browser");
        launcher(s, v, ACTION_SETTINGS, "Device information");
        text(s, 48, 334, "Hex / decimal / octal / binary", 0);
        text(s, 300, 334, "Browse folders and read files", 0);
        text(s, 552, 334, "Platform and capabilities", 0);
#if SURFACE_ARITHMETIC
        button(s,v,ACTION_ARITHMETIC,"Calculator",1);
        if (v->open_apps && !SURFACE_EDITOR) text(s,300,370,"Use Applications to switch; Close finishes a task.",0);
#else
        if (v->open_apps) text(s, 32, 370, "Open applications retain their work. Use Close to finish an application.", 0);
#endif
#if SURFACE_EDITOR
        button(s,v,ACTION_EDITOR,"Editor",1);
#if SURFACE_TERMINAL
        button(s,v,ACTION_TERMINAL,"Terminal",1);
#endif
#endif
#endif
        if (v->retiring) text(s, 32, 430, "Closing an application; waiting for its request to finish.", 0);
        else if (v->status != TIKU_RESOURCE_OK) text(s, 32, 430, "Application could not start. Select an application to retry.", 0);
#if SURFACE_ARITHMETIC
    } else if(v->foreground==SURFACE_ARITHMETIC_APP) {
        surface_arithmetic_paint(s,v);
#if SURFACE_DEVICE
#if SURFACE_READER
    } else if(v->foreground==SURFACE_READER_APP){surface_reader_paint(s,v);
#endif
    } else if(v->foreground==SURFACE_MONITOR_APP){surface_device_paint(s,v);
#if SURFACE_VFS
    } else if(v->foreground==SURFACE_VFS_APP){surface_inspector_paint(s,v);
#endif
#endif
#endif
#if SURFACE_TERMINAL
    } else if(v->foreground==SURFACE_TERMINAL_APP){surface_terminal_paint(s,v);
#endif
#if SURFACE_EDITOR
#if SURFACE_NOTES
    } else if(v->foreground==SURFACE_NOTES_APP){surface_notes_paint(s,v);
#endif
    } else if(v->foreground==SURFACE_EDITOR_APP){surface_editor_paint(s,v);
#if SURFACE_HEX
#if SURFACE_DIFF
    } else if(v->foreground==SURFACE_DIFF_APP){surface_diff_paint(s,v);
#endif
    } else if(v->foreground==SURFACE_HEX_APP){surface_hex_paint(s,v);
#endif
#endif
#if SURFACE_IMAGE
    } else if(v->foreground==SURFACE_IMAGE_APP){surface_image_paint(s,v);
#endif
    } else if (v->foreground == SURFACE_CALCULATOR) {
#if SURFACE_TEXT_INPUT
        button(s,v,ACTION_WORD_TAB,"Word",1);
        button(s,v,ACTION_BITS_TAB,"Bits",1);
        button(s,v,ACTION_CHECKSUM_TAB,"Checksums",1);
        if (v->word_mode==1) {
            for (int id=ACTION_BIT;id<ACTION_BIT+32;++id) {
                unsigned bit=v->bit_page*32+31-(unsigned)(id-ACTION_BIT);
                char label[6]={(char)('0'+bit/10),(char)('0'+bit%10),':',
                    (char)('0'+((v->word_value>>bit)&1)),0};
                tiku_ui_button(s,box(v,id),label,bit>=v->word_width?TIKU_S_DISABLED:
                    (v->range_select?(bit>=v->range_low && bit<=v->range_high):
                    !!(v->word_value & (UINT64_C(1)<<bit)))?TIKU_S_PRESSED:0);
            }
            button(s,v,ACTION_BIT_PAGE,v->bit_page?"Bits 63 through 32":"Bits 31 through 0",v->word_width==64);
            button(s,v,ACTION_RANGE_SELECT,v->range_select?"Toggle bits":"Select range",1);
            button(s,v,ACTION_RANGE_COPY,"Copy range",1);
            char range[13]={'R','a','n','g','e',' ',(char)('0'+v->range_high/10),(char)('0'+v->range_high%10),':',
                (char)('0'+v->range_low/10),(char)('0'+v->range_low%10),0};
            text(s,500,382,range,1);text(s,500,408,v->range_hex,0);
            text(s,32,435,v->range_copied?"Range copied as hexadecimal. The original word is unchanged.":
                v->range_select?(v->range_pending?"Tap the other endpoint. You can switch bit pages.":"Tap two endpoints to select a range. The original word is unchanged."):
                "Tap a bit to toggle it. Select range extracts without changing the word.",0);
        } else if (v->word_mode==2) {
            button(s,v,ACTION_CHECKSUM_MODE,v->checksum_text?"UTF-8 text (tap for hex)":"Hex bytes (tap for text)",1);
            tiku_ui_sunken(s,(tiku_rect_t){32,130,736,88},TIKU_C_DOC);
            fitted_text(s,44,158,v->checksum_input[0]?v->checksum_input:"Tap to enter checksum input",712,0,TIKU_C_TEXT);
            text(s,44,194,v->checksum_text?"Literal UTF-8 bytes":"Decoded hexadecimal byte pairs",0);
            text(s,32,260,"CRC-32 / ISO-HDLC",1);text(s,300,260,v->crc32,0);
            text(s,32,300,"CRC-16 / CCITT-FALSE",1);text(s,300,300,v->crc16,0);
            text(s,32,344,"255 input bytes; hex admits 127 pairs. No implicit terminator.",0);
            button(s,v,ACTION_CHECKSUM_COPY,"Copy checksums",1);
            text(s,32,435,v->word_copied==2?"Checksums copied to the desktop clipboard.":"Each input mode retains its own draft. Empty input is valid.",0);
        } else {
#else
        text(s, 32, 106, "UNSIGNED WORD", 1);
#endif
        tiku_ui_sunken(s, (tiku_rect_t){32, 120, 348, 52}, TIKU_C_DOC);
        if (strlen(v->hex) > 32) {
            char first[33]; memcpy(first, v->hex, 32); first[32] = 0;
            text(s, 48, 140, first, 1); text(s, 48, 160, v->hex + 32, 1);
        } else text(s, 48, 151, v->hex[0] ? v->hex : "0", 1);
        for (int i = 0; i < 16; ++i) {
            char label[2] = {"0123456789ABCDEF"[i], 0};
            button(s, v, ACTION_HEX + i, label, (unsigned)i < (v->word_base ? v->word_base : 16));
        }
        static const char *labels[] = {"Hexadecimal", "Decimal", "Octal", "Binary"};
        for (int i = 0; i < 4; ++i) {
            text(s, 424, 105 + i * 48, labels[i], 1);
            char line[33] = {0};
            size_t n = strlen(v->bases[i]);
            memcpy(line, v->bases[i], n > 32 ? 32 : n);
            text(s, 424, 125 + i * 48, line, 0);
            if (n > 32) text(s, 424, 141 + i * 48, v->bases[i] + 32, 0);
        }
        button(s, v, ACTION_WORD_WIDTH, v->word_width == 8 ? "8-bit" :
            v->word_width == 16 ? "16-bit" : v->word_width == 32 ? "32-bit" : "64-bit", 1);
        button(s, v, ACTION_WORD_BASE, v->word_base == 2 ? "Binary input" :
            v->word_base == 8 ? "Octal input" : v->word_base == 10 ? "Decimal input" : "Hex input", 1);
        button(s, v, ACTION_WORD_SWAP, "Swap bytes", 1);
        button(s, v, ACTION_ERASE, "Erase", 1);
        button(s, v, ACTION_CLEAR, "Clear", 1);
#if SURFACE_TEXT_INPUT
        button(s,v,ACTION_WORD_COPY,"Copy",1);
        if(v->word_copied==1)text(s,32,438,"Conversions copied to the desktop clipboard.",0);
#endif
        if (v->status == TIKU_RESOURCE_LIMIT) text(s, 32, 438, "Value or input length exceeds the selected width. Word unchanged.", 0);
        else if (v->status == TIKU_RESOURCE_INVALID) text(s, 32, 438, "Digit is not valid for the selected input base.", 0);
#if SURFACE_TEXT_INPUT
        }
#endif
    } else if (v->foreground == SURFACE_BROWSER) {
        #if SURFACE_FILE_JOBS
        fitted_text(s,32,96,v->reading?v->file_path:v->path,736,1,TIKU_C_TEXT);
#else
        fitted_text(s, 32, 96, v->path, 736, 1, TIKU_C_TEXT);
#endif
#if SURFACE_FILE_INFO
        if(v->reading&&v->status==TIKU_RESOURCE_OK&&v->file_details){
            char number[65];text(s,32,132,"File information",1);
            text(s,32,170,"Size in bytes",0);
            if(v->file_info_known){(void)tiku_word_format(v->file_length,10,number);text(s,240,170,number,0);}
            else text(s,240,170,"Unavailable from this provider",0);
            text(s,32,202,"Content version",0);
            if(v->file_info_known&&v->file_version){(void)tiku_word_format(v->file_version,10,number);text(s,240,202,number,0);}
            else text(s,240,202,"Unavailable from this provider",0);
            text(s,32,234,"Catalog view",0);(void)tiku_word_format(v->file_epoch,10,number);text(s,240,234,number,0);
            text(s,32,266,"Access",0);text(s,240,266,v->file_can_write?"Read and write":"Read only",0);
            text(s,32,298,"Preview",0);text(s,240,298,v->preview_hex?"Hexadecimal / non-ASCII":"Plain text",0);
        }else
#endif
#if SURFACE_DIRECTORIES
        if(v->reading&&v->file_directory){
            text(s,32,132,"Folder selected",1);text(s,32,170,"Copy, rename / move or permanently delete this folder and its contents.",0);
            text(s,32,202,"The whole subtree changes together. Existing files are not merged.",0);
            text(s,32,234,"Back returns to the containing folder.",0);
        }else
#endif
        if (v->reading && v->status == TIKU_RESOURCE_OK) {
            fitted_text(s,32,132,v->preview_hex?"Binary / non-ASCII: first 128 bytes in hexadecimal":"Text preview (up to 1024 bytes)",SURFACE_DOCUMENTS?384:736,1,TIKU_C_TEXT);
            size_t at = 0;
            for (unsigned row = 0; row < 9 && v->preview[at]; ++row) {
                char line[91] = {0}; unsigned n = 0;
                while (n < 90 && v->preview[at] && v->preview[at] != '\n') line[n++] = v->preview[at++];
                if (v->preview[at] == '\n') ++at;
                fitted_text(s, 32, 160 + (int)row * 20, line, 736, 0, TIKU_C_TEXT);
            }
        } else if (v->status == TIKU_RESOURCE_OK && !v->row_count) text(s, 32, 148, "This folder is empty.", 0);
        for (unsigned i = 0; i < v->row_count; ++i) {
            tiku_rect_t r = box(v,ACTION_ROW + (int)i);
            int selected = v->pressed == ACTION_ROW + (int)i;
            tiku_rgb_t ink = selected ? TIKU_C_SELTEXT : TIKU_C_TEXT;
            tiku_fill(s, r, selected ? TIKU_C_SELECT : TIKU_C_DOC);
            fitted_text(s, 48, r.y + 25, v->rows[i], 632, 0, ink);
            colored_text(s, tiku_font_plain(), 696, r.y + 25, v->directory[i] ? "Folder" : "File", ink);
        }
#if SURFACE_FILE_JOBS
        button(s,v,ACTION_REFRESH,v->file_job?"Cancel":"Refresh",!v->close_confirmation);
        if(v->reading) {
#if SURFACE_FILE_INFO
            button(s,v,ACTION_FILE_INFO,v->file_details?"Preview":"Information",v->status==TIKU_RESOURCE_OK
#if SURFACE_DIRECTORIES
                &&!v->file_directory
#endif
                );
#endif
#if SURFACE_CONTENT_JOBS
            button(s,v,ACTION_FILE_COPY,"Copy to...",v->file_actions&&!v->close_confirmation);
#endif
            #if SURFACE_EDITOR
            button(s,v,ACTION_FILE_EDIT,"Edit text",v->file_open&&!v->close_confirmation);
#if SURFACE_HEX
            button(s,v,ACTION_FILE_BYTES,"Edit bytes",v->file_open&&!v->close_confirmation);
#endif
#if SURFACE_READER
            button(s,v,ACTION_FILE_READ,"Read document",v->file_open&&!v->close_confirmation);
#endif
            #endif
            button(s,v,ACTION_FILE_RENAME,"Rename / move",v->file_actions&&!v->close_confirmation);
            button(s,v,ACTION_FILE_DELETE,"Delete",v->file_actions&&!v->close_confirmation);
        } else
#else
        button(s, v, ACTION_REFRESH, "Refresh", 1);
#endif
        {
#if SURFACE_CONTENT_JOBS
            button(s,v,ACTION_FILE_NEW,"New file",v->file_create);
#if SURFACE_DIRECTORIES
            button(s,v,ACTION_FOLDER_NEW,v->file_upgrade?"Enable folders":"New folder",v->file_create||v->file_upgrade);
            button(s,v,ACTION_FOLDER_SELECT,v->file_selecting?"Cancel":"Select",v->file_create);
#endif
#endif
            button(s, v, ACTION_PREVIOUS, "Previous", v->previous);
            button(s, v, ACTION_NEXT, "Next", v->more);
        }
#if SURFACE_TRANSFERS
    } else if(v->foreground==SURFACE_SETTINGS&&v->transfers_page){surface_transfers_paint(s,v);
#endif
#if SURFACE_PREFERENCES
    } else if(v->foreground==SURFACE_SETTINGS&&v->preferences_page){surface_preferences_paint(s,v);
#endif
    } else {
        text(s, 32, 116, v->platform_name, 1);
        fitted_text(s, 32, 158, v->build_identity, 736, 0, TIKU_C_TEXT);
        text(s, 32, 202, v->media_description, 0);
#if SURFACE_TRANSFERS
        fitted_text(s,32,246,v->services_description,548,0,TIKU_C_TEXT);
        surface_transfers_paint(s,v);
#else
        text(s, 32, 246, v->services_description, 0);
#endif
        text(s, 32, 290, "Touch orientation: choose axes, then Save to apply.", 0);
        int editable = v->settings_status != SURFACE_SETTINGS_PENDING &&
            v->settings_status != SURFACE_SETTINGS_UNAVAILABLE && v->settings_status != SURFACE_SETTINGS_INCOMPATIBLE
#if SURFACE_PREFERENCES
            &&v->preferences_status!=SURFACE_SETTINGS_PENDING
#endif
            ;
        button(s, v, ACTION_SWAP, v->calibration.swap ? "Swap axes: on" : "Swap axes: off", editable);
        button(s, v, ACTION_INVERT_X, v->calibration.invert_x ? "Reverse X: on" : "Reverse X: off", editable);
        button(s, v, ACTION_INVERT_Y, v->calibration.invert_y ? "Reverse Y: on" : "Reverse Y: off", editable);
        button(s, v, ACTION_SAVE, "Save", editable);
        button(s, v, ACTION_DEFAULTS, "Defaults", editable);
        static const char *states[] = {"Default calibration; panel check required", "Calibration saved",
            "Saving calibration", "Invalid record; using defaults", "Settings unavailable",
            "Save failed; active calibration unchanged", "Unsupported record; using defaults"};
        if (v->settings_dirty && editable && v->settings_status != SURFACE_SETTINGS_FAILED)
            fitted_text(s, 220, 412, "Changes not saved. Choose Save.", 540, 0, TIKU_C_TEXT);
        else if (v->settings_status < sizeof states / sizeof states[0]) fitted_text(s, 220, 412, states[v->settings_status], 540, 0, TIKU_C_TEXT);
    }
#if SURFACE_PREFERENCES
    if(v->foreground==SURFACE_SETTINGS&&!v->preferences_page
#if SURFACE_TRANSFERS
       &&!v->transfers_page
#endif
       )surface_preferences_paint(s,v);
#endif
#if SURFACE_DOCUMENTS
    surface_document_paint(s,v);
#endif
    if (v->applications_menu) {
#if SURFACE_DEVICE
        (void)tiku_ui_window(s,(tiku_rect_t){64,80,672,356},"Applications",1);
#else
        (void)tiku_ui_window(s, (tiku_rect_t){224, SURFACE_TERMINAL?64:SURFACE_EDITOR?76:96, 352, SURFACE_TERMINAL?392:SURFACE_EDITOR?352:264+66*(SURFACE_APP_LAST-3)}, "Applications", 1);
#endif
        static const char *names[] = {"Word calculator", SURFACE_DOCUMENTS?"TikuTracker":"Media browser", SURFACE_PREFERENCES?"Settings":"Device information", "Calculator", "Editor", "Terminal"
#if SURFACE_DEVICE
        , "Device Monitor"
#if SURFACE_VFS
        , "VFS Inspector"
#if SURFACE_READER
        , "Reader"
#if SURFACE_HEX
        , "TikuHex"
#if SURFACE_DIFF
        , "TikuDiff"
#if SURFACE_NOTES
        , "Notes"
#if SURFACE_IMAGE
        , "TikuImage"
#endif
#endif
#endif
#endif
#endif
#endif
#endif
    };
        static const char *open_names[] = {"Word calculator (open)", SURFACE_DOCUMENTS?"TikuTracker (open)":"Media browser (open)", SURFACE_PREFERENCES?"Settings (open)":"Device information (open)", "Calculator (open)", "Editor (open)", "Terminal (open)"
#if SURFACE_DEVICE
        , "Device Monitor (open)"
#if SURFACE_VFS
        , "VFS Inspector (open)"
#if SURFACE_READER
        , "Reader (open)"
#if SURFACE_HEX
        , "TikuHex (open)"
#if SURFACE_DIFF
        , "TikuDiff (open)"
#if SURFACE_NOTES
        , "Notes (open)"
#if SURFACE_IMAGE
        , "TikuImage (open)"
#endif
#endif
#endif
#endif
#endif
#endif
#endif
    };
        for (unsigned i = 0; i < SURFACE_APP_LAST; ++i)
            button(s, v, ACTION_TASK + (int)i,
                v->open_apps & (1u << (i + 1)) ? open_names[i] : names[i], 1);
#if SURFACE_IMAGE
        button(s,v,ACTION_LAUNCH_PREVIOUS,"Previous",v->task_page>0);
        button(s,v,ACTION_LAUNCH_NEXT,"Next",(v->task_page+1)*10<SURFACE_APP_LAST);
#endif
        if(!SURFACE_READER)text(s, SURFACE_DEVICE?80:240, SURFACE_DEVICE?420:SURFACE_TERMINAL?444:SURFACE_EDITOR?416:348+66*(SURFACE_APP_LAST-3), "Select a task; Escape dismisses this menu.", 0);
    } else if (v->close_confirmation) {
#if SURFACE_DIRECTORIES
        if(v->close_confirmation==4){
            (void)tiku_ui_window(s,(tiku_rect_t){224,160,352,168},"Enable persistent folders?",1);
            text(s,240,204,"Existing files are preserved.",1);
            text(s,240,226,"Older firmware cannot open this volume.",0);
            text(s,240,246,"An interrupted upgrade needs resume.",0);
            button(s,v,ACTION_KEEP,"Keep current",1);button(s,v,ACTION_DISCARD,"Enable folders",1);
        }else
#endif
#if SURFACE_FILE_JOBS
        if(v->close_confirmation==2) {
            int folder=0;
#if SURFACE_DIRECTORIES
            folder=v->file_directory;
#endif
            (void)tiku_ui_window(s,(tiku_rect_t){224,160,352,168},folder?"Delete folder?":"Delete file?",1);
            text(s,240,210,folder?"Delete folder and all contents?":"Delete this file from storage?",1);
            fitted_text(s,240,238,v->file_path,320,0,TIKU_C_TEXT);
            button(s,v,ACTION_KEEP,folder?"Keep folder":"Keep file",1);button(s,v,ACTION_DISCARD,folder?"Delete folder":"Delete file",1);
        } else
#endif
        {
        (void)tiku_ui_window(s, (tiku_rect_t){224, 160, 352, 168}, (SURFACE_NOTES&&v->foreground==SURFACE_NOTES_APP)?"Unsaved notes":v->close_confirmation==3?"Unsaved document":"Unsaved settings", 1);
        text(s, 240, 210, "These changes have not been saved.", 1);
        text(s, 240, 238, (SURFACE_NOTES&&v->foreground==SURFACE_NOTES_APP)?"Discard unsaved changes to every note?":"Keep editing or discard this draft?", 0);
        button(s, v, ACTION_KEEP, "Keep editing", 1);
        button(s, v, ACTION_DISCARD, "Discard", 1);
        }
    }
    tiku_ui_sunken(s, (tiku_rect_t){8, 448, 784, 28}, TIKU_C_PANEL);
    const char *status = v->clock_failed ? "Clock unavailable" : v->display_failed ? "Display submission failed" :
        v->foreground == SURFACE_BROWSER ? storage_status(v->status) : v->platform_name;
#if SURFACE_FILE_JOBS
    if(v->foreground==SURFACE_BROWSER && v->status==TIKU_RESOURCE_OK && v->media_writable)
        status="Open a file to preview, rename or delete it.";
    if(v->file_job) status=v->file_job==3?"Renaming file... Cancel may require remount.":"Deleting file... Cancel may require remount.";
#if SURFACE_CONTENT_JOBS
#if SURFACE_DIRECTORIES
    if(v->file_job==9)status="Enabling folders... Cancel may require remount and resume.";
    if(v->file_job==8)status="Creating folder... Cancel may require remount.";
    if(v->foreground==SURFACE_BROWSER&&v->file_selecting)status="Select a folder to copy, rename or delete. Tap Select again to navigate.";
    if(v->foreground==SURFACE_BROWSER&&v->status==TIKU_RESOURCE_UNSUPPORTED)status="Persistent folders are not enabled. Enable folders preserves existing files.";
#endif
    if(v->file_job==5)status="Creating file... Cancel may require remount.";
    char copy_status[112],copy_number[65];
    if(v->file_job==6){
        (void)tiku_word_format(v->copy_bytes,10,copy_number);size_t n=strlen(copy_number);
        memcpy(copy_status,"Copy: ",6);memcpy(copy_status+6,copy_number,n);
        memcpy(copy_status+6+n," bytes read (64 KiB limit).",sizeof " bytes read (64 KiB limit).");status=copy_status;
    }
#if SURFACE_DIRECTORIES
    if(v->file_job==10){
        size_t at=sizeof "Folder copy: "-1,n;memcpy(copy_status,"Folder copy: ",at);
        (void)tiku_word_format(v->copy_prepared,10,copy_number);n=strlen(copy_number);memcpy(copy_status+at,copy_number,n);at+=n;
        memcpy(copy_status+at," / ",3);at+=3;
        (void)tiku_word_format(v->copy_total,10,copy_number);n=strlen(copy_number);memcpy(copy_status+at,copy_number,n);at+=n;
        memcpy(copy_status+at," bytes prepared (64 KiB per file).",sizeof " bytes prepared (64 KiB per file).");status=copy_status;
    }
#endif
    if(v->file_job==7)status="Writing copy... Cancel may require remount.";
#endif
#endif
#if SURFACE_EDITOR
    char editor_status[96];
#if SURFACE_NOTES
    if(v->foreground==SURFACE_NOTES_APP)status=v->notes_loading?"Loading saved notes...":v->status==TIKU_RESOURCE_STALE_CURSOR?"Stored note changed. Your draft is retained; Note tools can duplicate it.":v->status!=TIKU_RESOURCE_OK&&v->status!=TIKU_RESOURCE_PENDING?"Storage unavailable or refused. Drafts are retained; Save all retries.":"24 retained notes | 8 KiB each | Changes save after 500 ms without typing.";
#endif
    if(v->foreground==SURFACE_EDITOR_APP){
        if(v->editor_busy)status=v->editor_busy==1?"Opening document...":"Saving document...";
        else if(v->status!=TIKU_RESOURCE_OK)status=storage_status(v->status);
        else if(v->editor_tools==2&&v->editor_error==TIKU_EDIT_NOTHING)status="No lines changed for this selection or document boundary.";
        else if(v->editor_error==TIKU_EDIT_NOTHING)status="No matching text. Find sets the query; Replace needs the selected match.";
        else if(v->editor_error&&v->editor_error!=TIKU_EDIT_NOTHING)status="Edit refused: invalid text, document/history limit, or no matching text.";
        else if(v->editor_replaced){
            char number[65];tiku_word_format(v->editor_replaced,10,number);
            strcpy(editor_status,number);strcpy(editor_status+strlen(number)," matches replaced as one undo step.");status=editor_status;
        }
        else if(v->editor_wrapped)status="Search wrapped to the other end of the document.";
        else status=v->editor_dirty?"Unsaved changes | 8 KiB document limit":"Document saved | 8 KiB document limit";
    }
#endif
#if SURFACE_PREFERENCES
    if(v->foreground==SURFACE_SETTINGS&&v->preferences_page==1){
        static const char *states[]={"Default desktop preferences","Desktop preferences saved","Saving desktop preferences","Invalid record; defaults applied","Desktop preferences unavailable","Save failed; applied preferences unchanged","Unsupported record; defaults applied"};
        status=v->preferences_status<sizeof states/sizeof *states?states[v->preferences_status]:"Desktop preference error";
        if(v->preferences_dirty&&v->preferences_status!=SURFACE_SETTINGS_PENDING&&v->preferences_status!=SURFACE_SETTINGS_FAILED)status="Changes not saved. Save applies background and document defaults.";
        if(v->settings_status==SURFACE_SETTINGS_PENDING)status="Saving touch calibration; desktop changes remain pending.";
    }
#endif
#if SURFACE_VFS
    if(v->foreground==SURFACE_VFS_APP)status=v->inspector_phase?"Device operation pending":v->inspector_saved&&v->inspector_error?"Setting written; readback failed. Refresh before editing.":v->inspector_error== -11?"Value changed since review. Read again before editing.":v->inspector_error?"Operation refused or node unavailable. Refresh to inspect again.":v->inspector_saved?"Setting written and read back":v->inspector_dynamic?"Dynamic child enumeration is not implemented in this provider":"Live local namespace; node values are read on request.";
#endif
#if SURFACE_PREFERENCES
    if(v->foreground==SURFACE_SETTINGS&&v->preferences_page==3)status=v->inspector_phase?"Device operation pending":v->inspector_saved&&v->inspector_error?"Setting written; readback failed. Read again to check the value.":v->inspector_error==-11?"Value changed since review. Read again before editing.":v->inspector_error?"Operation refused or setting unavailable. Return to Device settings and retry.":v->inspector_saved?"Setting written and read back":"Read the current value before reviewing a change.";
    if(v->foreground==SURFACE_SETTINGS&&v->preferences_page==2)status=v->preferences_device_error?"Setting could not be opened. Finish pending work or close other tasks and retry.":"Choose a device setting. Reading and changing values are explicit actions.";
#endif
#if SURFACE_HEX
#if SURFACE_DIFF
    if(v->foreground==SURFACE_DIFF_APP)status=v->diff_busy&&v->diff_export?"Exporting patch... Cancel retains the compared files.":v->diff_patch_result==1?"Unified patch saved.":v->diff_patch_result==2?"Unified patch copied to the clipboard.":v->diff_patch_result==3?"Patch exceeds 255 clipboard bytes. Use Save patch; the clipboard is unchanged.":v->diff_patch_result==4?"Binary files cannot be exported as a text patch.":v->diff_patch_result==5?"No differences under the selected options. The clipboard is unchanged.":v->diff_export&&v->status!=TIKU_RESOURCE_OK?"Patch not saved. Choose an unused path or retry storage; compared files remain.":v->diff_busy?"Comparing files... Cancel stops the current operation.":v->status==TIKU_RESOURCE_LIMIT?"File exceeds 8 KiB or 256 lines. Previous files are retained.":v->status==TIKU_RESOURCE_CANCELLED?"Comparison cancelled. Loaded files are retained.":v->status!=TIKU_RESOURCE_OK?"File unavailable or changed during loading. Previous files are retained.":v->diff_ready?"Loaded file snapshots | Up/Down scroll; Left/Right visit changes.":"Choose left and right files to compare. Each file: 8 KiB, 256 lines.";
#endif
    if(v->foreground==SURFACE_HEX_APP)status=v->byte_busy?"File operation pending; Cancel retains the document. Publication may need refresh.":v->status!=TIKU_RESOURCE_OK?storage_status(v->status):v->byte_search==1?"Searching bytes... Cancel stops the search.":v->byte_search==2?"Pattern found.":v->byte_search==3?"Pattern not found.":v->byte_high>=0?"First hex digit staged. Complete the byte or navigate to discard it.":v->byte_dirty?"Unsaved byte changes | 8 KiB document limit":!v->byte_editing?"Keyboard navigation only. Choose Keyboard to enable byte editing.":v->byte_ascii?"ASCII keyboard editing | 8 KiB document limit":"Hexadecimal keyboard editing | Two digits replace one byte.";
#endif
#if SURFACE_READER
#if SURFACE_IMAGE
    if(v->foreground==SURFACE_IMAGE_APP)status=v->image_busy?"Opening or rendering image... Cancel keeps the previous view.":v->image_error?"Image unavailable, changed, unsupported or over the limit. Previous image kept.":v->image_frame?v->image_path:"BMP and PNG through 1 MiB and 131072 pixels; JPEG is unavailable in this build.";
#endif
    if(v->foreground==SURFACE_READER_APP)status=v->reader_copied>0?"Match copied to the desktop clipboard.":v->reader_copied<0?"Find a passage before copying it.":v->reader_searching?"Searching document text... Cancel stops the search.":v->reader_busy?"Opening document... Cancel retains the current page.":v->reader_error?"Open refused: unsupported format/link, file changed, unavailable or exceeds limits.":v->reader_notices?"Document uses bounded layout and fallback fonts; images are not decoded.":"Local documents up to 8 KiB; Back returns to the previous page.";
#endif
#if SURFACE_DOCUMENTS
    if(v->document_error)status=v->foreground==SURFACE_BROWSER?
        (v->document_error==SURFACE_EDITOR_APP?"Editor has unfinished work. Use Applications to save, close or finish it.":v->document_error==SURFACE_HEX_APP?"TikuHex has unfinished work. Use Applications to save, close or finish it.":"Reader is opening a document. Use Applications to finish or cancel it."):
        "The application could not open this document. Its existing work is retained.";
#endif
    text(s, 16, 469, status, 0);
}
