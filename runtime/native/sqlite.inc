/* SQLite stable C ABI. Connections are thread-affine shared identities;
   statements/transactions retain their connection, never the reverse. The
   intrusive weak statement list permits immediate close without an RC cycle. */
typedef struct QDatabase QDatabase;
typedef struct QStatement QStatement;
typedef struct { WOwner owner; QDatabase *db; WSize id; int active; } QTransaction;
typedef void (*QDestructor)(void *);
typedef struct {
    void *library;
#define Q_API(name,result,...) result (*name)(__VA_ARGS__)
    Q_API(libversion,const char *,void);
    Q_API(libversion_number,int,void);
    Q_API(threadsafe,int,void);
    Q_API(open_v2,int,const char *,void **,int,const char *);
    Q_API(close_v2,int,void *);
    Q_API(errmsg,const char *,void *);
    Q_API(errcode,int,void *);
    Q_API(extended_result_codes,int,void *,int);
    Q_API(busy_timeout,int,void *,int);
    Q_API(progress_handler,void,void *,int,int (*)(void *),void *);
    Q_API(limit,int,void *,int,int);
    Q_API(db_config,int,void *,int,...);
    Q_API(prepare_v2,int,void *,const char *,int,void **,const char **);
    Q_API(finalize,int,void *);
    Q_API(step,int,void *);
    Q_API(reset,int,void *);
    Q_API(clear_bindings,int,void *);
    Q_API(bind_parameter_count,int,void *);
    Q_API(bind_int64,int,void *,int,WInt);
    Q_API(bind_double,int,void *,int,double);
    Q_API(bind_text,int,void *,int,const char *,int,QDestructor);
    Q_API(bind_blob,int,void *,int,const void *,int,QDestructor);
    Q_API(bind_null,int,void *,int);
    Q_API(column_count,int,void *);
    Q_API(column_name,const char *,void *,int);
    Q_API(column_type,int,void *,int);
    Q_API(column_int64,WInt,void *,int);
    Q_API(column_double,double,void *,int);
    Q_API(column_text,const unsigned char *,void *,int);
    Q_API(column_blob,const void *,void *,int);
    Q_API(column_bytes,int,void *,int);
    Q_API(changes64,WInt,void *);
    Q_API(last_insert_rowid,WInt,void *);
#undef Q_API
} QApi;
static QApi q_api;
static WLock q_load_lock;
static int q_loaded;
struct QDatabase {
    WOwner owner;
    void *handle;
    WInt thread,timeout;
    QStatement *statements;
    WSize transactions[64],next;
    int depth;
};
struct QStatement {
    WOwner owner;
    QDatabase *db;
    QStatement *previous,*next;
    void *handle;
    int phase,parameters;
    /* Explicitly initialized bound flags follow the object. */
};
static int q_load(void *ctx) {
    if(__atomic_load_n(&q_loaded,__ATOMIC_ACQUIRE)) return 1;
    w_lock(&q_load_lock);
    if(!q_loaded) {
        QApi api={0};
#ifdef _WIN32
        WStr env=d_literal("WOOD_SQLITE_DIR"),directory={0}; d_environment(ctx,&directory,&env);
        const char *name=directory.size?"sqlite3.dll":"winsqlite3.dll",*alternate=0; wd_m_str_drop(&directory);
#else
        const char *name="libsqlite3.so.0",*alternate="libsqlite3.so";
#endif
        if(!wd_rt_unwinding(ctx)) api.library=optional_open(ctx,"WOOD_SQLITE_DIR",name,alternate,"R4020");
        if(api.library) {
            int valid=1;
#define Q_LOAD(name) do { void *symbol=optional_symbol(api.library,"sqlite3_" #name); copy_bytes(&api.name,&symbol,sizeof symbol); if(!symbol) valid=0; } while(0)
            Q_LOAD(libversion); Q_LOAD(libversion_number); Q_LOAD(threadsafe);
            Q_LOAD(open_v2); Q_LOAD(close_v2); Q_LOAD(errmsg); Q_LOAD(errcode);
            Q_LOAD(extended_result_codes); Q_LOAD(busy_timeout); Q_LOAD(progress_handler);
            Q_LOAD(limit); Q_LOAD(db_config); Q_LOAD(prepare_v2); Q_LOAD(finalize);
            Q_LOAD(step); Q_LOAD(reset); Q_LOAD(clear_bindings); Q_LOAD(bind_parameter_count);
            Q_LOAD(bind_int64); Q_LOAD(bind_double); Q_LOAD(bind_text); Q_LOAD(bind_blob); Q_LOAD(bind_null);
            Q_LOAD(column_count); Q_LOAD(column_name); Q_LOAD(column_type); Q_LOAD(column_int64);
            Q_LOAD(column_double); Q_LOAD(column_text); Q_LOAD(column_blob); Q_LOAD(column_bytes);
            Q_LOAD(changes64); Q_LOAD(last_insert_rowid);
#undef Q_LOAD
            if(!valid) fail(ctx,"R4020","native library is missing a required SQLite ABI symbol");
            else if(api.libversion_number()<3038000 || !api.threadsafe())
                fail(ctx,"R4020","SQLite 3.38+ with thread safety is required");
            else { q_api=api; __atomic_store_n(&q_loaded,1,__ATOMIC_RELEASE); }
            if(!q_loaded) optional_close(api.library);
        }
    }
    int result=q_loaded; w_unlock(&q_load_lock); return result;
}
static int q_error(void *ctx,QDatabase *db,int result) {
    if(!result) return 1;
    if(wd_rt_unwinding(ctx)) return 0;
    WStr code={0},message={0}; wd_m_int_str(ctx,&code,result);
    JBuffer b={ctx,0}; jb_word(&b,"SQLite error "); jb_add(&b,code.data,code.size); jb_word(&b,": ");
    jb_word(&b,db->handle?q_api.errmsg(db->handle):"database is closed"); jb_finish(&b,&message);
    if(!wd_rt_unwinding(ctx)) wd_rt_error_value(ctx,"R4020",5,&message);
    wd_m_str_drop(&code); wd_m_str_drop(&message); return 0;
}
static int q_utf8(void *ctx,const WStr *text) {
    if(j_utf8(text)) return 1;
    fail(ctx,"R4020","SQLite text must be valid UTF-8; use a blob for arbitrary bytes"); return 0;
}
static int q_db_valid(void *ctx,QDatabase *db) {
    if(!db || !db->handle) { fail(ctx,"R4020","database is closed"); return 0; }
    if(db->thread!=wt_id()) { fail(ctx,"R4020","database belongs to another thread"); return 0; }
    return 1;
}
static int q_stmt_valid(void *ctx,QStatement *s) {
    if(!s) { fail(ctx,"R4020","statement is closed"); return 0; }
    if(!q_db_valid(ctx,s->db)) return 0;
    if(!s->handle) { fail(ctx,"R4020","statement is closed"); return 0; }
    return 1;
}
static void q_stmt_close(QStatement *s) {
    if(s && s->handle) { void *handle=s->handle; s->handle=0; (void)q_api.finalize(handle); }
}
static void q_db_close(QDatabase *db) {
    if(!db || !db->handle) return;
    for(QStatement *s=db->statements;s;s=s->next) q_stmt_close(s);
    db->depth=0; void *handle=db->handle; db->handle=0; (void)q_api.close_v2(handle);
}
static void q_db_destroy(void *value) { q_db_close(value); }
static void q_stmt_destroy(void *value) {
    QStatement *s=value; q_stmt_close(s);
    if(s->previous) s->previous->next=s->next; else s->db->statements=s->next;
    if(s->next) s->next->previous=s->previous;
    wd_m_release(s->db);
}
/* Internal SQL is fixed ASCII and contains no user bytes. Always finalize. */
static int q_control(QDatabase *db,const char *sql) {
    void *statement=0;
    int result=q_api.prepare_v2(db->handle,sql,-1,&statement,0);
    if(!result) { result=statement?q_api.step(statement):1; if(result==101) result=0; }
    if(statement) (void)q_api.finalize(statement);
    return result;
}
static int q_present(QTransaction *tx) {
    if(!tx || !tx->active || !tx->db->handle) return -1;
    for(int i=0;i<tx->db->depth;++i) if(tx->db->transactions[i]==tx->id) return i;
    return -1;
}
/* Decimal uint64 IDs avoid signed overflow, SQL injection and allocation in
   destructors. The fixed buffer covers the prefix and all 20 ID digits. */
