/* Included by managed.c: file resources share identity, not value storage.
   Unbuffered OS operations keep binary bytes and never inherit a descriptor.
   The final owner closes best-effort; explicit and scoped closes report errors. */
typedef struct WStream {
    WOwner owner;
    WInt handle;
    int readable, writable;
    WSeq *input;
    WInt offset;
    int eof;
    int kind; /* 0 file, 1 connection, 2 listener. Private, opaque ABI. */
    WInt timeout;
    void *secure; /* Separately owned stable TLS transport; opaque ABI. */
} WStream;

/* Networking is defined after initialized system-service/thread support. */
static int n_close(WInt handle);
static void n_error(void *ctx,const char *action,int number);
static WInt n_until(void *ctx,WInt timeout);
static WInt n_read(void *ctx,WStream *stream,void *bytes,WInt count,WInt until);
static WInt n_write(void *ctx,WStream *stream,const void *bytes,WInt count,WInt until);
static WInt t_read(void *,WStream *,void *,WInt,WInt);
static WInt t_write(void *,WStream *,const void *,WInt,WInt);
static void t_shutdown(void *,WStream *,WInt);
static void t_finish(void *,WStream *);

static void file_error(void *ctx, const char *prefix, WInt number) {
    WStr code = {0}, message = {0}, begin = {0}, end = {(unsigned char *)")",1,0};
    while (prefix[begin.size]) ++begin.size;
    begin.data = (unsigned char *)prefix;
    wd_m_int_str(ctx, &code, number);
    if (!code.data) return;
    wd_m_str_concat(ctx, &message, &begin, &code);
    wd_m_str_drop(&code);
    if (!message.data) return;
    WStr full = {0};
    wd_m_str_concat(ctx, &full, &message, &end);
    wd_m_str_drop(&message);
    if (!full.data) return;
    wd_rt_error_value(ctx,"R4016",5,&full);
    wd_m_str_drop(&full);
}
static void stream_destroy(void *value) {
    WStream *stream = value;
    wd_m_release(stream->secure);
    if (stream->handle != -1) {
        if(stream->kind) (void)n_close(stream->handle);
        else (void)wd_file_close(stream->handle);
    }
    wd_m_release(stream->input);
}
static WStream *stream_state(void *ctx, void *value) {
    WStream *stream = value;
    if (!stream || stream->handle == -1) {
        fail(ctx,"R4016","stream is closed");
        return 0;
    }
    return stream;
}
static WStream *stream_transfer_prepare(void *ctx,void *value) {
    WStream *old=stream_state(ctx,value); if(!old) return 0;
    if(old->kind!=1) { fail(ctx,"R4016","transfer requires a connected socket"); return 0; }
    WStream *next=(WStream*)allocate(ctx,sizeof(WStream),stream_destroy); if(!next) return 0;
    zero_bytes((unsigned char*)next+sizeof(WOwner),sizeof(WStream)-sizeof(WOwner));
    next->handle=-1; next->kind=1; return next;
}
static void stream_transfer_commit(WStream *old,WStream *next) {
    next->handle=old->handle; old->handle=-1;
    next->readable=old->readable; next->writable=old->writable;
    next->eof=old->eof; next->timeout=old->timeout;
    next->input=old->input; old->input=0;
    next->offset=old->offset; old->offset=0;
    next->secure=old->secure; old->secure=0;
}
void *wd_m_stream_open(void *ctx, const WStr *path, const WStr *mode) {
    for (WInt i = 0; i < path->size; ++i) {
        if (!path->data[i]) {
            fail(ctx,"R4016","file path contains NUL");
            return 0;
        }
    }
    WInt which = -1;
    if (mode->size == 1) {
        if (mode->data[0] == 'r') which = 0;
        if (mode->data[0] == 'w') which = 1;
        if (mode->data[0] == 'a') which = 2;
        if (mode->data[0] == 'x') which = 3;
    }
    if (which < 0) {
        fail(ctx,"R4016","file mode must be r, w, a, or x (binary)");
        return 0;
    }
    /* Allocate and terminate before opening, so OOM cannot leak a descriptor. */
    WStream *stream = (WStream *)allocate(ctx,sizeof(WStream),stream_destroy);
    if (!stream) return 0;
    stream->handle = -1;
    stream->input = 0;
    stream->offset = 0;
    stream->eof = 0;
    stream->kind = 0;
    stream->secure = 0;
    stream->timeout = 30000;
    stream->readable = which == 0;
    stream->writable = which != 0;
    WStr terminated = {0};
    wd_m_str_new(ctx,&terminated,path->size);
    if (!terminated.data) { wd_m_release(stream); return 0; }
    copy_bytes(terminated.data,path->data,(WSize)path->size);
    WInt handle = wd_file_open(terminated.data,terminated.size,which);
    wd_m_str_drop(&terminated);
    if (handle < 0) {
        wd_m_release(stream);
        if (handle == -84) { fail(ctx,"R4008","invalid filesystem path"); return 0; }
        file_error(ctx,"cannot open file stream (OS error ",-handle);
        return 0;
    }
    stream->handle = handle;
    return stream;
}
void wd_m_stream_check(void *ctx, void *stream) { (void)stream_state(ctx,stream); }
WInt wd_m_stream_closed(void *value) {
    WStream *stream = value;
    return !stream || stream->handle == -1;
}
void wd_m_stream_close(void *ctx, void *value, WInt report) {
    WStream *stream = value;
    if (!stream || stream->handle == -1) return;
    if(stream->secure && report && !wd_rt_unwinding(ctx)) t_finish(ctx,stream);
    wd_m_release(stream->secure); stream->secure=0;
    WInt handle = stream->handle;
    stream->handle = -1;
    /* Never retry close(EINTR): Linux has already released the descriptor. */
    WInt status = stream->kind ? n_close(handle) : wd_file_close(handle);
    wd_m_release(stream->input);
    stream->input = 0;
    stream->offset = 0;
    if (status && report && !wd_rt_unwinding(ctx)) {
        if(stream->kind) n_error(ctx,"stream close",(int)status);
        else file_error(ctx,"stream close failed (OS error ",-status);
    }
}
void wd_m_stream_read(void *ctx, WStr *out, void *value, WInt count) {
    *out = (WStr){0};
    if (count < 0 || count > 16777216) {
        fail(ctx,"R4016","stream byte count must be in 0..16777216");
        return;
    }
    WStream *stream = stream_state(ctx,value);
    if (!stream) return;
    if (!stream->readable) { fail(ctx,"R4016","stream is not readable"); return; }
    WInt remaining = stream->input ? stream->input->length - stream->offset : 0;
    if (remaining) {
        WInt size = count < remaining ? count : remaining;
        wd_m_str_new(ctx,out,size);
        if (!out->data) return;
        copy_bytes(out->data,data(stream->input)+stream->offset,(WSize)size);
        stream->offset += size;
        return;
    }
    wd_m_str_new(ctx,out,count);
    if (!out->data) return;
    if(stream->kind) {
        WInt until=n_until(ctx,stream->timeout);
        WInt received=count && !stream->eof && !wd_rt_unwinding(ctx)?n_read(ctx,stream,out->data,count,until):0;
        if(wd_rt_unwinding(ctx)) { wd_m_str_drop(out); *out=(WStr){0}; return; }
        if(count && !received) stream->eof=1;
        out->size=received; out->data[received]=0; return;
    }
    WInt total = 0;
    /* Match file fread: fill a requested byte span until EOF/error. */
    while (total < count && !stream->eof) {
        WInt received = wd_file_read(stream->handle,out->data+total,count-total);
        if (received == -4) continue;
        if (received < 0) {
            wd_m_str_drop(out); *out = (WStr){0};
            fail(ctx,"R4016","file stream read failed"); return;
        }
        if (received == 0) { stream->eof = 1; break; }
        total += received;
    }
    out->size = total;
    out->data[total] = 0;
}
void wd_m_stream_line(void *ctx, WStr *out, void *value, WInt limit) {
    *out = (WStr){0};
    if (limit < 0 || limit > 16777216) {
        fail(ctx,"R4016","stream byte count must be in 0..16777216"); return;
    }
    WStream *stream = stream_state(ctx,value);
    if (!stream) return;
    if (!stream->readable) { fail(ctx,"R4016","stream is not readable"); return; }
    WInt until=stream->kind?n_until(ctx,stream->timeout):0;
    if(wd_rt_unwinding(ctx)) return;
    if (!stream->input) {
        stream->input = wd_m_seq_new(ctx,1,0,0,0,0);
        if (!stream->input) return;
    }
    WSeq *input = stream->input;
    if (stream->offset) {
        input->length -= stream->offset;
        /* Forward copying is safe for these overlapping, left-shifting bytes. */
        copy_bytes(data(input),data(input)+stream->offset,(WSize)input->length);
        stream->offset = 0;
    }
    WInt scanned = 0;
    for (;;) {
        WInt end = scanned;
        while (end < input->length && data(input)[end] != '\n') ++end;
        int newline = end < input->length;
        WInt size = newline ? end + 1 : input->length;
        if (size > limit) { fail(ctx,"R4016","stream line exceeds byte limit"); return; }
        if (newline || stream->eof) {
            wd_m_str_new(ctx,out,size);
            if (!out->data) return;
            copy_bytes(out->data,data(input),(WSize)size);
            stream->offset = size;
            return;
        }
        scanned = end;
        /* Probe one byte beyond the budget to distinguish exact EOF, and never
           read unbounded data. Geometric capacity growth avoids quadratic copies. */
        WInt count = limit + 1 - input->length;
        if (count > 4096) count = 4096;
        input = wd_m_seq_unique(ctx,&stream->input,input->length+count);
        if (!input) return;
        WInt received = stream->kind?n_read(ctx,stream,data(input)+input->length,count,until):wd_file_read(stream->handle,data(input)+input->length,count);
        if(wd_rt_unwinding(ctx)) return;
        if (received == -4) continue;
        if (received < 0) { fail(ctx,"R4016","file stream read failed"); return; }
        if (!received) stream->eof = 1;
        input->length += received;
    }
}
void wd_m_stream_write(void *ctx, void *value, const WStr *data) {
    WStream *stream = stream_state(ctx,value);
    if (!stream) return;
    if (!stream->writable) { fail(ctx,"R4016","stream is not writable"); return; }
    WInt until=stream->kind?n_until(ctx,stream->timeout):0;
    if(wd_rt_unwinding(ctx)) return;
    WInt total = 0;
    while (total < data->size) {
        WInt remaining = data->size-total;
        WInt count=remaining > 16777216 ? 16777216 : remaining;
        WInt sent = stream->kind?n_write(ctx,stream,data->data+total,count,until):wd_file_write(stream->handle,data->data+total,count);
        if(wd_rt_unwinding(ctx)) return;
        if (sent == -4) continue;
        if (sent <= 0) { fail(ctx,"R4016","file stream write failed"); return; }
        total += sent;
    }
}
void wd_m_stream_seek(void *ctx, void *value, WInt offset) {
    WStream *stream = stream_state(ctx,value);
    if (!stream) return;
    if (stream->kind || offset < 0) {
        fail(ctx,"R4016","seek requires a file stream and nonnegative absolute offset"); return;
    }
    if (wd_file_seek(stream->handle,offset,0) < 0) {
        fail(ctx,"R4016","file seek failed");
        return;
    }
    if (stream->input) stream->input->length = 0;
    stream->offset = 0;
    stream->eof = 0;
}
WInt wd_m_stream_tell(void *ctx, void *value) {
    WStream *stream = stream_state(ctx,value);
    if (!stream) return 0;
    if(stream->kind) { fail(ctx,"R4016","tell requires a file stream"); return 0; }
    WInt result = wd_file_seek(stream->handle,0,1);
    if (result < 0) { fail(ctx,"R4016","file position is unavailable or outside int64 range"); return 0; }
    return result - (stream->input ? stream->input->length-stream->offset : 0);
}
void wd_m_stream_flush(void *ctx, void *value) {
    WStream *stream = stream_state(ctx,value);
    if (!stream) return;
    if (stream->kind || !stream->writable) fail(ctx,"R4016","flush requires a writable file stream");
    /* Every write has already reached the kernel. Like fflush, no fsync or
       durable-storage guarantee is implied. */
}
