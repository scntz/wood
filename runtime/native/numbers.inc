/* Exact finite decimal/binary64 conversion, independent of locale and libc.
   Fixed-size integers bound scratch space even for multi-megabyte inputs.
   A binary64 rounding boundary has at most 1075 fractional decimal places;
   1100 retained significant digits plus a sticky tail distinguish every one. */
typedef struct { uint32_t v[256]; unsigned n; } NBig;
static NBig nb_u(uint64_t v) {
    NBig a = {{0}, 0};
    while (v) { a.v[a.n++] = (uint32_t)v; v >>= 32; }
    return a;
}
static void nb_trim(NBig *a) { while (a->n && !a->v[a->n-1]) --a->n; }
static void nb_mul(NBig *a, unsigned m, unsigned add) {
    uint64_t carry = add;
    for (unsigned i=0;i<a->n;++i) { carry += (uint64_t)a->v[i]*m; a->v[i]=(uint32_t)carry; carry >>= 32; }
    if (carry) { if (a->n == 256) __builtin_trap(); a->v[a->n++]=(uint32_t)carry; }
}
static void nb_shift(NBig *a, unsigned bits) {
    if (!a->n) return;
    unsigned words=bits/32, rest=bits%32;
    if (a->n+words+(rest!=0)>256) __builtin_trap();
    for (unsigned i=a->n;i>0;--i) a->v[i-1+words]=a->v[i-1];
    for (unsigned i=0;i<words;++i) a->v[i]=0;
    a->n+=words;
    if (rest) {
        uint32_t carry=0;
        for (unsigned i=words;i<a->n;++i) {
            uint64_t v=((uint64_t)a->v[i]<<rest)|carry;
            a->v[i]=(uint32_t)v; carry=(uint32_t)(v>>32);
        }
        if (carry) a->v[a->n++]=carry;
    }
}
static int nb_cmp(const NBig *a,const NBig *b) {
    if (a->n!=b->n) return a->n>b->n?1:-1;
    for (unsigned i=a->n;i>0;--i) if (a->v[i-1]!=b->v[i-1]) return a->v[i-1]>b->v[i-1]?1:-1;
    return 0;
}
static unsigned nb_bits(const NBig *a) {
    if (!a->n) return 0;
    uint32_t v=a->v[a->n-1]; unsigned n=0;
    while(v) { ++n; v>>=1; }
    return (a->n-1)*32+n;
}
static void nb_sub(NBig *a,const NBig *b) {
    uint64_t borrow=0;
    for (unsigned i=0;i<a->n;++i) {
        uint64_t sub=(i<b->n?b->v[i]:0)+borrow;
        uint32_t old=a->v[i]; a->v[i]=(uint32_t)((uint64_t)old-sub); borrow=(uint64_t)old<sub;
    }
    nb_trim(a);
}
static void nb_pow10(NBig *a,unsigned n) { while(n--) nb_mul(a,10,0); }
static unsigned nb_div10(NBig *a) {
    uint64_t remainder=0;
    for(unsigned i=a->n;i>0;--i) {
        uint64_t v=(remainder<<32)|a->v[i-1]; a->v[i-1]=(uint32_t)(v/10); remainder=v%10;
    }
    nb_trim(a); return (unsigned)remainder;
}
static uint64_t nb_round(NBig a,NBig b,int sticky) {
    int shift=(int)nb_bits(&a)-(int)nb_bits(&b); uint64_t q=0;
    if (shift>63) __builtin_trap();
    for (int i=shift;i>=0;--i) {
        NBig t=b; nb_shift(&t,(unsigned)i);
        if (nb_cmp(&a,&t)>=0) { nb_sub(&a,&t); q|=UINT64_C(1)<<i; }
    }
    nb_shift(&a,1); int cmp=nb_cmp(&a,&b);
    if (cmp>0 || (cmp==0 && (sticky || (q&1)))) ++q;
    return q;
}
static uint64_t n_bits(double value) { uint64_t result; copy_bytes(&result,&value,8); return result; }
static double n_double(uint64_t value) { double result; copy_bytes(&result,&value,8); return result; }
static int n_digit(unsigned c) { return c>='0' && c<='9'; }
/* Result: 0 success, 1 syntax, 2 range. JSON permits rounded underflow to zero,
   whereas Wood's float(str) rejects a nonzero value rounded to zero. */
