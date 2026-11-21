/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_hex_keyboard.c - explicit byte-edit modes and retained keyboard transactions.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main media_fixture_main
#include "test_media.c"
#undef main
#include "tiku_event.h"
/** @brief Deliver a logical key with modifiers through normal application dispatch. */
static void key(surface_t *s,unsigned k,unsigned modifiers)
{tiku_input_event_t e={.type=TIKU_EVENT_KEY_DOWN,.key=k,.modifiers=modifiers};assert(surface_post(s,&e));surface_step(s);}
/** @brief Copy a presentation after currently queued storage work retires. */
static surface_view_t view(surface_t *s,surface_media_t *m)
{settle(s,m);surface_view_t v;surface_view(s,&v);return v;}
/** @brief Create a complete private binary file. */
static void document(const char *name,const void *bytes,size_t n)
{uint64_t id;assert(!tiku_mfs_begin(&fs,name,TIKU_MFS_STREAM,0,NULL,&id));if(n)assert(!tiku_mfs_append(&fs,bytes,n));assert(!tiku_mfs_finalize(&fs));}
/** @brief Check two-row updates produce the same pending-byte or mode page. */
static void painting(surface_t *s)
{
    static uint32_t whole[800*480],sliced[800*480];surface_view_t v;surface_view(s,&v);
    tiku_surface_t a={.px=whole,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=sliced;
    surface_paint(&a,&v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}assert(!memcmp(whole,sliced,sizeof whole));
}
int main(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();
    unsigned char bytes[257];for(unsigned i=0;i<257;++i)bytes[i]=(unsigned char)i;document("binary",bytes,sizeof bytes);document("empty",NULL,0);
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    cfg.storage=surface_media_init(&media,bridge);cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_HEX_APP));settle(s,&media);assert(surface_hex_open(s->active.state,"/binary",0,0));surface_view_t v=view(s,&media);
    assert(v.byte_size==257&&!v.byte_editing&&v.byte_high==-1);key(s,'f',0);v=view(s,&media);assert(!v.byte_dirty&&v.byte_high==-1);
    key(s,TIKU_KEY_END,0);v=view(s,&media);assert(v.byte_cursor==256&&v.byte_first==256);key(s,TIKU_KEY_DOWN,0);assert(view(s,&media).byte_cursor==256);
    key(s,TIKU_KEY_PAGE_UP,0);v=view(s,&media);assert(v.byte_cursor==128&&v.byte_first==128);key(s,TIKU_KEY_HOME,0);key(s,TIKU_KEY_RIGHT,0);key(s,TIKU_KEY_UP,0);assert(view(s,&media).byte_cursor==0);
    file_tap(s,&media,700,390);v=view(s,&media);assert(v.byte_input_page);painting(s);file_capture(s,&host,"hex-keyboard-modes");file_tap(s,&media,550,215);
    v=view(s,&media);assert(v.byte_editing&&!v.byte_ascii&&!v.byte_input_page);key(s,'f',0);v=view(s,&media);assert(v.byte_high==15&&!v.byte_dirty&&v.byte_cursor==0&&v.byte_cells[0]==0);painting(s);file_capture(s,&host,"hex-pending-digit");
    key(s,'0',0);v=view(s,&media);assert(v.byte_high==-1&&v.byte_dirty&&v.byte_cells[0]==0xf0&&v.byte_cursor==1);
    key(s,'z',TIKU_MOD_CMD);v=view(s,&media);assert(!v.byte_dirty&&!v.byte_cells[0]);key(s,'z',TIKU_MOD_CMD|TIKU_MOD_SHIFT);v=view(s,&media);assert(v.byte_dirty&&v.byte_cells[0]==0xf0);key(s,'c',TIKU_MOD_CMD);assert(!strcmp(s->clipboard,"F0"));
    key(s,TIKU_KEY_RIGHT,0);key(s,'a',0);key(s,TIKU_KEY_LEFT,0);v=view(s,&media);assert(v.byte_high==-1&&v.byte_cursor==0&&v.byte_cells[1]==1);
    key(s,'a',0);assert(view(s,&media).byte_high==10);assert(surface_launch(s,SURFACE_CALCULATOR));surface_step(s);assert(surface_launch(s,SURFACE_HEX_APP));v=view(s,&media);assert(v.byte_high==-1&&v.byte_cells[0]==0xf0);
    key(s,'b',0);tiku_input_event_t lost={.type=TIKU_EVENT_DEACTIVATED};assert(surface_post(s,&lost));surface_step(s);v=view(s,&media);assert(v.byte_high==-1&&v.byte_cells[0]==0xf0);
    key(s,'s',TIKU_MOD_CMD);v=view(s,&media);assert(!v.byte_dirty&&!v.byte_busy);
    file_tap(s,&media,700,390);file_tap(s,&media,180,280);v=view(s,&media);assert(v.byte_ascii&&v.byte_editing);key(s,TIKU_KEY_HOME,0);key(s,'Q',0);v=view(s,&media);assert(v.byte_cells[0]=='Q'&&v.byte_cursor==1&&v.byte_dirty);
    tiku_input_event_t utf8={.type=TIKU_EVENT_KEY_DOWN,.text="é"};assert(surface_post(s,&utf8));surface_step(s);v=view(s,&media);assert(v.byte_cursor==1&&v.byte_cells[1]==1);
    key(s,'s',TIKU_MOD_CMD|TIKU_MOD_SHIFT);surface_view(s,&v);assert(v.text_entry&&v.byte_dialog==2);file_name(s,"/keys");v=view(s,&media);assert(!v.byte_dirty&&!strcmp(v.byte_path,"/keys"));
    key(s,'g',TIKU_MOD_CMD);file_name(s,"128");v=view(s,&media);assert(v.byte_cursor==128&&v.byte_first==128);key(s,'f',TIKU_MOD_CMD);file_name(s,"51");for(unsigned i=0;i<5;++i)surface_step(s);v=view(s,&media);assert(v.byte_cursor==0&&v.byte_search==2);
    file_tap(s,&media,700,390);key(s,TIKU_KEY_ESCAPE,0);assert(!view(s,&media).byte_input_page&&view(s,&media).foreground==SURFACE_HEX_APP);
    key(s,'o',TIKU_MOD_CMD);file_name(s,"/empty");v=view(s,&media);assert(v.byte_loaded&&!v.byte_size&&!v.byte_dirty);key(s,'a',0);assert(!view(s,&media).byte_dirty);
    surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(&media);}assert(!s&&!host.memory.live&&!store.count&&!store.running);
    free(bridge);free(disk);puts("Hex keyboard: explicit modes, staged digits, one-byte undo, navigation, focus cancellation, shortcuts and saved bytes passed");return 0;
}
