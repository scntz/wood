/* Exercise the real extracted Windows conversion helper with a length-query
   fault injector and an allocator that refuses requests. No huge allocation or
   claim of real Windows Unicode/OS behavior is involved in this arithmetic test. */
#include "managed.h"
#include <assert.h>
#include <stdio.h>
static int count,errors;
static WInt requested;
static int p_valid(void *ctx,const WStr *text) { (void)ctx; (void)text; return 1; }
static void fail(void *ctx,const char *code,const char *message) {
    (void)ctx; (void)code; (void)message; ++errors;
}
static void no_children(void *owner) { (void)owner; }
static WOwner *allocate(void *ctx,WInt bytes,WSlot destroy) {
    (void)ctx; (void)destroy; requested=bytes; return 0;
}
void wd_m_release(void *owner) { (void)owner; }
static int MultiByteToWideChar(unsigned page,unsigned flags,const char *input,
                               int size,unsigned short *out,int capacity) {
    (void)page; (void)flags; (void)input; (void)size; (void)out; (void)capacity;
    return count;
}
#include "wide-helper.h"
int main(void) {
    WStr input={(unsigned char*)"x",1,0}; WOwner *owner=0;
    const int lengths[]={1,32767,INT32_MAX-1,INT32_MAX};
    for(unsigned i=0;i<sizeof(lengths)/sizeof(lengths[0]);++i) {
        count=lengths[i]; requested=0; errors=0;
        assert(p_wide(0,&input,&owner)==0 && owner==0);
        assert(requested==(WInt)sizeof(WOwner)+((WInt)count+1)*2);
        assert(errors==0);
    }
    requested=0; errors=0; input.size=(WInt)INT32_MAX+1;
    assert(p_wide(0,&input,&owner)==0 && requested==0 && errors==1);
    input.size=0; requested=0; errors=0;
    assert(p_wide(0,&input,&owner)==0 && requested==(WInt)sizeof(WOwner)+2);
    assert(errors==0);
    puts("Windows UTF-16 reservation arithmetic passed");
}
