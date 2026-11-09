/* Byte-preserving aggregate formatting; reuse the checked geometric text builder. */
void wd_m_format_join(void *ctx,WStr *out,const WStr *parts,WInt count) {
    JBuffer b={ctx,0};
    for(WInt i=0;i<count && !wd_rt_unwinding(ctx);++i) jb_add(&b,parts[i].data,parts[i].size);
    jb_finish(&b,out);
}
static void format_quote(JBuffer *b,const WStr *text) {
    static const char hex[]="0123456789abcdef";
    jb_byte(b,'"');
    for(WInt i=0;i<text->size && !wd_rt_unwinding(b->ctx);++i) {
        unsigned c=text->data[i];
        if(c=='"'||c=='\\') { jb_byte(b,'\\'); jb_byte(b,(unsigned char)c); }
        else if(c=='\n') jb_word(b,"\\n");
        else if(c=='\r') jb_word(b,"\\r");
        else if(c=='\t') jb_word(b,"\\t");
        else if(!c) jb_word(b,"\\0");
        else if(c<32 || c==127) { jb_word(b,"\\x"); jb_byte(b,(unsigned char)hex[c>>4]); jb_byte(b,(unsigned char)hex[c&15]); }
        else jb_byte(b,(unsigned char)c);
    }
    jb_byte(b,'"');
}
void wd_m_repr_str(void *ctx,WStr *out,const WStr *text) {
    JBuffer b={ctx,0}; format_quote(&b,text); jb_finish(&b,out);
}
/* Additive ABI-4 descriptor: every aggregate offset comes from LLVM's actual
   layout. This is distinct from JSON conversion and has no JSON depth/UTF-8
   restriction. An explicit work stack supports recursive record/container types
   and deep finite values without recursive native formatter calls. */
typedef struct FType FType;
typedef struct { WStr name; WSize offset; const FType *type; } FField;
struct FType { WInt kind,count; const FField *fields; WStr name; };
typedef struct { const FType *type; const unsigned char *value; WInt index; int quoted,part; } FFrame;
void wd_m_format_value(void *ctx,WStr *out,const void *descriptor,const void *value,WInt quoted) {
    JBuffer b={ctx,0}; WSeq *stack=wd_m_seq_new(ctx,sizeof(FFrame),0,0,0,0);
    FFrame initial={descriptor,value,-1,quoted!=0,0};
    if(stack) wd_m_seq_append(ctx,&stack,&initial);
    while(wd_m_seq_len(stack) && !wd_rt_unwinding(ctx)) {
        FFrame f=*(FFrame*)wd_m_seq_read(stack,stack->length-1);
        const FType *t=f.type; const unsigned char *p=f.value;
        WInt kind=t->kind;
        if(f.index<0) {
            WStr text={0}; int scalar=1;
            if(kind==0) jb_word(&b,"None");
            else if(kind==1) jb_word(&b,(*p&1)?"true":"false");
            else if(kind==2 || kind==3) {
                if(kind==2) wd_m_int_str(ctx,&text,*(const WInt*)p);
                else wd_m_float_str(ctx,&text,*(const double*)p);
                jb_add(&b,text.data,text.size);
            } else if(kind==4) {
                const WStr *s=(const WStr*)p;
                if(f.quoted) format_quote(&b,s); else jb_add(&b,s->data,s->size);
            } else if(kind==7) j_dump(&b,*(WJson*const*)p);
            else if(kind==8) {
                if(*p&1) {
                    *(FFrame*)wd_m_seq_read(stack,stack->length-1)=(FFrame){t->fields[0].type,p+t->fields[0].offset,-1,f.quoted,0};
                    continue;
                }
                jb_word(&b,"None");
            } else if(kind==11) {
                const WStr *code=(const WStr*)(p+t->fields[0].offset),*message=(const WStr*)(p+t->fields[1].offset);
                jb_add(&b,code->data,code->size); jb_word(&b,": "); jb_add(&b,message->data,message->size);
            } else if(kind==12) { wd_m_array_str(ctx,&text,*(void*const*)p); jb_add(&b,text.data,text.size); }
            else if(kind>=13 && kind<=17) {
                const char *names[]={"<function>","<callable>","<closure>","<cursor>","<range>"}; jb_word(&b,names[kind-13]);
            } else if(kind==18) jb_word(&b,wd_m_stream_closed(*(void*const*)p)?"<stream:closed>":"<stream:open>");
            else if(kind==20) jb_word(&b,wd_m_task_closed(*(void*const*)p)?"<task:closed>":"<task:open>");
            else if(kind==21) jb_word(&b,wd_m_db_closed(ctx,*(void*const*)p)?"<database:closed>":"<database:open>");
            else if(kind==22) jb_word(&b,wd_m_stmt_closed(*(void*const*)p)?"<statement:closed>":"<statement:open>");
            else if(kind==23) jb_word(&b,wd_m_transaction_closed(*(void*const*)p)?"<transaction:closed>":"<transaction:active>");
            else if(kind==25) jb_word(&b,"<py_object>");
            else if(kind==24) jb_word(&b,wd_m_tls_context_closed(*(void*const*)p)?"<tls_context:closed>":"<tls_context:open>");
            else if(kind==19 && !(*p&1)) jb_word(&b,"finished()");
            else {
                scalar=0;
                if(kind==5) jb_word(&b,"[");
                else if(kind==6) jb_word(&b,"{");
                else if(kind==9) jb_word(&b,"(");
                else if(kind==10) { jb_add(&b,t->name.data,t->name.size); jb_word(&b,"("); }
                else if(kind==19) jb_word(&b,"yielded(");
                else { fail(ctx,"R4003","invalid native formatting descriptor"); break; }
            }
            wd_m_str_drop(&text);
            if(scalar) { --stack->length; continue; }
            ((FFrame*)wd_m_seq_read(stack,stack->length-1))->index=0;
            f.index=0;
        }
        WInt count=kind==5?wd_m_seq_len(*(WSeq*const*)p):kind==6?wd_m_map_len(*(WMap*const*)p):t->count;
        if(f.index==count) {
            if(kind==9 && count==1) jb_word(&b,",");
            jb_word(&b,kind==5?"]":kind==6?"}":")"); --stack->length; continue;
        }
        FFrame *current=wd_m_seq_read(stack,stack->length-1);
        FFrame child={0}; child.index=-1; child.quoted=1;
        if(kind==6) {
            if(!f.part) { if(f.index) jb_word(&b,", "); current->part=1; }
            else { jb_word(&b,": "); current->part=0; ++current->index; }
            child.type=t->fields[f.part].type;
            child.value=wd_m_map_entry(*(WMap*const*)p,f.index,f.part);
        } else {
            if(f.index) jb_word(&b,", "); ++current->index;
            if(kind==5) { child.type=t->fields[0].type; child.value=wd_m_seq_read(*(WSeq*const*)p,f.index); }
            else {
                const FField *field=t->fields+f.index;
                if(kind==10) { jb_add(&b,field->name.data,field->name.size); jb_word(&b,"="); }
                child.type=field->type; child.value=p+field->offset;
            }
        }
        /* Never retain current across append: stack growth can relocate it. */
        if(!wd_rt_unwinding(ctx)) wd_m_seq_append(ctx,&stack,&child);
    }
    wd_m_release(stack); jb_finish(&b,out);
}
