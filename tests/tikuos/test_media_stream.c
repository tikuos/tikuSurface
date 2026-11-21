/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_media_stream.c - paced upload prefixes, extent growth and publication.
 * SPDX-License-Identifier: Apache-2.0
 */
#define SURFACE_MUTATION_QUEUE_FIXTURE 1
#include "test_media_mutation.c"
/** @brief Complete one admitted operation without synchronous I/O or large hash steps. */
static int run_stream(int admission)
{
    if(admission)return admission;
    for(unsigned n=0;;++n){
        uint32_t at=mutation->payload_at;int pending=advance();
        assert(mutation->payload_at>=at&&mutation->payload_at-at<=64);
        if(!pending)break;
        assert(n<50000);supply();
    }
    return tiku_mfs_mutation_result(mutation);
}
/** @brief Read a published file in fixed chunks and check its repeated fixture bytes. */
static void verify_stream(const char *name,uint64_t length)
{
    uint64_t token;tiku_mfs_object_t o;
    assert(!tiku_mfs_stat(&fs,name,&o)&&o.length==length);
    assert(!tiku_mfs_open_read(&fs,name,&token));
    for(uint64_t at=0;at<length;){
        unsigned n=(unsigned)(length-at);if(n>65536)n=65536;
        assert(tiku_mfs_read_at(&fs,token,at,out,n)==(int)n);
        for(unsigned j=0;j<n;++j)assert(out[j]==(uint8_t)((at+j)*37+11));
        at+=n;
    }
    assert(!tiku_mfs_close_read(&fs,token));
}
/** @brief Verify private replacement, durable prefixes, resume and old snapshot ownership. */
static void uploads(void)
{
    fresh();uint64_t old=create_file("file"),reader;assert(!tiku_mfs_open_read(&fs,"file",&reader));
    uint8_t digest[32];tiku_mfs_hash_t hash;hash_init(&hash);hash_add(&hash,0,payload,65536);hash_add(&hash,65536,payload,4097);hash_end(hash,69633,digest);
    assert(run_stream(tiku_mfs_mutation_upload_begin(&fs,mutation,0,"file",TIKU_MFS_ATOMIC,69633,digest))==TIKU_MFS_BUSY);
    assert(!run_stream(tiku_mfs_mutation_upload_begin(&fs,mutation,old,"file",TIKU_MFS_ATOMIC,69633,digest)));
    uint64_t id=fs.draft.id,epoch=fs.names_epoch;assert(id!=old&&fs.writing);
    assert(!run_stream(tiku_mfs_mutation_append_begin(&fs,mutation,payload,4097)));
    assert(!run_stream(tiku_mfs_mutation_flush_begin(&fs,mutation,0))&&fs.names_epoch==epoch);
    assert(!tiku_mfs_release(&fs));assert(!run_stream(tiku_mfs_mutation_resume_begin(&fs,mutation,id))&&fs.draft.length==4097);
    assert(!run_stream(tiku_mfs_mutation_append_begin(&fs,mutation,payload+4097,65536-4097)));
    assert(!run_stream(tiku_mfs_mutation_append_begin(&fs,mutation,payload,4097)));
    assert(!run_stream(tiku_mfs_mutation_flush_begin(&fs,mutation,1))&&!fs.writing&&fs.names_epoch!=epoch);
    assert(tiku_mfs_read_at(&fs,reader,0,out,3)==3&&!memcmp(out,"abc",3));assert(!tiku_mfs_close_read(&fs,reader));
    verify_stream("file",69633);reboot();verify_stream("file",69633);
    assert(run_stream(tiku_mfs_mutation_resume_begin(&fs,mutation,id))==TIKU_MFS_ARG);
    assert(!run_stream(tiku_mfs_mutation_upload_begin(&fs,mutation,0,"empty",TIKU_MFS_ATOMIC,0,digest)));
    assert(run_stream(tiku_mfs_mutation_flush_begin(&fs,mutation,1))==TIKU_MFS_DIGEST&&fs.writing&&!fs.failed);
    assert(!tiku_mfs_release(&fs));
}
/** @brief Cross partial frames, index heights and checkpoint rollover with resumable prefixes. */
static void extents(void)
{
    fresh();assert(!run_stream(tiku_mfs_mutation_upload_begin(&fs,mutation,0,"stream",TIKU_MFS_STREAM,0,NULL)));
    uint64_t id=fs.draft.id,length=0;
    static const unsigned pieces[]={1,63,64,65,511,512,513,4095,4096,4097,65535,65536};
    for(unsigned i=0;i<sizeof pieces/sizeof *pieces;++i){
        unsigned n=pieces[i];static uint8_t fragment[65536];
        for(unsigned j=0;j<n;++j)fragment[j]=(uint8_t)((length+j)*37+11);
        assert(!run_stream(tiku_mfs_mutation_append_begin(&fs,mutation,fragment,n)));length+=n;
        assert(!run_stream(tiku_mfs_mutation_flush_begin(&fs,mutation,0)));verify_stream("stream",length);
        assert(!tiku_mfs_release(&fs));reboot();assert(!run_stream(tiku_mfs_mutation_resume_begin(&fs,mutation,id)));
    }
    for(unsigned i=0;i<270;++i){
        static uint8_t fragment[65536];for(unsigned j=0;j<65536;++j)fragment[j]=(uint8_t)((length+j)*37+11);
        assert(!run_stream(tiku_mfs_mutation_append_begin(&fs,mutation,fragment,65536)));length+=65536;
        assert(!run_stream(tiku_mfs_mutation_flush_begin(&fs,mutation,0)));
    }
    assert(fs.draft.height==3);assert(!run_stream(tiku_mfs_mutation_flush_begin(&fs,mutation,1)));
    reboot();verify_stream("stream",length);printf("Streaming: %llu bytes, three index levels and prefix resume passed\n",(unsigned long long)length);
}
/** @brief Prepare a private prefix over an existing visible file. */
static uint64_t private_prefix(void)
{
    fresh();uint64_t old=create_file("file");uint8_t digest[32];tiku_mfs_hash_t hash;
    hash_init(&hash);hash_add(&hash,0,payload,5120);hash_end(hash,5120,digest);
    assert(!run_stream(tiku_mfs_mutation_upload_begin(&fs,mutation,old,"file",TIKU_MFS_ATOMIC,5120,digest)));
    assert(!run_stream(tiku_mfs_mutation_append_begin(&fs,mutation,payload,1023)));
    assert(!run_stream(tiku_mfs_mutation_flush_begin(&fs,mutation,0)));
    return fs.draft.id;
}
/** @brief Append the remainder and optionally publish it. */
static int finish_prefix(unsigned final)
{
    int rc=run_stream(tiku_mfs_mutation_append_begin(&fs,mutation,payload+1023,4097));
    return rc?rc:run_stream(tiku_mfs_mutation_flush_begin(&fs,mutation,(int)final));
}
/** @brief Require a complete visible replacement or a resumable private prefix after recovery. */
static void recovered_prefix(uint64_t id,unsigned final)
{
    tiku_mfs_object_t o;assert(!tiku_mfs_stat(&fs,"file",&o));
    if(o.id==id){assert(final&&o.complete&&o.length==5120);verify_stream("file",5120);}
    else {
        assert(o.length==3&&o.complete);assert(!run_stream(tiku_mfs_mutation_resume_begin(&fs,mutation,id)));
        assert(fs.draft.length==1023||(!final&&fs.draft.length==5120));
        uint64_t token=fs.next_token++;fs.readers[0]=(tiku_mfs_reader_t){.token=token,.object=fs.draft};
        assert(tiku_mfs_read_at(&fs,token,0,out,(uint32_t)fs.draft.length)==(int)fs.draft.length);
        assert(!memcmp(out,payload,(size_t)fs.draft.length));assert(!tiku_mfs_close_read(&fs,token));
        assert(!tiku_mfs_release(&fs));
    }
}
/** @brief Cut each write and ordered barrier before, during and after persistence. */
static void stream_cuts(void)
{
    unsigned total=0;
    for(unsigned final=0;final<2;++final){
        uint64_t id=private_prefix();memcpy(snapshot,stable,BYTES);reboot();
        assert(!run_stream(tiku_mfs_mutation_resume_begin(&fs,mutation,id)));events=0;
        assert(!finish_prefix(final));unsigned count=events;
        for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=count;++cut){
            memcpy(stable,snapshot,BYTES);reboot();assert(!run_stream(tiku_mfs_mutation_resume_begin(&fs,mutation,id)));
            events=0;fail_at=cut;fail_mode=mode;
            assert(finish_prefix(final)==TIKU_MFS_IO&&fs.failed);reboot();recovered_prefix(id,final);++total;
        }
    }
    printf("Streaming: %u interrupted append/publication cuts passed\n",total);
}
/** @brief Recover private upload admission and preserve any preceding visible binding. */
static void admission_cuts(void)
{
    unsigned total=0;
    for(unsigned replacing=0;replacing<2;++replacing){
        fresh();uint64_t old=replacing?create_file("file"):0;
        memcpy(snapshot,stable,BYTES);uint8_t digest[32];tiku_mfs_hash_t hash;hash_init(&hash);hash_end(hash,0,digest);
        reboot();events=0;
        assert(!run_stream(tiku_mfs_mutation_upload_begin(&fs,mutation,old,"file",TIKU_MFS_ATOMIC,0,digest)));
        unsigned count=events;
        assert(!run_stream(tiku_mfs_mutation_flush_begin(&fs,mutation,1)));verify_stream("file",0);
        for(unsigned mode=0;mode<3;++mode)for(unsigned cut=1;cut<=count;++cut){
            memcpy(stable,snapshot,BYTES);reboot();events=0;fail_at=cut;fail_mode=mode;
            assert(run_stream(tiku_mfs_mutation_upload_begin(&fs,mutation,old,"file",TIKU_MFS_ATOMIC,0,digest))==TIKU_MFS_IO);
            reboot();tiku_mfs_object_t o;int rc=tiku_mfs_stat(&fs,"file",&o);
            assert(replacing?(!rc&&o.id==old&&o.length==3):rc==TIKU_MFS_NOENT);
            tiku_mfs_cursor_t cursor={0};rc=tiku_mfs_list_pending(&fs,&cursor,&o);
            assert(rc==TIKU_MFS_NOENT||(!rc&&!o.length&&!o.complete&&!strcmp(o.name,"file")));
            if(!rc){assert(!run_stream(tiku_mfs_mutation_resume_begin(&fs,mutation,o.id)));
                assert(!run_stream(tiku_mfs_mutation_flush_begin(&fs,mutation,1)));verify_stream("file",0);}
            ++total;
        }
    }
    printf("Streaming: %u interrupted admission cuts passed\n",total);
}
/** @brief Cancel each append turn while retaining pending sector leases. */
static void stream_cancel(void)
{
    uint64_t id=private_prefix();memcpy(snapshot,stable,BYTES);unsigned completed=0;
    for(unsigned stop=0;stop<220;++stop){
        memcpy(stable,snapshot,BYTES);reboot();assert(!run_stream(tiku_mfs_mutation_resume_begin(&fs,mutation,id)));
        assert(!tiku_mfs_mutation_append_begin(&fs,mutation,payload+1023,4097));
        for(unsigned i=0;i<stop&&!mutation->done;++i){advance();if(i+1<stop)supply();}
        uint64_t lba;void *sector;if(tiku_mfs_mutation_io(mutation,&lba,&sector)){
            assert(tiku_mfs_mutation_cancel(&fs,mutation)==TIKU_MFS_BUSY);supply();
        }
        unsigned changed=mutation->changed,done=mutation->done;completed+=done;
        assert(!tiku_mfs_mutation_cancel(&fs,mutation));if(changed&&!done)assert(fs.failed);
        reboot();recovered_prefix(id,1);
    }
    assert(completed);puts("Streaming: 220 append cancellation boundaries passed");
}
/** @brief Refuse damaged partial-frame metadata and content before publishing a new prefix. */
static void stream_corruption(void)
{
    uint64_t id=private_prefix();tiku_mfs_ref_t frame;assert(!lookup(&fs,&fs.draft,0,&frame));
    memcpy(snapshot,stable,BYTES);
    const uint64_t positions[]={frame.lba*512+32,(frame.lba+1)*512+7,(frame.lba+2)*512+200,(frame.lba+3)*512+64};
    for(unsigned i=0;i<sizeof positions/sizeof *positions;++i){
        memcpy(stable,snapshot,BYTES);reboot();assert(!run_stream(tiku_mfs_mutation_resume_begin(&fs,mutation,id)));
        cache[positions[i]]^=1;
        assert(run_stream(tiku_mfs_mutation_append_begin(&fs,mutation,payload+1023,4097))==TIKU_MFS_CORRUPT);
        reboot();recovered_prefix(id,1);
    }
    fresh();assert(!run_stream(tiku_mfs_mutation_upload_begin(&fs,mutation,0,"empty",TIKU_MFS_STREAM,0,NULL)));
    assert(tiku_mfs_mutation_append_begin(&fs,mutation,payload,65537)==TIKU_MFS_ARG);
    assert(tiku_mfs_mutation_append_begin(&fs,mutation,NULL,1)==TIKU_MFS_ARG);
    assert(!run_stream(tiku_mfs_mutation_flush_begin(&fs,mutation,1)));reboot();verify_stream("empty",0);
}
int main(void)
{
    stable=calloc(1,BYTES);cache=calloc(1,BYTES);snapshot=calloc(1,BYTES);
    void *memory=malloc(tiku_mfs_mutation_size());assert(stable&&cache&&snapshot&&memory);
    mutation=tiku_mfs_mutation_init(memory,tiku_mfs_mutation_size());assert(mutation);
    for(unsigned i=0;i<sizeof payload;++i)payload[i]=(uint8_t)(i*37+11);
    uploads();extents();stream_cuts();admission_cuts();stream_cancel();stream_corruption();printf("Streaming: owner storage %zu bytes\n",tiku_mfs_mutation_size());
    free(memory);free(stable);free(cache);free(snapshot);return 0;
}
