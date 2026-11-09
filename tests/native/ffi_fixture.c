/* Trusted C ABI oracle. C17, freestanding, no C/C++ runtime dependencies. */
#include <stdint.h>
int64_t wood_saturating_add(int64_t a,int64_t b) {
    if(b>0 && a>INT64_MAX-b) return INT64_MAX;
    if(b<0 && a<INT64_MIN-b) return INT64_MIN;
    return a+b;
}
double wood_native_lerp(double a,double b,double t) { return a*(1.0-t)+b*t; }
void wood_native_accept(int64_t n,double v) { (void)n; (void)v; }
int64_t wood_bytes_sum(const char *p,int64_t n) {
    int64_t total=0; for(int64_t i=0;i<n;++i) total+=(unsigned char)p[i]; return total;
}
int64_t wood_native_fold(int64_t n,int64_t (*f)(int64_t,int64_t)) {
    int64_t total=0; for(int64_t i=0;i<n;++i) total=f(total,i); return total;
}
double wood_native_map(double value,double (*f)(double)) { return f(value); }
void wood_native_visit(int64_t value,void (*f)(int64_t)) { f(value); }
int64_t wood_native_pair(int64_t (*a)(int64_t),int64_t (*b)(int64_t)) {
    int64_t value=b(3); return a(2)+value;
}
double wood_array_sum(const double *p,int64_t n) {
    double total=0; for(int64_t i=0;i<n;++i) total+=p[i]; return total;
}
void wood_array_scale(double *p,int64_t n,double factor) {
    for(int64_t i=0;i<n;++i) p[i]*=factor;
}
void wood_array_copy(double *out,int64_t n,const double *in,int64_t m) {
    if(n==m) for(int64_t i=0;i<n;++i) out[i]=in[i];
}
int64_t wood_int_sum(const int64_t *p,int64_t n) {
    int64_t total=0; for(int64_t i=0;i<n;++i) total+=p[i]; return total;
}
void wood_int_scale(int64_t *p,int64_t n,int64_t factor) {
    for(int64_t i=0;i<n;++i) p[i]*=factor;
}
/* ABI register/stack transitions and a callback with the same signature. */
double wood_mixed(int64_t a,double b,int64_t c,double d,int64_t e,double f,
                  int64_t g,double h,int64_t i,double j,int64_t k,double l,
                  int64_t m,double n,double o,double p,double q) {
    return (double)(a+c+e+g+i+k+m)+b+d+f+h+j+l+n+o+p+q;
}
int64_t wood_null_buffers(const int64_t *a,int64_t n,const double *b,int64_t m) {
    return a==0 && b==0 && n==0 && m==0;
}
static int64_t calls;
int64_t wood_foreign_calls(void) { return calls; }
int64_t wood_failure_probe(int64_t (*f)(int64_t),double *p,int64_t n) {
    ++calls;
    int64_t first=f(1);
    if(n) p[0]=123.0;
    int64_t second=f(2);
    return first+second;
}
void wood_zero(void (*f)(void)) { f(); }
