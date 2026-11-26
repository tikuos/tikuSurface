/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * text_entry.c - modal touch input using shared field selection and history.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "touch_key.h"
#include "tiku_ui.h"
#include "tiku_font.h"
#include "tiku_word_model.h"
#include <string.h>

/** @brief Touch actions outside the character-key range. */
enum { KEY_CHARACTER=1, KEY_ALL=50, KEY_CUT, KEY_COPY, KEY_PASTE, KEY_UNDO,
    KEY_REDO, KEY_ERASE, KEY_SHIFT, KEY_SYMBOLS, KEY_CANCEL, KEY_SPACE,
    KEY_LEFT, KEY_RIGHT, KEY_ACCEPT };
/** @brief Return fixed dialog geometry for one key. */
static tiku_rect_t rect(int id)
{
    if(id>=KEY_CHARACTER && id<KEY_CHARACTER+40) {
        int n=id-KEY_CHARACTER;
        return (tiku_rect_t){24+(n%10)*76,208+(n/10)*46,68,42};
    }
    if(id>=KEY_ALL && id<=KEY_ERASE) return (tiku_rect_t){24+(id-KEY_ALL)*108,158,100,42};
    if(id==KEY_SHIFT) return (tiku_rect_t){24,396,92,44};
    if(id==KEY_SYMBOLS) return (tiku_rect_t){124,396,92,44};
    if(id==KEY_CANCEL) return (tiku_rect_t){224,396,92,44};
    if(id==KEY_SPACE) return (tiku_rect_t){324,396,140,44};
    if(id==KEY_LEFT) return (tiku_rect_t){472,396,72,44};
    if(id==KEY_RIGHT) return (tiku_rect_t){552,396,72,44};
    if(id==KEY_ACCEPT) return (tiku_rect_t){632,396,144,44};
    return (tiku_rect_t){0};
}
/** @brief Find a touch key; field selection is routed separately. */
static int hit(int x,int y)
{
    for(int id=KEY_CHARACTER;id<=KEY_ACCEPT;++id) {
        tiku_rect_t r=rect(id);
        if(x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h) return id;
    }
    return 0;
}
/** @brief Read the shell clipboard into the model's bounded transfer buffer. */
static int read_clipboard(void *context,char *text,size_t capacity,size_t *bytes)
{
    surface_t *s=context;size_t n=strlen(s->clipboard);
    if(n>=capacity) return 0;
    memcpy(text,s->clipboard,n+1);*bytes=n;return 1;
}
/** @brief Replace the shell clipboard after checking the complete transfer. */
static int write_clipboard(void *context,const char *text,size_t bytes)
{
    surface_t *s=context;
    if(bytes>=sizeof s->clipboard) return 0;
    memcpy(s->clipboard,text,bytes);s->clipboard[bytes]=0;return 1;
}
void surface_text_open(surface_t *s)
{
    if(!s->active.state) return;
#if SURFACE_VFS
#if SURFACE_HEX
#if SURFACE_DIFF
    if(s->active.state->id==SURFACE_DIFF_APP)tiku_field_start(&s->text_field,surface_diff_draft(s->active.state));else
#endif
    if(s->active.state->id==SURFACE_HEX_APP)tiku_field_start(&s->text_field,surface_hex_draft(s->active.state));else
#endif
#if SURFACE_READER
    if(s->active.state->id==SURFACE_READER_APP)tiku_field_start(&s->text_field,surface_reader_draft(s->active.state));else
#endif
    if(s->active.state->id==SURFACE_VFS_APP
#if SURFACE_PREFERENCES
       ||(s->active.state->id==SURFACE_SETTINGS&&s->active.state->preferences_page==3)
#endif
      )tiku_field_start(&s->text_field,surface_inspector_draft(s->active.state));else
#endif
#if SURFACE_TERMINAL
    if(s->active.state->id==SURFACE_TERMINAL_APP)tiku_field_start(&s->text_field,"");else
#endif
#if SURFACE_EDITOR
#if SURFACE_IMAGE
    if(s->active.state->id==SURFACE_IMAGE_APP)tiku_field_start(&s->text_field,surface_image_draft(s->active.state));else
#endif
#if SURFACE_NOTES
    if(s->active.state->id==SURFACE_NOTES_APP)tiku_field_start(&s->text_field,surface_editor_draft(surface_notes_editor(s->active.state)));else
#endif
    if(s->active.state->id==SURFACE_EDITOR_APP)tiku_field_start(&s->text_field,surface_editor_draft(s->active.state));
    else
#endif
#if SURFACE_FILE_JOBS
    if(s->active.state->id==SURFACE_BROWSER) {
#if SURFACE_CONTENT_JOBS
        if(s->active.state->file_dialog==2||s->active.state->file_dialog==4)tiku_field_start(&s->text_field,s->active.state->destination);
        else
#endif
        tiku_field_start(&s->text_field,s->active.state->browser->target);
    }
    else
#endif
    if(s->active.state->id!=SURFACE_CALCULATOR) return;
    else tiku_field_start(&s->text_field,s->active.state->word_mode==2?
        surface_calculator_draft(s->active.state):s->active.state->hex);
    tiku_field_input_init(&s->text_input);
    s->text_symbols=s->text_shift=s->text_error=0;
    s->text_press=(tiku_press_t){0};
}
/** @brief Accept a validated word or keep the draft visible with an error. */
static void accept(surface_t *s)
{
    surface_app_state_t *state=s->active.state;uint64_t word;
    if(!state) return;
#if SURFACE_TERMINAL
#if SURFACE_VFS
#if SURFACE_HEX
#if SURFACE_DIFF
    if(state->id==SURFACE_DIFF_APP){if(!surface_diff_accept(state,s->text_field.text)){s->text_error=1;return;}tiku_field_stop(&s->text_field);return;}
#endif
    if(state->id==SURFACE_HEX_APP){if(!surface_hex_accept(state,s->text_field.text)){s->text_error=1;return;}tiku_field_stop(&s->text_field);return;}
#endif
#if SURFACE_READER
    if(state->id==SURFACE_READER_APP){if(!surface_reader_accept(state,s->text_field.text)){s->text_error=1;return;}tiku_field_stop(&s->text_field);return;}
#endif
    if(state->id==SURFACE_VFS_APP
#if SURFACE_PREFERENCES
       ||(state->id==SURFACE_SETTINGS&&state->preferences_page==3)
#endif
      ){if(!surface_inspector_accept(state,s->text_field.text)){s->text_error=1;return;}tiku_field_stop(&s->text_field);return;}
#endif
    if(state->id==SURFACE_TERMINAL_APP){if(!surface_terminal_command(state,s->text_field.text)){s->text_error=1;return;}tiku_field_stop(&s->text_field);return;}
#endif
#if SURFACE_EDITOR
#if SURFACE_IMAGE
    if(state->id==SURFACE_IMAGE_APP){if(!surface_image_accept(state,s->text_field.text)){s->text_error=1;return;}tiku_field_stop(&s->text_field);return;}
#endif
#if SURFACE_NOTES
    if(state->id==SURFACE_NOTES_APP){if(!surface_editor_accept(surface_notes_editor(state),s->text_field.text)){s->text_error=1;return;}tiku_field_stop(&s->text_field);return;}
#endif
    if(state->id==SURFACE_EDITOR_APP){if(!surface_editor_accept(state,s->text_field.text)){s->text_error=1;return;}tiku_field_stop(&s->text_field);return;}
#endif
#if SURFACE_FILE_JOBS
    if(state->id==SURFACE_BROWSER) {
        int accepted;
#if SURFACE_CONTENT_JOBS
        if(state->file_dialog==3)accepted=surface_file_copy(state,s->text_field.text);
        else if(state->file_dialog==2||state->file_dialog==4)accepted=surface_file_create(state,s->text_field.text);
        else
#endif
        accepted=surface_file_rename(state,s->text_field.text);
        if(!accepted){s->text_error=1;return;}
        tiku_field_stop(&s->text_field);return;
    }
#endif
    if(state->id!=SURFACE_CALCULATOR) return;
    if(state->word_mode==2) {
        if(!surface_calculator_checksum_accept(state,s->text_field.text)){s->text_error=1;return;}
        tiku_field_stop(&s->text_field);return;
    }
    if(s->text_field.len>64 || !tiku_word_parse(s->text_field.len?s->text_field.text:"0",
        state->word_base,(int)state->word_width,&word)) {s->text_error=1;return;}
    memcpy(state->hex,s->text_field.text,(size_t)s->text_field.len+1);
    state->range_copied=0;
    state->status=TIKU_RESOURCE_OK;tiku_field_stop(&s->text_field);
}
/** @brief Send one key through the shared history and selection controller. */
static void field_event(surface_t *s,const tiku_input_event_t *event)
{
    tiku_field_clipboard_t clipboard={s,read_clipboard,write_clipboard};
    tiku_field_result_t result=tiku_field_event_with_clipboard(&s->text_field,&s->text_input,
        event,(tiku_rect_t){24,100,752,48},1,&clipboard);
    if(result==TIKU_FIELD_COMMITTED) accept(s);
    else if(result==TIKU_FIELD_CANCELLED) tiku_field_stop(&s->text_field);
    else if(result==TIKU_FIELD_REJECTED) s->text_error=1;
    else if(result==TIKU_FIELD_HANDLED) s->text_error=0;
}
/** @brief Translate an accepted touch release into a logical field action. */
static void action(surface_t *s,int id)
{
    tiku_input_event_t event={.type=TIKU_EVENT_KEY_DOWN};
    if(id>=KEY_CHARACTER && id<KEY_CHARACTER+40) {
        event.text[0]=surface_touch_key(id-KEY_CHARACTER,s->text_symbols,s->text_shift);
        event.key=(unsigned char)event.text[0];
    } else switch(id) {
    case KEY_ALL: event.key='a';event.modifiers=TIKU_MOD_CMD;break;
    case KEY_CUT: event.key='x';event.modifiers=TIKU_MOD_CMD;break;
    case KEY_COPY: event.key='c';event.modifiers=TIKU_MOD_CMD;break;
    case KEY_PASTE: event.key='v';event.modifiers=TIKU_MOD_CMD;break;
    case KEY_UNDO: event.key='z';event.modifiers=TIKU_MOD_CMD;break;
    case KEY_REDO: event.key='z';event.modifiers=TIKU_MOD_CMD|TIKU_MOD_SHIFT;break;
    case KEY_ERASE: event.key=TIKU_KEY_BACKSPACE;break;
    case KEY_LEFT: event.key=TIKU_KEY_LEFT;break;
    case KEY_RIGHT: event.key=TIKU_KEY_RIGHT;break;
    case KEY_SPACE: event.text[0]=' ';event.key=' ';break;
    case KEY_ACCEPT: accept(s);return;
    case KEY_CANCEL: tiku_field_stop(&s->text_field);return;
    case KEY_SHIFT: s->text_shift^=1;return;
    case KEY_SYMBOLS: s->text_symbols^=1;return;
    default:return;
    }
    field_event(s,&event);
}
int surface_text_event(surface_t *s,const tiku_input_event_t *event)
{
    if(!s->text_field.active) return 0;
    if(event->type==TIKU_EVENT_DEACTIVATED) {
        (void)tiku_press_release(&s->text_press,0);field_event(s,event);
    } else if(event->type==TIKU_EVENT_POINTER_DOWN || event->type==TIKU_EVENT_POINTER_MOVE ||
        event->type==TIKU_EVENT_POINTER_UP) {
        int id=hit(event->x,event->y);
        field_event(s,event);
        if(event->type==TIKU_EVENT_POINTER_DOWN && event->button==1) (void)tiku_press_arm(&s->text_press,id);
        else if(event->type==TIKU_EVENT_POINTER_MOVE) (void)tiku_press_track(&s->text_press,id);
        else if(event->type==TIKU_EVENT_POINTER_UP && event->button==1)
            action(s,tiku_press_release(&s->text_press,id));
    } else {
        tiku_input_event_t logical=*event;
        if(logical.type==TIKU_EVENT_KEY_DOWN) {
            if(logical.key>=32 && logical.key<127 && !logical.text[0]) logical.text[0]=(char)logical.key;
        }
        field_event(s,&logical);
    }
    return 1;
}
void surface_text_paint(tiku_surface_t *s,const surface_view_t *v)
{
    tiku_fill(s,(tiku_rect_t){0,0,800,480},TIKU_C_BACKDROP);
    int file=v->foreground==SURFACE_BROWSER,creating=0,copying=0,folder=0;
#if SURFACE_CONTENT_JOBS
    creating=file&&(v->file_dialog==2||v->file_dialog==4);copying=file&&v->file_dialog==3;folder=file&&v->file_dialog==4;
#endif
#if SURFACE_TERMINAL
#if SURFACE_VFS
#if SURFACE_HEX
#if SURFACE_DIFF
    if(v->foreground==SURFACE_DIFF_APP){
        (void)tiku_ui_window(s,(tiku_rect_t){8,8,784,464},v->diff_dialog==3?"Save unified patch":v->diff_dialog==1?"Open left file":"Open right file",1);
        tiku_text(s,tiku_font_plain(),24,85,v->diff_dialog==3?"Choose an unused media path. Existing files are preserved.":"Absolute media path. Each file: up to 8 KiB and 256 lines.",TIKU_C_TEXT);
    }else
#endif
    if(v->foreground==SURFACE_HEX_APP){
        const char *title=v->byte_dialog==1?"Open binary file":v->byte_dialog==2?"Save bytes as":v->byte_dialog==3?"Change selected byte":v->byte_dialog==4?"Go to offset":"Find bytes";
        (void)tiku_ui_window(s,(tiku_rect_t){8,8,784,464},title,1);
        tiku_text(s,tiku_font_plain(),24,85,v->byte_dialog<=2?"Absolute path; Save as requires a new name. Documents through 8 KiB.":v->byte_dialog==3?"Enter exactly one hexadecimal byte, such as FF.":v->byte_dialog==4?"Decimal or 0x hexadecimal offset within the document.":"Enter complete hex byte pairs; search wraps after the selected byte.",TIKU_C_TEXT);
    }else
#endif
#if SURFACE_READER
    if(v->foreground==SURFACE_READER_APP){
        (void)tiku_ui_window(s,(tiku_rect_t){16,40,768,408},v->reader_find_dialog?"Find in document":"Open document",1);
        tiku_text(s,tiku_font_plain(),24,85,v->reader_find_dialog?"Literal text, ignoring ASCII letter case. Next and Previous wrap.":"Open a local HTML, Markdown or text file up to 8 KiB.",TIKU_C_TEXT);
    }else
#endif
    if(v->foreground==SURFACE_VFS_APP
#if SURFACE_PREFERENCES
       ||(v->foreground==SURFACE_SETTINGS&&v->preferences_page==3)
#endif
      ){
        (void)tiku_ui_window(s,(tiku_rect_t){16,40,768,408},v->inspector_dialog==1?"VFS path":"Review device setting",1);
        tiku_text(s,tiku_font_plain(),24,85,v->inspector_dialog==1?"Enter an absolute path. No node value is read until requested.":"Apply writes this setting if the reviewed value and permission still match.",TIKU_C_TEXT);
    }else
#endif
    if(v->foreground==SURFACE_TERMINAL_APP){
        (void)tiku_ui_window(s,(tiku_rect_t){16,40,768,408},"Local shell command",1);
        tiku_text(s,tiku_font_plain(),24,85,"Apply sends this command to TikuOS. Stop sends Ctrl-C.",TIKU_C_TEXT);
    }else
#endif
#if SURFACE_IMAGE
    if(v->foreground==SURFACE_IMAGE_APP){
        (void)tiku_ui_window(s,(tiku_rect_t){16,40,768,408},"Open image",1);
        tiku_text(s,tiku_font_plain(),24,85,"Enter an absolute media path. BMP and PNG content are detected independently of its name.",TIKU_C_TEXT);
    }else
#endif
#if SURFACE_EDITOR
    if(v->foreground==SURFACE_EDITOR_APP||(SURFACE_NOTES&&v->foreground==SURFACE_NOTES_APP)){
        const char *title=v->editor_dialog==2?"Save document as":v->editor_dialog==3?"Find text":v->editor_dialog==4?"Replace selected match":v->editor_dialog==5?"Replace all matches":v->editor_dialog==6?"Go to line":"Insert text";
        (void)tiku_ui_window(s,(tiku_rect_t){16,40,768,408},title,1);
        tiku_text(s,tiku_font_plain(),24,85,v->editor_dialog==2?"Choose an unused absolute path. Existing files are kept.":v->editor_dialog==3?"Find literal text using the Search tools case and word options.":v->editor_dialog==4?"Replace the selected match. Empty text deletes it.":v->editor_dialog==5?"Replace all matches as one undo step. Empty text deletes them.":v->editor_dialog==6?"Enter a line number starting at 1. Existing lines are required.":"Apply inserts text at the selection. New line is available in the document.",TIKU_C_TEXT);
    }else
#endif
    {
    (void)tiku_ui_window(s,(tiku_rect_t){16,40,768,408},creating?(folder?"New folder":"New file"):copying?"Copy to destination":file?"Rename or move":v->word_mode==2?"Checksum input":"Edit word",1);
    tiku_text(s,tiku_font_plain(),24,85,copying?"Copy to an unused absolute path. Limit: 64 KiB per file.":creating?(folder?"Create an empty folder on a volume with persistent-folder support.":"Create an empty file at an unused absolute path (up to 64 bytes)."):file?"Enter an absolute destination path (up to 64 bytes). Existing files are kept.":v->word_mode==2?(v->checksum_text?"Enter literal UTF-8 text, up to 255 bytes. No terminator is included.":"Enter hexadecimal pairs, up to 255 characters / 127 decoded bytes."):
        "Enter a value in the selected input base. Apply keeps the selected word width.",TIKU_C_TEXT);
    }
    tiku_ui_textfield_scroll(s,(tiku_rect_t){24,100,752,48},v->text_value,v->text_caret,
        v->text_a,v->text_b,TIKU_S_FOCUS,v->text_scroll);
    static const char *labels[]={"All","Cut","Copy","Paste","Undo","Redo","Erase",
        "Shift","Symbols","Cancel","Space","Left","Right","Apply"};
    for(int id=KEY_CHARACTER;id<=KEY_ACCEPT;++id) {
        tiku_rect_t r=rect(id);if(!r.w) continue;
        if(r.y>=s->clip.y+s->clip.h || r.y+r.h<=s->clip.y) continue;
        char label[2]={0};const char *value;
        if(id<KEY_CHARACTER+40) {label[0]=surface_touch_key(id-KEY_CHARACTER,v->text_symbols,v->text_shift);value=label;}
        else value=labels[id-KEY_ALL];
        unsigned state=v->text_pressed==id || (id==KEY_SHIFT && v->text_shift)||(id==KEY_SYMBOLS && v->text_symbols)?TIKU_S_PRESSED:0;
        tiku_ui_button(s,r,value,state);
    }
    if(v->text_error) tiku_text(s,tiku_font_plain(),24,470,
#if SURFACE_HEX
#if SURFACE_DIFF
        v->foreground==SURFACE_DIFF_APP?"Input refused: use an absolute media path; finish or cancel pending work.":
#endif
        v->foreground==SURFACE_HEX_APP?"Input refused: check the path, byte pair or offset. Save or close unsaved work first.":
#endif
#if SURFACE_READER
        v->foreground==SURFACE_READER_APP?"Input refused: use an absolute path to an HTML, Markdown or text file.":
#endif
#if SURFACE_VFS
        v->foreground==SURFACE_VFS_APP?"Input refused: use a canonical absolute path or a nonempty setting within 255 bytes.":
#endif
        file?"Input refused: use an absolute path without empty, dot or parent components.":v->word_mode==2?"Input refused: use complete pairs or literal text within 255 bytes.":
        "Input refused: check the selected base, word width or field length.",TIKU_C_TEXT);
}
