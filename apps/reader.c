/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * reader.c - local documents and help through the shared document engine.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "surface_internal.h"
#include "tiku_doc.h"
#include "tiku_html_text.h"
#include "tiku_text_search.h"
#include "tiku_text_layout.h"
#include "tiku_word_model.h"
#include "tiku_doc_paint.h"
#include "tiku_markdown.h"
#include "tiku_font.h"
#include "tiku_ui.h"
#include <string.h>
#define READER_BYTES 8192
/** @brief A published page remains immutable while a frame holds a reference. */
typedef struct {
    tiku_owned_buffer_t self;unsigned references;tiku_doc_t doc;
    char text[TIKU_DOC_TEXT_MAX+TIKU_DOC_BLOCKS_MAX+3];
    int starts[TIKU_DOC_RUNS_MAX],length;
    unsigned heading_count;uint16_t headings[TIKU_DOC_BLOCKS_MAX];
} reader_page_t;
/** @brief One document, pending replacement and eight navigation positions. */
struct surface_reader {
    tiku_owned_buffer_t self,html,scratch;
    reader_page_t *page,*pending;
    char source[READER_BYTES+1],path[65],target[65],parent[65],anchor[64];
    struct {char path[65];int scroll;} history[8];
    unsigned history_count;
    size_t bytes;
    uint64_t epoch,node,after;
    int job,submitted,scroll,restore,back,help,error;
    char query[256];
    tiku_text_search_t search;
    int find_dialog,searching,match,match_length,matches,ordinal,reveal;
    unsigned tools,heading_first,heading_scan;int copied;
};
static const char help[]=
    "<h1 id='top'>TikuSurface Help</h1><p>A local TikuDesktop adaptation on TikuOS.</p>"
    "<p><a href='#tracker'>Files</a> | <a href='#editor'>Editing</a> | <a href='#device'>Device tools</a></p>"
    "<h2 id='tracker'>Files and documents</h2><p>Tracker opens files, shows information and supports New file, Copy to, Rename or Move, and confirmed permanent Delete. File jobs can be cancelled.</p>"
#if SURFACE_DIRECTORIES
    "<p>New folder creates a persistent folder on a directory-enabled volume. Select chooses a folder for Copy, Rename or Delete. Rename and Delete include all descendants in one transaction. Delete is permanent. Existing implicit folders support these actions. Existing volumes are not reformatted. If New folder reports unsupported, Enable folders asks before preserving the files and upgrading the volume. Older firmware cannot open an upgraded volume.</p>"
#else
    "<p>New folders are not available in this build.</p>"
#endif
    "<p>Choose Read document for HTML, Markdown and text, or Edit text to change a document. Reader accepts files through 8 KiB. Links to local documents and anchors work; network links and image decoding are unavailable.</p>"
#if SURFACE_HEX
    "<h2>TikuHex</h2><p>Choose Edit bytes in Tracker to inspect and change a binary file through 8 KiB. Touch a byte, then Edit byte. Undo, Redo, Go to and Find bytes retain the desktop byte model. Save checks the opened version; Save as needs a new filename. Save or close the current draft before opening another file. Storage failures retain edits.</p>"
#endif
    "<h2 id='editor'>Editor and BASIC</h2><p>Editor retains a UTF-8 document through 8 KiB. Use selection, Copy, Cut, Paste, Undo, Redo and Find. Save replaces the opened file; Save as creates a new file. Keep or Discard resolves unsaved Close.</p>"
    "<p>Run BASIC submits the current draft to the local interpreter. Terminal displays its output; Stop cancels the owned job. A busy interpreter refuses another run.</p>"
    "<h2 id='device'>Device tools</h2><p>Terminal sends commands to the local TikuOS shell. Device Monitor shows aggregate readings and process details. Pause stops automatic sampling; Refresh requests one cycle.</p>"
    "<p>VFS Inspector navigates metadata without reading values. Read once invokes the node handler; consuming and effectful reads are labelled. Reviewed setting writes recheck the value and permission.</p>"
    "<h2>Navigation</h2><p>Applications switches retained tasks. Home backgrounds the current task. Close retires it. Reader Back returns to the prior document or scroll position; Up and Down scroll a page. Find highlights literal text; Next and Previous wrap between matches. Tools lists headings and copies a selected match to the desktop clipboard.</p>"
    "<p><a href='#top'>Return to top</a></p>";
