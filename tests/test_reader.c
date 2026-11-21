/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_reader.c - queued documents, navigation and retained painting lifetime.
 * SPDX-License-Identifier: Apache-2.0
 */
#define main terminal_fixture_main
#include "test_terminal.c"
#undef main
/** @brief One delayed storage operation and controllable document contents. */
static struct {
    tiku_paged_storage_t *owner;
    const tiku_storage_request_t *request;
    const char *data;
    size_t bytes,reads;
    int cancelled,stale,missing;
} io;
/** @brief Retain the request until the explicit completion turn. */
static tiku_resource_error_t submit(void *p,tiku_paged_storage_t *owner,const tiku_storage_request_t *r)
{assert(!io.owner);io.owner=owner;io.request=r;io.cancelled=0;return TIKU_RESOURCE_OK;}
/** @brief Cancellation retains the request until retirement. */
static void cancel(void *p,tiku_paged_storage_t *owner,uint64_t token)
{assert(io.owner==owner&&io.request->token==token);io.cancelled=1;}
static const tiku_storage_provider_ops_t ops={submit,cancel};
/** @brief Complete a directory page or a bounded read with checked identity. */
static void retire(void)
{
    if(!io.owner)return;
    const tiku_storage_request_t *r=io.request;tiku_paged_storage_t *owner=io.owner;
    uint64_t token=r->token;size_t count=0;int eof=1;
    tiku_resource_error_t status=io.cancelled?TIKU_RESOURCE_CANCELLED:io.stale?TIKU_RESOURCE_STALE:TIKU_RESOURCE_OK;
    if(status==TIKU_RESOURCE_OK){
        assert(!r->epoch||r->epoch==42);
        if(r->operation==TIKU_STORAGE_LIST){
            const char *names[]={"/docs/one.html","/docs/two.md","/docs/plain.txt"};
            if(!strcmp(r->path,"/")){tiku_resource_entry_t *e=&r->entries[count++];memset(e,0,sizeof *e);strcpy(e->path,"/docs");strcpy(e->id,"docs");e->node_id=1;e->kind=TIKU_RESOURCE_DIRECTORY;}
            else for(unsigned i=0;i<3&&!io.missing;++i){
                tiku_resource_entry_t *e=&r->entries[count++];memset(e,0,sizeof *e);
                strcpy(e->path,names[i]);strcpy(e->id,"document");e->node_id=i+1;e->kind=TIKU_RESOURCE_STREAM;
            }
        }else{
            assert(r->operation==TIKU_STORAGE_READ&&r->node_id&&r->offset<=io.bytes);
            ++io.reads;count=io.bytes-(size_t)r->offset;if(count>r->capacity)count=r->capacity;
            if(count>137)count=137;
            memcpy(r->bytes,io.data+r->offset,count);eof=r->offset+count==io.bytes;
        }
    }
    io.owner=NULL;io.request=NULL;assert(tiku_paged_storage_complete(owner,token,status,count,42,eof));
}
/** @brief Bind the deterministic document provider to the host allocator. */
static surface_t *create(surface_host_t *h)
{
    memset(&io,0,sizeof io);surface_config_t c=surface_host_config(h);
    c.storage=(tiku_storage_provider_t){&ops,NULL,TIKU_RESOURCE_FEATURE_COLLECTION|TIKU_RESOURCE_FEATURE_STREAM|TIKU_RESOURCE_READ};
    c.paint_rows=2;surface_t *s=surface_create(&c);assert(s);return s;
}
/** @brief Finish parsing without advancing a live display frame. */
static surface_view_t drain(surface_t *s)
{
    surface_view_t v;
    for(unsigned i=0;i<2000;++i){retire();surface_step(s);surface_view(s,&v);if(!v.reader_busy)return v;}
    assert(0);return v;
}
/** @brief Open bytes using a listed identity or the path lookup lifecycle. */
static surface_view_t open_document(surface_t *s,const char *path,const char *data,int listed)
{
    io.data=data;io.bytes=strlen(data);assert(surface_reader_open(s->active.state,path,listed?42:0,listed?1:0));return drain(s);
}
/** @brief Retire provider storage before releasing the stopped shell. */
static void cleanup(surface_host_t *h,surface_t **s)
{surface_stop(*s);retire();finish(h,s);}
/** @brief Compare whole-frame rendering with two-row clipping. */
static void painting(const surface_view_t *v,const char *output)
{
    static uint32_t whole[800*480],sliced[800*480];tiku_surface_t a={.px=whole,.w=800,.h=480,.clip={0,0,800,480}},b=a;b.px=sliced;
    surface_paint(&a,v);for(int y=0;y<480;y+=2){b.clip=(tiku_rect_t){0,y,800,2};surface_paint(&b,v);}assert(!memcmp(whole,sliced,sizeof whole));
    if(output){FILE *f=fopen(output,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");for(unsigned i=0;i<800*480;++i){fputc(whole[i]>>16&255,f);fputc(whole[i]>>8&255,f);fputc(whole[i]&255,f);}assert(!fclose(f));}
}
/** @brief Submit Find through the touch dialog and its regular text field. */
static surface_view_t find_query(surface_t *s,const char *query)
{
    click(s,690,410);surface_view_t v;surface_view(s,&v);assert(v.text_entry&&v.reader_find_dialog);
    tiku_field_start(&s->text_field,query);key(s,TIKU_KEY_RETURN,0);surface_view(s,&v);assert(!v.text_entry);
    return drain(s);
}
/** @brief Search rendered words across runs, wrap matches and retain immutable frames. */
static void searches(const char *capture)
{
    surface_host_t h;surface_t *s=create(&h);assert(surface_launch(s,SURFACE_READER_APP));surface_step(s);(void)drain(s);
    surface_view_t v=open_document(s,"/docs/one.html","<p>alpha <b>be</b>ta ALPHA</p><p>alpha</p>",1);
    v=find_query(s,"BETA");assert(v.reader_matches==1&&v.reader_ordinal==1&&v.reader_match_length==4);painting(&v,capture);
    v=find_query(s,"alpha");assert(v.reader_matches==3&&v.reader_ordinal==1);int first=v.reader_match;
    click(s,690,100);v=drain(s);assert(v.reader_ordinal==2&&v.reader_match>first);
    key(s,'g',TIKU_MOD_CMD|TIKU_MOD_SHIFT);v=drain(s);assert(v.reader_ordinal==1);
    key(s,'g',TIKU_MOD_CMD|TIKU_MOD_SHIFT);v=drain(s);assert(v.reader_ordinal==3);
    key(s,'g',TIKU_MOD_CMD);v=drain(s);assert(v.reader_ordinal==1&&v.reader_match==first);
    v=find_query(s,"missing");assert(!v.reader_matches&&v.reader_match==-1);
    v=find_query(s,"");assert(!v.reader_matches&&!v.reader_query[0]);
    v=open_document(s,"/docs/plain.txt","é É é",1);v=find_query(s,"é");assert(v.reader_matches==2);
    static char words[8193];memset(words,'a',8192);words[8192]=0;v=open_document(s,"/docs/plain.txt",words,1);
    key(s,'f',TIKU_MOD_CMD);surface_view(s,&v);assert(v.text_entry&&v.reader_find_dialog);
    tiku_field_start(&s->text_field,"aaa");key(s,TIKU_KEY_RETURN,0);surface_view(s,&v);assert(v.reader_searching);
    assert(!surface_reader_open(s->active.state,"/docs/one.html",42,1));
    click(s,690,410);surface_view(s,&v);assert(!v.reader_searching&&v.status==TIKU_RESOURCE_CANCELLED);
    key(s,'g',TIKU_MOD_CMD);v=drain(s);assert(v.reader_matches==8190&&v.reader_ordinal==1);painting(&v,NULL);
    click(s,690,410);tiku_field_start(&s->text_field,"zzz");key(s,TIKU_KEY_RETURN,0);surface_view(s,&v);
    assert(v.reader_searching&&v.reader_match==-1&&!v.reader_matches);
    click(s,690,410);surface_view(s,&v);assert(!v.reader_searching&&v.reader_match==-1);
    assert(surface_render(s,surface_host_presenter(&h))==TIKU_PRESENT_BUSY);const void *old=s->painted.reader_page;
    v=open_document(s,"/docs/plain.txt","replacement",1);assert(v.reader_page!=old&&s->painted.reader_page==old&&v.reader_match==-1&&!v.reader_matches);
    key(s,'g',TIKU_MOD_CMD);v=drain(s);assert(!v.reader_matches);
    for(unsigned i=0;i<239;++i)(void)surface_render(s,surface_host_presenter(&h));
    cleanup(&h,&s);
}
/** @brief Navigate paged headings, restore reading position and copy exact matched text. */
static void tools_workflow(const char *capture)
{
    surface_host_t h;surface_t *s=create(&h);assert(surface_launch(s,SURFACE_READER_APP));surface_step(s);(void)drain(s);
    char html[8193];size_t used=0;
    for(unsigned i=0;i<19;++i){unsigned level=i%6+1;
        int n=snprintf(html+used,sizeof html-used,"<h%u>Heading %u <b>café</b></h%u><p>Mixed Case paragraph.</p>",level,i,level);
        assert(n>0&&(size_t)n<sizeof html-used);used+=(size_t)n;}
    surface_view_t v=open_document(s,"/docs/one.html",html,1);assert(v.reader_heading_count==19);
    click(s,240,410);surface_view(s,&v);assert(v.reader_tools==1);painting(&v,NULL);
    click(s,240,230);surface_view(s,&v);assert(v.reader_tools==2&&v.reader_heading_rows==6&&v.reader_heading_level[5]==6);
    assert(strstr(v.reader_heading_title[0],"Heading 0 café"));painting(&v,capture);
    click(s,690,410);surface_view(s,&v);assert(v.reader_heading_first==6);
    click(s,690,410);click(s,690,410);surface_view(s,&v);assert(v.reader_heading_first==18&&v.reader_heading_rows==1);
    click(s,690,410);surface_view(s,&v);assert(v.reader_heading_first==18);
    click(s,200,130);surface_view(s,&v);assert(!v.reader_tools&&v.reader_scroll>0&&v.reader_scroll<=v.reader_height-242);
    assert(surface_reader_back(s->active.state));surface_view(s,&v);assert(!v.reader_scroll&&!strcmp(v.reader_path,"/docs/one.html"));
    click(s,240,410);click(s,240,230);click(s,520,410);surface_view(s,&v);assert(v.reader_heading_first==12);
    assert(surface_reader_back(s->active.state));surface_view(s,&v);assert(v.reader_tools==1);
    assert(surface_reader_back(s->active.state));surface_view(s,&v);assert(!v.reader_tools);
    v=find_query(s,"mixed case");assert(v.reader_matches==19);key(s,'c',TIKU_MOD_CMD);surface_view(s,&v);
    assert(v.reader_copied==1&&!strcmp(s->clipboard,"Mixed Case"));
    click(s,240,410);click(s,240,290);surface_view(s,&v);assert(v.reader_tools==1&&v.reader_copied==1&&!strcmp(s->clipboard,"Mixed Case"));
    click(s,80,410);v=open_document(s,"/docs/plain.txt","No headings",1);assert(!v.reader_heading_count&&!v.reader_copied);
    key(s,'c',TIKU_MOD_CMD);surface_view(s,&v);assert(v.reader_copied==-1&&!strcmp(s->clipboard,"Mixed Case"));
    click(s,240,410);click(s,240,230);surface_view(s,&v);assert(v.reader_tools==2&&!v.reader_heading_rows);painting(&v,NULL);
    assert(surface_reader_back(s->active.state));click(s,240,164);v=drain(s);assert(!strcmp(v.reader_path,"Help")&&!v.reader_tools&&v.reader_heading_count);
    cleanup(&h,&s);
}
int main(int argc,char **argv)
{
    searches(argc>=2?argv[1]:NULL);tools_workflow(argc==3?argv[2]:NULL);
    const char *html="<h1>Local documents</h1><p><a href='two.md#second'>Markdown</a> <a href='#bottom'>Bottom</a> <a href='https://example.invalid'>Network</a></p><p>One</p><p>Two</p><p>Three</p><p>Four</p><p>Five</p><p>Six</p><p>Seven</p><p>Eight</p><p>Nine</p><p>Ten</p><h2 id='bottom'>End</h2>";
    surface_host_t h;surface_t *s=create(&h);
    click(s,590,32);click(s,200,SURFACE_DIFF?350:414);assert(s->active.state->id==SURFACE_READER_APP);
    surface_view_t v=drain(s);assert(v.reader_page&&!strcmp(v.reader_path,"Help")&&v.reader_height>242);
    surface_reader_action(s->active.state,ACTION_READER_LINK+3);surface_view(s,&v);
    assert(v.reader_scroll<=(v.reader_height>242?v.reader_height-242:0));
    v=open_document(s,"/docs/one.html",html,0);assert(!v.reader_error&&!strcmp(v.reader_path,"/docs/one.html")&&io.reads>1);
    painting(&v,NULL);
    const void *old=v.reader_page;surface_reader_action(s->active.state,ACTION_READER_LINK+2);surface_view(s,&v);assert(v.reader_error&&v.reader_page==old&&!io.owner);
    surface_reader_action(s->active.state,ACTION_READER_LINK+1);surface_view(s,&v);assert(v.reader_scroll<=(v.reader_height>242?v.reader_height-242:0)&&v.reader_history>=2);
    assert(surface_reader_back(s->active.state));surface_view(s,&v);assert(v.reader_scroll==0);
    io.data="# Markdown\n\n**Strong** and `code`.\n\n## Second\n\nA paragraph.\n";io.bytes=strlen(io.data);
    surface_reader_action(s->active.state,ACTION_READER_LINK);v=drain(s);assert(!v.reader_error&&!strcmp(v.reader_path,"/docs/two.md"));painting(&v,NULL);
    int history=v.reader_history;io.data=html;io.bytes=strlen(html);assert(surface_reader_back(s->active.state));v=drain(s);assert(!strcmp(v.reader_path,"/docs/one.html")&&v.reader_history==history-1);
    v=open_document(s,"/docs/plain.txt","<literal> & text\nnext line\n",1);assert(!v.reader_error);painting(&v,NULL);old=v.reader_page;
    assert(!surface_reader_open(s->active.state,"/docs/no.pdf",0,0));
    assert(!surface_reader_open(s->active.state,"/docs/../two.md",0,0));
    io.stale=1;v=open_document(s,"/docs/one.html",html,1);assert(v.reader_error&&v.reader_page==old);io.stale=0;
    io.missing=1;v=open_document(s,"/docs/one.html",html,0);assert(v.reader_error&&v.reader_page==old);io.missing=0;
    static char large[8194];memset(large,'x',sizeof large-1);v=open_document(s,"/docs/plain.txt",large,1);assert(v.reader_error&&v.reader_page==old);
    io.data="a\0b";io.bytes=3;assert(surface_reader_open(s->active.state,"/docs/plain.txt",42,1));v=drain(s);assert(v.reader_error&&v.reader_page==old);
    for(unsigned phase=0;phase<45;++phase){
        io.data=html;io.bytes=strlen(html);assert(surface_reader_open(s->active.state,"/docs/one.html",42,1));
        for(unsigned i=0;i<phase;++i){retire();surface_step(s);}
        surface_view(s,&v);int pending=v.reader_busy;const void *before=v.reader_page;
        surface_reader_action(s->active.state,ACTION_READER_CANCEL);v=drain(s);assert(!v.reader_busy&&(!pending||v.reader_page==before));
        v=open_document(s,"/docs/plain.txt","retained",1);assert(!v.reader_error);
    }
    for(unsigned fault=1;fault<=5;++fault){
        surface_view(s,&v);old=v.reader_page;h.memory.calls=0;h.memory.fail_at=fault;
        v=open_document(s,"/docs/two.md","# Failure injection\n\nold page stays visible",1);h.memory.fail_at=0;
        if(fault<=3)assert(v.reader_error&&v.reader_page==old);
    }
    v=open_document(s,"/docs/one.html",html,1);old=v.reader_page;
    assert(surface_render(s,surface_host_presenter(&h))==TIKU_PRESENT_BUSY);
    assert(s->painted.reader_page==old);
    v=open_document(s,"/docs/two.md","# Replacement\n\nnew page",1);assert(v.reader_page!=old&&s->painted.reader_page==old);
    assert(surface_close(s)==SURFACE_CLOSE_ACCEPTED);for(unsigned i=0;i<8;++i)surface_step(s);
    for(unsigned i=0;i<239;++i)(void)surface_render(s,surface_host_presenter(&h));
    assert(h.frame.owner);cleanup(&h,&s);
    s=create(&h);assert(surface_launch(s,SURFACE_READER_APP));surface_step(s);v=drain(s);
    assert(surface_render(s,surface_host_presenter(&h))==TIKU_PRESENT_BUSY);cleanup(&h,&s);
    for(unsigned fault=1;fault<12;++fault){
        s=create(&h);h.memory.calls=0;h.memory.fail_at=fault;assert(surface_launch(s,SURFACE_READER_APP));
        surface_step(s);for(unsigned i=0;i<100;++i)surface_step(s);h.memory.fail_at=0;cleanup(&h,&s);
    }
    puts("Reader: formats, links, history, bounds, cancellation, failures, sliced painting and retained frame lifetime passed");return 0;
}
