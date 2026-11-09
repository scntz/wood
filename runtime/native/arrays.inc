/* ABI-4 extension: opaque numeric descriptors own contiguous typed buffers.
   Descriptors and buffers are independently counted; views are value snapshots.
   All allocation/error paths preserve existing inputs. No hosted math library. */
#pragma STDC FP_CONTRACT OFF
typedef struct {
    WOwner owner;
    WSeq *storage;
    WInt rank, count, offset, floating;
    WInt dims[8], strides[8];
} WArray;
static void a_error(void *ctx,const char *text) { fail(ctx,"R4026",text); }
static void a_destroy(void *p) { wd_m_release(((WArray*)p)->storage); }
static WArray *a_descriptor(void *ctx) {
    WArray *a=(WArray*)allocate(ctx,sizeof(WArray),a_destroy);
    if(a) zero_bytes((unsigned char*)a+sizeof(WOwner),sizeof(WArray)-sizeof(WOwner));
    return a;
}
static int a_shape(void *ctx,WArray *a,const WInt *dims,WInt rank) {
    if(rank<1 || rank>8) { a_error(ctx,"array rank must be in 1..8"); return 0; }
    a->rank=rank; WInt count=1;
    for(WInt i=rank;i-- >0;) {
        WInt n=dims[i];
        if(n<0 || (n && count>maximum/n)) {
            a_error(ctx,"array dimensions must be nonnegative and their product must fit int"); return 0;
        }
        a->dims[i]=n; a->strides[i]=count; count*=n;
    }
    a->count=count;
    return 1;
}
static WArray *a_full(void *ctx,const WInt *dims,WInt rank,const void *fill,WInt floating) {
    WArray layout={0};
    if(!a_shape(ctx,&layout,dims,rank)) return 0;
    /* Match the qualified host vector ceilings: MSVC uses SIZE_MAX/sizeof(T),
       libstdc++ also bounds its byte distance by PTRDIFF_MAX. */
#ifdef _WIN32
    WSize native_limit=UINT64_MAX/8;
#else
    WSize native_limit=(WSize)INT64_MAX/8;
#endif
    if((WSize)layout.count>native_limit) {
        a_error(ctx,"array exceeds native allocation limits"); return 0;
    }
    if(layout.count>(maximum-(WInt)sizeof(WSeq))/8) {
        /* No qualified host can allocate this span; the ABI-4 signed buffer
           arithmetic cannot include its header. Preserve allocation-failure
           classification without issuing an enormous OS request. */
#ifdef _WIN32
        fail(ctx,"R4099","bad allocation");
#else
        fail(ctx,"R4099","memory allocation failed");
#endif
        return 0;
    }
    WArray *a=a_descriptor(ctx); if(!a) return 0;
    a->rank=rank; a->count=layout.count; a->floating=floating;
    copy_bytes(a->dims,layout.dims,sizeof a->dims);
    copy_bytes(a->strides,layout.strides,sizeof a->strides);
    if(a->count) {
        a->storage=wd_m_seq_new(ctx,8,a->count,a->count,0,0);
        if(!a->storage) { wd_m_release(a); return 0; }
        if(floating) for(WInt i=0;i<a->count;++i) ((double*)data(a->storage))[i]=fill?*(const double*)fill:0.0;
        else for(WInt i=0;i<a->count;++i) ((WInt*)data(a->storage))[i]=fill?*(const WInt*)fill:0;
    } else for(WInt i=0;i<rank;++i) a->strides[i]=0;
    return a;
}
void *wd_m_array_full(void *ctx,const WInt *dims,WInt rank,const void *fill,WInt floating) {
    return a_full(ctx,dims,rank,fill,floating);
}
WInt wd_m_array_len(const void *p) { return p?((const WArray*)p)->count:0; }
WInt wd_m_array_rank(const void *p) { return p?((const WArray*)p)->rank:1; }
WInt wd_m_array_contiguous(const void *p) {
    const WArray *a=p; if(!a || !a->count) return 1;
    WInt stride=1;
    for(WInt i=a->rank;i-- >0;) {
        if(a->dims[i]>1 && a->strides[i]!=stride) return 0;
        stride*=a->dims[i];
    }
    return 1;
}
static WInt a_physical(const WArray *a,WInt flat) {
    WInt at=a->offset;
    for(WInt i=a->rank;i-- >0;) { at+=(flat%a->dims[i])*a->strides[i]; flat/=a->dims[i]; }
    return at;
}
void *wd_m_array_flat(const void *p,WInt flat) {
    const WArray *a=p;
    WInt at=wd_m_array_contiguous(a)?a->offset+flat:a_physical(a,flat);
    return data(a->storage)+8*at;
}
static int a_coordinate(void *ctx,const WArray *a,const WInt *indices,WInt rank,WInt *at) {
    if(rank!=a->rank) { a_error(ctx,"one integer index is required for each array axis"); return 0; }
    *at=a->offset;
    for(WInt i=0;i<rank;++i) {
        WInt n=indices[i];
        if(n<0) n+=a->dims[i];
        if(n<0 || n>=a->dims[i]) { fail(ctx,"R4006","index out of bounds"); return 0; }
        *at+=n*a->strides[i];
    }
    return 1;
}
void *wd_m_array_at(void *ctx,const void *p,const WInt *indices,WInt rank) {
    const WArray *a=p; WInt at;
    if(!a_coordinate(ctx,a,indices,rank,&at)) return 0;
    return data(a->storage)+8*at;
}
void *wd_m_array_copy(void *ctx,const void *p) {
    const WArray *a=p;
    WArray *out=a_full(ctx,a->dims,a->rank,0,a->floating);
    if(out) for(WInt i=0;i<a->count;++i) copy_bytes(data(out->storage)+8*i,wd_m_array_flat(a,i),8);
    return out;
}
void *wd_m_array_edit(void *ctx,void **slot,const WInt *indices,WInt rank) {
    WArray *a=*slot; WInt at;
    if(!a_coordinate(ctx,a,indices,rank,&at)) return 0;
    if(owner_refs(&a->owner)>1 || (a->storage && owner_refs(&a->storage->owner)>1)) {
        WArray *next=wd_m_array_copy(ctx,a); if(!next) return 0;
        *slot=next; wd_m_release(a); a=next;
        if(!a_coordinate(ctx,a,indices,rank,&at)) __builtin_trap();
    }
    return data(a->storage)+8*at;
}
void *wd_m_array_borrow(void *ctx,void **slot,WInt writable) {
    WArray *a=*slot;
    if(!wd_m_array_contiguous(a)) {
        a_error(ctx,writable?"mutable native buffer imports require contiguous arrays; use num.copy":
                             "native buffer imports require contiguous arrays; use num.copy");
        return 0;
    }
    if(writable && (owner_refs(&a->owner)>1 || (a->storage && owner_refs(&a->storage->owner)>1))) {
        WArray *next=wd_m_array_copy(ctx,a); if(!next) return 0;
        *slot=next; wd_m_release(a); a=next;
    }
    return a->count?data(a->storage)+8*a->offset:0;
}
void *wd_m_array_from_list(void *ctx,const WSeq *values,WInt floating) {
    WInt size=wd_m_seq_len(values);
    WArray *a=a_full(ctx,&size,1,0,floating);
    if(a && size) copy_bytes(data(a->storage),data(values),(WSize)size*8);
    return a;
}
void *wd_m_array_to_list(void *ctx,const void *p) {
    const WArray *a=p; WSeq *out=wd_m_seq_new(ctx,8,a->count,a->count,0,0);
    if(out) for(WInt i=0;i<a->count;++i) copy_bytes(data(out)+8*i,wd_m_array_flat(a,i),8);
    return out;
}
void *wd_m_array_shape(void *ctx,const void *p) {
    const WArray *a=p;
    WSeq *out=wd_m_seq_new(ctx,8,a->rank,a->rank,0,0);
    if(out) copy_bytes(data(out),a->dims,(WSize)a->rank*8);
    return out;
}
static WArray *a_view(void *ctx,const WArray *a) {
    WArray *out=a_descriptor(ctx);
    if(out) { copy_bytes((unsigned char*)out+sizeof(WOwner),(const unsigned char*)a+sizeof(WOwner),sizeof(WArray)-sizeof(WOwner)); wd_m_retain(out->storage); }
    return out;
}
void *wd_m_array_reshape(void *ctx,const void *p,const WInt *dims,WInt rank) {
    const WArray *a=p; WArray shape={0};
    if(!a_shape(ctx,&shape,dims,rank)) return 0;
    if(a->count!=shape.count) { a_error(ctx,"reshape must preserve the number of elements"); return 0; }
    WArray *out=wd_m_array_contiguous(a)?a_view(ctx,a):wd_m_array_copy(ctx,a);
    if(out) { out->rank=rank; copy_bytes(out->dims,shape.dims,sizeof shape.dims); copy_bytes(out->strides,shape.strides,sizeof shape.strides); }
    return out;
}
void *wd_m_array_transpose(void *ctx,const void *p,const WSeq *axes) {
    const WArray *a=p; WInt size=wd_m_seq_len(axes); int seen[8]={0};
    if(size && size!=a->rank) { a_error(ctx,"transpose requires a permutation of all axes"); return 0; }
    WInt order[8];
    for(WInt i=0;i<a->rank;++i) {
        WInt axis=size?*(const WInt*)wd_m_seq_read(axes,i):a->rank-1-i;
        if(axis<0 || axis>=a->rank || seen[axis]) { a_error(ctx,"transpose axes must form a permutation"); return 0; }
        seen[axis]=1; order[i]=axis;
    }
    WArray *out=a_view(ctx,a);
    if(out) for(WInt i=0;i<a->rank;++i) { out->dims[i]=a->dims[order[i]]; out->strides[i]=a->strides[order[i]]; }
    return out;
}
void *wd_m_array_slice(void *ctx,const void *p,WInt axis,WInt first,WInt last,WInt step,WInt has_first,WInt has_last) {
    const WArray *a=p;
    if(axis<0) axis+=a->rank;
    if(axis<0 || axis>=a->rank) { fail(ctx,"R4006","index out of bounds"); return 0; }
    Slice s=slice(ctx,a->dims[axis],first,last,step,has_first,has_last);
    if(wd_rt_unwinding(ctx)) return 0;
    WInt stride=a->strides[axis];
    if(s.count>1 && __builtin_mul_overflow(stride,step,&stride)) {
        fail(ctx,"R4001","integer overflow in multiplication"); return 0;
    }
    WArray *out=a_view(ctx,a); if(!out) return 0;
    out->dims[axis]=s.count; out->strides[axis]=stride; out->count=a->count?1:0;
    for(WInt i=0;i<a->rank;++i) out->count*=out->dims[i];
    out->offset=out->count?a->offset+s.first*a->strides[axis]:0;
    return out;
}
void *wd_m_array_convert(void *ctx,const void *p,WInt floating) {
    const WArray *a=p;
    if(a->floating==floating) { wd_m_retain((void*)a); return (void*)a; }
    WArray *out=a_full(ctx,a->dims,a->rank,0,floating);
    if(!out) return 0;
    for(WInt i=0;i<a->count;++i) {
        if(floating) ((double*)data(out->storage))[i]=(double)*(const WInt*)wd_m_array_flat(a,i);
        else {
            double value=*(const double*)wd_m_array_flat(a,i);
            if(!(value>=-9223372036854775808.0 && value<9223372036854775808.0)) {
                fail(ctx,"R4003","float is outside the signed 64-bit range"); wd_m_release(out); return 0;
            }
            ((WInt*)data(out->storage))[i]=(WInt)value;
        }
    }
    return out;
}
WInt wd_m_array_equal(const void *ap,const void *bp) {
    const WArray *a=ap,*b=bp;
    if(!a || !b) return a==b;
    if(a->rank!=b->rank) return 0;
    for(WInt i=0;i<a->rank;++i) if(a->dims[i]!=b->dims[i]) return 0;
    for(WInt i=0;i<a->count;++i) {
        if(a->floating) { if(*(const double*)wd_m_array_flat(a,i)!=*(const double*)wd_m_array_flat(b,i)) return 0; }
        else if(*(const WInt*)wd_m_array_flat(a,i)!=*(const WInt*)wd_m_array_flat(b,i)) return 0;
    }
    return 1;
}
static int a_int(void *ctx,WInt x,WInt y,WInt op,WInt *out) {
    int overflow=op==0?__builtin_add_overflow(x,y,out):op==1?__builtin_sub_overflow(x,y,out):__builtin_mul_overflow(x,y,out);
    if(overflow) fail(ctx,"R4001",op==0?"integer overflow in addition":op==1?"integer overflow in subtraction":"integer overflow in multiplication");
    return !overflow;
}
static double a_number(const void *p,WInt kind,WInt index) {
    const void *at=kind<2?p:wd_m_array_flat(p,index);
    return (kind&1)?*(const double*)at:(double)*(const WInt*)at;
}
void *wd_m_array_binary(void *ctx,const void *ap,WInt ak,const void *bp,WInt bk,WInt op) {
    const WArray *shape=ak>=2?ap:bp;
    if(ak>=2 && bk>=2) {
        const WArray *b=bp;
        int same=shape->rank==b->rank;
        for(WInt i=0;same && i<shape->rank;++i) same=shape->dims[i]==b->dims[i];
        if(!same) { a_error(ctx,"elementwise arrays must have identical shapes"); return 0; }
    }
    /* As in the reference kernel, checked division errors precede allocation.
       Inputs cannot change during this synchronous, callback-free kernel. */
    if(op==3) for(WInt i=0;i<shape->count;++i) {
        if(a_number(bp,bk,i)==0) { fail(ctx,"R4002","division by zero in array"); return 0; }
    }
    WInt floating=(ak&1)||(bk&1)||op==3;
    WArray *out=a_full(ctx,shape->dims,shape->rank,0,floating); if(!out) return 0;
    for(WInt i=0;i<out->count;++i) {
        if(floating) {
            double x=a_number(ap,ak,i),y=a_number(bp,bk,i);
            ((double*)data(out->storage))[i]=op==0?x+y:op==1?x-y:op==2?x*y:x/y;
        } else {
            WInt x=*(const WInt*)(ak<2?ap:wd_m_array_flat(ap,i));
            WInt y=*(const WInt*)(bk<2?bp:wd_m_array_flat(bp,i));
            if(!a_int(ctx,x,y,op,(WInt*)data(out->storage)+i)) { wd_m_release(out); return 0; }
        }
    }
    return out;
}
typedef struct { double total,correction; } ASum;
static void a_add(ASum *s,double value) {
    double next=s->total+value;
    if((n_bits(next)&UINT64_C(0x7fffffffffffffff))>=UINT64_C(0x7ff0000000000000)) { s->total=next; s->correction=0; return; }
    double a=n_double(n_bits(s->total)&UINT64_C(0x7fffffffffffffff));
    double b=n_double(n_bits(value)&UINT64_C(0x7fffffffffffffff));
    s->correction+=a>=b?(s->total-next)+value:(value-next)+s->total;
    s->total=next;
}
static int a_dot_shape(void *ctx,const WArray *a,const WArray *b) {
    if(b && (a->rank!=1 || b->rank!=1 || a->count!=b->count)) { a_error(ctx,"dot requires equal-length rank-one arrays"); return 0; }
    return 1;
}
WInt wd_m_array_reduce_i(void *ctx,const void *ap,const void *bp) {
    const WArray *a=ap,*b=bp; WInt total=0;
    if(!a_dot_shape(ctx,a,b)) return 0;
    for(WInt i=0;i<a->count;++i) {
        WInt value=*(const WInt*)wd_m_array_flat(a,i);
        if(b && !a_int(ctx,value,*(const WInt*)wd_m_array_flat(b,i),2,&value)) return 0;
        if(!a_int(ctx,total,value,0,&total)) return 0;
    }
    return total;
}
double wd_m_array_reduce_f(void *ctx,const void *ap,const void *bp) {
    const WArray *a=ap,*b=bp; ASum s={0};
    if(!a_dot_shape(ctx,a,b)) return 0;
    for(WInt i=0;i<a->count;++i) {
        double value=*(const double*)wd_m_array_flat(a,i);
        if(b) value*=*(const double*)wd_m_array_flat(b,i);
        a_add(&s,value);
    }
    return s.total+s.correction;
}
void *wd_m_array_matmul(void *ctx,const void *ap,const void *bp) {
    const WArray *a=ap,*b=bp;
    if(a->rank!=2 || b->rank!=2 || a->dims[1]!=b->dims[0]) { a_error(ctx,"matmul requires (m,k) and (k,n) float arrays"); return 0; }
    WInt dims[2]={a->dims[0],b->dims[1]};
    WArray *out=a_full(ctx,dims,2,0,1); if(!out || !out->count || !a->dims[1]) return out;
    const double *left=(const double*)data(a->storage)+a->offset,*right=(const double*)data(b->storage)+b->offset;
    double *output=(double*)data(out->storage); WInt m=dims[0],k=a->dims[1],n=dims[1];
    for(WInt i=0;i<m;++i) for(WInt inner=0;inner<k;++inner) {
        double value=left[i*a->strides[0]+inner*a->strides[1]];
        for(WInt j=0;j<n;++j) output[i*n+j]+=value*right[inner*b->strides[0]+j*b->strides[1]];
    }
    return out;
}
void *wd_m_array_affine(void *ctx,const void *pp,const void *mp) {
    const WArray *p=pp,*m=mp;
    if(p->rank!=2 || p->dims[1]!=3 || m->rank!=2 || m->dims[0]!=4 || m->dims[1]!=4) {
        a_error(ctx,"affine transform requires (n,3) points and a (4,4) matrix"); return 0;
    }
    double coefficients[12];
    for(WInt i=0;i<16;++i) {
        double v=*(const double*)wd_m_array_flat(m,i);
        if(i>=12 && v!=(i==15?1.0:0.0)) { a_error(ctx,"affine transform does not accept a projective matrix"); return 0; }
        if(i<12) coefficients[i]=v;
    }
    WArray *out=a_full(ctx,p->dims,2,0,1); if(!out) return 0;
    for(WInt row=0;row<p->dims[0];++row) {
        double x=*(const double*)wd_m_array_flat(p,row*3),y=*(const double*)wd_m_array_flat(p,row*3+1),z=*(const double*)wd_m_array_flat(p,row*3+2);
        for(WInt axis=0;axis<3;++axis) {
            double v=0; v+=x*coefficients[axis*4]; v+=y*coefficients[axis*4+1]; v+=z*coefficients[axis*4+2];
            ((double*)data(out->storage))[row*3+axis]=v+coefficients[axis*4+3];
        }
    }
    return out;
}
void wd_m_array_str(void *ctx,WStr *out,const void *p) {
    const WArray *a=p; JBuffer b={ctx,0}; jb_word(&b,"array(shape=[");
    for(WInt i=0;i<a->rank;++i) {
        if(i) jb_word(&b,", "); WStr s={0}; wd_m_int_str(ctx,&s,a->dims[i]); jb_add(&b,s.data,s.size); wd_m_str_drop(&s);
    }
    jb_word(&b,"], values=[");
    for(WInt i=0;i<a->count && !wd_rt_unwinding(ctx);++i) {
        if(i) jb_word(&b,", "); WStr s={0}; const void *v=wd_m_array_flat(a,i);
        if(a->floating) wd_m_float_str(ctx,&s,*(const double*)v); else wd_m_int_str(ctx,&s,*(const WInt*)v);
        jb_add(&b,s.data,s.size); wd_m_str_drop(&s);
    }
    jb_word(&b,"])"); jb_finish(&b,out);
}
