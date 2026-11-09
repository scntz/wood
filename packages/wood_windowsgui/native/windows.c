/* Optional Win32 adapter. No Wood object/pointer crosses this ABI. All OS
   callbacks are C; owned Wood closures execute in Wood's ordinary event loop. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <commdlg.h>
#include <stdint.h>
#include <limits.h>

#define TEXT_LIMIT 1048576
#define EVENT_LIMIT 1024
#define QUEUE_BYTES 4194304
#define WAKE_MESSAGE (WM_APP+0x127)
enum { E_ID=-1,E_THREAD=-2,E_ARGUMENT=-3,E_TEXT=-4,E_LIMIT=-5,E_CANCEL=-6,E_BUSY=-7 };
enum { EV_CLOSE=1,EV_CLICK,EV_CHANGE,EV_RESIZE,EV_KEY,EV_MOUSE,EV_MESSAGE,EV_SELECT,EV_MENU };
typedef struct Item Item;
struct Item { Item *next; int64_t kind,control,a,b,size; unsigned char bytes[]; };
typedef struct {
    HWND window; DWORD thread; int64_t id; HFONT font; DPI_AWARENESS_CONTEXT dpi;
    WCHAR class_name[64]; ATOM atom; int closing; DWORD fatal;
    Item *head,*tail,*current; unsigned queued,controls; int64_t bytes;
    unsigned char *context; int64_t context_size;
} App;
static SRWLOCK gate=SRWLOCK_INIT;
static App *active;
static int opening;
static int64_t sequence,retired;

static void *reserve(SIZE_T bytes) { return HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,bytes); }
static void release(void *p) { if(p) HeapFree(GetProcessHeap(),0,p); }
static int64_t os_error(void) { DWORD code=GetLastError(); return -1000000-(int64_t)(code?code:ERROR_GEN_FAILURE); }
static int64_t os_code(DWORD code) { return -1000000-(int64_t)code; }
static WCHAR *wide(const unsigned char *text,int64_t size) {
    if(size<0 || size>TEXT_LIMIT || (size && !text)) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    for(int64_t i=0;i<size;++i) if(!text[i]) { SetLastError(ERROR_NO_UNICODE_TRANSLATION); return 0; }
    int n=size?MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char*)text,(int)size,0,0):0;
    if(size && !n) return 0;
    WCHAR *out=reserve(((SIZE_T)n+1)*sizeof(WCHAR));
    if(!out) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return 0; }
    if(n && !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char*)text,(int)size,out,n)) { release(out); return 0; }
    return out;
}
static App *owned(int64_t id,int64_t *error) {
    AcquireSRWLockShared(&gate);
    App *a=active;
    if(!a || a->id!=id || a->closing) { a=0; *error=E_ID; }
    else if(a->thread!=GetCurrentThreadId()) { a=0; *error=E_THREAD; }
    ReleaseSRWLockShared(&gate);
    /* Only this UI thread can close the returned App. Workers never obtain it. */
    return a;
}
static int64_t push_locked(App *a,int64_t kind,int64_t control,int64_t x,int64_t y,const unsigned char *bytes,int64_t size) {
    if(size<0 || size>TEXT_LIMIT || (size && !bytes)) return E_ARGUMENT;
    if(a->queued>=EVENT_LIMIT || size>QUEUE_BYTES-a->bytes) return E_LIMIT;
    Item *item=reserve(sizeof(Item)+(SIZE_T)size);
    if(!item) return os_code(ERROR_NOT_ENOUGH_MEMORY);
    item->kind=kind; item->control=control; item->a=x; item->b=y; item->size=size;
    for(int64_t i=0;i<size;++i) item->bytes[i]=bytes[i];
    if(a->tail) a->tail->next=item; else a->head=item;
    a->tail=item; ++a->queued; a->bytes+=size;
    return 0;
}
static void event(App *a,int64_t kind,int64_t control,int64_t x,int64_t y) {
    AcquireSRWLockExclusive(&gate);
    if(!a->closing && push_locked(a,kind,control,x,y,0,0)<0) a->fatal=ERROR_NOT_ENOUGH_QUOTA;
    ReleaseSRWLockExclusive(&gate);
}
static int pixels(int64_t value,UINT dpi) { return (int)((value*(int64_t)dpi+48)/96); }
static int logical(int value,UINT dpi) { return (int)(((int64_t)value*96)/(dpi?dpi:96)); }
static HFONT font(UINT dpi) {
    return CreateFontW(-MulDiv(9,(int)dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                       DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,
                       DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
}
static BOOL CALLBACK update_font(HWND child,LPARAM value) {
    SendMessageW(child,WM_SETFONT,(WPARAM)value,TRUE); return TRUE;
}
static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l) {
    App *a=(App*)GetWindowLongPtrW(window,GWLP_USERDATA);
    if(message==WM_NCCREATE) {
        a=(App*)((CREATESTRUCTW*)l)->lpCreateParams; a->window=window;
        SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)a);
    }
    if(!a) return DefWindowProcW(window,message,w,l);
    switch(message) {
    case WM_CLOSE: event(a,EV_CLOSE,0,0,0); return 0;
    case WM_COMMAND:
        if(!l && HIWORD(w)==0) event(a,EV_MENU,LOWORD(w),0,0);
        else if(l && HIWORD(w)==BN_CLICKED) event(a,EV_CLICK,LOWORD(w),0,0);
        else if(l && HIWORD(w)==EN_CHANGE) event(a,EV_CHANGE,LOWORD(w),0,0);
        else if(l && HIWORD(w)==LBN_SELCHANGE) event(a,EV_SELECT,LOWORD(w),0,0);
        return 0;
    case WM_SIZE: {
        RECT r; if(GetClientRect(window,&r)) { UINT dpi=GetDpiForWindow(window); event(a,EV_RESIZE,0,logical(r.right,dpi),logical(r.bottom,dpi)); }
        return 0;
    }
    case WM_DPICHANGED: {
        RECT *r=(RECT*)l; HFONT next=font(HIWORD(w));
        if(!next) { a->fatal=ERROR_NOT_ENOUGH_MEMORY; return 0; }
        HFONT previous=a->font; a->font=next;
        EnumChildWindows(window,update_font,(LPARAM)next);
        if(previous) DeleteObject(previous);
        SetWindowPos(window,0,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOACTIVATE|SWP_NOZORDER);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        UINT dpi=GetDpiForWindow(window);
        event(a,EV_MOUSE,1,logical((short)LOWORD(l),dpi),logical((short)HIWORD(l),dpi)); return 0;
    }
    case WAKE_MESSAGE: return 0;
    default: return DefWindowProcW(window,message,w,l);
    }
}
static BOOL CALLBACK release_image(HWND child,LPARAM unused) {
    (void)unused;
    WCHAR name[8]; DWORD kind=(DWORD)GetWindowLongPtrW(child,GWL_STYLE)&SS_TYPEMASK;
    if(kind==SS_BITMAP && GetClassNameW(child,name,8) && (name[0]==L'S' || name[0]==L's')) {
        HBITMAP image=(HBITMAP)SendMessageW(child,STM_SETIMAGE,IMAGE_BITMAP,0);
        if(image) DeleteObject(image);
    }
    return TRUE;
}
static void dispose(App *a) {
    AcquireSRWLockExclusive(&gate);
    a->closing=1;
    if(active==a) active=0;
    retired=a->id;
    ReleaseSRWLockExclusive(&gate);
    if(a->window) {
        MSG message;
        while(PeekMessageW(&message,a->window,WAKE_MESSAGE,WAKE_MESSAGE,PM_REMOVE)) {}
        EnumChildWindows(a->window,release_image,0);
        DestroyWindow(a->window);
    }
    if(a->font) DeleteObject(a->font);
    if(a->atom) UnregisterClassW(a->class_name,GetModuleHandleW(0));
    Item *item=a->head;
    while(item) { Item *next=item->next; release(item); item=next; }
    release(a->current);
    release(a->context);
    if(a->dpi) SetThreadDpiAwarenessContext(a->dpi);
    release(a);
}
int64_t wood_gui_open(const unsigned char *title,int64_t size,int64_t width,int64_t height) {
    if(width<96 || height<96 || width>32767 || height>32767) return E_ARGUMENT;
    WCHAR *name=wide(title,size); if(!name) return os_error();
    App *a=reserve(sizeof(App));
    if(!a) { release(name); return os_code(ERROR_NOT_ENOUGH_MEMORY); }
    AcquireSRWLockExclusive(&gate);
    if(active || opening || sequence==INT64_MAX) { ReleaseSRWLockExclusive(&gate); release(a); release(name); return E_BUSY; }
    opening=1; a->id=++sequence;
    ReleaseSRWLockExclusive(&gate);
    a->thread=GetCurrentThreadId();
    a->dpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int64_t error=0;
    if(!a->dpi) { error=os_error(); goto failed; }
    const WCHAR prefix[]=L"wood_windowsgui_";
    unsigned at=0;
    while(prefix[at]) { a->class_name[at]=prefix[at]; ++at; }
    for(unsigned i=0;i<16;++i) a->class_name[at+i]=L"0123456789abcdef"[((uint64_t)a->id>>(60-i*4))&15];
    WNDCLASSEXW cls={0}; cls.cbSize=sizeof cls; cls.lpfnWndProc=procedure;
    cls.hInstance=GetModuleHandleW(0); cls.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));
    cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); cls.lpszClassName=a->class_name;
    a->atom=RegisterClassExW(&cls);
    if(!a->atom) { error=os_error(); goto failed; }
    UINT dpi=GetDpiForSystem(); if(!dpi) dpi=96;
    RECT rect={0,0,pixels(width,dpi),pixels(height,dpi)};
    if(!AdjustWindowRectExForDpi(&rect,WS_OVERLAPPEDWINDOW,FALSE,WS_EX_CONTROLPARENT,dpi)) { error=os_error(); goto failed; }
    HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,a->class_name,name,WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,0,0,GetModuleHandleW(0),a);
    if(!window) { error=os_error(); goto failed; }
    a->font=font(GetDpiForWindow(window));
    if(!a->font) { error=os_error(); goto failed; }
    AcquireSRWLockExclusive(&gate); active=a; opening=0; ReleaseSRWLockExclusive(&gate);
    release(name); return a->id;
