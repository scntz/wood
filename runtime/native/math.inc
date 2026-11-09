/* Full-range binary64 sin/cos. See math/README.md and preserved Sun notices. */
#pragma STDC FP_CONTRACT OFF
#include "math/musl/__rem_pio2_large.c"
#include "math/musl/__rem_pio2.c"
#include "math/musl/__sin.c"
#include "math/musl/__cos.c"
#include "math/musl/sin.c"
#include "math/musl/cos.c"
#undef floor
#undef scalbn
#undef __sin
#undef __cos
#undef __rem_pio2
#undef __rem_pio2_large
#undef sin
#undef cos
#undef FORCE_EVAL
#undef GET_HIGH_WORD
#undef predict_false
#undef FLT_EVAL_METHOD
#undef DBL_EPSILON
#undef LDBL_MAX_EXP
#undef EPS
double wd_m_math_sin(void *ctx,double x) {
    union { double f; uint64_t i; } bits={x};
    if((bits.i>>52 & 2047)==2047) { fail(ctx,"R4027","sin requires a finite value"); return 0; }
    return wd_trig_sin(x);
}
double wd_m_math_cos(void *ctx,double x) {
    union { double f; uint64_t i; } bits={x};
    if((bits.i>>52 & 2047)==2047) { fail(ctx,"R4027","cos requires a finite value"); return 0; }
    return wd_trig_cos(x);
}
