/* Immutable JSON nodes share the native owner/sequence/AVL implementation.
   Parsing and conversion have a 64-level limit; no borrowed input escapes. */
typedef struct WJson WJson;
struct WJson { WOwner owner; WInt kind; union { WInt integer; double floating; WStr string; WSeq *array; WMap *object; } as; };
typedef struct { WStr name; WSize offset; const void *type; } JField;
typedef struct { WInt kind,count,stride; const JField *fields; } JType;
typedef struct { WStr key; WJson *value; } JPair;
static void j_retain_slot(void *p) { wd_m_retain(*(void**)p); }
static void j_drop_slot(void *p) { wd_m_release(*(void**)p); }
static void j_pair_retain(void *p) { JPair *v=p; wd_m_str_retain(&v->key); wd_m_retain(v->value); }
static void j_pair_drop(void *p) { JPair *v=p; wd_m_str_drop(&v->key); wd_m_release(v->value); }
static int32_t j_key(const void *ap,const void *bp) {
    const WStr *a=ap,*b=bp; WInt n=a->size<b->size?a->size:b->size;
    for(WInt i=0;i<n;++i) if(a->data[i]!=b->data[i]) return a->data[i]<b->data[i]?-1:1;
    return (a->size>b->size)-(a->size<b->size);
}
static void j_destroy(void *p) {
    WJson *v=p;
    if(v->kind==4) wd_m_str_drop(&v->as.string);
    if(v->kind==5) wd_m_release(v->as.array);
    if(v->kind==6) wd_m_release(v->as.object);
}
static WJson *j_new(void *ctx,WInt kind) {
    WJson *v=(WJson*)allocate(ctx,sizeof(WJson),j_destroy);
    if(v) { v->kind=kind; zero_bytes(&v->as,sizeof v->as); }
    return v;
}
static WMap *j_map(void *ctx) {
    return wd_m_map_new(ctx,sizeof(JPair),offsetof(JPair,value),sizeof(WStr),j_pair_retain,j_pair_drop,wd_m_str_retain,j_key);
}
typedef struct { void *ctx; WSeq *seq; } JBuffer;
static void jb_add(JBuffer *b,const void *p,WInt n) {
    if(wd_rt_unwinding(b->ctx) || !n) return;
    WInt old=wd_m_seq_len(b->seq),size;
    if(!b->seq) b->seq=wd_m_seq_new(b->ctx,1,0,0,0,0);
    if(!b->seq) return;
    if(!sum(b->ctx,old,n,&size) || !wd_m_seq_unique(b->ctx,&b->seq,size)) return;
    copy_bytes(data(b->seq)+old,p,(WSize)n); b->seq->length=size;
}
static void jb_word(JBuffer *b,const char *p) { WInt n=0; while(p[n]) ++n; jb_add(b,p,n); }
static void jb_byte(JBuffer *b,unsigned char c) { jb_add(b,&c,1); }
static void jb_finish(JBuffer *b,WStr *out) {
    *out=(WStr){0};
    if(!wd_rt_unwinding(b->ctx)) { wd_m_str_new(b->ctx,out,wd_m_seq_len(b->seq)); if(out->size) copy_bytes(out->data,data(b->seq),(WSize)out->size); }
    wd_m_release(b->seq); b->seq=0;
}
static void j_quote(JBuffer *b,const WStr *s) {
    static const char hex[]="0123456789abcdef";
    jb_byte(b,'"');
    for(WInt i=0;i<s->size;) {
        unsigned c=s->data[i];
        if(c=='"'||c=='\\') { jb_byte(b,'\\'); jb_byte(b,(unsigned char)c); ++i; }
        else if(c<32) { unsigned char esc[6]={'\\','u','0','0',(unsigned char)hex[c>>4],(unsigned char)hex[c&15]}; jb_add(b,esc,6); ++i; }
        else { WInt n=wd_m_utf8_span(s->data+i,s->size-i); if(n) { jb_add(b,s->data+i,n); i+=n; } else { jb_word(b,"\\ufffd"); ++i; } }
    }
    jb_byte(b,'"');
}
static int j_utf8(const WStr *s) {
    for(WInt i=0;i<s->size;) { WInt n=wd_m_utf8_span(s->data+i,s->size-i); if(!n) return 0; i+=n; }
    return 1;
}
static void j_validate(void *ctx,const WJson *v,WInt depth) {
    if(depth>64) { fail(ctx,"R4012","JSON nesting exceeds 64 levels"); return; }
    if(!v || wd_rt_unwinding(ctx)) return;
    if(v->kind==5) for(WInt i=0;i<wd_m_seq_len(v->as.array);++i) j_validate(ctx,*(WJson**)wd_m_seq_read(v->as.array,i),depth+1);
    if(v->kind==6) for(WInt i=0;i<wd_m_map_len(v->as.object);++i) j_validate(ctx,*(WJson**)wd_m_map_entry(v->as.object,i,1),depth+1);
}
void *wd_m_json_convert(void *ctx,const JType *type,const void *p,WInt depth) {
    if(wd_rt_unwinding(ctx)) return 0;
    if(type->kind<0) { fail(ctx,"R4012","value cannot be converted to JSON"); return 0; }
    if(depth>64) { fail(ctx,"R4012","JSON nesting exceeds 64 levels"); return 0; }
    if(type->kind==7) { WJson *v=*(WJson*const*)p; if(depth) j_validate(ctx,v,depth); if(wd_rt_unwinding(ctx)) return 0; wd_m_retain(v); return v; }
    if(type->kind==8) return *(const unsigned char*)p?wd_m_json_convert(ctx,type->fields[0].type,(const unsigned char*)p+type->fields[0].offset,depth):j_new(ctx,0);
    WInt kind=type->kind;
    if(kind>=9) kind=type->kind==9?5:6;
    WJson *v=j_new(ctx,kind); if(!v) return 0;
    if(kind==1) v->as.integer=*(const unsigned char*)p!=0;
    if(kind==2) v->as.integer=*(const WInt*)p;
    if(kind==3) { v->as.floating=*(const double*)p; if((n_bits(v->as.floating)&UINT64_C(0x7fffffffffffffff))>=UINT64_C(0x7ff0000000000000)) fail(ctx,"R4012","JSON numbers must be finite"); }
    if(kind==4) { v->as.string=*(const WStr*)p; wd_m_str_retain(&v->as.string); if(!j_utf8(&v->as.string)) fail(ctx,"R4012","JSON strings must contain valid UTF-8"); }
    if(kind==5 || kind==6) {
        if(kind==5) v->as.array=wd_m_seq_new(ctx,sizeof(WJson*),0,0,j_retain_slot,j_drop_slot);
        else v->as.object=j_map(ctx);
        WInt count=type->count;
        if(type->kind==5) count=wd_m_seq_len(*(WSeq*const*)p);
        if(type->kind==6) count=wd_m_map_len(*(WMap*const*)p);
        for(WInt i=0;i<count && !wd_rt_unwinding(ctx);++i) {
            const JField *field=&type->fields[type->kind<9?0:i];
            const void *child=(const unsigned char*)p+field->offset; WStr key=field->name;
            if(type->kind==5) child=wd_m_seq_read(*(WSeq*const*)p,i);
            if(type->kind==6) { WMap *m=*(WMap*const*)p; child=wd_m_map_entry(m,i,1); key=*(WStr*)wd_m_map_entry(m,i,0); }
            WJson *item=wd_m_json_convert(ctx,field->type,child,depth+1);
            if(!wd_rt_unwinding(ctx)) {
                if(kind==5) wd_m_seq_append(ctx,&v->as.array,&item);
                else {
                    if(!j_utf8(&key)) fail(ctx,"R4012","JSON strings must contain valid UTF-8");
                    else { WJson **slot=wd_m_map_edit(ctx,&v->as.object,&key,1); if(slot) { *slot=item; wd_m_retain(item); } }
                }
            }
            wd_m_release(item);
        }
    }
    if(wd_rt_unwinding(ctx)) { wd_m_release(v); return 0; }
    return v;
}
static void j_dump(JBuffer *b,const WJson *v);
static void j_dump_object(JBuffer *b,const WMap *m,WInt index,int *first) {
    if(index<0 || wd_rt_unwinding(b->ctx)) return;
    const WNode *n=wd_m_seq_read(m->nodes,index);
    j_dump_object(b,m,n->left,first);
    if(!*first) jb_byte(b,',');
    *first=0;
    WStr *key=wd_m_map_entry(m,n->entry,0); j_quote(b,key); jb_byte(b,':');
    j_dump(b,*(WJson**)wd_m_map_entry(m,n->entry,1));
    j_dump_object(b,m,n->right,first);
}
static void j_dump(JBuffer *b,const WJson *v) {
    if(wd_rt_unwinding(b->ctx)) return;
    if(!v || !v->kind) jb_word(b,"null");
    else if(v->kind==1) jb_word(b,v->as.integer?"true":"false");
    else if(v->kind==2 || v->kind==3) {
        WStr s={0}; if(v->kind==2) wd_m_int_str(b->ctx,&s,v->as.integer); else wd_m_float_str(b->ctx,&s,v->as.floating);
        jb_add(b,s.data,s.size); wd_m_str_drop(&s);
    } else if(v->kind==4) j_quote(b,&v->as.string);
    else if(v->kind==5) {
        jb_byte(b,'['); for(WInt i=0;i<wd_m_seq_len(v->as.array);++i) { if(i) jb_byte(b,','); j_dump(b,*(WJson**)wd_m_seq_read(v->as.array,i)); } jb_byte(b,']');
    } else { int first=1; jb_byte(b,'{'); j_dump_object(b,v->as.object,v->as.object->root,&first); jb_byte(b,'}'); }
}
void wd_m_json_dump(void *ctx,WStr *out,const WJson *v) { JBuffer b={ctx,0}; j_dump(&b,v); jb_finish(&b,out); }
void wd_m_json_kind(void *ctx,WStr *out,const WJson *v) {
    (void)ctx; static const char *names[]={"null","bool","int","float","str","list","dict"};
    const char *p=names[v?v->kind:0]; WInt n=0; while(p[n]) ++n; *out=(WStr){(unsigned char*)p,n,0};
}
WInt wd_m_json_int(void *ctx,const WJson *v) { if(v && v->kind==2) return v->as.integer; fail(ctx,"R4012","expected JSON int"); return 0; }
WInt wd_m_json_bool(void *ctx,const WJson *v) { if(v && v->kind==1) return v->as.integer; fail(ctx,"R4012","expected JSON bool"); return 0; }
double wd_m_json_float(void *ctx,const WJson *v) { if(v && v->kind==3) return v->as.floating; if(v && v->kind==2) return (double)v->as.integer; fail(ctx,"R4012","expected JSON number"); return 0; }
void wd_m_json_str(void *ctx,WStr *out,const WJson *v) { *out=(WStr){0}; if(v && v->kind==4) { *out=v->as.string; wd_m_str_retain(out); } else fail(ctx,"R4012","expected JSON str"); }
void *wd_m_json_list(void *ctx,const WJson *v) { if(v && v->kind==5) { wd_m_retain(v->as.array); return v->as.array; } fail(ctx,"R4012","expected JSON list"); return 0; }
static void j_sorted(void *ctx,WMap **out,const WMap *m,WInt index) {
    if(index<0 || wd_rt_unwinding(ctx)) return;
    const WNode *n=wd_m_seq_read(m->nodes,index); j_sorted(ctx,out,m,n->left);
    if(wd_rt_unwinding(ctx)) return;
    WStr *key=wd_m_map_entry(m,n->entry,0); WJson *v=*(WJson**)wd_m_map_entry(m,n->entry,1);
    WJson **slot=wd_m_map_edit(ctx,out,key,1); if(slot) { *slot=v; wd_m_retain(v); }
    j_sorted(ctx,out,m,n->right);
}
void *wd_m_json_dict(void *ctx,const WJson *v) {
    if(!v || v->kind!=6) { fail(ctx,"R4012","expected JSON dict"); return 0; }
    WMap *out=j_map(ctx); if(out) j_sorted(ctx,&out,v->as.object,v->as.object->root);
    if(wd_rt_unwinding(ctx)) { wd_m_release(out); return 0; } return out;
}
WInt wd_m_json_equal(const WJson *a,const WJson *b) {
    WInt ak=a?a->kind:0,bk=b?b->kind:0;
    if(ak!=bk) return 0;
    if(!ak) return 1;
    if(ak==1 || ak==2) return a->as.integer==b->as.integer;
    if(ak==3) return a->as.floating==b->as.floating;
    if(ak==4) return j_key(&a->as.string,&b->as.string)==0;
    WInt count=ak==5?wd_m_seq_len(a->as.array):wd_m_map_len(a->as.object);
    if(count!=(ak==5?wd_m_seq_len(b->as.array):wd_m_map_len(b->as.object))) return 0;
    for(WInt i=0;i<count;++i) {
        WJson *av,*bv;
        if(ak==5) { av=*(WJson**)wd_m_seq_read(a->as.array,i); bv=*(WJson**)wd_m_seq_read(b->as.array,i); }
        else { WStr *key=wd_m_map_entry(a->as.object,i,0); WInt j=wd_m_map_find(b->as.object,key); if(j<0) return 0; av=*(WJson**)wd_m_map_entry(a->as.object,i,1); bv=*(WJson**)wd_m_map_entry(b->as.object,j,1); }
        if(!wd_m_json_equal(av,bv)) return 0;
    }
    return 1;
}
typedef struct { void *ctx; const WStr *text; WInt pos; } JReader;
static void jr_bad(JReader *r,const char *message) {
    if(wd_rt_unwinding(r->ctx)) return;
    JBuffer b={r->ctx,0}; WStr pos={0},text={0};
    jb_word(&b,"JSON at byte "); wd_m_int_str(r->ctx,&pos,r->pos+1); jb_add(&b,pos.data,pos.size); wd_m_str_drop(&pos);
    jb_word(&b,": "); jb_word(&b,message); jb_finish(&b,&text);
    if(text.owner) { wd_rt_error_value(r->ctx,"R4012",5,&text); wd_m_str_drop(&text); }
}
static int jr_take(JReader *r,unsigned c) { if(r->pos<r->text->size && r->text->data[r->pos]==c) { ++r->pos; return 1; } return 0; }
static void jr_space(JReader *r) { while(r->pos<r->text->size) { unsigned c=r->text->data[r->pos]; if(c!=' '&&c!='\n'&&c!='\r'&&c!='\t') break; ++r->pos; } }
static unsigned jr_hex(JReader *r) {
    unsigned v=0;
    for(unsigned i=0;i<4;++i) {
        if(r->pos==r->text->size) { jr_bad(r,"incomplete Unicode escape"); return 0; }
        unsigned c=r->text->data[r->pos++],d;
        if(c>='0'&&c<='9') d=c-'0'; else if(c>='a'&&c<='f') d=c-'a'+10; else if(c>='A'&&c<='F') d=c-'A'+10; else { jr_bad(r,"invalid Unicode escape"); return 0; }
        v=v*16+d;
    }
    return v;
}
static WStr jr_string(JReader *r) {
    JBuffer b={r->ctx,0}; WStr result={0};
    if(!jr_take(r,'"')) jr_bad(r,"expected string");
    else while(r->pos<r->text->size && !wd_rt_unwinding(r->ctx)) {
        unsigned c=r->text->data[r->pos++];
        if(c=='"') { jb_finish(&b,&result); return result; }
        if(c<32) { jr_bad(r,"unescaped control character"); break; }
        if(c=='\\') {
            if(r->pos==r->text->size) { jr_bad(r,"unterminated escape"); break; }
            c=r->text->data[r->pos++];
            if(c=='"'||c=='\\'||c=='/') jb_byte(&b,(unsigned char)c);
            else if(c=='b'||c=='f'||c=='n'||c=='r'||c=='t') jb_byte(&b,c=='b'?8:c=='f'?12:c=='n'?10:c=='r'?13:9);
            else if(c=='u') {
                unsigned cp=jr_hex(r);
                if(cp>=0xd800 && cp<=0xdbff) {
                    if(!jr_take(r,'\\')||!jr_take(r,'u')) { jr_bad(r,"missing low surrogate"); break; }
                    unsigned low=jr_hex(r);
                    if(low<0xdc00 || low>0xdfff) { jr_bad(r,"invalid low surrogate"); break; }
                    cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
                } else if(cp>=0xdc00 && cp<=0xdfff) { jr_bad(r,"unpaired low surrogate"); break; }
                if(cp<0x80) jb_byte(&b,(unsigned char)cp);
                else {
                    if(cp>=0x10000) jb_byte(&b,(unsigned char)(0xf0|(cp>>18)));
                    if(cp>=0x800) jb_byte(&b,(unsigned char)((cp>=0x10000?0x80:0xe0)|((cp>>12)&63)));
                    jb_byte(&b,(unsigned char)((cp>=0x800?0x80:0xc0)|((cp>>6)&(cp>=0x800?63:31)))); jb_byte(&b,(unsigned char)(0x80|(cp&63)));
                }
            } else { jr_bad(r,"unknown escape"); break; }
        } else {
            WInt n=wd_m_utf8_span(r->text->data+r->pos-1,r->text->size-r->pos+1);
            if(!n) { jr_bad(r,"invalid UTF-8"); break; }
            jb_add(&b,r->text->data+r->pos-1,n); r->pos+=n-1;
        }
    }
    jr_bad(r,"unterminated string"); wd_m_release(b.seq); return result;
}
static WJson *jr_value(JReader *r,WInt depth) {
    if(depth>64) { jr_bad(r,"nesting exceeds 64 levels"); return 0; }
    jr_space(r);
    if(r->pos==r->text->size) { jr_bad(r,"expected value"); return 0; }
    unsigned c=r->text->data[r->pos]; WJson *v=0;
    if(c=='"') { WStr s=jr_string(r); if(!wd_rt_unwinding(r->ctx)) { v=j_new(r->ctx,4); if(v) { v->as.string=s; return v; } } wd_m_str_drop(&s); return 0; }
    if(c=='{' || c=='[') {
        ++r->pos; int object=c=='{'; v=j_new(r->ctx,object?6:5); if(!v) return 0;
        if(object) v->as.object=j_map(r->ctx); else v->as.array=wd_m_seq_new(r->ctx,sizeof(WJson*),0,0,j_retain_slot,j_drop_slot);
        jr_space(r);
        if(!jr_take(r,object?'}':']')) for(;;) {
            WStr key={0};
            if(object) { jr_space(r); key=jr_string(r); jr_space(r); if(!wd_rt_unwinding(r->ctx) && !jr_take(r,':')) jr_bad(r,"expected ':'"); }
            WJson *item=wd_rt_unwinding(r->ctx)?0:jr_value(r,depth+1);
            if(!wd_rt_unwinding(r->ctx)) {
                if(object) {
                    if(wd_m_map_find(v->as.object,&key)>=0) jr_bad(r,"duplicate object key");
                    else { WJson **slot=wd_m_map_edit(r->ctx,&v->as.object,&key,1); if(slot) { *slot=item; wd_m_retain(item); } }
                } else wd_m_seq_append(r->ctx,&v->as.array,&item);
            }
            wd_m_release(item); wd_m_str_drop(&key);
            if(wd_rt_unwinding(r->ctx)) break;
            jr_space(r); if(jr_take(r,object?'}':']')) return v;
            if(!jr_take(r,',')) { jr_bad(r,object?"expected ',' or '}'":"expected ',' or ']'"); break; }
        }
        if(!wd_rt_unwinding(r->ctx)) return v;
    } else if(c=='-' || n_digit(c)) {
        WInt start=r->pos; jr_take(r,'-');
        if(r->pos==r->text->size || !n_digit(r->text->data[r->pos])) { jr_bad(r,"invalid number"); return 0; }
        if(!jr_take(r,'0')) while(r->pos<r->text->size && n_digit(r->text->data[r->pos])) ++r->pos;
        int floating=0;
        if(jr_take(r,'.')) { floating=1; WInt first=r->pos; while(r->pos<r->text->size && n_digit(r->text->data[r->pos])) ++r->pos; if(first==r->pos) jr_bad(r,"missing fractional digits"); }
        if(jr_take(r,'e')||jr_take(r,'E')) { floating=1; if(!jr_take(r,'+')) jr_take(r,'-'); WInt first=r->pos; while(r->pos<r->text->size && n_digit(r->text->data[r->pos])) ++r->pos; if(first==r->pos) jr_bad(r,"missing exponent digits"); }
        if(wd_rt_unwinding(r->ctx)) return 0;
        WStr s={r->text->data+start,r->pos-start,0};
        if(floating) {
            double number=0;
            /* Match the qualified production stream policy: MSVC reports
               nonzero underflow-to-zero; libstdc++ accepts rounded zero. */
            int reject_underflow=0;
#ifdef _WIN32
            reject_underflow=1;
#endif
            if(n_parse(&s,&number,reject_underflow)) jr_bad(r,"number outside binary64 range");
            else { v=j_new(r->ctx,3); if(v) v->as.floating=number; }
        }
        else {
            int negative=s.data[0]=='-'; uint64_t number=0,limit=negative?UINT64_C(0x8000000000000000):INT64_MAX;
            for(WInt i=negative;i<s.size;++i) { unsigned d=s.data[i]-'0'; if(number>(limit-d)/10) { jr_bad(r,"integer outside signed 64-bit range"); break; } number=number*10+d; }
            if(!wd_rt_unwinding(r->ctx)) { v=j_new(r->ctx,2); if(v) v->as.integer=negative?(WInt)(UINT64_C(0)-number):(WInt)number; }
        }
    } else {
        const char *words[]={"true","false","null"};
        for(unsigned i=0;i<3;++i) {
            WInt n=i==1?5:4; if(r->text->size-r->pos<n) continue;
            WInt j=0; while(j<n && r->text->data[r->pos+j]==(unsigned char)words[i][j]) ++j;
            if(j==n) { r->pos+=n; v=j_new(r->ctx,i==2?0:1); if(v) v->as.integer=i==0; return v; }
        }
        jr_bad(r,"unexpected token");
    }
    if(wd_rt_unwinding(r->ctx)) { wd_m_release(v); return 0; } return v;
}
void *wd_m_json_parse(void *ctx,const WStr *text) {
    if(text->size>16777216) { fail(ctx,"R4012","JSON input exceeds 16 MiB"); return 0; }
    JReader r={ctx,text,0}; WJson *v=jr_value(&r,0);
    if(!wd_rt_unwinding(ctx)) { jr_space(&r); if(r.pos!=text->size) jr_bad(&r,"trailing content"); }
    if(wd_rt_unwinding(ctx)) { wd_m_release(v); return 0; } return v;
}