static int n_parse(const WStr *s,double *out,int reject_underflow) {
    WInt i=0; int negative=0;
    if (i<s->size && (s->data[i]=='-' || s->data[i]=='+')) negative=s->data[i++]=='-';
    WInt begin=i;
    while(i<s->size && n_digit(s->data[i])) ++i;
    if (i==begin) return 1;
    WInt integer_end=i, fraction=0;
    if (i<s->size && s->data[i]=='.') {
        WInt start=++i;
        while(i<s->size && n_digit(s->data[i])) ++i;
        if (i==start) return 1;
        fraction=i-start;
    }
    WInt mantissa_end=i, exponent=0;
    if (i<s->size && (s->data[i]=='e' || s->data[i]=='E')) {
        ++i; int neg=0;
        if (i<s->size && (s->data[i]=='+' || s->data[i]=='-')) neg=s->data[i++]=='-';
        WInt start=i;
        while(i<s->size && n_digit(s->data[i])) { if(exponent<1000000000) exponent=exponent*10+s->data[i]-'0'; ++i; }
        if(i==start) return 1;
        if(neg) exponent=-exponent;
    }
    if(i!=s->size) return 1;
    NBig a=nb_u(0); WInt kept=0,dropped=0; int nonzero=0,sticky=0;
    for(i=begin;i<mantissa_end;++i) {
        if(i==integer_end && s->data[i]=='.') continue;
        unsigned digit=s->data[i]-'0';
        if(!nonzero && !digit) continue;
        nonzero=1;
        if(kept<1100) { nb_mul(&a,10,digit); ++kept; }
        else { ++dropped; sticky|=digit!=0; }
    }
    uint64_t sign=(uint64_t)negative<<63;
    if(!nonzero) { *out=n_double(sign); return 0; }
    exponent+=dropped-fraction;
    WInt order=kept+exponent-1;
    if(order>308) return 2;
    if(order< -324) { *out=n_double(sign); return reject_underflow?2:0; }
    NBig b=nb_u(1);
    if(exponent>=0) nb_pow10(&a,(unsigned)exponent); else nb_pow10(&b,(unsigned)-exponent);
    int e=(int)nb_bits(&a)-(int)nb_bits(&b);
    NBig test=e>=0?b:a; nb_shift(&test,(unsigned)(e>=0?e:-e));
    if((e>=0?nb_cmp(&a,&test):nb_cmp(&test,&b))<0) --e;
    int shift=e< -1022?1074:52-e;
    if(shift>=0) nb_shift(&a,(unsigned)shift); else nb_shift(&b,(unsigned)-shift);
    uint64_t q=nb_round(a,b,sticky), bits;
    if(e< -1022) bits=q;
    else {
        if(q==UINT64_C(0x20000000000000)) { q>>=1; ++e; }
        if(e>1023) return 2;
        bits=((uint64_t)(e+1023)<<52)|(q&UINT64_C(0xfffffffffffff));
    }
    *out=n_double(sign|bits);
    return !bits && reject_underflow?2:0;
}
double wd_m_str_float(void *ctx,const WStr *text) {
    double out=0; int result=n_parse(text,&out,1);
    if(result) fail(ctx,"R4003",result==1?"invalid finite decimal float":"float is outside the finite binary64 range");
    return out;
}
static unsigned n_uint(unsigned char *out,uint64_t value) {
    unsigned char reverse[24]; unsigned n=0;
    do { reverse[n++]=(unsigned char)('0'+value%10); value/=10; } while(value);
    for(unsigned i=0;i<n;++i) out[i]=reverse[n-i-1];
    return n;
}
static unsigned n_scientific(unsigned char *out,const unsigned char *digits,unsigned n,int exponent) {
    unsigned p=0; out[p++]=digits[0];
    if(n>1) { out[p++]='.'; for(unsigned i=1;i<n;++i) out[p++]=digits[i]; }
    out[p++]='e'; out[p++]=exponent<0?'-':'+';
    unsigned e=(unsigned)(exponent<0?-exponent:exponent);
    if(e<10) out[p++]='0';
    return p+n_uint(out+p,e);
}
/* Search significant precision 1..17 using exact rational rounding. Choose the
   shorter fixed/scientific representation (fixed wins ties), as to_chars does.
   The candidate is checked by our independently directed decimal parser. */
