/* Wood's private freestanding adapter, NOT musl's internal libm.h.
   Only binary64 kernels are included. No hosted math calls or ABI changes.
   The original six musl 1.2.5 .c files remain byte-for-byte unchanged. */
#ifndef WD_TRIG_LIBM_H
#define WD_TRIG_LIBM_H
#include <stdint.h>
#if __FLT_EVAL_METHOD__ != 0 || __DBL_MANT_DIG__ != 53 || __DBL_MAX_EXP__ != 1024
#error Wood native trig requires direct IEEE binary64 evaluation
#endif
#define FLT_EVAL_METHOD 0
#define DBL_EPSILON 0x1p-52
/* Omit the unused extended-precision reduction table. */
#define LDBL_MAX_EXP 1024
typedef double double_t;
#define predict_false(x) (x)
#define GET_HIGH_WORD(out,x) do { union { double f; uint64_t i; } wd_u={x}; (out)=(uint32_t)(wd_u.i>>32); } while(0)
#define FORCE_EVAL(x) do { volatile double wd_eval=(x); (void)wd_eval; } while(0)

/* Internal reduction operands only: finite, nonnegative, < 2^63, and powers
   of two within the normal binary64 exponent range. The qualified binary64
   reducer uses at most 20 base-2^24 chunks; these helpers are not public floor
   or scalbn implementations. Check their preconditions before conversions. */
static double wd_trig_floor(double x) {
    if(!(x>=0.0 && x<0x1p63)) __builtin_trap();
    return (double)(int64_t)x;
}
static double wd_trig_scale(double x,int n) {
    if(n < -1022 || n > 1023) __builtin_trap();
    union { uint64_t i; double f; } factor={(uint64_t)(n+1023)<<52};
    return x*factor.f;
}
#define floor wd_trig_floor
#define scalbn wd_trig_scale
#define __sin wd_trig_kernel_sin
#define __cos wd_trig_kernel_cos
#define __rem_pio2 wd_trig_reduce
#define __rem_pio2_large wd_trig_reduce_large
#define sin wd_trig_sin
#define cos wd_trig_cos
static double __sin(double,double,int);
static double __cos(double,double);
static int __rem_pio2(double,double*);
static int __rem_pio2_large(double*,double*,int,int,int);
static double sin(double);
static double cos(double);
#endif
