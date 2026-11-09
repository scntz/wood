/* Test-only hosted C harness around the actual optional adapter archive. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
extern int64_t wood_gui_run(const unsigned char*,int64_t,int64_t,int64_t,
    const unsigned char*,int64_t,int64_t(*)(int64_t),int64_t*,int64_t);
extern int64_t wood_gui_control(int64_t,int64_t,int64_t,const unsigned char*,int64_t,
    int64_t,int64_t,int64_t,int64_t,int64_t);
extern int64_t wood_gui_post(int64_t,int64_t,const unsigned char*,int64_t);
static int64_t body(int64_t token) {
    assert(wood_gui_control(token,1,3,(const unsigned char*)"owned",5,8,8,100,28,0)==0);
    assert(wood_gui_post(token,1,(const unsigned char*)"queued",6)==1);
    return token;
}
static void cycle(void) {
    int64_t result=0;
    assert(wood_gui_run((const unsigned char*)"Lifecycle",9,320,200,
                       (const unsigned char*)"context",7,body,&result,1)==0);
    assert(result>0 && wood_gui_post(result,1,0,0)==0);
}
int main(void) {
    cycle(); /* OS lazy initialization is outside the retained-resource sample. */
    DWORD before_user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    DWORD before_gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int i=0;i<64;++i) cycle();
    DWORD after_user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    DWORD after_gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    printf("USER objects %lu -> %lu; GDI objects %lu -> %lu; 64 adapter lifecycles\n",
           (unsigned long)before_user,(unsigned long)after_user,(unsigned long)before_gdi,(unsigned long)after_gdi);
    assert(after_user==before_user && after_gdi==before_gdi);
    return 0;
}
