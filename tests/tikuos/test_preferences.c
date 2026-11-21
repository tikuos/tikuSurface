/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_preferences.c - independent records, explicit save and compatible document defaults.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main media_fixture_main
#include "test_media.c"
#undef main
static surface_preferences_t saved,requested;
static unsigned state=SURFACE_SETTINGS_DEFAULT,saves;
/** @brief Copy applied preferences while retaining independent request state. */
static unsigned read_preferences(void *p,surface_preferences_t *out){*out=saved;return state;}
/** @brief Copy a pending update without changing applied preferences. */
static int save_preferences(void *p,const surface_preferences_t *value)
{assert(state!=SURFACE_SETTINGS_PENDING);requested=*value;state=SURFACE_SETTINGS_PENDING;++saves;return 1;}
/** @brief Read a copied presentation. */
static surface_view_t view(surface_t *s){surface_view_t v;surface_view(s,&v);return v;}
/** @brief Create a real media document for routed opening. */
static void document(const char *name,const char *text)
{uint64_t id;assert(!tiku_mfs_begin(&fs,name,TIKU_MFS_STREAM,0,NULL,&id));assert(!tiku_mfs_append(&fs,text,strlen(text)));assert(!tiku_mfs_finalize(&fs));}
/** @brief Seal schema changes with an independent CRC implementation. */
static void seal(unsigned char *bytes)
{
    uint32_t crc=UINT32_MAX;for(unsigned i=0;i<28;++i){crc^=bytes[i];for(unsigned j=0;j<8;++j)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
    crc=~crc;for(unsigned i=0;i<4;++i)bytes[28+i]=(unsigned char)(crc>>(i*8));
}
/** @brief Verify record validation leaves output and calibration bytes unchanged. */
static void records(void)
{
    surface_preferences_t p={5,1,2,1},out=p;unsigned char bytes[32],copy[32];
    assert(surface_preferences_encode(&p,bytes)&&!memcmp(bytes,"TSDP\1\40\5\1\2\1",10));
    assert(surface_preferences_decode(bytes,32,&out)==SURFACE_SETTINGS_SAVED&&!memcmp(&p,&out,sizeof p));
    for(unsigned i=0;i<32;++i){memcpy(copy,bytes,32);copy[i]^=1;assert(surface_preferences_decode(copy,32,&out)==SURFACE_SETTINGS_CORRUPT&&!memcmp(&p,&out,sizeof p));assert(surface_preferences_decode(bytes,i,&out)==SURFACE_SETTINGS_CORRUPT);}
    memcpy(copy,bytes,32);copy[4]=2;seal(copy);assert(surface_preferences_decode(copy,32,&out)==SURFACE_SETTINGS_INCOMPATIBLE);
    memcpy(copy,bytes,32);copy[10]=1;seal(copy);assert(surface_preferences_decode(copy,32,&out)==SURFACE_SETTINGS_INCOMPATIBLE);
    for(unsigned i=6;i<10;++i){memcpy(copy,bytes,32);copy[i]=255;seal(copy);assert(surface_preferences_decode(copy,32,&out)==SURFACE_SETTINGS_CORRUPT);}
    assert(!surface_preferences_encode(&(surface_preferences_t){6,0,0,0},copy));assert(!surface_preferences_encode(&(surface_preferences_t){0,2,0,0},copy));
    surface_touch_map_t cal={0,799,0,479,0,0,0},got=cal;assert(surface_settings_encode(&cal,copy));assert(surface_preferences_decode(copy,32,&out)==SURFACE_SETTINGS_CORRUPT);assert(surface_settings_decode(bytes,32,&got)==SURFACE_SETTINGS_CORRUPT);
}
/** @brief Compare clipped painting and capture the preferences page. */
static void painting(surface_t *s,surface_host_t *host)
{
    static uint32_t whole[800*480],sliced[800*480];surface_view_t v=view(s);
    tiku_surface_t a={.px=whole,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=sliced;
    surface_paint(&a,&v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,&v);}assert(!memcmp(whole,sliced,sizeof whole));file_capture(s,host,"desktop-preferences");
}
int main(void)
{
    records();disk=calloc(1,DISK_BYTES);assert(disk);block=(tiku_block_t){NULL,DISK_BYTES/512,read_disk,write_disk,sync_disk};reset();document("a.html","<h1>Hello</h1>");document("b.unknown","opaque");
    surface_host_t host;surface_config_t cfg=surface_host_config(&host);surface_media_t media;
    void *bridge=calloc(1,surface_os_media_size());assert(bridge);surface_os_media_init(bridge);cfg.storage=surface_media_init(&media,bridge);
    cfg.preferences=(surface_preferences_provider_t){NULL,read_preferences,save_preferences};cfg.caller=(tiku_resource_caller_t){.principal="surface-local",.authenticated=1,.scopes=TIKU_RESOURCE_READ|TIKU_RESOURCE_WRITE};
    surface_t *s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_SETTINGS));settle(s,&media);file_tap(s,&media,600,110);assert(view(s).preferences_page);
    file_tap(s,&media,300,140);file_tap(s,&media,300,258);file_tap(s,&media,300,310);file_tap(s,&media,300,310);file_tap(s,&media,300,360);
    surface_view_t v=view(s);assert(v.preferences.background==1&&v.preferences.text==1&&v.preferences.html==2&&v.preferences.markdown==1&&v.preferences_dirty&&!saves&&v.desktop_color==0x336698);
    assert(surface_preferences_handler(s,"/a.html",SURFACE_READER_APP)==SURFACE_READER_APP);painting(s,&host);
    assert(surface_close(s)==SURFACE_CLOSE_CONFIRM&&surface_resolve_close(s,0));file_tap(s,&media,360,415);assert(saves==1&&view(s).preferences_status==SURFACE_SETTINGS_PENDING&&surface_close(s)==SURFACE_CLOSE_BUSY);
    file_tap(s,&media,550,140);assert(view(s).preferences.background==1);assert(surface_launch(s,SURFACE_HOME));settle(s,&media);
    saved=requested;state=SURFACE_SETTINGS_SAVED;surface_step(s);assert(view(s).desktop_color==0x287878);
    assert(surface_preferences_handler(s,"/a.txt",SURFACE_EDITOR_APP)==SURFACE_HEX_APP&&surface_preferences_handler(s,"/a.html",SURFACE_READER_APP)==SURFACE_HEX_APP&&surface_preferences_handler(s,"/a.md",SURFACE_READER_APP)==SURFACE_EDITOR_APP);
    assert(!surface_preferences_handler(s,"/unknown",SURFACE_HOME));
    assert(surface_launch(s,SURFACE_BROWSER));settle(s,&media);file_tap(s,&media,70,130);settle(s,&media);assert(view(s).document_default==SURFACE_HEX_APP);file_tap(s,&media,500,128);settle(s,&media);assert(view(s).foreground==SURFACE_HEX_APP&&!strcmp(view(s).byte_path,"/a.html"));
    assert(surface_launch(s,SURFACE_BROWSER));settle(s,&media);file_tap(s,&media,160,30);settle(s,&media);file_tap(s,&media,70,174);settle(s,&media);file_tap(s,&media,500,128);assert(view(s).document_menu&&!view(s).document_default);file_tap(s,&media,600,390);
    assert(surface_launch(s,SURFACE_SETTINGS));settle(s,&media);file_tap(s,&media,580,415);assert(view(s).preferences_dirty);file_tap(s,&media,360,415);state=SURFACE_SETTINGS_FAILED;surface_step(s);assert(view(s).preferences_status==SURFACE_SETTINGS_FAILED&&view(s).preferences_dirty&&view(s).desktop_color==0x287878);
    assert(surface_close(s)==SURFACE_CLOSE_CONFIRM&&surface_resolve_close(s,1));settle(s,&media);assert(surface_launch(s,SURFACE_SETTINGS));settle(s,&media);assert(view(s).preferences.background==1);
    surface_stop(s);for(unsigned i=0;i<30000&&!surface_destroy(&s);++i){surface_step(s);surface_media_poll(&media);}assert(!s&&!host.memory.live);
    state=SURFACE_SETTINGS_SAVED;s=surface_create(&cfg);assert(s&&view(s).desktop_color==0x287878);assert(surface_launch(s,SURFACE_SETTINGS));settle(s,&media);file_tap(s,&media,600,110);file_tap(s,&media,580,415);file_tap(s,&media,360,415);saved=requested;state=SURFACE_SETTINGS_SAVED;surface_step(s);assert(view(s).desktop_color==0x336698&&!view(s).preferences_dirty);
    surface_stop(s);assert(surface_destroy(&s));assert(!host.memory.live);
    state=SURFACE_SETTINGS_INCOMPATIBLE;s=surface_create(&cfg);assert(s&&surface_launch(s,SURFACE_SETTINGS));settle(s,&media);file_tap(s,&media,600,110);unsigned before=saves;file_tap(s,&media,300,140);file_tap(s,&media,360,415);assert(saves==before&&!view(s).preferences_dirty);surface_stop(s);assert(surface_destroy(&s));
    free(bridge);free(disk);puts("Preferences: independent CRC, draft/save/discard, restart, background, compatible routes, unknown types, failure and clipped painting passed");return 0;
}