void surface_reader_hold(const void *page)
{if(page)++((reader_page_t *)page)->references;}
void surface_reader_drop(const void *page)
{
    reader_page_t *p=(reader_page_t *)page;if(!p||--p->references)return;
    tiku_owned_buffer_t memory=p->self;tiku_buffer_dispose(&memory);
}
/** @brief Allocate zeroed storage through the product's explicit allocator. */
static int buffer(surface_app_state_t *s,tiku_owned_buffer_t *out,size_t bytes)
{return tiku_buffer_init(out,s->shell->config.allocator,_Alignof(max_align_t))&&tiku_buffer_resize(out,1,bytes,1);}
/** @brief Release unpublished parsing state without affecting the current page. */
static void temporary(struct surface_reader *r)
{
    surface_reader_drop(r->pending);r->pending=NULL;tiku_buffer_dispose(&r->html);tiku_buffer_dispose(&r->scratch);
}
/** @brief Refuse one pending open and retain the current document and history. */
static void failed(surface_app_state_t *s,tiku_resource_error_t error)
{
    struct surface_reader *r=s->reader;temporary(r);r->job=0;r->error=1;s->status=error;s->shell->dirty=1;
}
/** @brief Accept a canonical absolute local path within the media namespace bound. */
static int path_ok(const char *path)
{
    size_t n=strlen(path);if(n<2||n>64||path[0]!='/'||path[n-1]=='/')return 0;
    const char *part=path+1;
    for(size_t i=1;i<=n;++i){
        if(path[i]&&((unsigned char)path[i]<32||(unsigned char)path[i]==127||path[i]=='\\'))return 0;
        if(!path[i]||path[i]=='/'){size_t m=(size_t)(path+i-part);
            if(!m||(m==1&&part[0]=='.')||(m==2&&part[0]=='.'&&part[1]=='.'))return 0;
            part=path+i+1;}
    }
    return 1;
}
/** @brief Select the supported local source formats by filename suffix. */
static int format(const char *path)
{
    const char *ext=strrchr(path,'.');if(!ext)return 0;
    char suffix[10];size_t n=strlen(ext);if(n>=sizeof suffix)return 0;
    for(size_t i=0;i<=n;++i)suffix[i]=ext[i]>='A'&&ext[i]<='Z'?(char)(ext[i]+'a'-'A'):ext[i];
    if(!strcmp(suffix,".html")||!strcmp(suffix,".htm"))return 1;
    if(!strcmp(suffix,".md")||!strcmp(suffix,".markdown"))return 2;
    if(!strcmp(suffix,".txt")||!strcmp(suffix,".bas")||!strcmp(suffix,".log"))return 3;
    return 0;
}
/** @brief Queue the built-in help document through the same parser lifecycle. */
static void help_open(surface_app_state_t *s)
{
    struct surface_reader *r=s->reader;if(r->job||r->submitted)return;
    r->tools=0;strcpy(r->target,"Help");memcpy(r->source,help,sizeof help);r->bytes=sizeof help-1;r->help=1;r->back=0;r->restore=0;r->anchor[0]=0;r->job=4;r->error=0;s->status=TIKU_RESOURCE_PENDING;s->shell->dirty=1;
}
int surface_reader_start(surface_app_state_t *s)
{
    tiku_owned_buffer_t memory;if(!buffer(s,&memory,sizeof(struct surface_reader)))return 0;
    s->reader=memory.data;s->reader->self=memory;s->reader->match=-1;
    s->store=tiku_paged_storage_create(s->shell->config.allocator,s->shell->config.storage,&s->shell->config.caller,16,1024);
    if(!s->store)return 0;
    if(!s->shell->open_path[0])help_open(s);
    return 1;
}
#if SURFACE_DOCUMENTS
int surface_reader_busy(const surface_app_state_t *s)
{return s&&s->reader&&(s->reader->job||s->reader->submitted||s->reader->searching);}
#endif
void surface_reader_dispose(surface_app_state_t *s)
{
    struct surface_reader *r=s->reader;if(!r)return;
    tiku_text_search_cancel(&r->search);temporary(r);surface_reader_drop(r->page);tiku_owned_buffer_t memory=r->self;s->reader=NULL;tiku_buffer_dispose(&memory);
}
int surface_reader_open(surface_app_state_t *s,const char *path,uint64_t epoch,uint64_t node)
{
    if(!s||!s->reader||s->stopped||!path)return 0;
    struct surface_reader *r=s->reader;if(r->job||r->submitted||r->searching){s->status=TIKU_RESOURCE_BUSY;s->shell->dirty=1;return 0;}
    if(!path_ok(path)||!format(path)){r->error=1;s->status=TIKU_RESOURCE_INVALID;s->shell->dirty=1;return 0;}
    r->tools=0;strcpy(r->target,path);strcpy(r->parent,path);char *slash=strrchr(r->parent,'/');if(slash==r->parent)slash[1]=0;else *slash=0;
    r->bytes=0;r->epoch=epoch;r->node=node;r->after=0;r->help=r->back=r->restore=0;r->anchor[0]=0;
    r->job=epoch&&node?3:1;r->error=0;s->status=TIKU_RESOURCE_PENDING;s->shell->dirty=1;return 1;
}
/** @brief Copy the current navigation position when a replacement publishes. */
static void remember(struct surface_reader *r)
{
    if(!r->page)return;
    if(r->history_count==8){memmove(r->history,r->history+1,7*sizeof *r->history);--r->history_count;}
    strcpy(r->history[r->history_count].path,r->path);r->history[r->history_count++].scroll=r->scroll;
}
/** @brief Begin one queued read or directory page while its lease retains the app. */
static void submit(surface_app_state_t *s)
{
    struct surface_reader *r=s->reader;if(!tiku_app_lease_begin(s->owner,&s->lease)){failed(s,TIKU_RESOURCE_LIMIT);return;}
    if(r->job==1)s->status=tiku_paged_storage_list(s->store,"/",0,0);
    else if(r->job==2)s->status=tiku_paged_storage_list(s->store,r->parent,r->epoch,r->after);
    else s->status=tiku_paged_storage_read(s->store,r->target,r->epoch,r->node,r->bytes);
    r->submitted=tiku_paged_storage_pending(s->store);
    if(!r->submitted){tiku_app_lease_release(&s->lease);failed(s,s->status);}
}
/** @brief Advance shared search and reveal geometry without reading media. */
static void search_poll(surface_app_state_t *s)
{
    struct surface_reader *r=s->reader;
    if(r->searching==1){
        int rc=tiku_text_search_step(&r->search,64);if(rc>0)return;
        tiku_text_search_result_t found;
        if(rc<0||!tiku_text_search_result(&r->search,&found)){r->searching=0;return;}
        r->match=found.offset==SIZE_MAX?-1:(int)found.offset;r->matches=(int)found.count;r->ordinal=(int)found.ordinal;
        r->searching=r->match>=0?2:0;r->reveal=0;s->status=TIKU_RESOURCE_OK;s->shell->dirty=1;return;
    }
    const reader_page_t *p=r->page;
    for(unsigned work=0;work<32&&r->reveal<p->doc.npiece;++work,++r->reveal){
        const tiku_doc_piece_t *piece=&p->doc.piece[r->reveal];
        int start=p->starts[piece->run]+piece->off-p->doc.run[piece->run].off;
        if(r->match>=start&&r->match<start+piece->len){
            tiku_rect_t box=tiku_doc_piece_rect(&p->doc,r->reveal);int maximum=tiku_doc_height(&p->doc)-242;
            if(maximum<0)maximum=0;
            r->scroll=box.y>maximum?maximum:box.y;r->searching=0;break;
        }
    }
    if(r->reveal==p->doc.npiece)r->searching=0;
    s->shell->dirty=1;
}
/** @brief Begin a query against the current immutable page and retain repeat position. */
static int find(surface_app_state_t *s,const char *query,int backwards)
{
    struct surface_reader *r=s->reader;size_t length=strlen(query);
    if(!r->page||r->job||r->searching||length>255)return 0;
    size_t prior=strcmp(query,r->query)||r->match<0?SIZE_MAX:(size_t)r->match;
    if(prior==SIZE_MAX){r->match=-1;r->matches=r->ordinal=0;}
    memmove(r->query,query,length+1);r->match_length=(int)length;
    if(!tiku_text_search_begin(&r->search,r->page->text,(size_t)r->page->length,r->query,length,prior,backwards))return 0;
    r->searching=1;r->error=0;s->status=TIKU_RESOURCE_PENDING;s->shell->dirty=1;return 1;
}
void surface_reader_poll(surface_app_state_t *s)
{
    struct surface_reader *r=s->reader;if(!r)return;
    if(s->stopped){tiku_text_search_cancel(&r->search);r->searching=0;}
    if(r->searching){search_poll(s);return;}
    if(r->submitted){
        if(tiku_paged_storage_pending(s->store))return;
        r->submitted=0;tiku_app_lease_release(&s->lease);if(s->stopped)return;
        if(r->error==2){temporary(r);r->job=r->error=0;s->status=TIKU_RESOURCE_CANCELLED;s->shell->dirty=1;return;}
        size_t count;uint64_t epoch;int eof;
        const tiku_storage_request_t *request=tiku_paged_storage_result(s->store,&count,&epoch,&eof);
        tiku_resource_error_t status=tiku_paged_storage_status(s->store);
        if(!request||status!=TIKU_RESOURCE_OK){failed(s,status);return;}
        r->epoch=epoch;
        if(r->job==1){r->job=2;r->after=0;}
        else if(r->job==2){
            int found=0;for(size_t i=0;i<count;++i)if(!strcmp(request->entries[i].path,r->target)&&request->entries[i].kind==TIKU_RESOURCE_STREAM){r->node=request->entries[i].node_id;found=1;break;}
            if(found)r->job=3;
            else if(!eof&&count)r->after=request->entries[count-1].node_id;
            else{failed(s,TIKU_RESOURCE_NOT_FOUND);return;}
        }else{
            if(count>READER_BYTES-r->bytes||(!count&&!eof)){failed(s,TIKU_RESOURCE_LIMIT);return;}
            memcpy(r->source+r->bytes,request->bytes,count);r->bytes+=count;
            if(eof){r->source[r->bytes]=0;if(memchr(r->source,0,r->bytes)){failed(s,TIKU_RESOURCE_INVALID);return;}r->job=4;}
            else if(r->bytes==READER_BYTES){failed(s,TIKU_RESOURCE_LIMIT);return;}
        }
        s->shell->dirty=1;return;
    }
    if(s->stopped||!r->job)return;
    if(r->job<=3){submit(s);return;}
    if(r->job==4){
        tiku_owned_buffer_t memory;if(!buffer(s,&memory,sizeof(reader_page_t))){failed(s,TIKU_RESOURCE_LIMIT);return;}
        r->pending=memory.data;r->pending->self=memory;r->pending->references=1;tiku_doc_init(&r->pending->doc);
        size_t scratch=tiku_doc_reader_size();if(tiku_markdown_writer_size()>scratch)scratch=tiku_markdown_writer_size();
        if(!buffer(s,&r->scratch,scratch)){failed(s,TIKU_RESOURCE_LIMIT);return;}
        int kind=r->help?1:format(r->target);
        if(kind==1){r->job=6;return;}
        if(!buffer(s,&r->html,65536)){failed(s,TIKU_RESOURCE_LIMIT);return;}
        if(kind==2){
            if(!tiku_markdown_writer_begin(r->scratch.data,r->scratch.bytes,r->html.data,r->html.bytes,r->source,NULL)){failed(s,TIKU_RESOURCE_INVALID);return;}
            r->job=5;return;
        }
        char *out=r->html.data;size_t at=0;memcpy(out,"<pre>",5);at=5;
        for(size_t i=0;i<r->bytes;++i){const char *entity=r->source[i]=='&'?"&amp;":r->source[i]=='<'?"&lt;":r->source[i]=='>'?"&gt;":NULL;
            if(entity){size_t n=strlen(entity);memcpy(out+at,entity,n);at+=n;}else out[at++]=r->source[i];}
        memcpy(out+at,"</pre>",7);r->job=6;return;
    }
    if(r->job==5){int result=tiku_markdown_writer_step(r->scratch.data,1);if(result<0){failed(s,TIKU_RESOURCE_LIMIT);return;}if(!result)r->job=6;return;}
    if(r->job==6){
        if(!tiku_doc_reader_begin(r->scratch.data,r->scratch.bytes,&r->pending->doc,r->html.data?r->html.data:r->source)){failed(s,TIKU_RESOURCE_INVALID);return;}
        r->job=7;return;
    }
    if(r->job==7){int result=tiku_doc_reader_step(r->scratch.data,64);if(result<0){failed(s,TIKU_RESOURCE_INVALID);return;}if(!result)r->job=8;return;}
    if(r->job==8){(void)tiku_doc_layout(&r->pending->doc,720);r->heading_scan=0;r->job=9;return;}
    if(r->job==9){
        reader_page_t *p=r->pending;
        for(unsigned work=0;work<32&&r->heading_scan<(unsigned)p->doc.nblock;++work,++r->heading_scan)
            if(tiku_doc_heading_level(&p->doc.block[r->heading_scan]))p->headings[p->heading_count++]=(uint16_t)r->heading_scan;
        if(r->heading_scan==(unsigned)p->doc.nblock)r->job=10;
        return;
    }
    if(r->back){if(r->history_count)--r->history_count;}else remember(r);
    r->pending->length=tiku_html_text(&r->pending->doc,r->pending->text,sizeof r->pending->text,r->pending->starts);
    r->match=-1;r->matches=r->ordinal=r->match_length=0;r->heading_first=0;r->copied=0;
    surface_reader_drop(r->page);r->page=r->pending;r->pending=NULL;strcpy(r->path,r->target);r->scroll=r->restore;
    if(r->anchor[0]){int y=tiku_doc_anchor_y(&r->page->doc,r->anchor);if(y>=0)r->scroll=y;}
    int maximum=tiku_doc_height(&r->page->doc)-242;if(maximum<0)maximum=0;if(r->scroll>maximum)r->scroll=maximum;
    temporary(r);r->job=r->error=0;s->status=TIKU_RESOURCE_OK;s->shell->dirty=1;
}
/** @brief Resolve a local relative link, folding dot components within the media root. */
static int resolve(const char *current,const char *href,char *out,char *anchor)
{
    if(strchr(href,':')||strchr(href,'?')||strlen(href)>255)return 0;
    const char *hash=strchr(href,'#');size_t n=hash?(size_t)(hash-href):strlen(href);
    if(hash&&strlen(hash+1)>63)return 0;
    strcpy(anchor,hash?hash+1:"");
    if(!n){strcpy(out,current);return 1;}
    char joined[322];size_t used=0;
    if(href[0]!='/'){const char *slash=strrchr(current,'/');if(!slash)return 0;used=(size_t)(slash-current)+1;memcpy(joined,current,used);}
    memcpy(joined+used,href,n);joined[used+n]=0;out[0]='/';out[1]=0;size_t length=1;
    const char *part=joined;while(*part){while(*part=='/')++part;if(!*part)break;const char *end=strchr(part,'/');size_t size=end?(size_t)(end-part):strlen(part);
        if(size==1&&part[0]=='.'){}
        else if(size==2&&part[0]=='.'&&part[1]=='.'){if(length==1)return 0;char *slash=strrchr(out,'/');if(slash==out)length=1;else length=(size_t)(slash-out);out[length]=0;}
        else{if(length+(length>1)+size>64)return 0;if(length>1)out[length++]='/';memcpy(out+length,part,size);length+=size;out[length]=0;}
        part+=size;
    }
    return path_ok(out);
}
int surface_reader_back(surface_app_state_t *s)
{
    struct surface_reader *r=s->reader;if(!r)return 0;if(r->tools){r->tools=r->tools==2?1:0;s->shell->dirty=1;return 1;}if(r->job||r->searching)return 1;if(!r->history_count)return 0;
    unsigned i=r->history_count-1;char path[65];strcpy(path,r->history[i].path);int scroll=r->history[i].scroll;
    if(!strcmp(path,r->path)){r->scroll=scroll;--r->history_count;s->shell->dirty=1;return 1;}
    if(!strcmp(path,"Help"))help_open(s);else if(!surface_reader_open(s,path,0,0))return 1;
    r->back=1;r->restore=scroll;return 1;
}
void surface_reader_action(surface_app_state_t *s,int action)
{
    struct surface_reader *r=s->reader;if(!r||s->stopped)return;
    if(action==ACTION_READER_CANCEL&&r->searching){
        tiku_text_search_cancel(&r->search);r->searching=0;s->status=TIKU_RESOURCE_CANCELLED;s->shell->dirty=1;return;}
    if(action==ACTION_READER_CANCEL&&r->job){
        if(r->submitted){tiku_paged_storage_cancel(s->store);s->status=TIKU_RESOURCE_PENDING;r->error=2;}
        else{temporary(r);r->job=0;s->status=TIKU_RESOURCE_OK;}s->shell->dirty=1;return;
    }
    if(r->job||r->searching)return;
    r->copied=0;
    if(action==ACTION_READER_TOOLS){r->tools=1;s->shell->dirty=1;return;}
    if(action==ACTION_READER_OUTLINE){r->tools=2;s->shell->dirty=1;return;}
    if(action==ACTION_READER_DOCUMENT){r->tools=0;s->shell->dirty=1;return;}
    if(action==ACTION_READER_COPY){
        if(r->page&&r->match>=0&&r->match_length>0&&(size_t)r->match_length<sizeof s->shell->clipboard&&
           r->match<=r->page->length-r->match_length){
            memcpy(s->shell->clipboard,r->page->text+r->match,(size_t)r->match_length);s->shell->clipboard[r->match_length]=0;r->copied=1;
        }else r->copied=-1;
        s->shell->dirty=1;return;
    }
    if(action==ACTION_READER_OUTLINE_PREVIOUS){r->heading_first=r->heading_first>=6?r->heading_first-6:0;s->shell->dirty=1;return;}
    if(action==ACTION_READER_OUTLINE_NEXT){if(r->page&&r->heading_first+6<r->page->heading_count)r->heading_first+=6;s->shell->dirty=1;return;}
    if(action>=ACTION_READER_HEADING&&action<ACTION_READER_HEADING+6&&r->page&&r->tools==2){
        unsigned index=r->heading_first+(unsigned)(action-ACTION_READER_HEADING);
        if(index<r->page->heading_count){
            remember(r);int maximum=tiku_doc_height(&r->page->doc)-242;if(maximum<0)maximum=0;
            int y=r->page->doc.block[r->page->headings[index]].y;r->scroll=y>maximum?maximum:y;r->tools=0;
        }
        s->shell->dirty=1;return;
    }
    if(action==ACTION_READER_PATH){r->find_dialog=0;surface_text_open(s->shell);}
    else if(action==ACTION_READER_FIND&&r->page){r->tools=0;r->find_dialog=1;surface_text_open(s->shell);}
    else if(action==ACTION_READER_NEXT||action==ACTION_READER_PREVIOUS)(void)find(s,r->query,action==ACTION_READER_PREVIOUS);
    else if(action==ACTION_READER_HELP)help_open(s);
    else if(action==ACTION_READER_UP){r->scroll-=220;if(r->scroll<0)r->scroll=0;}
    else if(action==ACTION_READER_DOWN&&r->page){int maximum=tiku_doc_height(&r->page->doc)-242;if(maximum<0)maximum=0;r->scroll+=220;if(r->scroll>maximum)r->scroll=maximum;}
    else if(action>=ACTION_READER_LINK&&action<ACTION_READER_LINK+256&&r->page){
        char path[65],anchor[64];const char *href=tiku_doc_link_href(&r->page->doc,action-ACTION_READER_LINK);
        if(!resolve(r->path,href,path,anchor)){r->error=1;s->status=TIKU_RESOURCE_INVALID;}
        else if(!strcmp(path,r->path)){int y=tiku_doc_anchor_y(&r->page->doc,anchor);if(y>=0){remember(r);int maximum=tiku_doc_height(&r->page->doc)-242;if(maximum<0)maximum=0;r->scroll=y>maximum?maximum:y;r->error=0;s->status=TIKU_RESOURCE_OK;}}
        else if(surface_reader_open(s,path,0,0))strcpy(r->anchor,anchor);
    }
    s->shell->dirty=1;
}
const char *surface_reader_draft(surface_app_state_t *s)
{return s->reader->find_dialog?s->reader->query:s->reader->path[0]=='/'?s->reader->path:"/document.md";}
int surface_reader_accept(surface_app_state_t *s,const char *text)
{return s->reader->find_dialog?find(s,text,0):surface_reader_open(s,text,0,0);}
void surface_reader_view(const surface_app_state_t *s,surface_view_t *v)
{
    const struct surface_reader *r=s->reader;if(!r)return;
    v->reader_tools=r->tools;v->reader_copied=r->copied;v->reader_heading_first=r->heading_first;
    if(r->page){v->reader_heading_count=r->page->heading_count;
        for(unsigned row=0;row<6&&row+r->heading_first<r->page->heading_count;++row){
            const tiku_doc_block_t *block=&r->page->doc.block[r->page->headings[row+r->heading_first]];
            v->reader_heading_level[row]=(unsigned)tiku_doc_heading_level(block);
            char *title=v->reader_heading_title[row];unsigned n=0;
            if(block->nrun){const char *text=r->page->text+r->page->starts[block->run0];
                while(n<64&&text[n]&&text[n]!='\n'){title[n]=text[n];++n;}
                while(n&&((unsigned char)text[n]&0xc0)==0x80)--n;
            }
            title[n]=0;if(!n)strcpy(title,"Untitled heading");++v->reader_heading_rows;
        }
    }
    v->reader_find_dialog=r->find_dialog;v->reader_searching=r->searching;v->reader_match=r->match;
    v->reader_match_length=r->match_length;v->reader_matches=r->matches;v->reader_ordinal=r->ordinal;strcpy(v->reader_query,r->query);
    v->reader_page=r->page;strcpy(v->reader_path,r->path);v->reader_scroll=r->scroll;v->reader_busy=r->job||r->searching;v->reader_error=r->error;v->reader_history=(int)r->history_count;
    if(r->page){v->reader_height=tiku_doc_height(&r->page->doc);v->reader_notices=r->page->doc.full|r->page->doc.notices;}
    v->status=s->status;
}
int surface_reader_hit(const surface_view_t *v,int x,int y)
{
    if(v->reader_tools){
        if(x>=32&&x<172&&y>=394&&y<436)return ACTION_READER_DOCUMENT;
        if(v->reader_tools==1){
            if(x>=96&&x<704&&y>=144&&y<320&&(y-144)%62<48){
                int row=(y-144)/62;
                return row==0?ACTION_READER_HELP:row==1?(v->reader_page?ACTION_READER_OUTLINE:0):
                    row==2&&v->reader_match>=0&&v->reader_match_length?ACTION_READER_COPY:0;
            }
        }else{
            if(x>=32&&x<768&&y>=112&&y<376&&(y-112)%44<42){unsigned row=(unsigned)(y-112)/44;
                if(row<v->reader_heading_rows)return ACTION_READER_HEADING+(int)row;}
            if(x>=476&&x<616&&y>=394&&y<436&&v->reader_heading_first)return ACTION_READER_OUTLINE_PREVIOUS;
            if(x>=624&&x<764&&y>=394&&y<436&&v->reader_heading_first+6<v->reader_heading_count)return ACTION_READER_OUTLINE_NEXT;
        }
        return 0;
    }
    if(y>=394&&y<436&&x>=32&&x<768&&(x-32)%148<140){int column=(x-32)/148;
        if(v->reader_busy)return column==4?ACTION_READER_CANCEL:0;
        return column==1?ACTION_READER_TOOLS:column<4?ACTION_READER_PATH+column:v->reader_page?ACTION_READER_FIND:0;}
    if(!v->reader_busy&&v->reader_page&&v->reader_query[0]&&y>=80&&y<122){
        if(x>=464&&x<608)return ACTION_READER_PREVIOUS;
        if(x>=624&&x<768)return ACTION_READER_NEXT;
    }
    if(!v->reader_busy&&v->reader_page&&x>=40&&x<760&&y>=132&&y<374){
        const reader_page_t *page=v->reader_page;tiku_doc_hit_t hit=tiku_doc_hit(&page->doc,x-40,y-132+v->reader_scroll);
        if(hit.link>=0&&hit.link<256)return ACTION_READER_LINK+hit.link;
    }
    return 0;
}
/** @brief Shade the selected match through the shared flattened-text geometry. */
static void mark(void *context,tiku_surface_t *surface,int piece,tiku_rect_t where)
{
    const surface_view_t *v=context;const reader_page_t *p=v->reader_page;
    if(v->reader_match<0||!v->reader_match_length)return;
    tiku_rect_t box=tiku_html_text_range(&p->doc,p->starts,piece,v->reader_match,v->reader_match+v->reader_match_length);
    tiku_rect_t whole=tiku_doc_piece_rect(&p->doc,piece);box.x+=where.x-whole.x;box.y+=where.y-whole.y;
    if(box.w>0)tiku_fill(surface,box,0xffe18a);
}
void surface_reader_key(surface_app_state_t *s,const tiku_input_event_t *event)
{
    if(!event||event->type!=TIKU_EVENT_KEY_DOWN)return;
    if((event->modifiers&TIKU_MOD_CMD)&&event->key=='c')surface_reader_action(s,ACTION_READER_COPY);
    else if((event->modifiers&TIKU_MOD_CMD)&&event->key=='f')surface_reader_action(s,ACTION_READER_FIND);
    else if((event->modifiers&TIKU_MOD_CMD)&&event->key=='g')surface_reader_action(s,event->modifiers&TIKU_MOD_SHIFT?ACTION_READER_PREVIOUS:ACTION_READER_NEXT);
    else if(event->key==TIKU_KEY_PAGE_UP)surface_reader_action(s,ACTION_READER_UP);
    else if(event->key==TIKU_KEY_PAGE_DOWN)surface_reader_action(s,ACTION_READER_DOWN);
}
void surface_reader_paint(tiku_surface_t *s,const surface_view_t *v)
{
    if(v->reader_tools){
        tiku_text(s,tiku_font_bold(),32,104,v->reader_tools==1?"Document tools":"Document headings",TIKU_C_TEXT);
        if(v->reader_tools==1){
            tiku_ui_button(s,(tiku_rect_t){96,144,608,48},"TikuSurface Help",0);
            tiku_ui_button(s,(tiku_rect_t){96,206,608,48},"Headings",v->reader_page?0:TIKU_S_DISABLED);
            tiku_ui_button(s,(tiku_rect_t){96,268,608,48},"Copy selected match",v->reader_match>=0&&v->reader_match_length?0:TIKU_S_DISABLED);
            tiku_text(s,tiku_font_plain(),96,350,"Find selects a passage. Copy retains its original text and letter case.",TIKU_C_TEXT);
        }else{
            for(unsigned row=0;row<v->reader_heading_rows;++row){
                char title[72];int indent=(int)(v->reader_heading_level[row]-1)*12;
                tiku_text_elide(TIKU_MEASURE_PLAIN,v->reader_heading_title[row],700-indent,title,sizeof title);
                tiku_ui_button(s,(tiku_rect_t){32+indent,112+(int)row*44,736-indent,42},title,0);
            }
            if(!v->reader_heading_count)tiku_text(s,tiku_font_plain(),32,152,"This document has no headings.",TIKU_C_TEXT);
            tiku_ui_button(s,(tiku_rect_t){476,394,140,42},"Previous",v->reader_heading_first?0:TIKU_S_DISABLED);
            tiku_ui_button(s,(tiku_rect_t){624,394,140,42},"Next",v->reader_heading_first+6<v->reader_heading_count?0:TIKU_S_DISABLED);
        }
        tiku_ui_button(s,(tiku_rect_t){32,394,140,42},"Document",0);return;
    }
    char path[72];tiku_text_elide(TIKU_MEASURE_BOLD,v->reader_path[0]?v->reader_path:"Opening document",416,path,sizeof path);
    tiku_text(s,tiku_font_bold(),32,98,path,TIKU_C_TEXT);
    tiku_ui_button(s,(tiku_rect_t){464,80,144,42},"Previous match",v->reader_busy||!v->reader_query[0]?TIKU_S_DISABLED:0);
    tiku_ui_button(s,(tiku_rect_t){624,80,144,42},"Next match",v->reader_busy||!v->reader_query[0]?TIKU_S_DISABLED:0);
    tiku_ui_sunken(s,(tiku_rect_t){32,126,736,256},0xffffff);
    if(v->reader_page){const reader_page_t *p=v->reader_page;tiku_surface_t clipped=*s;tiku_rect_t damage=s->clip;tiku_clip_damage(&clipped,&damage);
        tiku_doc_paint_t how={.scroll=v->reader_scroll,.focus_link=-1,.cut="Document layout limit reached"};
        tiku_doc_draw_marked(&clipped,&p->doc,(tiku_rect_t){40,132,720,242},&how,mark,(void *)v);
    }
    const char *labels[]={"Open path","Tools","Up","Down",v->reader_busy?"Cancel":"Find"};
    for(unsigned i=0;i<5;++i)tiku_ui_button(s,(tiku_rect_t){32+(int)i*148,394,140,42},labels[i],v->reader_busy?(i==4?0:TIKU_S_DISABLED):(i==4&&!v->reader_page?TIKU_S_DISABLED:0));
    if(v->reader_query[0]){char number[65];tiku_text(s,tiku_font_plain(),32,118,v->reader_searching?"Searching...":v->reader_matches?"Match":"No matches",TIKU_C_TEXT);
        if(!v->reader_searching&&v->reader_matches){(void)tiku_word_format((uint64_t)v->reader_ordinal,10,number);tiku_text(s,tiku_font_plain(),104,118,number,TIKU_C_TEXT);
            tiku_text(s,tiku_font_plain(),160,118,"of",TIKU_C_TEXT);(void)tiku_word_format((uint64_t)v->reader_matches,10,number);tiku_text(s,tiku_font_plain(),192,118,number,TIKU_C_TEXT);}}
}
