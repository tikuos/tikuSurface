/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_editor_search.c - search options, replacements and line navigation through GUI input.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main media_fixture_main
#include "tiku_event.h"
#include "test_media.c"
#undef main
#include "tiku_edit_search.h"
#include "tiku_pool.h"
/** @brief Inject one allocation failure into the Editor's existing pool provider. */
static tiku_allocator_t original_allocator;
static size_t allocation_calls,fail_allocation;
tiku_allocator_t __real_tiku_pool_allocator(tiku_pool_t *);
/** @brief Preserve the pool contract while failing the selected allocation. */
static void *allocate(void *context,size_t bytes,size_t alignment)
{if(++allocation_calls==fail_allocation)return NULL;return original_allocator.ops->allocate(original_allocator.context,bytes,alignment);}
/** @brief Return document allocations to their original pool. */
static void release(void *context,void *memory,size_t bytes,size_t alignment)
{original_allocator.ops->release(original_allocator.context,memory,bytes,alignment);}
/** @brief Interpose the public pool binding used by Editor startup. */
tiku_allocator_t __wrap_tiku_pool_allocator(tiku_pool_t *pool)
{
    static const tiku_allocator_ops_t ops={.allocate=allocate,.release=release};
    original_allocator=__real_tiku_pool_allocator(pool);return (tiku_allocator_t){.ops=&ops};
}
/** @brief Deliver one logical key through the shell queue. */
static void key(surface_t *s,unsigned code,unsigned modifiers)
{tiku_input_event_t e={.type=TIKU_EVENT_KEY_DOWN,.key=code,.modifiers=modifiers};assert(surface_post(s,&e));surface_step(s);}
/** @brief Deliver complete UTF-8 through document commits or field key text. */
static void type(surface_t *s,const char *text)
{
    while(*text){size_t n=strlen(text);if(n>7)n=7;
        while(n&&((unsigned char)text[n]&0xc0)==0x80)--n;
        assert(n);tiku_input_event_t e={.type=s->text_field.active?TIKU_EVENT_KEY_DOWN:TIKU_EVENT_TEXT_INPUT};memcpy(e.text,text,n);
        assert(surface_post(s,&e));surface_step(s);text+=n;}
}
/** @brief Replace a modal draft, including an empty replacement. */
static void apply(surface_t *s,const char *text)
{key(s,'a',TIKU_MOD_CMD);key(s,TIKU_KEY_BACKSPACE,0);type(s,text);key(s,TIKU_KEY_RETURN,0);}
/** @brief Copy the current view for assertions. */
static surface_view_t view(surface_t *s)
{surface_view_t v;surface_view(s,&v);return v;}
/** @brief Compare the admitted program snapshot without moving the caret. */
static void content(surface_t *s,const char *expected)
{
    size_t length=0;const char *text=surface_editor_program(s->active.state,&length);
    assert(length==strlen(expected));if(length)assert(text&&!memcmp(text,expected,length));
}
/** @brief Replace the full draft using normal document selection. */
static void draft(surface_t *s,const char *text)
{key(s,'a',TIKU_MOD_CMD);type(s,text);content(s,text);}
/** @brief Verify complete and clipped painting agree for an application page. */
static void painting(surface_t *s)
{
    static uint32_t whole[800*480],sliced[800*480];surface_view_t v=view(s);
    tiku_surface_t a={.px=whole,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=sliced;
    surface_paint(&a,&v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}assert(!memcmp(whole,sliced,sizeof whole));
}
int main(void)
{
    disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);
    cfg.storage=surface_media_init(&media,bridge);cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_EDITOR_APP));settle(s,&media);
    const char *original="One one stone\none café\r\nlast\rend\n";type(s,original);content(s,original);
    key(s,'f',TIKU_MOD_CMD);apply(s,"one");surface_view_t v=view(s);assert(!v.text_entry&&v.editor_wrapped&&v.editor_grid.selected[0]==7);
    key(s,'g',TIKU_MOD_CMD);v=view(s);assert(v.editor_grid.selected[0]==112&&!v.editor_wrapped);
    key(s,'g',TIKU_MOD_CMD);v=view(s);assert(v.editor_grid.selected[0]==7168);
    key(s,'g',TIKU_MOD_CMD|TIKU_MOD_SHIFT);v=view(s);assert(v.editor_grid.selected[0]==112);
    file_tap(s,&media,600,140);v=view(s);assert(v.editor_tools);painting(s);file_capture(s,&host,"editor-search-tools");
    type(s,"hidden");key(s,'v',TIKU_MOD_CMD);content(s,original);
    file_tap(s,&media,580,310);v=view(s);assert(v.editor_search_flags==TIKU_EDIT_WHOLE_WORD);
    key(s,'g',TIKU_MOD_CMD);v=view(s);assert(v.editor_grid.selected[1]==7&&v.editor_grid.selected[0]==0);
    file_tap(s,&media,200,310);v=view(s);assert(v.editor_search_flags==(TIKU_EDIT_WHOLE_WORD|TIKU_EDIT_MATCH_CASE));
    key(s,TIKU_KEY_ESCAPE,0);assert(!view(s).editor_tools);key(s,'l',TIKU_MOD_CMD);apply(s,"1");v=view(s);assert(!v.text_entry&&v.editor_grid.caret_x==0&&v.editor_grid.caret_y==0);
    key(s,'f',TIKU_MOD_CMD);apply(s,"one");v=view(s);assert(v.editor_grid.selected[0]==112);
    key(s,'h',TIKU_MOD_CMD);apply(s,"ONE");v=view(s);assert(!v.text_entry&&v.editor_replaced==1);content(s,"One ONE stone\none café\r\nlast\rend\n");
    key(s,'z',TIKU_MOD_CMD);content(s,original);key(s,'z',TIKU_MOD_CMD|TIKU_MOD_SHIFT);content(s,"One ONE stone\none café\r\nlast\rend\n");
    key(s,'h',TIKU_MOD_CMD);apply(s,"bad");v=view(s);assert(v.text_entry&&v.text_error&&v.editor_error==TIKU_EDIT_NOTHING);key(s,TIKU_KEY_ESCAPE,0);content(s,"One ONE stone\none café\r\nlast\rend\n");
    key(s,'h',TIKU_MOD_CMD|TIKU_MOD_SHIFT);apply(s,"");v=view(s);assert(!v.text_entry&&v.editor_replaced==1);content(s,"One ONE stone\n café\r\nlast\rend\n");
    key(s,'z',TIKU_MOD_CMD);content(s,"One ONE stone\none café\r\nlast\rend\n");
    key(s,'l',TIKU_MOD_CMD);apply(s,"3");v=view(s);assert(!v.text_entry&&v.editor_grid.caret_y==2&&v.editor_grid.caret_x==0);
    key(s,'l',TIKU_MOD_CMD);apply(s,"0");assert(view(s).text_entry&&view(s).text_error);apply(s,"9999999999999999999999999999999");assert(view(s).text_entry);apply(s,"6");assert(view(s).text_entry);apply(s,"2x");assert(view(s).text_entry);key(s,TIKU_KEY_ESCAPE,0);v=view(s);assert(v.editor_grid.caret_y==2&&v.editor_grid.caret_x==0);
    key(s,'l',TIKU_MOD_CMD);apply(s,"5");v=view(s);assert(!v.text_entry&&v.editor_grid.caret_y==4&&v.editor_grid.caret_x==0);
    key(s,'f',TIKU_MOD_CMD);apply(s,"");assert(view(s).text_entry&&view(s).text_error);key(s,TIKU_KEY_ESCAPE,0);assert(!strcmp(view(s).editor_query,"one"));
    key(s,'f',TIKU_MOD_CMD);apply(s,"absent");v=view(s);assert(!v.text_entry&&v.editor_error==TIKU_EDIT_NOTHING);content(s,"One ONE stone\none café\r\nlast\rend\n");
    file_tap(s,&media,600,140);file_tap(s,&media,200,310);file_tap(s,&media,580,310);v=view(s);assert(!v.editor_search_flags);
    file_tap(s,&media,150,30);assert(!view(s).editor_tools&&view(s).foreground==SURFACE_EDITOR_APP);
    draft(s,"café café\nCAFÉ");key(s,'f',TIKU_MOD_CMD);apply(s,"café");key(s,'h',TIKU_MOD_CMD|TIKU_MOD_SHIFT);apply(s,"tea");v=view(s);assert(v.editor_replaced==2);content(s,"tea tea\nCAFÉ");
    key(s,'z',TIKU_MOD_CMD);content(s,"café café\nCAFÉ");key(s,'z',TIKU_MOD_CMD|TIKU_MOD_SHIFT);content(s,"tea tea\nCAFÉ");
    key(s,'f',TIKU_MOD_CMD);apply(s,"tea");key(s,'h',TIKU_MOD_CMD);type(s,"cancel");key(s,TIKU_KEY_ESCAPE,0);content(s,"tea tea\nCAFÉ");
    assert(surface_launch(s,SURFACE_CALCULATOR));surface_step(s);assert(surface_launch(s,SURFACE_EDITOR_APP));surface_step(s);assert(!strcmp(view(s).editor_query,"tea"));
    file_tap(s,&media,190,338);apply(s,"/search.txt");settle(s,&media);v=view(s);assert(!v.editor_dirty&&!v.editor_busy);
    tiku_mfs_object_t info;uint64_t handle;char bytes[64];assert(!tiku_mfs_stat(&fs,"search.txt",&info)&&info.length==13);assert(!tiku_mfs_open_read(&fs,"search.txt",&handle));assert(tiku_mfs_read_at(&fs,handle,0,bytes,sizeof bytes)==13&&!memcmp(bytes,"tea tea\nCAFÉ",13));assert(!tiku_mfs_close_read(&fs,handle));
    painting(s);file_capture(s,&host,"editor-search-document");
    draft(s,"one one one");key(s,'f',TIKU_MOD_CMD);apply(s,"one");unsigned failure;
    for(failure=1;failure<200;++failure){
        key(s,'h',TIKU_MOD_CMD|TIKU_MOD_SHIFT);allocation_calls=0;fail_allocation=failure;apply(s,"two");fail_allocation=0;v=view(s);
        if(!v.text_entry){assert(v.editor_replaced==3);content(s,"two two two");break;}
        assert(v.text_error&&v.editor_error==TIKU_EDIT_NO_MEMORY);content(s,"one one one");key(s,TIKU_KEY_ESCAPE,0);
    }
    assert(failure>3&&failure<200);printf("Replace-all allocation failures preserved the draft at %u positions\n",failure-1);
    key(s,'z',TIKU_MOD_CMD);content(s,"one one one");
    static char full[8193];memset(full,'a',8192);full[8192]=0;draft(s,full);
    key(s,'f',TIKU_MOD_CMD);apply(s,"a");key(s,'h',TIKU_MOD_CMD|TIKU_MOD_SHIFT);apply(s,"aa");v=view(s);assert(v.text_entry&&v.text_error&&v.editor_error);key(s,TIKU_KEY_ESCAPE,0);content(s,full);
    file_tap(s,&media,600,140);file_tap(s,&media,200,365);assert(!view(s).editor_tools);key(s,'c',TIKU_MOD_CMD);assert(view(s).editor_error==TIKU_EDIT_SHORT);
    surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(&media);}assert(!s&&!host.memory.live&&!store.count&&!store.running);
    free(bridge);free(disk);puts("Editor search: options, repeat directions, UTF-8, replace/undo, empty delete, limits, line navigation, cancellation, retention and media save passed");return 0;
}
