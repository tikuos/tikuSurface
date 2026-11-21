/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * document.c - static handler discovery and identity-bound Tracker handoff.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_document_handler.h"
#include "tiku_font.h"
#include "tiku_ui.h"
#include "tiku_text_layout.h"
#include <string.h>
/** @brief Product filename types; unknown suffixes require an explicit handler choice. */
static const struct {const char *suffix,*type;} types[]={
    {".txt","text/plain"},{".bas","text/x-basic"},{".log","text/plain"},
    {".c","text/x-csrc"},{".h","text/x-chdr"},{".json","text/json"},
    {".xml","text/xml"},{".ini","text/plain"},{".csv","text/csv"},
    {".md","text/markdown"},{".markdown","text/markdown"},
    {".htm","text/html"},{".html","text/html"},{".bin","application/octet-stream"},
    {".png","image/png"},{".jpg","image/jpeg"},{".jpeg","image/jpeg"},
    {".bmp","image/bmp"},{".pdf","application/pdf"},{".zip","application/zip"}
};
/** @brief Installed document claims derived from product manifests and admitted providers. */
static const struct {surface_app_t app;tiku_document_handler_t claims;} handlers[]={
#if SURFACE_IMAGE
    {SURFACE_IMAGE_APP,{{"image/bmp","image/png"},2,0}},
#endif
    {SURFACE_EDITOR_APP,{{"text/"},1,0}},
    {SURFACE_READER_APP,{{"text/html","text/markdown"},2,0}},
    {SURFACE_HEX_APP,{{"application/octet-stream"},1,1}}
};
/** @brief Compare filename suffixes with ASCII letter folding. */
static int suffix_equal(const char *a,const char *b)
{
    while(*a&&*b){unsigned char ch=(unsigned char)*a++;if(ch>='A'&&ch<='Z')ch+='a'-'A';if(ch!=(unsigned char)*b++)return 0;}
    return !*a&&!*b;
}
const char *surface_document_type(const char *path)
{
    if(!path)return NULL;
    const char *leaf=strrchr(path,'/');leaf=leaf?leaf+1:path;
    const char *suffix=strrchr(leaf,'.');if(!suffix)return NULL;
    for(unsigned i=0;i<sizeof types/sizeof *types;++i)if(suffix_equal(suffix,types[i].suffix))return types[i].type;
    return NULL;
}
const char *surface_document_name(surface_app_t app)
{return app==SURFACE_IMAGE_APP?"TikuImage":app==SURFACE_EDITOR_APP?"Editor":app==SURFACE_READER_APP?"Reader":app==SURFACE_HEX_APP?"TikuHex":"Application";}
int surface_document_choices(const char *path,surface_app_t *apps,unsigned *relations)
{
    const char *type=surface_document_type(path);int count=0;
    for(unsigned i=0;i<sizeof handlers/sizeof *handlers;++i){
        unsigned relation=tiku_document_relation(&handlers[i].claims,type);if(!relation)continue;
        int at=count++;
        while(at>0&&(relations[at-1]<relation||(relations[at-1]==relation&&
            strcmp(surface_document_name(handlers[i].app),surface_document_name(apps[at-1]))<0))){
            apps[at]=apps[at-1];relations[at]=relations[at-1];--at;}
        apps[at]=handlers[i].app;relations[at]=relation;
    }
    return count;
}
/** @brief The selected file still owns a successful preview and listed identity. */
static int ready(const surface_t *s)
{
    const surface_app_state_t *b=s->active.state;
    return b&&b->id==SURFACE_BROWSER&&b->reading&&b->epoch&&b->node&&
        b->browser->target[0]&&!b->queued&&!b->file_job&&
#if SURFACE_DIRECTORIES
        !b->browser->directory&&
#endif
        !tiku_paged_storage_pending(b->store)&&tiku_paged_storage_status(b->store)==TIKU_RESOURCE_OK;
}
/** @brief Queue a document without replacing dirty or busy target state. */
static void route(surface_t *s,surface_app_t app)
{
    if(!ready(s))return;
    surface_app_state_t *target=s->background[app].state;
    if((app==SURFACE_EDITOR_APP&&(!surface_editor_can_open(target,s->active.state->browser->target)))||
       (app==SURFACE_HEX_APP&&(surface_hex_modified(target)||surface_hex_busy(target)))||
       (app==SURFACE_READER_APP&&surface_reader_busy(target))
#if SURFACE_IMAGE
       ||(app==SURFACE_IMAGE_APP&&surface_image_busy(target))
#endif
#if SURFACE_DIFF
       ||(app==SURFACE_DIFF_APP&&surface_diff_busy(target))
#endif
       ){
        s->document_menu=0;s->document_error=app;s->dirty=1;return;
    }
    s->document_menu=0;
    if(!surface_launch(s,app)){s->document_error=app;s->dirty=1;return;}
    surface_app_state_t *b=s->active.state;
    strcpy(s->open_path,b->browser->target);s->open_epoch=b->epoch;s->open_node=b->node;s->document_target=app;
}
int surface_document_action(surface_t *s,int action)
{
    if(s->document_menu){
#if SURFACE_DIFF
        if(action==ACTION_DOCUMENT_COMPARE&&s->document_menu==1){s->document_menu=2;s->dirty=1;return 1;}
        if(s->document_menu==2&&action>=ACTION_DOCUMENT_CHOICE&&action<ACTION_DOCUMENT_CHOICE+2){
            s->document_side=(unsigned)(action-ACTION_DOCUMENT_CHOICE);route(s,SURFACE_DIFF_APP);return 1;}
        if(s->document_menu==2&&action!=ACTION_DOCUMENT_CANCEL)return 1;
#endif
        if(action==ACTION_DOCUMENT_CANCEL){s->document_menu=0;s->dirty=1;return 1;}
        if(action>=ACTION_DOCUMENT_CHOICE&&action<ACTION_DOCUMENT_CHOICE+3){
            surface_app_t apps[3];unsigned relations[3];surface_app_state_t *b=s->active.state;
            if(b&&b->browser){int count=surface_document_choices(b->browser->target,apps,relations);
                if(action-ACTION_DOCUMENT_CHOICE<count)route(s,apps[action-ACTION_DOCUMENT_CHOICE]);}
        }
        return 1;
    }
    if(action!=ACTION_DOCUMENT_OPEN&&action!=ACTION_DOCUMENT_WITH&&action!=ACTION_FILE_EDIT&&action!=ACTION_FILE_READ&&action!=ACTION_FILE_BYTES)return 0;
    if(!ready(s)||s->close_confirmation||s->text_field.active)return 1;
    s->document_error=SURFACE_HOME;
    if(action==ACTION_FILE_EDIT)route(s,SURFACE_EDITOR_APP);
    else if(action==ACTION_FILE_READ)route(s,SURFACE_READER_APP);
    else if(action==ACTION_FILE_BYTES)route(s,SURFACE_HEX_APP);
    else{
        surface_app_t apps[3];unsigned relations[3];int count=surface_document_choices(s->active.state->browser->target,apps,relations);
        if(action==ACTION_DOCUMENT_OPEN&&count&&relations[0]>=TIKU_DOCUMENT_SUPERTYPE)route(s,
#if SURFACE_PREFERENCES
            surface_preferences_handler(s,s->active.state->browser->target,apps[0])
#else
            apps[0]
#endif
            );
        else{s->document_menu=1;s->applications_menu=0;s->dirty=1;(void)tiku_press_release(&s->press,0);}
    }
    return 1;
}
void surface_document_deliver(surface_t *s)
{
    if(!s->open_path[0])return;
    surface_app_t target=s->document_target;int accepted=0;
    if(s->active.state&&s->active.state->id==target){
        #if SURFACE_DIFF
        if(target==SURFACE_DIFF_APP)accepted=surface_diff_selected(s->active.state,s->document_side,s->open_path,s->open_epoch,s->open_node);
        else
#endif
#if SURFACE_IMAGE
        if(target==SURFACE_IMAGE_APP)accepted=surface_image_open(s->active.state,s->open_path,s->open_epoch,s->open_node);
        else
#endif
        if(target==SURFACE_EDITOR_APP)accepted=surface_editor_open(s->active.state,s->open_path,s->open_epoch,s->open_node);
        else if(target==SURFACE_READER_APP)accepted=surface_reader_open(s->active.state,s->open_path,s->open_epoch,s->open_node);
        else if(target==SURFACE_HEX_APP)accepted=surface_hex_open(s->active.state,s->open_path,s->open_epoch,s->open_node);
    }
    if(!accepted)s->document_error=target;
    s->open_path[0]=0;s->document_target=SURFACE_HOME;
}
void surface_document_view(const surface_t *s,surface_view_t *v)
{
    v->document_menu=s->document_menu;v->document_error=s->document_error;
    if(!s->active.state||s->active.state->id!=SURFACE_BROWSER||!s->active.state->browser)return;
    const char *path=s->active.state->browser->target,*type=surface_document_type(path);
    strcpy(v->document_type,type?type:"Unknown file type");
    v->document_count=surface_document_choices(path,v->document_apps,v->document_relations);
    if(v->document_count&&v->document_relations[0]>=TIKU_DOCUMENT_SUPERTYPE)v->document_default=
#if SURFACE_PREFERENCES
        surface_preferences_handler(s,path,v->document_apps[0]);
#else
        v->document_apps[0];
#endif
}
/** @brief Modal rows and preview actions have distinct touch rectangles. */
static tiku_rect_t rect(int action)
{
    if(action==ACTION_DOCUMENT_OPEN)return (tiku_rect_t){428,108,160,42};
    if(action==ACTION_DOCUMENT_WITH)return (tiku_rect_t){608,108,160,42};
    if(action==ACTION_DOCUMENT_COMPARE)return (tiku_rect_t){96,368,232,44};
    if(action==ACTION_DOCUMENT_CANCEL)return (tiku_rect_t){552,368,152,44};
    return (tiku_rect_t){96,156+(action-ACTION_DOCUMENT_CHOICE)*66,608,56};
}
/** @brief Test a half-open logical touch rectangle. */
static int contains(tiku_rect_t r,int x,int y)
{return x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h;}
int surface_document_hit(const surface_view_t *v,int x,int y)
{
    if(v->document_menu){
#if SURFACE_DIFF
        if(v->document_menu==1&&contains(rect(ACTION_DOCUMENT_COMPARE),x,y))return ACTION_DOCUMENT_COMPARE;
        if(v->document_menu==2){
            for(int i=0;i<2;++i)if(contains(rect(ACTION_DOCUMENT_CHOICE+i),x,y))return ACTION_DOCUMENT_CHOICE+i;
            return contains(rect(ACTION_DOCUMENT_CANCEL),x,y)?ACTION_DOCUMENT_CANCEL:0;
        }
#endif
        if(contains(rect(ACTION_DOCUMENT_CANCEL),x,y))return ACTION_DOCUMENT_CANCEL;
        for(int i=0;i<v->document_count;++i)if(contains(rect(ACTION_DOCUMENT_CHOICE+i),x,y))return ACTION_DOCUMENT_CHOICE+i;
    }else if(v->foreground==SURFACE_BROWSER&&v->file_open){
        if(contains(rect(ACTION_DOCUMENT_OPEN),x,y))return ACTION_DOCUMENT_OPEN;
        if(contains(rect(ACTION_DOCUMENT_WITH),x,y))return ACTION_DOCUMENT_WITH;
    }
    return 0;
}
void surface_document_paint(tiku_surface_t *s,const surface_view_t *v)
{
    if(v->document_menu){
#if SURFACE_DIFF
        if(v->document_menu==2){
            (void)tiku_ui_window(s,(tiku_rect_t){80,96,640,328},"Compare in TikuDiff",1);
            tiku_text(s,tiku_font_plain(),96,138,"Use the selected file as:",TIKU_C_TEXT);
            tiku_ui_button(s,rect(ACTION_DOCUMENT_CHOICE),"Left file",0);
            tiku_ui_button(s,rect(ACTION_DOCUMENT_CHOICE+1),"Right file",0);
            tiku_text(s,tiku_font_plain(),96,326,"The other snapshot stays loaded. Each file accepts 8 KiB / 256 lines.",TIKU_C_TEXT);
            tiku_ui_button(s,rect(ACTION_DOCUMENT_CANCEL),"Cancel",0);return;
        }
#endif
        (void)tiku_ui_window(s,(tiku_rect_t){80,96,640,328},"Open with",1);
        tiku_text(s,tiku_font_plain(),96,138,v->document_type,TIKU_C_TEXT);
#if SURFACE_PREFERENCES
        tiku_text(s,tiku_font_plain(),440,138,v->document_default==SURFACE_HEX_APP?"Default: TikuHex":v->document_default==SURFACE_EDITOR_APP?"Default: Editor":v->document_default==SURFACE_READER_APP?"Default: Reader":"Choose a handler",TIKU_C_TEXT);
#endif
        for(int i=0;i<v->document_count;++i){
            const char *label=v->document_apps[i]==SURFACE_HEX_APP?"TikuHex - inspect or edit raw bytes":
                v->document_apps[i]==SURFACE_READER_APP?"Reader - read this document":"Editor - edit UTF-8 text";
            tiku_ui_button(s,rect(ACTION_DOCUMENT_CHOICE+i),label,0);
        }
        tiku_text(s,tiku_font_plain(),96,362,v->document_default?"Document handlers accept files through 8 KiB.":"No matching document handler. Raw-byte inspection is available.",TIKU_C_TEXT);
        #if SURFACE_DIFF
        tiku_ui_button(s,rect(ACTION_DOCUMENT_COMPARE),"Compare...",0);
#endif
        tiku_ui_button(s,rect(ACTION_DOCUMENT_CANCEL),"Cancel",0);
    }else if(v->foreground==SURFACE_BROWSER&&v->reading&&v->file_open){
        tiku_ui_button(s,rect(ACTION_DOCUMENT_OPEN),"Open",0);
        tiku_ui_button(s,rect(ACTION_DOCUMENT_WITH),"Open with...",0);
    }
}
