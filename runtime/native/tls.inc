/* Optional OpenSSL 3 stable C ABI. BIO callbacks do only nonblocking socket I/O:
   no Wood callback, checked error or unwind crosses the external C boundary.
   The BIO points at a separately owned stable transport, never a stream slot. */
typedef int (*TPassword)(char *,int,int,void *);
typedef struct {
    void *crypto,*ssl,*method;
#define T_API(name,result,...) result (*name)(__VA_ARGS__)
    T_API(OpenSSL_version_num,unsigned long,void);
    T_API(OpenSSL_version,const char *,int);
    T_API(ERR_clear_error,void,void);
    T_API(ERR_get_error,unsigned long,void);
    T_API(ERR_error_string_n,void,unsigned long,char *,WSize);
    T_API(BIO_new_mem_buf,void *,const void *,int);
    T_API(BIO_free,int,void *);
    T_API(PEM_read_bio_X509,void *,void *,void **,TPassword,void *);
    T_API(PEM_read_bio_PrivateKey,void *,void *,void **,TPassword,void *);
    T_API(X509_free,void,void *);
    T_API(EVP_PKEY_free,void,void *);
    T_API(X509_STORE_add_cert,int,void *,void *);
    T_API(X509_VERIFY_PARAM_set1_ip_asc,int,void *,const char *);
    T_API(TLS_method,const void *,void);
    T_API(SSL_CTX_new,void *,const void *);
    T_API(SSL_CTX_free,void,void *);
    T_API(SSL_CTX_ctrl,long,void *,int,long,void *);
    T_API(SSL_CTX_set_options,uint64_t,void *,uint64_t);
    T_API(SSL_CTX_set_security_level,void,void *,int);
    T_API(SSL_CTX_set_verify,void,void *,int,int (*)(int,void *));
    T_API(SSL_CTX_set_default_verify_paths,int,void *);
    T_API(SSL_CTX_get_cert_store,void *,const void *);
    T_API(SSL_CTX_use_certificate,int,void *,void *);
    T_API(SSL_CTX_use_PrivateKey,int,void *,void *);
    T_API(SSL_CTX_check_private_key,int,const void *);
    T_API(SSL_new,void *,void *);
    T_API(SSL_free,void,void *);
    T_API(SSL_set_bio,void,void *,void *,void *);
    T_API(SSL_ctrl,long,void *,int,long,void *);
    T_API(SSL_set1_host,int,void *,const char *);
    T_API(SSL_set_hostflags,void,void *,unsigned);
    T_API(SSL_get0_param,void *,void *);
    T_API(SSL_connect,int,void *);
    T_API(SSL_accept,int,void *);
    T_API(SSL_read_ex,int,void *,void *,WSize,WSize *);
    T_API(SSL_write_ex,int,void *,const void *,WSize,WSize *);
    T_API(SSL_get_error,int,const void *,int);
    T_API(SSL_shutdown,int,void *);
    T_API(BIO_get_new_index,int,void);
    T_API(BIO_meth_new,void *,int,const char *);
    T_API(BIO_meth_free,void,void *);
    T_API(BIO_meth_set_write,int,void *,int (*)(void *,const char *,int));
    T_API(BIO_meth_set_read,int,void *,int (*)(void *,char *,int));
    T_API(BIO_meth_set_ctrl,int,void *,long (*)(void *,int,long,void *));
    T_API(BIO_meth_set_create,int,void *,int (*)(void *));
    T_API(BIO_new,void *,const void *);
    T_API(BIO_set_data,void,void *,void *);
    T_API(BIO_get_data,void *,void *);
    T_API(BIO_set_init,void,void *,int);
    T_API(BIO_set_flags,void,void *,int);
    T_API(BIO_clear_flags,void,void *,int);
#undef T_API
} TApi;
static TApi t_api;
static WLock t_load_lock;
static int t_loaded;
typedef struct { WOwner owner; void *handle; } TContext;
typedef struct { WOwner owner; WInt fd; void *ssl; int sent_shutdown,failed; } TTransport;
static int t_bio_create(void *bio) { t_api.BIO_set_init(bio,1); return 1; }
static long t_bio_ctrl(void *bio,int command,long argument,void *pointer) {
    (void)bio; (void)argument; (void)pointer; return command==11?1L:0L;
}
static int t_bio_read(void *bio,char *bytes,int length) {
    TTransport *t=t_api.BIO_get_data(bio); t_api.BIO_clear_flags(bio,15);
    for(;;) {
        NResult result=n_api.recv((NSocket)t->fd,bytes,(NCount)length,0);
        if(result>=0) return (int)result;
        int code=n_errno(); if(n_interrupted(code)) continue;
        if(n_pending(code)) t_api.BIO_set_flags(bio,9); /* READ | SHOULD_RETRY */
        return -1;
    }
}
static int t_bio_write(void *bio,const char *bytes,int length) {
    TTransport *t=t_api.BIO_get_data(bio); t_api.BIO_clear_flags(bio,15);
    for(;;) {
#ifdef _WIN32
        int flags=0;
#else
        int flags=0x4000; /* Linux MSG_NOSIGNAL; no process signal-policy change. */
#endif
        NResult result=n_api.send((NSocket)t->fd,bytes,(NCount)length,flags);
        if(result>=0) return (int)result;
        int code=n_errno(); if(n_interrupted(code)) continue;
        if(n_pending(code)) t_api.BIO_set_flags(bio,10); /* WRITE | SHOULD_RETRY */
        return -1;
    }
}
static int t_load(void *ctx) {
    if(__atomic_load_n(&t_loaded,__ATOMIC_ACQUIRE)) return 1;
    w_lock(&t_load_lock);
    if(!t_loaded) {
        TApi a={0};
#ifdef _WIN32
        const char *crypto="libcrypto-3-x64.dll",*ssl="libssl-3-x64.dll";
#else
        const char *crypto="libcrypto.so.3",*ssl="libssl.so.3";
#endif
        a.crypto=optional_open(ctx,"WOOD_OPENSSL_DIR",crypto,0,"R4021");
        if(a.crypto) a.ssl=optional_open(ctx,"WOOD_OPENSSL_DIR",ssl,0,"R4021");
        if(a.ssl) {
            int valid=1;
#define T_LOAD(library,name) do { void *p=optional_symbol(a.library,#name); copy_bytes(&a.name,&p,sizeof p); if(!p) valid=0; } while(0)
            T_LOAD(crypto,OpenSSL_version_num); T_LOAD(crypto,OpenSSL_version);
            T_LOAD(crypto,ERR_clear_error); T_LOAD(crypto,ERR_get_error); T_LOAD(crypto,ERR_error_string_n);
            T_LOAD(crypto,BIO_new_mem_buf); T_LOAD(crypto,BIO_free);
            T_LOAD(crypto,PEM_read_bio_X509); T_LOAD(crypto,PEM_read_bio_PrivateKey);
            T_LOAD(crypto,X509_free); T_LOAD(crypto,EVP_PKEY_free); T_LOAD(crypto,X509_STORE_add_cert);
            T_LOAD(crypto,X509_VERIFY_PARAM_set1_ip_asc);
            T_LOAD(ssl,TLS_method); T_LOAD(ssl,SSL_CTX_new); T_LOAD(ssl,SSL_CTX_free);
            T_LOAD(ssl,SSL_CTX_ctrl); T_LOAD(ssl,SSL_CTX_set_options); T_LOAD(ssl,SSL_CTX_set_security_level);
            T_LOAD(ssl,SSL_CTX_set_verify); T_LOAD(ssl,SSL_CTX_set_default_verify_paths);
            T_LOAD(ssl,SSL_CTX_get_cert_store); T_LOAD(ssl,SSL_CTX_use_certificate);
            T_LOAD(ssl,SSL_CTX_use_PrivateKey); T_LOAD(ssl,SSL_CTX_check_private_key);
            T_LOAD(ssl,SSL_new); T_LOAD(ssl,SSL_free); T_LOAD(ssl,SSL_set_bio); T_LOAD(ssl,SSL_ctrl);
            T_LOAD(ssl,SSL_set1_host); T_LOAD(ssl,SSL_set_hostflags); T_LOAD(ssl,SSL_get0_param);
            T_LOAD(ssl,SSL_connect); T_LOAD(ssl,SSL_accept); T_LOAD(ssl,SSL_read_ex); T_LOAD(ssl,SSL_write_ex);
            T_LOAD(ssl,SSL_get_error); T_LOAD(ssl,SSL_shutdown);
            T_LOAD(crypto,BIO_get_new_index); T_LOAD(crypto,BIO_meth_new); T_LOAD(crypto,BIO_meth_free);
            T_LOAD(crypto,BIO_meth_set_write); T_LOAD(crypto,BIO_meth_set_read); T_LOAD(crypto,BIO_meth_set_ctrl);
            T_LOAD(crypto,BIO_meth_set_create); T_LOAD(crypto,BIO_new); T_LOAD(crypto,BIO_set_data);
            T_LOAD(crypto,BIO_get_data); T_LOAD(crypto,BIO_set_init); T_LOAD(crypto,BIO_set_flags); T_LOAD(crypto,BIO_clear_flags);
#undef T_LOAD
            if(!valid) fail(ctx,"R4021","native library is missing a required OpenSSL ABI symbol");
            else if((a.OpenSSL_version_num()>>28)!=3) fail(ctx,"R4021","OpenSSL major version 3 is required");
            else {
                int index=a.BIO_get_new_index();
                if(index!=-1) a.method=a.BIO_meth_new(index|0x400,"Wood nonblocking socket");
                if(!a.method || !a.BIO_meth_set_read(a.method,t_bio_read) || !a.BIO_meth_set_write(a.method,t_bio_write) ||
                   !a.BIO_meth_set_create(a.method,t_bio_create) || !a.BIO_meth_set_ctrl(a.method,t_bio_ctrl))
                    fail(ctx,"R4021","cannot initialize native TLS socket BIO");
            }
            if(!wd_rt_unwinding(ctx)) { t_api=a; __atomic_store_n(&t_loaded,1,__ATOMIC_RELEASE); }
            else if(a.method) a.BIO_meth_free(a.method);
        }
        if(!t_loaded) { optional_close(a.ssl); optional_close(a.crypto); }
    }
    int result=t_loaded; w_unlock(&t_load_lock); return result;
}
static void t_error(void *ctx,const char *action) {
    if(wd_rt_unwinding(ctx)) return;
    char detail[256]={0}; t_api.ERR_error_string_n(t_api.ERR_get_error(),detail,sizeof(detail));
    JBuffer b={ctx,0}; WStr message={0}; jb_word(&b,action); jb_word(&b,": "); jb_word(&b,detail); jb_finish(&b,&message);
    if(!wd_rt_unwinding(ctx)) wd_rt_error_value(ctx,"R4021",5,&message);
    wd_m_str_drop(&message);
}
static int t_check(void *ctx,int status,const char *action) {
    if(status==1) return 1;
    t_error(ctx,action); return 0;
}
static void t_context_destroy(void *value) {
    TContext *c=value; if(c->handle) t_api.SSL_CTX_free(c->handle);
}
WInt wd_m_tls_context_closed(void *value) { TContext *c=value; return !c || !c->handle; }
void wd_m_tls_context_check(void *ctx,void *value) {
    if(wd_m_tls_context_closed(value)) fail(ctx,"R4021","TLS context is closed");
}
void wd_m_tls_context_close(void *ctx,void *value) {
    (void)ctx; TContext *c=value;
    if(c && c->handle) { void *p=c->handle; c->handle=0; t_api.SSL_CTX_free(p); }
}
static TContext *t_context(void *ctx) {
    TContext *c=(TContext*)allocate(ctx,sizeof(TContext),t_context_destroy); if(!c) return 0;
    c->handle=t_api.SSL_CTX_new(t_api.TLS_method());
    if(!c->handle) t_error(ctx,"create TLS context");
    else if(t_api.SSL_CTX_ctrl(c->handle,123,0x0303,0)!=1) t_error(ctx,"set TLS 1.2 minimum");
    else {
        t_api.SSL_CTX_set_security_level(c->handle,2);
        t_api.SSL_CTX_set_options(c->handle,(UINT64_C(1)<<17)|(UINT64_C(1)<<30));
    }
    if(wd_rt_unwinding(ctx)) { wd_m_release(c); return 0; }
    return c;
}
static void t_pem(void *ctx,WStr *out,const WStr *path) {
    *out=(WStr){0};
    for(WInt i=0;i<path->size;++i) if(!path->data[i]) { fail(ctx,"R4021","PEM path contains NUL"); return; }
    WStr terminated={0}; p_copy(ctx,&terminated,path->data,path->size); if(wd_rt_unwinding(ctx)) return;
    WInt fd=wd_file_open(terminated.data,terminated.size,0); wd_m_str_drop(&terminated);
    if(fd<0) { fail(ctx,fd==-84?"R4008":"R4021",fd==-84?"invalid filesystem path":"cannot open PEM file"); return; }
    wd_m_str_new(ctx,out,1048577); WInt size=0;
    while(!wd_rt_unwinding(ctx) && size<1048577) {
        WInt n=wd_file_read(fd,out->data+size,1048577-size);
        if(n==-4) continue;
        if(n<0) { fail(ctx,"R4021","cannot read PEM file"); break; }
        if(!n) break;
        size+=n;
    }
    (void)wd_file_close(fd);
    if(!wd_rt_unwinding(ctx) && (!size || size>1048576)) fail(ctx,"R4021","PEM file must contain 1..1048576 bytes");
    if(wd_rt_unwinding(ctx)) { wd_m_str_drop(out); *out=(WStr){0}; }
    else { out->size=size; out->data[size]=0; }
}
/* Strict whitespace-separated PEM blocks; never accept arbitrary trailing data. */
static int t_no_password(char *bytes,int size,int writing,void *data) { (void)bytes;(void)size;(void)writing;(void)data; return -1; }
static void t_certificates(void *ctx,TContext *context,const WStr *path,int server) {
    WStr bytes={0}; t_pem(ctx,&bytes,path); WInt offset=0,count=0;
    WStr begin=d_literal("-----BEGIN CERTIFICATE-----"),end=d_literal("-----END CERTIFICATE-----");
    while(offset<bytes.size && !wd_rt_unwinding(ctx)) {
        while(offset<bytes.size && (bytes.data[offset]==' ' || bytes.data[offset]=='\t' || bytes.data[offset]=='\r' || bytes.data[offset]=='\n')) ++offset;
        if(offset==bytes.size) break;
        WStr part={bytes.data+offset,bytes.size-offset,0}; WInt last=wd_m_find(&part,&end);
        if(wd_m_find(&part,&begin)!=0 || last<0 || ++count>512) {
            fail(ctx,"R4021","invalid PEM certificate bundle (maximum 512 certificates)"); break;
        }
        WInt length=last+end.size; void *bio=t_api.BIO_new_mem_buf(part.data,(int)length);
        if(!bio) { t_error(ctx,"read certificate PEM"); break; }
        void *cert=t_api.PEM_read_bio_X509(bio,0,t_no_password,0); t_api.BIO_free(bio);
        if(!cert) { t_error(ctx,"parse certificate PEM"); break; }
        if(!server) t_check(ctx,t_api.X509_STORE_add_cert(t_api.SSL_CTX_get_cert_store(context->handle),cert),"load trusted certificate");
        else if(count==1) t_check(ctx,t_api.SSL_CTX_use_certificate(context->handle,cert),"install server certificate");
        else t_check(ctx,(int)t_api.SSL_CTX_ctrl(context->handle,89,1,cert),"install server certificate chain");
        t_api.X509_free(cert); offset+=length;
    }
    if(!count && !wd_rt_unwinding(ctx)) fail(ctx,"R4021","PEM bundle contains no certificates");
    wd_m_str_drop(&bytes);
}
void *wd_m_tls_context_new(void *ctx,const WStr *certificate,const WStr *key) {
    if(!t_load(ctx)) return 0;
    t_api.ERR_clear_error(); TContext *c=t_context(ctx); if(!c) return 0;
    t_certificates(ctx,c,certificate,1); WStr bytes={0};
    if(!wd_rt_unwinding(ctx)) t_pem(ctx,&bytes,key);
    if(!wd_rt_unwinding(ctx)) {
        void *bio=t_api.BIO_new_mem_buf(bytes.data,(int)bytes.size);
        if(!bio) t_error(ctx,"read private key PEM");
        else {
            void *private_key=t_api.PEM_read_bio_PrivateKey(bio,0,t_no_password,0); t_api.BIO_free(bio);
            if(!private_key) t_error(ctx,"parse unencrypted private key PEM");
            else { int ok=t_api.SSL_CTX_use_PrivateKey(c->handle,private_key); t_api.EVP_PKEY_free(private_key); t_check(ctx,ok,"install private key"); }
        }
    }
    wd_m_str_drop(&bytes);
    if(!wd_rt_unwinding(ctx)) t_check(ctx,t_api.SSL_CTX_check_private_key(c->handle),"certificate/private key mismatch");
    if(wd_rt_unwinding(ctx)) { wd_m_release(c); return 0; }
    return c;
}
static void t_destroy(void *value) { TTransport *t=value; if(t->ssl) t_api.SSL_free(t->ssl); }
static TTransport *t_transport(void *ctx,TContext *context,WInt fd) {
    TTransport *t=(TTransport*)allocate(ctx,sizeof(TTransport),t_destroy); if(!t) return 0;
    t->fd=fd; t->sent_shutdown=t->failed=0; t->ssl=t_api.SSL_new(context->handle);
    if(!t->ssl) t_error(ctx,"create TLS connection");
    else {
        void *bio=t_api.BIO_new(t_api.method);
        if(!bio) t_error(ctx,"create socket BIO");
        else { t_api.BIO_set_data(bio,t); t_api.SSL_set_bio(t->ssl,bio,bio); }
    }
    if(wd_rt_unwinding(ctx)) { wd_m_release(t); return 0; }
    return t;
}
static int t_retry(void *ctx,TTransport *t,int result,WInt until,const char *action,int eof) {
    /* Must be the next OpenSSL call, on this same registered OS thread. */
    int status=t_api.SSL_get_error(t->ssl,result);
    if(status==2 || status==3) return n_wait(ctx,t->fd,status==3,until);
    if(status==6 && eof) return 0; /* Only authenticated close_notify is EOF. */
    t->failed=1; t_error(ctx,action); return 0;
}
static void t_handshake(void *ctx,TTransport *t,int server,WInt until) {
    while(n_check(ctx,until)) {
        t_api.ERR_clear_error(); int result=server?t_api.SSL_accept(t->ssl):t_api.SSL_connect(t->ssl);
        if(result==1) return;
        if(!t_retry(ctx,t,result,until,"TLS handshake failed",0)) return;
    }
}
static WInt t_read(void *ctx,WStream *s,void *bytes,WInt count,WInt until) {
    TTransport *t=s->secure;
    if(t->failed) { fail(ctx,"R4021","TLS connection has failed"); return 0; }
    while(n_check(ctx,until)) {
        t_api.ERR_clear_error(); WSize received=0; int result=t_api.SSL_read_ex(t->ssl,bytes,(WSize)count,&received);
        if(result==1) return (WInt)received;
        if(!t_retry(ctx,t,result,until,"TLS read failed",1)) return 0;
    }
    return 0;
}
static WInt t_write(void *ctx,WStream *s,const void *bytes,WInt count,WInt until) {
    TTransport *t=s->secure;
    if(t->failed) { fail(ctx,"R4021","TLS connection has failed"); return 0; }
    while(n_check(ctx,until)) {
        t_api.ERR_clear_error(); WSize sent=0; int result=t_api.SSL_write_ex(t->ssl,bytes,(WSize)count,&sent);
        if(result==1 && sent) return (WInt)sent;
        if(!t_retry(ctx,t,result,until,"TLS write failed",0)) return 0;
    }
    return 0;
}
static void t_shutdown(void *ctx,WStream *s,WInt until) {
    TTransport *t=s->secure;
    if(t->sent_shutdown || t->failed) return;
    while(n_check(ctx,until)) {
        t_api.ERR_clear_error(); int result=t_api.SSL_shutdown(t->ssl);
        if(result>=0) { t->sent_shutdown=1; return; }
        if(!t_retry(ctx,t,result,until,"TLS shutdown failed",0)) return;
    }
}
static void t_finish(void *ctx,WStream *s) {
    TTransport *t=s->secure; if(t->failed) return;
    WInt until=n_until(ctx,s->timeout); t_shutdown(ctx,s,until);
    if(wd_rt_unwinding(ctx)) return;
    if(n_api.shutdown((NSocket)t->fd,N_SEND)) { n_error(ctx,"TLS transport write shutdown",n_errno()); return; }
    unsigned char pending[16384];
    while(n_check(ctx,until)) {
        NResult received=n_api.recv((NSocket)t->fd,pending,sizeof(pending),0);
        if(!received) return;
        if(received>0) continue;
        int code=n_errno(); if(n_interrupted(code)) continue;
        if(n_pending(code)) { if(!n_wait(ctx,t->fd,0,until)) return; continue; }
#ifdef _WIN32
        if(code==10054) return;
#else
        if(code==104) return;
#endif
        n_error(ctx,"TLS transport close drain",code); return;
    }
}
void *wd_m_tls_accept(void *ctx,void *connection,void *context) {
    wd_m_tls_context_check(ctx,context); if(wd_rt_unwinding(ctx)) return 0;
    WStream *old=stream_state(ctx,connection); if(!old) return 0;
    if(old->secure || old->eof || !old->readable || !old->writable || (old->input && old->offset<old->input->length)) {
        fail(ctx,"R4021","TLS upgrade requires an unread, full-duplex plain TCP connection"); return 0;
    }
    if(old->kind!=1) { fail(ctx,"R4016","transfer requires a connected socket"); return 0; }
    WStream *next=n_stream(ctx,-1,1,old->timeout); if(!next) return 0;
    next->handle=old->handle; old->handle=-1; next->input=old->input; old->input=0; next->offset=old->offset; old->offset=0;
    next->secure=t_transport(ctx,context,next->handle);
    if(next->secure) t_handshake(ctx,next->secure,1,n_until(ctx,next->timeout));
    if(wd_rt_unwinding(ctx)) { wd_m_release(next); return 0; }
    return next;
}
void *wd_m_tls_connect(void *ctx,const WStr *host,WInt port,const WStr *ca,WInt timeout) {
    if(!n_timeout(ctx,timeout)) return 0;
    if(host->size<1 || host->size>253 || wd_m_find(host,&(WStr){(unsigned char*)"\0",1,0})>=0) {
        fail(ctx,"R4021","TLS requires a nonempty ASCII hostname or numeric IP"); return 0;
    }
    for(WInt i=0;i<host->size;++i) if(host->data[i]<=32 || host->data[i]>=127) {
        fail(ctx,"R4021","TLS hostname must be ASCII; encode international DNS names with IDNA"); return 0;
    }
    if(!t_load(ctx)) return 0;
    t_api.ERR_clear_error(); TContext *c=t_context(ctx); if(!c) return 0;
    t_api.SSL_CTX_set_verify(c->handle,1,0); /* VERIFY_PEER, no permissive callback. */
    if(ca->size) t_certificates(ctx,c,ca,0);
    else t_check(ctx,t_api.SSL_CTX_set_default_verify_paths(c->handle),"load default OpenSSL trust roots");
    WStream *s=0; WStr terminated={0};
    if(!wd_rt_unwinding(ctx)) s=wd_m_tcp_connect(ctx,host,port,timeout);
    if(s) {
        WInt until=n_until(ctx,timeout); s->secure=t_transport(ctx,c,s->handle);
        if(s->secure) {
            TTransport *t=s->secure; p_copy(ctx,&terminated,host->data,host->size);
            if(!wd_rt_unwinding(ctx)) {
                unsigned char address[16]={0}; const char *name=(char*)terminated.data;
                if(n_api.inet_pton(2,name,address)==1 || n_api.inet_pton(N_INET6,name,address)==1)
                    t_check(ctx,t_api.X509_VERIFY_PARAM_set1_ip_asc(t_api.SSL_get0_param(t->ssl),name),"set TLS IP verification");
                else {
                    t_check(ctx,t_api.SSL_set1_host(t->ssl,name),"set TLS hostname verification");
                    t_api.SSL_set_hostflags(t->ssl,4|32); /* NO_PARTIAL_WILDCARDS | NEVER_CHECK_SUBJECT */
                    if(!wd_rt_unwinding(ctx)) t_check(ctx,(int)t_api.SSL_ctrl(t->ssl,55,0,(void*)name),"set TLS SNI hostname");
                }
            }
            if(!wd_rt_unwinding(ctx)) t_handshake(ctx,t,0,until);
        }
    }
    wd_m_str_drop(&terminated); wd_m_release(c); /* SSL_new retained the SSL_CTX. */
    if(wd_rt_unwinding(ctx)) { wd_m_release(s); return 0; }
    return s;
}
void wd_m_tls_version(void *ctx,WStr *out) {
    *out=(WStr){0}; if(!t_load(ctx)) return;
    const char *version=t_api.OpenSSL_version(0); WInt size=0; while(version[size]) ++size;
    p_copy(ctx,out,(const unsigned char*)version,size);
}
