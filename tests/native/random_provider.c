/* Fault injection around the actual extracted entropy loop, not a substitute
   for the separate real-OS Wood execution tests. No entropy-quality claim. */
#include "managed.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int mode,calls,failed,force_cancel;
static WInt observed_flags;
static const char *failure;
WInt wd_rt_unwinding(void *ctx) { (void)ctx; return failed; }
WInt wd_m_task_cancelled(void) { return force_cancel || (mode==3 && calls>=2); }
static void fail(void *ctx,const char *code,const char *text) {
    (void)ctx; (void)text; failed=1; failure=code;
}
static WInt d_sys(WInt number,WInt address,WInt count,WInt flags,WInt d,WInt e,WInt f) {
    (void)d; (void)e; (void)f;
    assert(number==318 && count>0 && count<=256);
    observed_flags=flags; ++calls;
    if(mode==1 && calls==1) return -4;
    if(mode==3) return -4;
    if(mode==2 && calls>2) return -11;
    if(mode==4) return count+1;
    WInt n=(mode==1 || mode==2) && count>3?3:count;
    unsigned char *out=(unsigned char*)(intptr_t)address;
    for(WInt i=0;i<n;++i) out[i]=0x5a;
    return n;
}
#include "random-helper.h"
static void reset(int value) { mode=value; calls=failed=force_cancel=0; failure=0; observed_flags=-1; }
int main(void) {
    unsigned char bytes[600];
    reset(0); assert(r_random(0,bytes,sizeof bytes,1,"R4008"));
    assert(calls==3 && observed_flags==1 && bytes[599]==0x5a);
    reset(1); assert(r_random(0,bytes,sizeof bytes,1,"R4008"));
    assert(calls>3 && bytes[599]==0x5a);
    reset(2); assert(!r_random(0,bytes,sizeof bytes,1,"R4008"));
    assert(calls==3 && failed && !strcmp(failure,"R4008"));
    reset(3); assert(!r_random(0,bytes,sizeof bytes,1,"R4008"));
    assert(calls==2 && !strcmp(failure,"R4022"));
    reset(4); assert(!r_random(0,bytes,sizeof bytes,1,"R4008"));
    assert(calls==1 && !strcmp(failure,"R4008"));
    reset(0); force_cancel=1;
    assert(!r_random(0,bytes,16,1,"R4008") && calls==0 && !strcmp(failure,"R4022"));
    reset(0); force_cancel=1;
    assert(r_random(0,bytes,16,0,"D5001") && calls==1 && observed_flags==0);
    reset(0); failed=1;
    assert(!r_random(0,bytes,16,1,"R4008") && calls==0 && failure==0);
    puts("actual entropy loop fault cases passed");
}