failed:
    release(name); dispose(a);
    AcquireSRWLockExclusive(&gate); opening=0; ReleaseSRWLockExclusive(&gate);
    return error;
}
int64_t wood_gui_close(int64_t id) {
    int64_t error=0; App *a=owned(id,&error);
    if(!a) {
        AcquireSRWLockShared(&gate); int done=id>0 && id==retired; ReleaseSRWLockShared(&gate);
        return done?0:error;
    }
    dispose(a); return 0;
}
int64_t wood_gui_show(int64_t id) {
    int64_t error=0; App *a=owned(id,&error); if(!a) return error;
    ShowWindow(a->window,SW_SHOWNOACTIVATE); return 0;
}
int64_t wood_gui_control(int64_t id,int64_t control,int64_t kind,const unsigned char *text,int64_t size,
                         int64_t x,int64_t y,int64_t width,int64_t height,int64_t readonly) {
    int64_t error=0; App *a=owned(id,&error); if(!a) return error;
    if(control<1 || control>65535 || kind<1 || kind>7 || readonly<0 || readonly>1 ||
       x<0 || y<0 || x>32767 || y>32767 || width<1 || height<1 || width>32767 || height>32767) return E_ARGUMENT;
    if(a->controls>=256) return E_LIMIT;
    if(GetDlgItem(a->window,(int)control)) return E_ARGUMENT;
    WCHAR *value=wide(text,size); if(!value) return os_error();
    const WCHAR *cls=(kind==1 || kind==7)?L"STATIC":(kind==2 || kind==6)?L"BUTTON":kind==5?L"LISTBOX":L"EDIT";
    DWORD style=WS_CHILD|WS_VISIBLE;
    if(kind==2) style|=WS_TABSTOP|BS_PUSHBUTTON;
    if(kind==3 || kind==4) style|=WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL;
    if(kind==4) style|=ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL;
    if(readonly && (kind==3 || kind==4)) style|=ES_READONLY;
    if(kind==5) style|=WS_TABSTOP|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT;
    if(kind==6) style|=WS_TABSTOP|BS_AUTOCHECKBOX;
    if(kind==7) style|=SS_BITMAP|SS_CENTERIMAGE;
    UINT dpi=GetDpiForWindow(a->window);
    /* Window-creation name marshalling has a smaller limit than EDIT content.
       Create empty, then use the control's text message after initialization. */
    SetLastError(0);
    HWND child=CreateWindowExW(0,cls,L"",style,pixels(x,dpi),pixels(y,dpi),pixels(width,dpi),pixels(height,dpi),
                               a->window,(HMENU)(INT_PTR)control,GetModuleHandleW(0),0);
    if(!child) { error=os_error(); release(value); return error; }
    SendMessageW(child,WM_SETFONT,(WPARAM)a->font,TRUE);
    if(kind==3 || kind==4) SendMessageW(child,EM_SETLIMITTEXT,TEXT_LIMIT/4,0);
    SetLastError(0);
    if(kind!=5 && kind!=7 && !SetWindowTextW(child,value)) {
        error=os_error(); release(value); DestroyWindow(child); return error;
    }
    release(value);
    ++a->controls; return 0;
}
int64_t wood_gui_set_text(int64_t id,int64_t control,const unsigned char *text,int64_t size) {
    int64_t error=0; App *a=owned(id,&error); if(!a) return error;
    if(control<1 || control>65535) return E_ARGUMENT;
    HWND child=GetDlgItem(a->window,(int)control); if(!child) return E_ARGUMENT;
    WCHAR *value=wide(text,size); if(!value) return os_error();
    BOOL ok=SetWindowTextW(child,value); release(value);
    return ok?0:os_error();
}
int64_t wood_gui_text(int64_t id,int64_t control,int64_t *out,int64_t capacity) {
    int64_t error=0; App *a=owned(id,&error); if(!a) return error;
    if(control<1 || control>65535 || capacity<0 || capacity>TEXT_LIMIT || (capacity && !out)) return E_ARGUMENT;
    HWND child=GetDlgItem(a->window,(int)control); if(!child) return E_ARGUMENT;
    SetLastError(0); int count=GetWindowTextLengthW(child);
    if(!count && GetLastError()) return os_error();
    if(count<0 || count>TEXT_LIMIT) return E_LIMIT;
    WCHAR *value=reserve(((SIZE_T)count+1)*sizeof(WCHAR)); if(!value) return os_code(ERROR_NOT_ENOUGH_MEMORY);
    SetLastError(0); int actual=GetWindowTextW(child,value,count+1);
    if(!actual && GetLastError()) { error=os_error(); release(value); return error; }
    if(GetWindowTextLengthW(child)>actual) { release(value); return os_code(ERROR_INSUFFICIENT_BUFFER); }
    int bytes=actual?WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,actual,0,0,0,0):0;
    if(actual && !bytes) { error=os_error(); release(value); return error; }
    if(bytes>TEXT_LIMIT) { release(value); return E_LIMIT; }
    if(!capacity) { release(value); return bytes; }
    if(bytes>capacity) { release(value); return os_code(ERROR_INSUFFICIENT_BUFFER); }
    unsigned char *encoded=reserve((SIZE_T)bytes+1);
    if(!encoded) { release(value); return os_code(ERROR_NOT_ENOUGH_MEMORY); }
    if(bytes && !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,actual,(char*)encoded,bytes,0,0)) {
        error=os_error(); release(encoded); release(value); return error;
    }
    for(int i=0;i<bytes;++i) out[i]=encoded[i];
    release(encoded); release(value); return bytes;
}
int64_t wood_gui_move(int64_t id,int64_t control,int64_t x,int64_t y,int64_t width,int64_t height) {
    int64_t error=0; App *a=owned(id,&error); if(!a) return error;
    if(control<1 || control>65535 || x<0 || y<0 || x>32767 || y>32767 || width<1 || height<1 || width>32767 || height>32767) return E_ARGUMENT;
    HWND child=GetDlgItem(a->window,(int)control); if(!child) return E_ARGUMENT;
    UINT dpi=GetDpiForWindow(a->window);
    if(!MoveWindow(child,pixels(x,dpi),pixels(y,dpi),pixels(width,dpi),pixels(height,dpi),TRUE)) return os_error();
    return 0;
}
static HWND child_of(int64_t id,int64_t control,int64_t *error) {
    App *a=owned(id,error); if(!a) return 0;
    HWND child=(control<1 || control>65535)?0:GetDlgItem(a->window,(int)control);
    if(!child) *error=E_ARGUMENT;
    return child;
}
int64_t wood_gui_list_add(int64_t id,int64_t control,const unsigned char *text,int64_t size) {
    int64_t error=0; HWND child=child_of(id,control,&error); if(!child) return error;
    WCHAR *value=wide(text,size); if(!value) return os_error();
    LRESULT at=SendMessageW(child,LB_ADDSTRING,0,(LPARAM)value); release(value);
    return at<0?E_LIMIT:(int64_t)at;
}
int64_t wood_gui_list_clear(int64_t id,int64_t control) {
    int64_t error=0; HWND child=child_of(id,control,&error); if(!child) return error;
    SendMessageW(child,LB_RESETCONTENT,0,0); return 0;
}
/* Returns the selected index plus one, so that zero means nothing is selected. */
int64_t wood_gui_list_selected(int64_t id,int64_t control) {
    int64_t error=0; HWND child=child_of(id,control,&error); if(!child) return error;
    LRESULT at=SendMessageW(child,LB_GETCURSEL,0,0);
    return at<0?0:(int64_t)at+1;
}
int64_t wood_gui_list_select(int64_t id,int64_t control,int64_t index) {
    int64_t error=0; HWND child=child_of(id,control,&error); if(!child) return error;
    if(index<-1 || index>32767) return E_ARGUMENT;
    SendMessageW(child,LB_SETCURSEL,(WPARAM)index,0); return 0;
}
int64_t wood_gui_checked(int64_t id,int64_t control) {
    int64_t error=0; HWND child=child_of(id,control,&error); if(!child) return error;
    return SendMessageW(child,BM_GETCHECK,0,0)==BST_CHECKED?1:0;
}
int64_t wood_gui_set_checked(int64_t id,int64_t control,int64_t value) {
    int64_t error=0; HWND child=child_of(id,control,&error); if(!child) return error;
    SendMessageW(child,BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0); return 0;
}
/* RGBA8 values, one array element per channel, composited over the window color. */
int64_t wood_gui_set_image(int64_t id,int64_t control,const int64_t *pixels,int64_t count,int64_t width,int64_t height) {
    int64_t error=0; HWND child=child_of(id,control,&error); if(!child) return error;
    if(width<1 || height<1 || width>16384 || height>16384 || count!=width*height*4 || !pixels) return E_ARGUMENT;
    BITMAPINFO info={0};
    info.bmiHeader.biSize=sizeof info.bmiHeader; info.bmiHeader.biWidth=(LONG)width; info.bmiHeader.biHeight=-(LONG)height;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void *bits=0; HBITMAP image=CreateDIBSection(0,&info,DIB_RGB_COLORS,&bits,0,0);
    if(!image) return os_error();
    COLORREF back=GetSysColor(COLOR_WINDOW); unsigned char *to=bits;
    for(int64_t i=0;i<width*height;++i) {
        const int64_t *from=pixels+i*4; int64_t alpha=from[3];
        if(from[0]<0 || from[0]>255 || from[1]<0 || from[1]>255 || from[2]<0 || from[2]>255 || alpha<0 || alpha>255) { DeleteObject(image); return E_ARGUMENT; }
        to[i*4]=(unsigned char)((from[2]*alpha+GetBValue(back)*(255-alpha))/255);
        to[i*4+1]=(unsigned char)((from[1]*alpha+GetGValue(back)*(255-alpha))/255);
        to[i*4+2]=(unsigned char)((from[0]*alpha+GetRValue(back)*(255-alpha))/255);
        to[i*4+3]=0;
    }
    HBITMAP previous=(HBITMAP)SendMessageW(child,STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)image);
    if(previous) DeleteObject(previous);
    /* A control may keep a private copy; then this handle would otherwise be lost. */
    if((HBITMAP)SendMessageW(child,STM_GETIMAGE,IMAGE_BITMAP,0)!=image) DeleteObject(image);
    InvalidateRect(child,0,TRUE);
    return 0;
}
/* Adds an item under a named top-level menu, creating the bar and menu when needed. */
int64_t wood_gui_menu(int64_t id,const unsigned char *menu,int64_t menu_size,int64_t control,const unsigned char *text,int64_t text_size) {
    int64_t error=0; App *a=owned(id,&error); if(!a) return error;
    if(control<1 || control>65535) return E_ARGUMENT;
    WCHAR *name=wide(menu,menu_size); if(!name) return os_error();
    WCHAR *item=wide(text,text_size); if(!item) { error=os_error(); release(name); return error; }
    HMENU bar=GetMenu(a->window),popup=0;
    if(!bar) bar=CreateMenu();
    int count=bar?GetMenuItemCount(bar):-1;
    for(int i=0;i<count && !popup;++i) {
        WCHAR label[64]; int n=GetMenuStringW(bar,(UINT)i,label,64,MF_BYPOSITION);
        if(n>0 && !lstrcmpW(label,name)) popup=GetSubMenu(bar,i);
    }
    if(bar && !popup) { popup=CreatePopupMenu(); if(popup) AppendMenuW(bar,MF_POPUP|MF_STRING,(UINT_PTR)popup,name); }
    BOOL ok=bar && popup && AppendMenuW(popup,MF_STRING,(UINT_PTR)control,item) && SetMenu(a->window,bar) && DrawMenuBar(a->window);
    if(!ok) error=os_error();
    release(name); release(item);
    return ok?0:error;
}
/* kind: 0 information, 1 error, 2 yes/no question. Returns 1 for OK or Yes. */
int64_t wood_gui_message(int64_t id,const unsigned char *title,int64_t title_size,const unsigned char *text,int64_t text_size,int64_t kind) {
    int64_t error=0; App *a=owned(id,&error); if(!a) return error;
    if(kind<0 || kind>2) return E_ARGUMENT;
    WCHAR *name=wide(title,title_size); if(!name) return os_error();
    WCHAR *body=wide(text,text_size); if(!body) { error=os_error(); release(name); return error; }
    UINT flags=kind==0?MB_OK|MB_ICONINFORMATION:kind==1?MB_OK|MB_ICONERROR:MB_YESNO|MB_ICONQUESTION;
    int answer=MessageBoxW(a->window,body,name,flags);
    release(name); release(body);
    if(!answer) return os_error();
    return answer==IDOK || answer==IDYES?1:0;
}
typedef BOOL (WINAPI *FileDialog)(OPENFILENAMEW*);
/* Returns UTF-8 byte count, or 0 when cancelled. The filter uses | between fields. */
int64_t wood_gui_file_dialog(int64_t id,int64_t save,const unsigned char *title,int64_t title_size,const unsigned char *filter,int64_t filter_size,int64_t *out,int64_t capacity) {
    int64_t error=0; App *a=owned(id,&error); if(!a) return error;
    if(capacity<1 || capacity>TEXT_LIMIT || !out) return E_ARGUMENT;
    const WCHAR library_name[]=L"comdlg32.dll";
    HMODULE library=LoadLibraryExW(library_name,0,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!library) return os_error();
    FileDialog dialog=(FileDialog)(void*)GetProcAddress(library,save?"GetSaveFileNameW":"GetOpenFileNameW");
    WCHAR *name=wide(title,title_size),*types=wide(filter,filter_size),*path=reserve(32768*sizeof(WCHAR));
    int64_t result=0;
    if(!dialog || !name || !types || !path) result=os_code(ERROR_NOT_ENOUGH_MEMORY);
    else {
        /* wide() allocates one spare terminator, so the list ends with two NULs. */
        for(WCHAR *at=types;*at;++at) if(*at==L'|') *at=0;
        OPENFILENAMEW open={0};
        open.lStructSize=sizeof open; open.hwndOwner=a->window; open.lpstrFilter=types; open.lpstrFile=path; open.nMaxFile=32768;
        open.lpstrTitle=name; open.Flags=OFN_EXPLORER|OFN_HIDEREADONLY|OFN_NOCHANGEDIR|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST);
        if(dialog(&open)) {
            int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,0,0,0,0)-1;
            unsigned char *encoded=bytes>0?reserve((SIZE_T)bytes+1):0;
            if(bytes<=0 || bytes>capacity || !encoded) result=os_code(ERROR_INSUFFICIENT_BUFFER);
            else {
                WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,(char*)encoded,bytes+1,0,0);
                for(int i=0;i<bytes;++i) out[i]=encoded[i];
                result=bytes;
            }
            release(encoded);
        }
    }
    release(name); release(types); release(path); FreeLibrary(library);
    return result;
}
int64_t wood_gui_post(int64_t id,int64_t topic,const unsigned char *text,int64_t size) {
    if(topic<0 || size<0 || size>TEXT_LIMIT || (size && !text)) return E_ARGUMENT;
    AcquireSRWLockExclusive(&gate);
    App *a=active;
    if(!a || a->id!=id || a->closing) { ReleaseSRWLockExclusive(&gate); return 0; }
    Item *previous=a->tail;
    int64_t result=push_locked(a,EV_MESSAGE,topic,0,0,text,size);
    if(!result && !PostMessageW(a->window,WAKE_MESSAGE,0,0)) {
        result=os_error(); Item *item=a->tail;
        if(previous) previous->next=0; else a->head=0;
        a->tail=previous; --a->queued; a->bytes-=size; release(item);
    }
    ReleaseSRWLockExclusive(&gate);
    return result<0?result:1;
}
int64_t wood_gui_wait(int64_t id,int64_t (*live)(void),int64_t *out,int64_t capacity) {
    int64_t error=0; App *a=owned(id,&error); if(!a) return error;
    if(!live || !out || capacity<4) return E_ARGUMENT;
    AcquireSRWLockExclusive(&gate);
    if(a->current) { a->bytes-=a->current->size; release(a->current); a->current=0; }
    ReleaseSRWLockExclusive(&gate);
    for(;;) {
        if(!live()) return E_CANCEL; /* Wood callback failure is deferred by its FFI guard. */
        AcquireSRWLockExclusive(&gate);
        if(a->fatal) { error=os_code(a->fatal); ReleaseSRWLockExclusive(&gate); return error; }
        Item *item=a->head;
        if(item) {
            a->head=item->next; if(!a->head) a->tail=0;
            a->current=item; --a->queued;
            out[0]=item->kind; out[1]=item->control; out[2]=item->a; out[3]=item->b;
            ReleaseSRWLockExclusive(&gate); return 0;
        }
        ReleaseSRWLockExclusive(&gate);
        MSG message;
        for(unsigned batch=0;batch<64 && PeekMessageW(&message,0,0,0,PM_REMOVE);++batch) {
            if(message.message==WM_QUIT) { event(a,EV_CLOSE,0,0,0); break; }
            if(message.message==WM_KEYDOWN && (message.hwnd==a->window || IsChild(a->window,message.hwnd)))
                event(a,EV_KEY,0,(int64_t)message.wParam,0);
            if(!IsDialogMessageW(a->window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        AcquireSRWLockShared(&gate); int ready=a->head!=0 || a->fatal; ReleaseSRWLockShared(&gate);
        if(!ready && MsgWaitForMultipleObjectsEx(0,0,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE)==WAIT_FAILED) return os_error();
    }
}
int64_t wood_gui_payload(int64_t id,int64_t *out,int64_t capacity) {
    int64_t error=0; App *a=owned(id,&error); if(!a) return error;
    if(capacity<0 || capacity>TEXT_LIMIT || (capacity && !out)) return E_ARGUMENT;
    Item *item=a->current; int64_t size=item?item->size:0;
    if(!capacity) return size;
    if(capacity<size) return os_code(ERROR_INSUFFICIENT_BUFFER);
    for(int64_t i=0;i<size;++i) out[i]=item->bytes[i];
    return size;
}
int64_t wood_gui_context(int64_t id,int64_t *out,int64_t capacity) {
    int64_t error=0; App *a=owned(id,&error); if(!a) return error;
    if(capacity<0 || capacity>TEXT_LIMIT || (capacity && !out)) return E_ARGUMENT;
    if(!capacity) return a->context_size;
    if(capacity<a->context_size) return os_code(ERROR_INSUFFICIENT_BUFFER);
    for(int64_t i=0;i<a->context_size;++i) out[i]=a->context[i];
    return a->context_size;
}
int64_t wood_gui_run(const unsigned char *title,int64_t title_size,int64_t width,int64_t height,
                     const unsigned char *context,int64_t context_size,int64_t (*body)(int64_t),
                     int64_t *result,int64_t result_count) {
    if(!body || !result || result_count<1 || context_size<0 || context_size>TEXT_LIMIT || (context_size && !context)) return E_ARGUMENT;
    unsigned char *copy=reserve((SIZE_T)context_size+1);
    if(!copy) return os_code(ERROR_NOT_ENOUGH_MEMORY);
    for(int64_t i=0;i<context_size;++i) copy[i]=context[i];
    int64_t id=wood_gui_open(title,title_size,width,height);
    if(id<0) { release(copy); return id; }
    int64_t error=0; App *a=owned(id,&error);
    if(!a) { release(copy); wood_gui_close(id); return error; }
    a->context=copy; a->context_size=context_size;
    result[0]=body(id);
    /* The existing FFI guard defers Wood exceptions/exit until this function
       returns. This C-owned scope therefore cleans up every callback outcome. */
    return wood_gui_close(id);
}
