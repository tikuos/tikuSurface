/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * preferences.c - retained desktop choices and compatible document defaults.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_font.h"
#include "tiku_ui.h"
#include <string.h>
#include "tiku_text_layout.h"
/** @brief Audited device setting labels, paths and input guidance. */
static const char *device_names[]={"Device name","UTC time (epoch seconds)","CPU target (Hz)","Cryptography mode","Radio transmit power (dBm)","Available CPU rates (Hz)"};
static const char *device_paths[]={"/sys/device/name","/sys/time","/sys/cpu/freq_target","/sys/crypto/mode","/sys/radio/txpower","/sys/cpu/freq_available"};
static const char *device_help[]={"1 to 31 letters, digits, underscore or hyphen.","Unsigned seconds since the Unix epoch; the clock continues advancing.","Choose an advertised rate. Firmware may require a reboot.","auto selects hardware when available; sw selects software.","An integer in dBm. The radio driver validates its supported range.","Read-only rates advertised by this firmware."};
static const uint32_t colors[]={0x336698,0x287878,0x606060,0x703848,0x687038,0x202020};
void surface_preferences_refresh(surface_t *s)
{
    surface_preferences_t p={0};
    if(s->config.preferences.read&&s->config.preferences.read(s->config.preferences.context,&p)==SURFACE_SETTINGS_SAVED&&surface_preferences_valid(&p))s->preferences=p;
    s->dirty=1;
}
void surface_preferences_start(surface_app_state_t *s)
{
    s->preferences=s->shell->preferences;surface_preferences_t p={0};
    s->preferences_status=s->shell->config.preferences.read?s->shell->config.preferences.read(s->shell->config.preferences.context,&p):SURFACE_SETTINGS_UNAVAILABLE;
    if(s->preferences_status==SURFACE_SETTINGS_SAVED&&surface_preferences_valid(&p)){s->preferences=p;s->shell->preferences=p;}
}
void surface_preferences_poll(surface_app_state_t *s)
{
    if(s->preferences_status!=SURFACE_SETTINGS_PENDING||!s->shell->config.preferences.read)return;
    surface_preferences_t p={0};unsigned status=s->shell->config.preferences.read(s->shell->config.preferences.context,&p);
    if(status==SURFACE_SETTINGS_PENDING)return;
    if(status==SURFACE_SETTINGS_SAVED){
        if(surface_preferences_valid(&p)){s->preferences=p;s->shell->preferences=p;s->preferences_dirty=0;}
        else status=SURFACE_SETTINGS_FAILED;
    }
    s->preferences_status=status;s->shell->dirty=1;
}
/** @brief Known schemas and an idle save provider admit edits. */
static int editable(unsigned status)
{return status!=SURFACE_SETTINGS_PENDING&&status!=SURFACE_SETTINGS_UNAVAILABLE&&status!=SURFACE_SETTINGS_INCOMPATIBLE;}
int surface_preferences_action(surface_app_state_t *s,int action)
{
    if(action==ACTION_PREFS_DEVICE||action==ACTION_PREFS_DEVICE_LIST){s->preferences_page=2;s->preferences_device_error=0;s->shell->dirty=1;return 1;}
    if(s->preferences_page==2&&action>=ACTION_PREFS_DEVICE_ROW&&action<ACTION_PREFS_DEVICE_ROW+6){
        unsigned selected=(unsigned)(action-ACTION_PREFS_DEVICE_ROW);
        if(surface_inspector_open(s,device_paths[selected])){s->preferences_device=selected;s->preferences_page=3;s->preferences_device_error=0;}
        else s->preferences_device_error=1;
        s->shell->dirty=1;return 1;
    }
    if(s->preferences_page==3&&action>=ACTION_INSPECTOR_READ&&action<=ACTION_INSPECTOR_COPY){surface_inspector_action(s,action);return 1;}
    if(action==ACTION_PREFS_OPEN){s->preferences_page=1;s->shell->dirty=1;return 1;}
    if(action==ACTION_PREFS_TOUCH){s->preferences_page=0;s->shell->dirty=1;return 1;}
    if(!s->preferences_page)return 0;
    if(s->preferences_page>=2)return 1;
    if(!editable(s->preferences_status)||s->settings_status==SURFACE_SETTINGS_PENDING)return 1;
    if(action==ACTION_PREFS_SAVE){
        s->preferences_status=s->shell->config.preferences.save&&s->shell->config.preferences.save(s->shell->config.preferences.context,&s->preferences)?SURFACE_SETTINGS_PENDING:SURFACE_SETTINGS_FAILED;
    }else if(action==ACTION_PREFS_DEFAULTS){s->preferences=(surface_preferences_t){0};s->preferences_dirty=1;}
    else if(action>=ACTION_PREFS_COLOR&&action<ACTION_PREFS_COLOR+6){s->preferences.background=(uint8_t)(action-ACTION_PREFS_COLOR);s->preferences_dirty=1;}
    else if(action==ACTION_PREFS_TEXT){s->preferences.text^=1;s->preferences_dirty=1;}
    else if(action==ACTION_PREFS_HTML){s->preferences.html=(uint8_t)((s->preferences.html+1)%3);s->preferences_dirty=1;}
    else if(action==ACTION_PREFS_MARKDOWN){s->preferences.markdown=(uint8_t)((s->preferences.markdown+1)%3);s->preferences_dirty=1;}
    s->shell->dirty=1;return 1;
}
void surface_preferences_view(const surface_t *s,surface_view_t *v)
{
    v->desktop_color=colors[s->preferences.background];
    if(!s->active.state||s->active.state->id!=SURFACE_SETTINGS)return;
    const surface_app_state_t *a=s->active.state;
    v->preferences=a->preferences;v->preferences_status=a->preferences_status;v->preferences_dirty=a->preferences_dirty;v->preferences_page=a->preferences_page;v->preferences_device=a->preferences_device;v->preferences_device_error=a->preferences_device_error;
    if(a->preferences_page==3)surface_inspector_view(a,v);
}
surface_app_t surface_preferences_handler(const surface_t *s,const char *path,surface_app_t fallback)
{
    const char *type=surface_document_type(path);if(!fallback||!type)return fallback;
    static const surface_app_t rich[]={SURFACE_READER_APP,SURFACE_EDITOR_APP,SURFACE_HEX_APP};
    if(!strcmp(type,"text/html"))return rich[s->preferences.html];
    if(!strcmp(type,"text/markdown"))return rich[s->preferences.markdown];
    if(!strncmp(type,"text/",5))return s->preferences.text?SURFACE_HEX_APP:SURFACE_EDITOR_APP;
    return fallback;
}
/** @brief Touch controls for independent calibration and desktop settings pages. */
static tiku_rect_t rect(int action)
{
    if(action==ACTION_PREFS_DEVICE)return (tiku_rect_t){300,92,220,44};
    if(action>=ACTION_PREFS_DEVICE_ROW&&action<ACTION_PREFS_DEVICE_ROW+6){int n=action-ACTION_PREFS_DEVICE_ROW;return (tiku_rect_t){32+(n%2)*376,140+(n/2)*72,360,56};}
    if(action==ACTION_PREFS_DEVICE_LIST)return (tiku_rect_t){596,390,172,44};
    if(action==ACTION_INSPECTOR_READ)return (tiku_rect_t){32,390,172,44};
    if(action==ACTION_INSPECTOR_EDIT)return (tiku_rect_t){220,390,172,44};
    if(action==ACTION_INSPECTOR_COPY)return (tiku_rect_t){408,390,172,44};
    if(action==ACTION_PREFS_OPEN)return (tiku_rect_t){540,92,220,44};
    if(action>=ACTION_PREFS_COLOR&&action<ACTION_PREFS_COLOR+6)return (tiku_rect_t){32+((action-ACTION_PREFS_COLOR)%3)*248,120+((action-ACTION_PREFS_COLOR)/3)*52,232,44};
    if(action>=ACTION_PREFS_TEXT&&action<=ACTION_PREFS_MARKDOWN)return (tiku_rect_t){32,236+(action-ACTION_PREFS_TEXT)*52,736,44};
    return (tiku_rect_t){32+(action-ACTION_PREFS_TOUCH)*248,390,232,44};
}
/** @brief Half-open hit test for logical display coordinates. */
static int contains(tiku_rect_t r,int x,int y)
{return x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h;}
int surface_preferences_hit(const surface_view_t *v,int x,int y)
{
    if(!v->preferences_page){if(contains(rect(ACTION_PREFS_DEVICE),x,y))return ACTION_PREFS_DEVICE;return contains(rect(ACTION_PREFS_OPEN),x,y)?ACTION_PREFS_OPEN:0;}
    if(v->preferences_page==2){
        if(contains(rect(ACTION_PREFS_TOUCH),x,y))return ACTION_PREFS_TOUCH;
        for(int id=ACTION_PREFS_DEVICE_ROW;id<ACTION_PREFS_DEVICE_ROW+6;++id)if(contains(rect(id),x,y))return id;
        return 0;
    }
    if(v->preferences_page==3){
        if(contains(rect(ACTION_PREFS_DEVICE_LIST),x,y))return ACTION_PREFS_DEVICE_LIST;
        if(v->inspector_phase)return 0;
        if(contains(rect(ACTION_INSPECTOR_READ),x,y))return ACTION_INSPECTOR_READ;
        if(contains(rect(ACTION_INSPECTOR_EDIT),x,y)&&v->inspector_loaded&&!v->inspector_error&&(v->inspector_node.flags&SURFACE_NODE_EDIT))return ACTION_INSPECTOR_EDIT;
        if(contains(rect(ACTION_INSPECTOR_COPY),x,y)&&v->inspector_loaded)return ACTION_INSPECTOR_COPY;
        return 0;
    }
    if(contains(rect(ACTION_PREFS_TOUCH),x,y))return ACTION_PREFS_TOUCH;
    if(!editable(v->preferences_status)||v->settings_status==SURFACE_SETTINGS_PENDING)return 0;
    for(int id=ACTION_PREFS_SAVE;id<=ACTION_PREFS_MARKDOWN;++id)if(contains(rect(id),x,y))return id;
    for(int id=ACTION_PREFS_COLOR;id<ACTION_PREFS_COLOR+6;++id)if(contains(rect(id),x,y))return id;
    return 0;
}
void surface_preferences_paint(tiku_surface_t *s,const surface_view_t *v)
{
    if(!v->preferences_page){tiku_ui_button(s,rect(ACTION_PREFS_OPEN),"Desktop / files",0);tiku_ui_button(s,rect(ACTION_PREFS_DEVICE),"Device settings",0);return;}
    if(v->preferences_page==2){
        tiku_text(s,tiku_font_plain(),32,110,"Device settings",TIKU_C_TEXT);
        for(int id=ACTION_PREFS_DEVICE_ROW;id<ACTION_PREFS_DEVICE_ROW+6;++id)tiku_ui_button(s,rect(id),device_names[id-ACTION_PREFS_DEVICE_ROW],0);
        tiku_text(s,tiku_font_plain(),32,360,"Availability and write access depend on this firmware.",TIKU_C_TEXT);
        tiku_ui_button(s,rect(ACTION_PREFS_TOUCH),"Touch settings",0);return;
    }
    if(v->preferences_page==3){
        unsigned selected=v->preferences_device<6?v->preferences_device:0;
        tiku_text(s,tiku_font_bold(),32,116,device_names[selected],TIKU_C_TEXT);
        tiku_text(s,tiku_font_plain(),32,155,device_help[selected],TIKU_C_TEXT);
        tiku_ui_sunken(s,(tiku_rect_t){32,180,736,160},TIKU_C_DOC);
        const char *value=v->inspector_loaded?v->inspector_value:"Choose Read to obtain the current value.";
        size_t n=strlen(value);if(n>255)n=255;char line[256];memcpy(line,value,n);line[n]=0;
        for(size_t i=0;i<n;++i)if((unsigned char)line[i]<32)line[i]=' ';
        char fitted[256];tiku_text_elide(0,line,712,fitted,sizeof fitted);
        tiku_text(s,tiku_font_plain(),44,215,fitted,TIKU_C_TEXT);
        tiku_text(s,tiku_font_plain(),44,300,(v->inspector_node.flags&SURFACE_NODE_EDIT)?"Read, edit, then Apply to submit the reviewed value.":"This setting is read-only or unavailable in this firmware.",TIKU_C_TEXT);
        tiku_ui_button(s,rect(ACTION_INSPECTOR_READ),"Read",v->inspector_phase?TIKU_S_DISABLED:0);
        tiku_ui_button(s,rect(ACTION_INSPECTOR_EDIT),"Edit",v->inspector_phase||!v->inspector_loaded||v->inspector_error||!(v->inspector_node.flags&SURFACE_NODE_EDIT)?TIKU_S_DISABLED:0);
        tiku_ui_button(s,rect(ACTION_INSPECTOR_COPY),"Copy",v->inspector_phase||!v->inspector_loaded?TIKU_S_DISABLED:0);
        tiku_ui_button(s,rect(ACTION_PREFS_DEVICE_LIST),"Device settings",0);return;
    }
    tiku_text(s,tiku_font_plain(),32,105,"Desktop background and preferred document applications",TIKU_C_TEXT);
    unsigned disabled=editable(v->preferences_status)&&v->settings_status!=SURFACE_SETTINGS_PENDING?0:TIKU_S_DISABLED;
    static const char *names[]={"Blue","Teal","Gray","Wine","Olive","Black"};
    for(int i=0;i<6;++i){tiku_rect_t r=rect(ACTION_PREFS_COLOR+i);tiku_ui_button(s,r,names[i],disabled|(v->preferences.background==i?TIKU_S_PRESSED:0));tiku_fill(s,(tiku_rect_t){r.x+190,r.y+12,24,20},colors[i]);}
    static const char *text[]={"Text files: Editor","Text files: TikuHex"};
    static const char *html[]={"HTML: Reader","HTML: Editor","HTML: TikuHex"};
    static const char *markdown[]={"Markdown: Reader","Markdown: Editor","Markdown: TikuHex"};
    tiku_ui_button(s,rect(ACTION_PREFS_TEXT),text[v->preferences.text],disabled);
    tiku_ui_button(s,rect(ACTION_PREFS_HTML),html[v->preferences.html],disabled);
    tiku_ui_button(s,rect(ACTION_PREFS_MARKDOWN),markdown[v->preferences.markdown],disabled);
    tiku_ui_button(s,rect(ACTION_PREFS_TOUCH),"Touch settings",0);
    tiku_ui_button(s,rect(ACTION_PREFS_SAVE),"Save",disabled);
    tiku_ui_button(s,rect(ACTION_PREFS_DEFAULTS),"Defaults",disabled);
}
