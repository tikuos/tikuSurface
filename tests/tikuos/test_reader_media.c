/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_reader_media.c - Tracker document handoff through queued native media.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main media_fixture_main
#include "tiku_event.h"
#include "test_media.c"
#undef main
/** @brief Create one completed document in private native media. */
static void document(const char *name,const char *text,size_t bytes)
{
    uint64_t id;assert(!tiku_mfs_begin(&fs,name,TIKU_MFS_STREAM,0,NULL,&id));
    assert(!tiku_mfs_append(&fs,text,bytes));assert(!tiku_mfs_finalize(&fs));
}
/** @brief Dispatch a modal dismissal through the keyboard route. */
static void key(surface_t *s,unsigned k,unsigned modifiers)
{tiku_input_event_t e={.type=TIKU_EVENT_KEY_DOWN,.key=k,.modifiers=modifiers};assert(surface_post(s,&e));surface_step(s);}
int main(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};
    reset();document("guide.md","# Local guide\n\nA **document** from native media.\n",49);
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    cfg.storage=surface_media_init(&media,bridge);cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ};
    surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_BROWSER));settle(s,&media);
    file_tap(s,&media,70,130);settle(s,&media);file_tap(s,&media,470,360);settle(s,&media);
    surface_view_t v;surface_view(s,&v);assert(v.foreground==SURFACE_READER_APP&&!v.reader_error&&v.reader_page&&!strcmp(v.reader_path,"/guide.md"));
    const void *page=v.reader_page;file_tap(s,&media,70,410);surface_view(s,&v);assert(v.text_entry);
    key(s,'a',TIKU_MOD_CMD);for(const char *p="/guide.pdf";*p;++p)key(s,(unsigned char)*p,0);key(s,TIKU_KEY_RETURN,0);surface_view(s,&v);assert(v.text_entry&&v.text_error);key(s,TIKU_KEY_ESCAPE,0);
    document("bad.txt","a\0b",3);assert(surface_reader_open(s->active.state,"/bad.txt",0,0));settle(s,&media);surface_view(s,&v);
    assert(v.reader_error&&v.reader_page==page&&!strcmp(v.reader_path,"/guide.md"));
    document("plain.txt","<literal> & text",16);assert(surface_reader_open(s->active.state,"/plain.txt",0,0));settle(s,&media);surface_view(s,&v);
    assert(!v.reader_error&&!strcmp(v.reader_path,"/plain.txt"));
    assert(surface_reader_open(s->active.state,"/guide.md",0,0));surface_step(s);close_refusals=3;
    assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);settle(s,&media);
    surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(&media);}
    assert(!s&&!host.memory.live&&!store.count&&!store.running);free(bridge);free(disk);
    puts("Reader native media: Tracker handoff, Markdown, touch paths, invalid input preservation, text and queued close passed");return 0;
}