static int q_transaction_sql(QTransaction *tx,const char *prefix) {
    char text[64],digits[20]; int n=0,k=0; WSize id=tx->id;
    while(prefix[n]) { text[n]=prefix[n]; ++n; }
    const char *name="wood_tx_"; for(int i=0;name[i];++i) text[n++]=name[i];
    do { digits[k++]=(char)('0'+id%10); id/=10; } while(id);
    while(k) text[n++]=digits[--k]; text[n]=0;
    return q_control(tx->db,text);
}
static void q_abandon(QTransaction *tx) {
    int index=q_present(tx); if(index<0) return;
    int result=q_transaction_sql(tx,"ROLLBACK TO ");
    if(!result) result=q_transaction_sql(tx,"RELEASE ");
    if(result) q_db_close(tx->db); else tx->db->depth=index;
    tx->active=0;
}
static void q_tx_destroy(void *value) { QTransaction *tx=value; q_abandon(tx); wd_m_release(tx->db); }
void *wd_m_db_open(void *ctx,const WStr *path,WInt timeout) {
    if(!path->size || path->size>32768 || wd_m_find(path,&(WStr){(unsigned char*)"\0",1,0})>=0 || timeout<1 || timeout>86400000) {
        fail(ctx,"R4020","database requires a nonempty path and timeout in 1..86400000 milliseconds"); return 0;
    }
    if(!q_utf8(ctx,path) || !q_load(ctx)) return 0;
    QDatabase *db=(QDatabase*)allocate(ctx,sizeof(QDatabase),q_db_destroy); if(!db) return 0;
    zero_bytes((unsigned char*)db+sizeof(WOwner),sizeof(QDatabase)-sizeof(WOwner)); db->thread=wt_id(); db->timeout=timeout;
    WStr terminated={0}; p_copy(ctx,&terminated,path->data,path->size);
    if(!wd_rt_unwinding(ctx)) {
        int result=q_api.open_v2((const char*)terminated.data,&db->handle,0x10006,0);
        if(q_error(ctx,db,result) && q_error(ctx,db,q_api.extended_result_codes(db->handle,1)) && q_error(ctx,db,q_api.busy_timeout(db->handle,(int)timeout))) {
            int categories[]={0,1,2,3,9},limits[]={16777216,1048576,256,128,1000},enabled=0;
            for(int i=0;i<5;++i) (void)q_api.limit(db->handle,categories[i],limits[i]);
            if(q_error(ctx,db,q_api.db_config(db->handle,1010,1,&enabled)) && q_error(ctx,db,q_api.db_config(db->handle,1017,0,&enabled)) && q_error(ctx,db,q_api.db_config(db->handle,1005,0,&enabled)))
                (void)q_error(ctx,db,q_control(db,"PRAGMA foreign_keys=ON"));
        }
    }
    wd_m_str_drop(&terminated);
    if(wd_rt_unwinding(ctx)) { wd_m_release(db); return 0; }
    return db;
}
void wd_m_db_check(void *ctx,void *db) { (void)q_db_valid(ctx,db); }
WInt wd_m_db_closed(void *ctx,void *db) { (void)ctx; return !db || !((QDatabase*)db)->handle; }
void wd_m_db_close(void *ctx,void *value) { QDatabase *db=value; if(db && db->handle && q_db_valid(ctx,db)) q_db_close(db); }
WInt wd_m_db_changes(void *ctx,void *db) { return q_db_valid(ctx,db)?q_api.changes64(((QDatabase*)db)->handle):0; }
WInt wd_m_db_last_id(void *ctx,void *db) { return q_db_valid(ctx,db)?q_api.last_insert_rowid(((QDatabase*)db)->handle):0; }
void wd_m_db_version(void *ctx,WStr *out) { *out=(WStr){0}; if(q_load(ctx)) { WStr v=d_literal(q_api.libversion()); p_copy(ctx,out,v.data,v.size); } }
void *wd_m_db_prepare(void *ctx,void *value,const WStr *sql) {
    QDatabase *db=value; if(!q_db_valid(ctx,db)) return 0;
    if(!sql->size || sql->size>1048576 || wd_m_find(sql,&(WStr){(unsigned char*)"\0",1,0})>=0) {
        fail(ctx,"R4020","SQL must contain one statement, no NUL, and at most 1048576 bytes"); return 0;
    }
    if(!q_utf8(ctx,sql)) return 0;
    WStr terminated={0}; p_copy(ctx,&terminated,sql->data,sql->size); if(wd_rt_unwinding(ctx)) return 0;
    void *handle=0; const char *tail=0;
    int result=q_api.prepare_v2(db->handle,(const char*)terminated.data,(int)sql->size+1,&handle,&tail);
    if(q_error(ctx,db,result) && !handle) fail(ctx,"R4020","SQL must contain one statement");
    while(!wd_rt_unwinding(ctx) && tail && *tail) {
        void *extra=0; const char *next=0;
        result=q_api.prepare_v2(db->handle,tail,-1,&extra,&next); int another=extra!=0;
        if(extra) (void)q_api.finalize(extra);
        if(!q_error(ctx,db,result)) break;
        if(another) { fail(ctx,"R4020","multiple SQL statements are not permitted"); break; }
        if(next==tail) break; tail=next;
    }
    wd_m_str_drop(&terminated);
    if(wd_rt_unwinding(ctx)) { if(handle) (void)q_api.finalize(handle); return 0; }
    int count=q_api.bind_parameter_count(handle);
    if(count<0 || count>1000) { (void)q_api.finalize(handle); fail(ctx,"R4020","SQLite parameter limit exceeded"); return 0; }
    QStatement *s=(QStatement*)allocate(ctx,sizeof(QStatement)+count,q_stmt_destroy);
    if(!s) { (void)q_api.finalize(handle); return 0; }
    zero_bytes((unsigned char*)s+sizeof(WOwner),sizeof(QStatement)-sizeof(WOwner)+(WSize)count);
    s->db=db; wd_m_retain(db); s->handle=handle; s->parameters=count;
    s->next=db->statements; if(s->next) s->next->previous=s; db->statements=s;
    return s;
}
void wd_m_stmt_check(void *ctx,void *s) { (void)q_stmt_valid(ctx,s); }
WInt wd_m_stmt_closed(void *s) { return !s || !((QStatement*)s)->handle; }
void wd_m_stmt_close(void *ctx,void *s) { (void)ctx; q_stmt_close(s); }
typedef struct { void *ctx; WInt deadline; } QProgress;
static int q_interrupt(void *value) {
    QProgress *p=value;
    return wd_m_task_cancelled() || wd_m_time_ns(p->ctx)>=p->deadline || wd_rt_unwinding(p->ctx);
}
WInt wd_m_stmt_step(void *ctx,void *value) {
    QStatement *s=value; if(!q_stmt_valid(ctx,s)) return 0;
    if(s->phase==2) return 0;
    if(s->phase==3) { fail(ctx,"R4020","reset a failed statement before stepping"); return 0; }
    unsigned char *bound=(unsigned char*)(s+1);
    for(int i=0;i<s->parameters;++i) if(!bound[i]) { fail(ctx,"R4020","every SQL parameter must be explicitly bound"); return 0; }
    if(wd_m_task_cancelled()) { fail(ctx,"R4020","database operation cancelled"); return 0; }
    WInt now=wd_m_time_ns(ctx),duration=s->db->timeout*1000000;
    if(wd_rt_unwinding(ctx)) return 0;
    QProgress progress={ctx,now>INT64_MAX-duration?INT64_MAX:now+duration};
    q_api.progress_handler(s->db->handle,1000,q_interrupt,&progress);
    int result=q_api.step(s->handle);
    q_api.progress_handler(s->db->handle,0,0,0);
    s->phase=result==100?1:result==101?2:3;
    if(result!=100 && result!=101) (void)q_error(ctx,s->db,result);
    return result==100;
}
void wd_m_stmt_reset(void *ctx,void *value) {
    QStatement *s=value; if(!q_stmt_valid(ctx,s)) return;
    (void)q_api.reset(s->handle);
    if(q_error(ctx,s->db,q_api.clear_bindings(s->handle))) { zero_bytes(s+1,(WSize)s->parameters); s->phase=0; }
}
static int q_parameter(void *ctx,QStatement *s,WInt index) {
    if(!q_stmt_valid(ctx,s)) return 0;
    if(s->phase) { fail(ctx,"R4020","reset a statement before rebinding"); return 0; }
    if(index<1 || index>s->parameters) { fail(ctx,"R4020","parameter index is outside 1..parameter_count"); return 0; }
    return (int)index;
}
static void q_bound(void *ctx,QStatement *s,int index,int result) { if(q_error(ctx,s->db,result)) ((unsigned char*)(s+1))[index-1]=1; }
void wd_m_stmt_bind_int(void *ctx,void *value,WInt index,WInt number) { QStatement *s=value; int i=q_parameter(ctx,s,index); if(i) q_bound(ctx,s,i,q_api.bind_int64(s->handle,i,number)); }
void wd_m_stmt_bind_float(void *ctx,void *value,WInt index,double number) {
    if((n_bits(number)&UINT64_C(0x7fffffffffffffff))>=UINT64_C(0x7ff0000000000000)) { fail(ctx,"R4020","SQLite float binding must be finite"); return; }
    QStatement *s=value; int i=q_parameter(ctx,s,index); if(i) q_bound(ctx,s,i,q_api.bind_double(s->handle,i,number));
}
static void q_bind_bytes(void *ctx,QStatement *s,WInt index,const WStr *bytes,int text) {
    if(bytes->size>16777216) { fail(ctx,"R4020","SQLite value exceeds 16777216 bytes"); return; }
    if(text && !q_utf8(ctx,bytes)) return;
    int i=q_parameter(ctx,s,index); if(!i) return;
    const char *pointer=bytes->size?(const char*)bytes->data:""; /* Empty blob is not NULL. */
    QDestructor transient=(QDestructor)(intptr_t)-1;
    int result=text?q_api.bind_text(s->handle,i,pointer,(int)bytes->size,transient):q_api.bind_blob(s->handle,i,pointer,(int)bytes->size,transient);
    q_bound(ctx,s,i,result);
}
void wd_m_stmt_bind_text(void *ctx,void *s,WInt i,const WStr *bytes) { q_bind_bytes(ctx,s,i,bytes,1); }
void wd_m_stmt_bind_blob(void *ctx,void *s,WInt i,const WStr *bytes) { q_bind_bytes(ctx,s,i,bytes,0); }
void wd_m_stmt_bind_null(void *ctx,void *value,WInt index) { QStatement *s=value; int i=q_parameter(ctx,s,index); if(i) q_bound(ctx,s,i,q_api.bind_null(s->handle,i)); }
WInt wd_m_stmt_columns(void *ctx,void *s) { return q_stmt_valid(ctx,s)?q_api.column_count(((QStatement*)s)->handle):0; }
static int q_column(void *ctx,QStatement *s,WInt index,int row) {
    if(!q_stmt_valid(ctx,s)) return -1;
    if(index<0 || index>=q_api.column_count(s->handle)) { fail(ctx,"R4020","column index is outside 0..column_count-1"); return -1; }
    if(row && s->phase!=1) { fail(ctx,"R4020","column access requires a current row"); return -1; }
    return (int)index;
}
void wd_m_stmt_name(void *ctx,WStr *out,void *value,WInt index) {
    *out=(WStr){0}; QStatement *s=value; int i=q_column(ctx,s,index,0); if(i<0) return;
    const char *name=q_api.column_name(s->handle,i);
    if(!name) { fail(ctx,"R4020","cannot read SQLite column name"); return; }
    WStr str=d_literal(name); p_copy(ctx,out,str.data,str.size);
}
void wd_m_stmt_kind(void *ctx,WStr *out,void *value,WInt index) {
    *out=(WStr){0}; QStatement *s=value; int i=q_column(ctx,s,index,1); if(i<0) return;
    int kind=q_api.column_type(s->handle,i); const char *names[]={"null","int","float","str","blob"};
    WStr text=d_literal(names[kind>=1 && kind<=4?kind:0]); str_copy(out,&text);
}
WInt wd_m_stmt_int(void *ctx,void *value,WInt index) {
    QStatement *s=value; int i=q_column(ctx,s,index,1); if(i<0) return 0;
    if(q_api.column_type(s->handle,i)!=1) { fail(ctx,"R4020","SQLite column is not an integer"); return 0; }
    return q_api.column_int64(s->handle,i);
}
double wd_m_stmt_float(void *ctx,void *value,WInt index) {
    QStatement *s=value; int i=q_column(ctx,s,index,1); if(i<0) return 0;
    int kind=q_api.column_type(s->handle,i);
    if(kind!=1 && kind!=2) { fail(ctx,"R4020","SQLite column is not numeric"); return 0; }
    double result=q_api.column_double(s->handle,i);
    if((n_bits(result)&UINT64_C(0x7fffffffffffffff))>=UINT64_C(0x7ff0000000000000)) { fail(ctx,"R4020","SQLite column is not finite"); return 0; }
    return result;
}
static void q_bytes(void *ctx,WStr *out,QStatement *s,WInt index,int text) {
    *out=(WStr){0}; int i=q_column(ctx,s,index,1); if(i<0) return;
    if(q_api.column_type(s->handle,i)!=(text?3:4)) { fail(ctx,"R4020","SQLite column has a different kind"); return; }
    const void *bytes=text?(const void*)q_api.column_text(s->handle,i):q_api.column_blob(s->handle,i);
    int length=q_api.column_bytes(s->handle,i);
    if(length<0 || length>16777216 || (!bytes && (text || length || q_api.errcode(s->db->handle)==7))) { fail(ctx,"R4020","cannot read SQLite column bytes"); return; }
    p_copy(ctx,out,bytes,length);
    if(!wd_rt_unwinding(ctx) && text && !q_utf8(ctx,out)) { wd_m_str_drop(out); *out=(WStr){0}; }
}
void wd_m_stmt_text(void *ctx,WStr *out,void *s,WInt index) { q_bytes(ctx,out,s,index,1); }
void wd_m_stmt_blob(void *ctx,WStr *out,void *s,WInt index) { q_bytes(ctx,out,s,index,0); }
void *wd_m_db_begin(void *ctx,void *value) {
    QDatabase *db=value; if(!q_db_valid(ctx,db)) return 0;
    if(db->depth>=64) { fail(ctx,"R4020","transaction nesting exceeds 64"); return 0; }
    if(db->next==UINT64_MAX) { fail(ctx,"R4020","transaction identity exhausted"); return 0; }
    QTransaction *tx=(QTransaction*)allocate(ctx,sizeof(QTransaction),q_tx_destroy); if(!tx) return 0;
    tx->db=db; wd_m_retain(db); tx->id=++db->next; tx->active=0;
    if(!q_error(ctx,db,q_transaction_sql(tx,"SAVEPOINT "))) { wd_m_release(tx); return 0; }
    db->transactions[db->depth++]=tx->id; tx->active=1; return tx;
}
WInt wd_m_transaction_closed(void *tx) { return q_present(tx)<0; }
void wd_m_transaction_check(void *ctx,void *value) {
    QTransaction *tx=value;
    if(q_present(tx)<0) fail(ctx,"R4020","transaction is closed");
}
static void q_finish(void *ctx,QTransaction *tx,int commit) {
    int i=q_present(tx); if(i<0) { if(tx) tx->active=0; return; }
    QDatabase *db=tx->db; if(!q_db_valid(ctx,db)) return;
    if(i!=db->depth-1) { fail(ctx,"R4020","transactions must finish in reverse scope order"); return; }
    if(!commit && !q_error(ctx,db,q_transaction_sql(tx,"ROLLBACK TO "))) return;
    if(!q_error(ctx,db,q_transaction_sql(tx,"RELEASE "))) return;
    --db->depth; tx->active=0;
}
void wd_m_db_commit(void *ctx,void *tx) { wd_m_transaction_check(ctx,tx); if(!wd_rt_unwinding(ctx)) q_finish(ctx,tx,1); }
void wd_m_db_rollback(void *ctx,void *tx) { q_finish(ctx,tx,0); }
/* Scoped exceptional exits and destructor cleanup cannot replace a pending
   error/exit. Explicit rollback still reports order and database failures. */
void wd_m_sqlite_scope_close(void *ctx,void *value,WInt kind) {
    if(kind==0) { if(wd_rt_unwinding(ctx)) q_db_close(value); else wd_m_db_close(ctx,value); }
    else if(kind==1) q_stmt_close(value);
    else if(wd_rt_unwinding(ctx)) q_abandon(value);
    else wd_m_db_rollback(ctx,value);
}