void wd_m_float_str(void *ctx,WStr *out,double value) {
    unsigned char result[768],digits[24],sci[32],fixed[768]; unsigned p=0;
    uint64_t bits=n_bits(value), magnitude=bits&UINT64_C(0x7fffffffffffffff);
    if(bits>>63) result[p++]='-';
    if(magnitude>=UINT64_C(0x7ff0000000000000)) {
        const char *word=magnitude==UINT64_C(0x7ff0000000000000)?"inf":"nan";
        for(unsigned i=0;i<3;++i) result[p++]=(unsigned char)word[i];
    } else if(!magnitude) { result[p++]='0'; result[p++]='.'; result[p++]='0'; }
    else {
        int e=(int)(magnitude>>52), shift;
        uint64_t mantissa=magnitude&UINT64_C(0xfffffffffffff);
        if(e) { mantissa|=UINT64_C(0x10000000000000); shift=e-1023-52; } else shift=-1074;
        NBig a=nb_u(mantissa),b=nb_u(1);
        if(shift>=0) nb_shift(&a,(unsigned)shift); else nb_shift(&b,(unsigned)-shift);
        int k=0; NBig ten=b;
        if(nb_cmp(&a,&b)>=0) {
            for(;;) { NBig next=ten; nb_mul(&next,10,0); if(nb_cmp(&a,&next)<0) break; ten=next; ++k; }
        } else { NBig v=a; do { nb_mul(&v,10,0); --k; } while(nb_cmp(&v,&b)<0); }
        unsigned nd=0; int chosen=k;
        for(unsigned precision=1;precision<=17;++precision) {
            int power=k-(int)precision+1;
            NBig num=a,den=b;
            if(power>=0) nb_pow10(&den,(unsigned)power); else nb_pow10(&num,(unsigned)-power);
            uint64_t q=nb_round(num,den,0);
            nd=n_uint(digits,q); chosen=k;
            if(nd>precision) { ++chosen; --nd; }
            while(nd>1 && digits[nd-1]=='0') --nd;
            unsigned sn=n_scientific(sci,digits,nd,chosen);
            WStr trial={sci,(WInt)sn,0}; double parsed=0;
            if(!n_parse(&trial,&parsed,0) && n_bits(parsed)==magnitude) break;
            if(precision==17) __builtin_trap();
        }
        unsigned sn=n_scientific(sci,digits,nd,chosen),fn=0;
        int point=chosen+1;
        if(point<=0) { fixed[fn++]='0'; fixed[fn++]='.'; for(int i=0;i< -point;++i) fixed[fn++]='0'; }
        for(unsigned i=0;i<nd;++i) { if(point>0 && (int)i==point) fixed[fn++]='.'; fixed[fn++]=digits[i]; }
        if(point>(int)nd) for(int i=(int)nd;i<point;++i) fixed[fn++]='0';
        /* Fixed notation with no fractional places has many equally short
           round-tripping integers. Select the one closest to the exact value,
           rather than padding the shortest significand with zeros. */
        if(point>=(int)nd && fn<=sn) {
            NBig integer=b.n==1 && b.v[0]==1?a:nb_u(nb_round(a,b,0));
            unsigned char reverse[768]; fn=0;
            do { reverse[fn++]=(unsigned char)('0'+nb_div10(&integer)); } while(integer.n);
            for(unsigned i=0;i<fn;++i) fixed[i]=reverse[fn-i-1];
        }
        const unsigned char *chosen_text=fn<=sn?fixed:sci; unsigned count=fn<=sn?fn:sn;
        copy_bytes(result+p,chosen_text,count); p+=count;
        if(fn<=sn && point>=(int)nd) { result[p++]='.'; result[p++]='0'; }
    }
    wd_m_str_new(ctx,out,p);
    if(out->owner) copy_bytes(out->data,result,p);
}
