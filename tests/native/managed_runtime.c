/* Hosted sanitizer/oracle harness only; the actual native runtime is freestanding. */
#include "../../runtime/native/managed.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#define WOOD_TEST_FILE_FAULTS
#include "file_host.inc"

typedef struct { char code[6], message[160]; unsigned errors; } Context;
WInt wd_rt_context_size(void) { return sizeof(Context); }
void wd_rt_context_error(void *ctx,WStr *code,WStr *message) {
    Context *value=ctx;
    *code=(WStr){(unsigned char*)value->code,(WInt)strlen(value->code),0};
    *message=(WStr){(unsigned char*)value->message,(WInt)strlen(value->message),0};
}
void wd_rt_context_clear(void *ctx) { ((Context*)ctx)->errors=0; }
void wd_rt_context_forward(void *to,void *from) { if(((Context*)from)->errors) *(Context*)to=*(Context*)from; }
extern void wd_test_thread_fail(int);
extern int wd_test_threads_live(void);
static void *thread_identity(void *ctx,void *value) {
    (void)ctx;
    for(unsigned i=0;i<10000;++i) { wd_m_retain(value); wd_m_release(value); }
    wd_m_retain(value); return value;
}
WInt wd_rt_unwinding(void *opaque) { return ((Context *)opaque)->errors != 0; }
void wd_rt_error_value(void *ctx, const char *code, WInt size, const WStr *message) {
    wd_rt_error(ctx,code,size,(const char *)message->data,message->size);
}
static WInt fail_after = -1;
extern WInt wood_driver_call(const char *,WInt);
extern WInt wood_driver_response(WInt *,WInt);
static void *thread_private_guard(void *ctx,void *value) {
    (void)ctx; WInt ignored=0;
    assert(wood_driver_call("{}",2)==-1 && wood_driver_response(&ignored,0)==-1);
    wd_m_retain(value); return value;
}
static unsigned comparisons;
static int32_t compare_int(const void *a, const void *b) {
    ++comparisons;
    WInt left = *(const WInt *)a, right = *(const WInt *)b;
    return (left > right) - (left < right);
}
void *wd_platform_alloc(WSize bytes) {
    if (fail_after == 0) return 0;
    if (fail_after > 0) --fail_after;
    return malloc((size_t)bytes);
}
void wd_platform_free(void *memory, WSize bytes) { (void)bytes; free(memory); }
void wd_rt_error(void *opaque, const char *code, WInt n, const char *message, WInt size) {
    Context *ctx = opaque;
    assert(n == 5 && size < (WInt)sizeof ctx->message);
    memcpy(ctx->code, code, 5); ctx->code[5] = 0;
    memcpy(ctx->message, message, (size_t)size); ctx->message[size] = 0;
    ++ctx->errors;
}
static WStr literal(const char *text) {
    return (WStr){(unsigned char *)text, (WInt)strlen(text), 0};
}
static void expect(WStr value, const char *text) {
    assert(value.size == (WInt)strlen(text));
    assert(!value.size || memcmp(value.data, text, (size_t)value.size) == 0);
}
static void callable_marker(void) {}
static unsigned sequence_retains,sequence_drops;
static void sequence_string_retain(void *slot) { ++sequence_retains; wd_m_str_retain(slot); }
static void sequence_string_drop(void *slot) { ++sequence_drops; wd_m_str_drop(slot); }
typedef struct { WStr label; WSeq *children; } SequenceRecord;
static void sequence_record_retain(void *slot) {
    SequenceRecord *record=slot;
    ++sequence_retains; wd_m_str_retain(&record->label); wd_m_retain(record->children);
}
static void sequence_record_drop(void *slot) {
    SequenceRecord *record=slot;
    ++sequence_drops; wd_m_str_drop(&record->label); wd_m_release(record->children);
}
static void sequence_ownership(Context *ctx) {
    WStr a=literal("transfer-"),b=literal("payload"),value={0};
    wd_m_str_concat(ctx,&value,&a,&b);
    WSeq *values=wd_m_seq_new(ctx,sizeof(WStr),0,0,sequence_string_retain,sequence_string_drop);
    sequence_retains=sequence_drops=0;
    for(unsigned i=0;i<128;++i) wd_m_seq_append(ctx,&values,&value);
    assert(!ctx->errors && values->length==128 && value.owner->refs==129);
    /* Unique growth transfers elements. Appending retains each new argument
       exactly once; growth must not retain/drop the already-owned prefix. */
    assert(sequence_retains==128 && sequence_drops==0);
    WSeq *original=values;
    sequence_retains=sequence_drops=0;
    assert(!wd_m_seq_unique(ctx,&values,INT64_MAX));
    assert(ctx->errors && !strcmp(ctx->code,"R4003"));
    assert(values==original && values->length==128 && value.owner->refs==129);
    assert(!sequence_retains && !sequence_drops);
    ctx->errors=0;
    fail_after=0;
    assert(!wd_m_seq_unique(ctx,&values,values->capacity+1));
    assert(ctx->errors && !strcmp(ctx->code,"R4099"));
    assert(values==original && values->length==128 && value.owner->refs==129);
    assert(!sequence_retains && !sequence_drops);
    fail_after=-1; ctx->errors=0;
    assert(wd_m_seq_unique(ctx,&values,values->capacity+1));
    assert(values->length==128 && value.owner->refs==129);
    assert(!sequence_retains && !sequence_drops);
    for(unsigned i=0;i<128;++i) expect(*(WStr *)wd_m_seq_at(ctx,values,i),"transfer-payload");
    WSeq *snapshot=values; wd_m_retain(snapshot);
    assert(wd_m_seq_unique(ctx,&values,values->capacity));
    assert(values!=snapshot && values->length==128 && snapshot->length==128);
    assert(sequence_retains==128 && !sequence_drops && value.owner->refs==257);
    WStr popped={0}; wd_m_seq_pop(ctx,&values,&popped);
    assert(values->length==127 && snapshot->length==128);
    wd_m_release(values); wd_m_release(snapshot);
    assert(sequence_drops==255 && value.owner->refs==2);
    expect(popped,"transfer-payload"); wd_m_str_drop(&popped); wd_m_str_drop(&value);
    assert(!ctx->errors && wd_m_live_count()==0 && wd_m_live_bytes()==0);

    wd_m_str_concat(ctx,&value,&a,&b);
    WSeq *children=wd_m_seq_new(ctx,sizeof(WInt),0,0,0,0);
    WInt number=42; wd_m_seq_append(ctx,&children,&number);
    SequenceRecord record={value,children};
    values=wd_m_seq_new(ctx,sizeof(record),0,0,sequence_record_retain,sequence_record_drop);
    sequence_retains=sequence_drops=0;
    for(unsigned i=0;i<32;++i) wd_m_seq_append(ctx,&values,&record);
    assert(sequence_retains==32 && !sequence_drops);
    assert(value.owner->refs==33 && children->owner.refs==33);
    snapshot=values; wd_m_retain(snapshot);
    assert(wd_m_seq_unique(ctx,&values,values->capacity+1));
    assert(snapshot->length==32 && values->length==32);
    assert(sequence_retains==64 && !sequence_drops);
    assert(value.owner->refs==65 && children->owner.refs==65);
    wd_m_release(values); wd_m_release(snapshot);
    assert(sequence_drops==64 && value.owner->refs==1 && children->owner.refs==1);
    wd_m_release(children); wd_m_str_drop(&value);
    assert(!ctx->errors && wd_m_live_count()==0 && wd_m_live_bytes()==0);
}
int main(void) {
    Context sequence_context={0}; sequence_ownership(&sequence_context);
    /* Shared immutable owners, failure-to-start rollback and repeated waits. */
    wd_m_threads_init();
    Context task_context={0}; WStr task_json={(unsigned char*)"[1,2,3]",7,0};
    void *message=wd_m_json_parse(&task_context,&task_json),*jobs[16];
    for(unsigned i=0;i<16;++i) jobs[i]=wd_m_task_spawn(&task_context,(void*)thread_identity,message);
    assert(!task_context.errors);
    for(unsigned i=0;i<16;++i) {
        for(unsigned wait=0;wait<2;++wait) { void *result=wd_m_task_wait(&task_context,jobs[i],10000); assert(result==message); wd_m_release(result); }
        wd_m_task_close(jobs[i]); wd_m_task_close(jobs[i]); wd_m_release(jobs[i]);
    }
    assert(!task_context.errors && wd_test_threads_live()==0);
    WInt reply_size=wood_driver_call("{\"op\":\"cwd\"}",12),reply[65536];
    assert(reply_size>0 && reply_size<=65536);
    void *guard=wd_m_task_spawn(&task_context,(void*)thread_private_guard,message);
    void *guard_result=wd_m_task_wait(&task_context,guard,10000);
    wd_m_release(guard_result); wd_m_release(guard);
    assert(wood_driver_response(reply,reply_size)==reply_size && reply[0]=='{');
    wd_test_thread_fail(1);
    assert(!wd_m_task_spawn(&task_context,(void*)thread_identity,message));
    assert(task_context.errors && !strcmp(task_context.code,"R4022"));
    task_context.errors=0; wd_test_thread_fail(0);
    fail_after=0;
    assert(!wd_m_task_spawn(&task_context,(void*)thread_identity,message));
    fail_after=-1; assert(task_context.errors && !strcmp(task_context.code,"R4099")); task_context.errors=0;
    void *last=wd_m_task_spawn(&task_context,(void*)thread_identity,message);
    wd_m_release(last); wd_m_release(message);
    assert(!task_context.errors && wd_test_threads_live()==0 && wd_m_live_count()==0);
    Context ctx = {0};
    WStr a = literal("alpha"), b = literal(" beta"), joined, view, changed, number;
    wd_m_str_concat(&ctx, &joined, &a, &b);
    expect(joined, "alpha beta");
    wd_m_str_slice(&ctx, &view, &joined, 6, 10, 1, 1, 1);
    wd_m_release(joined.owner);
    expect(view, "beta"); /* Escaping view owns its original allocation. */
    WStr empty = {0}, dash = literal("-");
    wd_m_str_replace(&ctx, &changed, &view, &empty, &dash);
    assert(!changed.owner && ctx.errors == 1 && strcmp(ctx.code,"R4009") == 0);
    assert(strcmp(ctx.message,"replacement pattern cannot be empty") == 0);
    WStr pattern = literal("et");
    wd_m_str_replace(&ctx,&changed,&view,&pattern,&dash);
    expect(changed,"b-a");
    wd_m_release(view.owner); wd_m_release(changed.owner);
    wd_m_int_str(&ctx, &number, INT64_MIN);
    expect(number, "-9223372036854775808");
    assert(wd_m_str_int(&ctx, &number) == INT64_MIN);
    wd_m_release(number.owner);
    const char *valid[] = {"0", "-0", "+0", "42", "+42", "-42",
        "9223372036854775807", "-9223372036854775808"};
    for (size_t i = 0; i < sizeof valid / sizeof *valid; ++i) {
        WStr s = literal(valid[i]); unsigned before = ctx.errors;
        (void)wd_m_str_int(&ctx, &s); assert(ctx.errors == before);
    }
    const char *bad[] = {"", "+", "-", "+-1", "++1", " 1", "1 ", "1x",
        "9223372036854775808", "-9223372036854775809", "999999999999999999999999"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; ++i) {
        WStr s = literal(bad[i]); unsigned before = ctx.errors;
        (void)wd_m_str_int(&ctx, &s); assert(ctx.errors == before + 1);
        assert(strcmp(ctx.code, "R4003") == 0);
    }
    WInt steps[] = {1, 2, -1, -2, INT64_MIN, INT64_MAX};
    WStr six = literal("abcdef");
    const char *reversed[] = {"abcdef", "ace", "fedcba", "fdb", "f", "a"};
    for (size_t i = 0; i < sizeof steps / sizeof *steps; ++i) {
        wd_m_str_slice(&ctx, &changed, &six, 0, 0, steps[i], 0, 0);
        expect(changed, reversed[i]); wd_m_release(changed.owner);
    }
    assert(wd_m_find(&empty, &empty) == 0);
    wd_m_str_repeat(&ctx, &changed, &empty, INT64_MAX);
    expect(changed, ""); wd_m_release(changed.owner);
    WSeq *seq = wd_m_seq_new(&ctx, sizeof(WStr), 0, 0, wd_m_str_retain, wd_m_str_drop);
    wd_m_str_concat(&ctx, &joined, &a, &b);
    wd_m_seq_append(&ctx, &seq, &joined);
    WSeq *snapshot = seq; wd_m_retain(snapshot);
    wd_m_seq_append(&ctx, &seq, &joined);
    assert(seq != snapshot && seq->length == 2 && snapshot->length == 1);
    wd_m_release(joined.owner);
    WStr popped = {0}; wd_m_seq_pop(&ctx, &seq, &popped);
    expect(popped, "alpha beta");
    wd_m_release(seq); wd_m_release(snapshot);
    expect(popped, "alpha beta"); wd_m_release(popped.owner);
    WStr text = literal("one::two::::"), delimiter = literal("::");
    seq = wd_m_str_split(&ctx, &text, &delimiter);
    assert(seq->length == 4);
    wd_m_str_join(&ctx, &joined, seq, &dash);
    expect(joined, "one-two--");
    wd_m_release(seq); wd_m_release(joined.owner);
    assert(wd_m_live_count() == 0 && wd_m_live_bytes() == 0);
    /* Erased values detach on stateful invocation, including allocation failure.
       The payload owns its children independently of the source expression. */
    ctx.errors = 0;
    wd_m_str_concat(&ctx,&joined,&a,&b);
    void *callable = wd_m_callable_new(&ctx,sizeof(WStr),wd_m_str_retain,
        wd_m_str_drop,callable_marker,&joined);
    assert(callable && !ctx.errors && joined.owner->refs == 2);
    void *saved_callable = callable;
    wd_m_retain(saved_callable);
    fail_after = 0;
    assert(!wd_m_callable_unique(&ctx,&callable));
    assert(callable == saved_callable && joined.owner->refs == 2);
    assert(ctx.errors == 1 && strcmp(ctx.code,"R4099") == 0);
    fail_after = -1; ctx.errors = 0;
    void *detached_callable = wd_m_callable_unique(&ctx,&callable);
    assert(detached_callable == callable);
    assert(callable != saved_callable && joined.owner->refs == 3);
    WStr *payload = wd_m_callable_data(callable);
    wd_m_str_drop(payload); *payload = literal("changed");
    expect(*(WStr *)wd_m_callable_data(saved_callable),"alpha beta");
    assert(wd_m_callable_code(callable) == callable_marker);
    wd_m_str_drop(&joined);
    wd_m_release(callable); wd_m_release(saved_callable);
    callable = wd_m_callable_new(&ctx,0,0,0,callable_marker,0);
    assert(callable && !ctx.errors); wd_m_release(callable);
    callable = 0;
    assert(!wd_m_callable_unique(&ctx,&callable));
    assert(ctx.errors == 1 && strcmp(ctx.code,"R4025") == 0);
    ctx.errors = 0;
    assert(wd_m_live_count() == 0 && wd_m_live_bytes() == 0);
    for (unsigned i = 0; i < 10000; ++i) {
        wd_m_int_str(&ctx, &number, i);
        wd_m_str_concat(&ctx, &joined, &a, &number);
        wd_m_release(number.owner); wd_m_release(joined.owner);
        assert(wd_m_live_count() == 0);
    }
    /* Allocation failures leave existing owner/value graphs valid. */
    seq = wd_m_seq_new(&ctx, sizeof(WStr), 0, 0, wd_m_str_retain, wd_m_str_drop);
    wd_m_seq_append(&ctx, &seq, &a);
    snapshot = seq; wd_m_retain(snapshot);
    fail_after = 0;
    unsigned before = ctx.errors;
    wd_m_seq_append(&ctx, &seq, &b);
    assert(ctx.errors == before + 1 && seq == snapshot && seq->length == 1);
    assert(strcmp(ctx.code, "R4099") == 0);
    wd_m_str_new(&ctx, &joined, 100);
    assert(!joined.owner && !joined.size);
    fail_after = -1;
    wd_m_release(seq); wd_m_release(snapshot);
    assert(wd_m_live_count() == 0 && wd_m_live_bytes() == 0);
    typedef struct { WInt key, value; } Pair;
    WMap *map = wd_m_map_new(&ctx, sizeof(Pair), offsetof(Pair,value), sizeof(WInt),
                             0, 0, 0, compare_int);
    assert(map);
    for (WInt i = 0; i < 10000; ++i) {
        *(WInt *)wd_m_map_edit(&ctx, &map, &i, 1) = i * 3;
    }
    assert(wd_m_map_len(map) == 10000);
    for (WInt i = 9999; i >= 0; --i) {
        comparisons = 0;
        assert(*(WInt *)wd_m_map_at(&ctx,map,&i) == i * 3);
        assert(comparisons <= 32); /* Sorted insertion must remain balanced. */
        assert(((Pair *)wd_m_map_entry(map,i,0))->key == i);
    }
    WMap *saved_map = map; wd_m_retain(saved_map);
    WInt key = 5;
    *(WInt *)wd_m_map_edit(&ctx,&map,&key,0) = -1;
    assert(*(WInt *)wd_m_map_at(&ctx,saved_map,&key) == 15);
    assert(*(WInt *)wd_m_map_at(&ctx,map,&key) == -1);
    wd_m_release(map); wd_m_release(saved_map);
    /* Opposite insertion order exercises the other rotation direction. */
    map = wd_m_map_new(&ctx,sizeof(Pair),offsetof(Pair,value),sizeof(WInt),0,0,0,compare_int);
    for (WInt i = 1000; i >= 0; --i) *(WInt *)wd_m_map_edit(&ctx,&map,&i,1) = i;
    for (WInt i = 0; i <= 1000; ++i) {
        comparisons = 0;
        assert(*(WInt *)wd_m_map_at(&ctx,map,&i) == i);
        assert(comparisons <= 24);
    }
    saved_map = map; wd_m_retain(saved_map);
    fail_after = 0;
    key = 2000;
    assert(!wd_m_map_edit(&ctx,&map,&key,1));
    assert(map == saved_map && wd_m_map_len(map) == 1001);
    fail_after = -1;
    wd_m_release(map); wd_m_release(saved_map);
    assert(wd_m_live_count() == 0 && wd_m_live_bytes() == 0);
    /* Fresh exclusive fixture; this harness is run inside a private task dir. */
    ctx.errors=0;
    /* Dense insertion order and AVL lookup must survive arbitrary removals and
       reinsertions, with the old snapshot pinned across every mutation. */
    map=wd_m_map_new(&ctx,sizeof(Pair),offsetof(Pair,value),sizeof(WInt),0,0,0,compare_int);
    WInt order[257],length=0; uint64_t state=127;
    for(unsigned trial=0;trial<10000;++trial) {
        state=state*UINT64_C(6364136223846793005)+1;
        key=(WInt)((state>>32)%257);
        WInt position=0; while(position<length && order[position]!=key) ++position;
        saved_map=map; wd_m_retain(saved_map);
        if(state&1) {
            assert(wd_m_map_remove(&ctx,&map,&key)==(position<length));
            assert((wd_m_map_find(saved_map,&key)>=0)==(position<length));
            if(position<length) { for(WInt i=position+1;i<length;++i) order[i-1]=order[i]; --length; }
        } else {
            *(WInt*)wd_m_map_edit(&ctx,&map,&key,1)=key*3;
            if(position==length) order[length++]=key;
        }
        wd_m_release(saved_map);
        assert(!ctx.errors && wd_m_map_len(map)==length);
        for(WInt i=0;i<length;++i) {
            comparisons=0;
            assert(*(WInt*)wd_m_map_at(&ctx,map,order+i)==order[i]*3 && comparisons<=16);
            assert(((Pair*)wd_m_map_entry(map,i,0))->key==order[i]);
        }
    }
    for(WInt budget=0;budget<8;++budget) {
        saved_map=map; wd_m_retain(saved_map); key=order[0];
        fail_after=budget; WInt removed=wd_m_map_remove(&ctx,&map,&key); fail_after=-1;
        assert(*(WInt*)wd_m_map_at(&ctx,saved_map,&key)==key*3);
        assert(wd_m_map_len(map)==length-removed);
        ctx.errors=0;
        wd_m_release(map); map=saved_map;
    }
    key=-1; fail_after=0;
    assert(!wd_m_map_remove(&ctx,&map,&key) && !ctx.errors); fail_after=-1;
    wd_m_release(map);
    /* Exact QPC fractional scaling across the entire positive int64 range. */
    for(unsigned trial=0;trial<100000;++trial) {
        state=state*UINT64_C(6364136223846793005)+1;
        WInt frequency=(WInt)(state>>1); if(!frequency) frequency=1;
        state=state*UINT64_C(6364136223846793005)+1;
        WInt remainder=(WInt)((state>>1)%(uint64_t)frequency);
        WInt reference=(WInt)(((__uint128_t)remainder*1000000000)/(uint64_t)frequency);
        assert(wd_m_time_fraction(remainder,frequency)==reference);
    }
    assert(wd_m_time_fraction(INT64_MAX-1,INT64_MAX)==999999999);
    typedef struct TestFormat TestFormat;
    typedef struct { WStr name; WSize offset; const TestFormat *type; } TestField;
    struct TestFormat { WInt kind,count; const TestField *fields; WStr name; };
    TestFormat string_type={4,0,0,{0}};
    TestField element={{0},0,&string_type};
    TestFormat list_type={5,1,&element,{0}};
    WSeq *texts=wd_m_seq_new(&ctx,sizeof(WStr),0,0,wd_m_str_retain,wd_m_str_drop);
    WStr sample=literal("a\nb"); wd_m_seq_append(&ctx,&texts,&sample);
    WSize baseline_count=wd_m_live_count(),baseline_bytes=wd_m_live_bytes();
    unsigned format_success=0;
    for(WInt budget=0;budget<40;++budget) {
        WStr text={0}; fail_after=budget;
        wd_m_format_value(&ctx,&text,&list_type,&texts,0); fail_after=-1;
        if(!ctx.errors) { expect(text,"[\"a\\nb\"]"); ++format_success; }
        wd_m_str_drop(&text); ctx.errors=0;
        assert(wd_m_live_count()==baseline_count && wd_m_live_bytes()==baseline_bytes);
    }
    assert(format_success>0); wd_m_release(texts);
    assert(wd_m_live_count()==0 && wd_m_live_bytes()==0);
    /* Fresh exclusive fixture; this harness is run inside a private task dir. */
    ctx.errors = 0;
    WStr path = literal("primitive-stream.bin"), mode = literal("x");
    void *stream = wd_m_stream_open(&ctx,&path,&mode);
    assert(stream && file_handles == 1 && !ctx.errors);
    wd_m_retain(stream);
    WStr bytes = literal("binary\nbytes");
    wd_m_stream_write(&ctx,stream,&bytes);
    assert(wd_m_stream_tell(&ctx,stream) == 12);
    wd_m_release(stream); /* Alias remains open. */
    assert(!wd_m_stream_closed(stream));
    wd_m_stream_close(&ctx,stream,1);
    assert(wd_m_stream_closed(stream) && file_handles == 0);
    wd_m_release(stream);
    mode = literal("r");
    stream = wd_m_stream_open(&ctx,&path,&mode);
    wd_m_stream_read(&ctx,&joined,stream,12); expect(joined,"binary\nbytes");
    wd_m_str_drop(&joined);
    wd_m_stream_seek(&ctx,stream,0);
    wd_m_stream_line(&ctx,&joined,stream,12); expect(joined,"binary\n");
    wd_m_str_drop(&joined);
    assert(wd_m_stream_tell(&ctx,stream) == 7);
    wd_m_stream_read(&ctx,&joined,stream,2); expect(joined,"by");
    wd_m_str_drop(&joined);
    assert(wd_m_stream_tell(&ctx,stream) == 9);
    wd_m_stream_line(&ctx,&joined,stream,3); expect(joined,"tes");
    wd_m_str_drop(&joined);
    wd_m_stream_line(&ctx,&joined,stream,0); expect(joined,"");
    wd_m_str_drop(&joined);
    file_close_error = 5;
    wd_m_stream_close(&ctx,stream,1);
    assert(ctx.errors == 1 && strcmp(ctx.message,"stream close failed (OS error 5)") == 0);
    assert(file_handles == 0 && wd_m_stream_closed(stream));
    wd_m_release(stream);
    stream = wd_m_stream_open(&ctx,&path,&mode);
    unsigned primary = ctx.errors;
    wd_m_stream_close(&ctx,stream,1);
    assert(ctx.errors == primary && file_handles == 0); /* Preserve primary. */
    wd_m_release(stream);
    file_close_error = 0;
    ctx.errors = 0;
    for (WInt budget = 0; budget < 2; ++budget) {
        fail_after = budget;
        assert(!wd_m_stream_open(&ctx,&path,&mode));
        assert(file_handles == 0 && wd_m_live_count() == 0);
    }
    fail_after = -1;
    for (WInt budget = 0; budget < 5; ++budget) {
        ctx.errors = 0;
        stream = wd_m_stream_open(&ctx,&path,&mode);
        assert(stream && file_handles == 1);
        fail_after = budget;
        wd_m_stream_line(&ctx,&joined,stream,12);
        fail_after = -1;
        wd_m_str_drop(&joined);
        wd_m_stream_close(&ctx,stream,0); wd_m_release(stream);
        assert(file_handles == 0 && wd_m_live_count() == 0);
    }
    ctx.errors = 0;
    assert(remove("primitive-stream.bin") == 0);
    /* Every allocation prefix of a nested JSON parse/serialize must unwind.
       This exercises partial strings, arrays, maps, AVL nodes and error text. */
    WStr json_text=literal("{\"z\":[1,true,\"\\ud83d\\ude03\",null],\"a\":{\"x\":1.5}}");
    unsigned successes=0;
    for(WInt budget=0;budget<120;++budget) {
        ctx.errors=0; fail_after=budget;
        WJson *json=wd_m_json_parse(&ctx,&json_text); WStr text={0};
        if(!ctx.errors) wd_m_json_dump(&ctx,&text,json);
        if(!ctx.errors) { expect(text,"{\"a\":{\"x\":1.5},\"z\":[1,true,\"😃\",null]}"); ++successes; }
        fail_after=-1; wd_m_release(json); wd_m_str_drop(&text);
        assert(wd_m_live_count()==0 && wd_m_live_bytes()==0);
    }
    assert(successes>0);
    ctx.errors=0;
    WStr oversized={0}; wd_m_str_new(&ctx,&oversized,16777217);
    assert(oversized.owner && !ctx.errors);
    assert(!wd_m_json_parse(&ctx,&oversized));
    assert(ctx.errors==1 && strcmp(ctx.code,"R4012")==0 && strcmp(ctx.message,"JSON input exceeds 16 MiB")==0);
    wd_m_str_drop(&oversized);
    assert(wd_m_live_count() == 0 && wd_m_live_bytes() == 0);
    /* Numeric descriptors/storage are independent owners. Every failed detach
       must preserve the original descriptor, aliases, elements and live bytes. */
    WInt dimensions[2]={2,3},fill=7,coordinate[2]={0,0};
    for(WInt budget=0;budget<4;++budget) {
        ctx.errors=0; fail_after=budget;
        void *numeric=wd_m_array_full(&ctx,dimensions,2,&fill,0);
        fail_after=-1; wd_m_release(numeric);
        assert(wd_m_live_count()==0 && wd_m_live_bytes()==0);
    }
    for(WInt budget=0;budget<4;++budget) {
        ctx.errors=0;
        void *numeric=wd_m_array_full(&ctx,dimensions,2,&fill,0);
        void *alias=numeric; wd_m_retain(alias);
        assert(numeric && wd_m_live_count()==2);
        fail_after=budget;
        WInt *item=wd_m_array_edit(&ctx,&numeric,coordinate,2);
        fail_after=-1;
        if(item) { *item=99; assert(numeric!=alias); }
        else { assert(numeric==alias && ctx.errors); }
        assert(*(WInt*)wd_m_array_at(&ctx,alias,coordinate,2)==7);
        wd_m_release(numeric); wd_m_release(alias);
        assert(wd_m_live_count()==0 && wd_m_live_bytes()==0);
    }
    for(WInt budget=0;budget<4;++budget) {
        ctx.errors=0;
        void *borrowed=wd_m_array_full(&ctx,dimensions,2,&fill,0);
        void *snapshot=borrowed; wd_m_retain(snapshot);
        fail_after=budget;
        WInt *buffer=wd_m_array_borrow(&ctx,&borrowed,1);
        fail_after=-1;
        if(buffer) { buffer[0]=99; assert(borrowed!=snapshot); }
        else { assert(borrowed==snapshot && ctx.errors); }
        assert(*(WInt*)wd_m_array_at(&ctx,snapshot,coordinate,2)==7);
        wd_m_release(borrowed); wd_m_release(snapshot);
        assert(wd_m_live_count()==0 && wd_m_live_bytes()==0);
    }
    ctx.errors=0;
    void *numeric=wd_m_array_full(&ctx,dimensions,2,&fill,0);
    void *reverse=wd_m_array_slice(&ctx,numeric,1,2,-4,-1,1,1);
    assert(reverse && !wd_m_array_contiguous(reverse));
    fail_after=0;
    assert(!wd_m_array_borrow(&ctx,&reverse,0));
    assert(ctx.errors==1 && strcmp(ctx.code,"R4026")==0);
    ctx.errors=0;
    assert(!wd_m_array_borrow(&ctx,&reverse,1));
    assert(ctx.errors==1 && strcmp(ctx.code,"R4026")==0);
    ctx.errors=0; fail_after=-1;
    wd_m_release(numeric);
    /* A unique strided view can mutate without allocating or losing its owner. */
    fail_after=0;
    WInt *view_item=wd_m_array_edit(&ctx,&reverse,coordinate,2);
    fail_after=-1;
    assert(view_item && !ctx.errors); *view_item=41;
    assert(*(WInt*)wd_m_array_at(&ctx,reverse,coordinate,2)==41);
    wd_m_release(reverse);
    WInt empty_dims[3]={INT64_MAX,INT64_MAX,0};
    numeric=wd_m_array_full(&ctx,empty_dims,3,&fill,0);
    reverse=wd_m_array_slice(&ctx,numeric,0,0,0,1,0,0);
    assert(reverse && wd_m_array_len(reverse)==0 && !ctx.errors);
    wd_m_release(numeric); wd_m_release(reverse);
    WInt one_dim=1,large_fill=INT64_MAX,scalar=1;
    numeric=wd_m_array_full(&ctx,&one_dim,1,&large_fill,0);
    assert(!wd_m_array_binary(&ctx,numeric,2,&scalar,0,0));
    assert(ctx.errors && strcmp(ctx.code,"R4001")==0 && wd_m_live_count()==2);
    ctx.errors=0;
    for(WInt budget=0;budget<4;++budget) {
        fail_after=budget;
        void *converted=wd_m_array_convert(&ctx,numeric,1);
        fail_after=-1; wd_m_release(converted); ctx.errors=0;
        assert(wd_m_live_count()==2);
    }
    wd_m_release(numeric);
    assert(wd_m_live_count()==0 && wd_m_live_bytes()==0);
    /* Invalid shapes must be diagnosed even if no allocation can succeed. */
    WInt invalid_dimension=-1;
    ctx.errors=0; fail_after=0;
    assert(!wd_m_array_full(&ctx,&invalid_dimension,1,&fill,0));
    fail_after=-1;
    assert(ctx.errors==1 && strcmp(ctx.code,"R4026")==0);
    ctx.errors=0;
    numeric=wd_m_array_full(&ctx,&one_dim,1,&fill,0);
    WInt zero_divisor=0;
    fail_after=0;
    assert(!wd_m_array_binary(&ctx,numeric,2,&zero_divisor,0,3));
    fail_after=-1;
    assert(ctx.errors==1 && strcmp(ctx.code,"R4002")==0);
    ctx.errors=0;
    for(WInt budget=0;budget<20;++budget) {
        ctx.errors=0; fail_after=budget;
        WStr representation={0}; wd_m_array_str(&ctx,&representation,numeric);
        fail_after=-1; wd_m_str_drop(&representation);
        assert(wd_m_live_count()==2);
    }
    wd_m_release(numeric); ctx.errors=0;
    /* Whole-file reads close on every allocation failure and preserve bytes. */
    WStr read_path=literal("primitive-read.bin"),read_data=literal("binary\0tail");
    read_data.size=11;
    wd_m_write_text(&ctx,&read_path,&read_data);
    assert(!ctx.errors);
    successes=0;
    for(WInt budget=0;budget<12;++budget) {
        ctx.errors=0; fail_after=budget;
        WStr content={0}; wd_m_read_text(&ctx,&content,&read_path);
        fail_after=-1;
        if(!ctx.errors) { assert(content.size==11 && !memcmp(content.data,read_data.data,11)); ++successes; }
        wd_m_str_drop(&content);
        assert(wd_m_live_count()==0 && wd_m_live_bytes()==0);
    }
    assert(successes>0 && file_handles==0); assert(remove("primitive-read.bin")==0); ctx.errors=0;
    /* Deterministic binary64 mantissas/signs across the exponent space. This
       is a correctness oracle, never a throughput/performance benchmark. */
    uint64_t random_bits=UINT64_C(0xd87f021e5b4ac903);
    unsigned math_cases=0;
    double worst_units=0;
    for(unsigned trial=0;trial<1000000;++trial) {
        random_bits=random_bits*UINT64_C(6364136223846793005)+UINT64_C(1442695040888963407);
        union { uint64_t i; double f; } x={random_bits};
        if(!isfinite(x.f)) continue;
        double actual[2]={wd_m_math_sin(&ctx,x.f),wd_m_math_cos(&ctx,x.f)};
        double reference[2]={sin(x.f),cos(x.f)};
        assert(!ctx.errors);
        for(unsigned operation=0;operation<2;++operation) {
            double unit=fabs(nextafter(reference[operation],INFINITY)-reference[operation]);
            double units=fabs(actual[operation]-reference[operation])/unit;
            assert(isfinite(actual[operation]) && units<=2.0);
            if(units>worst_units) worst_units=units;
        }
        ++math_cases;
    }
    assert(signbit(wd_m_math_sin(&ctx,-0.0)));
    assert(wd_m_math_cos(&ctx,-0.0)==1.0);
    printf("native math oracle: %u finite inputs, maximum %.0f reference ULP\n",math_cases,worst_units);
    assert(wd_m_live_count()==0 && wd_m_live_bytes()==0);
    /* Exercise every allocation prefix of private bridge parsing, randomness,
       directory registration and response construction, then clean owned paths. */
    const char request[]="{\"op\":\"temp\",\"parent\":\".\"}";
    const char cleanup[]="{\"op\":\"cleanup\"}";
    WInt response[65536];
    for(WInt budget=0;budget<70;++budget) {
        fail_after=budget;
        WInt count=wood_driver_call(request,sizeof request-1);
        fail_after=-1;
        assert(count>0 && count<=65536);
        assert(wood_driver_response(response,count-1)==-1);
        assert(wood_driver_response(response,count)==count);
        count=wood_driver_call(cleanup,sizeof cleanup-1);
        assert(count>0 && count<=65536 && wood_driver_response(response,count)==count);
        if(wd_m_live_count() || wd_m_live_bytes() || file_handles)
            fprintf(stderr,"bridge allocation prefix %lld: %llu owners, %llu bytes, %u files\n",
                (long long)budget,(unsigned long long)wd_m_live_count(),
                (unsigned long long)wd_m_live_bytes(),file_handles);
        assert(wd_m_live_count()==0 && wd_m_live_bytes()==0 && file_handles==0);
    }
    puts("managed primitive ownership and file resource checks passed");
    return 0;
}
