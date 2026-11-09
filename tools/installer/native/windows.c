/* Installer-only controls over the qualified native GUI lifecycle. */
#include "../../../packages/wood_windowsgui/native/windows.c"
#define COBJMACROS
#include <shobjidl.h>
#include <commctrl.h>

extern void wood_setup_poll_function(int (*callback)(void));
static int64_t setup_id;
static int cancelled;
static HFONT heading_font;
typedef struct {int id,x,y,w,h;} Position;
static Position positions[128];static unsigned position_count;

static void heading(HWND window){
    UINT dpi=GetDpiForWindow(window);
    HFONT next=CreateFontW(-MulDiv(20,(int)dpi,72),0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    if(next){SendDlgItemMessageW(window,1,WM_SETFONT,(WPARAM)next,TRUE);if(heading_font)DeleteObject(heading_font);heading_font=next;}
}
static LRESULT CALLBACK setup_procedure(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_CLOSE)cancelled=1;
    if(message==WM_COMMAND&&LOWORD(w)==102&&HIWORD(w)==BN_CLICKED&&IsWindowEnabled(GetDlgItem(window,102)))cancelled=1;
    if(message==DM_GETDEFID)return MAKELONG(100,DC_HASDEFID);
    if(message==WM_COMMAND&&!l&&(LOWORD(w)==IDOK||LOWORD(w)==IDCANCEL)){
        int id=LOWORD(w)==IDOK?100:102;HWND button=GetDlgItem(window,id);
        if(button&&IsWindowVisible(button)&&IsWindowEnabled(button)){if(id==102)cancelled=1;event((App*)GetWindowLongPtrW(window,GWLP_USERDATA),EV_CLICK,id,0,0);}
        return 0;
    }
    LRESULT result=procedure(window,message,w,l);
    if(message==WM_DPICHANGED){
        UINT dpi=GetDpiForWindow(window);
        for(unsigned i=0;i<position_count;++i){Position p=positions[i];SetWindowPos(GetDlgItem(window,p.id),0,pixels(p.x,dpi),pixels(p.y,dpi),pixels(p.w,dpi),pixels(p.h,dpi),SWP_NOZORDER|SWP_NOACTIVATE);}
        heading(window);
    }
    if(message==WM_CTLCOLORSTATIC){SetBkColor((HDC)w,GetSysColor(COLOR_WINDOW));return (LRESULT)GetSysColorBrush(COLOR_WINDOW);}
    return result;
}
static int pump(void){
    int64_t error=0;App *a=owned(setup_id,&error);if(!a)return -125;
    MSG message;
    for(unsigned i=0;i<64&&PeekMessageW(&message,a->window,0,0,PM_REMOVE);++i){
        if(message.message==WM_CLOSE||(message.message==WM_COMMAND&&LOWORD(message.wParam)==102))cancelled=1;
        if(!IsDialogMessageW(a->window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    if(cancelled){wood_setup_poll_function(0);return -125;}return 0;
}
int64_t wood_setup_ui_run(const unsigned char *title,int64_t tn,const unsigned char *context,int64_t cn,int64_t(*body)(int64_t),int64_t *out,int64_t count){
    setup_id=0;cancelled=0;position_count=0;
    int64_t result=wood_gui_run(title,tn,640,480,context,cn,body,out,count);
    wood_setup_poll_function(0);setup_id=0;if(heading_font){DeleteObject(heading_font);heading_font=0;}return result;
}
int64_t wood_setup_ui_control(int64_t id,int64_t control,int64_t kind,const unsigned char *text,int64_t size,int64_t x,int64_t y,int64_t width,int64_t height){
    int64_t error=0;App *a=owned(id,&error);if(!a)return error;
    if((kind!=5&&kind!=6)||control<1||control>65535||GetDlgItem(a->window,(int)control)||width<1||height<1||x<0||y<0||x>32767||y>32767||width>32767||height>32767)return E_ARGUMENT;
    if(a->controls>=128)return E_LIMIT;
    WCHAR *label=wide(text,size);if(!label)return os_error();
    INITCOMMONCONTROLSEX init={sizeof(init),ICC_PROGRESS_CLASS};if(!InitCommonControlsEx(&init)){release(label);return os_error();}
    UINT dpi=GetDpiForWindow(a->window);
    HWND item=CreateWindowExW(0,kind==5?L"BUTTON":PROGRESS_CLASSW,label,WS_CHILD|WS_VISIBLE|(kind==5?(WS_TABSTOP|BS_AUTOCHECKBOX):PBS_SMOOTH),pixels(x,dpi),pixels(y,dpi),pixels(width,dpi),pixels(height,dpi),a->window,(HMENU)(INT_PTR)control,GetModuleHandleW(0),0);
    release(label);if(!item)return os_error();SendMessageW(item,WM_SETFONT,(WPARAM)a->font,TRUE);
    if(kind==6)SendMessageW(item,PBM_SETRANGE32,0,1000);++a->controls;return 0;
}
int64_t wood_setup_ui_state(int64_t id,int64_t control,int64_t action,int64_t value){
    int64_t error=0;App *a=owned(id,&error);if(!a)return error;
    if(control<0||control>65535)return E_ARGUMENT;
    HWND item=control?GetDlgItem(a->window,(int)control):a->window;if(!item)return E_ARGUMENT;
    if(action==0)ShowWindow(item,value?SW_SHOWNOACTIVATE:SW_HIDE);
    else if(action==1)EnableWindow(item,value!=0);
    else if(action==2)return SendMessageW(item,BM_GETCHECK,0,0)==BST_CHECKED;
    else if(action==3)SendMessageW(item,BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);
    else if(action==4){if(value<0||value>1000)return E_ARGUMENT;SendMessageW(item,PBM_SETPOS,(WPARAM)value,0);}
    else if(action==5){
        setup_id=id;cancelled=0;
        LONG_PTR style=GetWindowLongPtrW(a->window,GWL_STYLE);SetWindowLongPtrW(a->window,GWL_STYLE,style&~(WS_THICKFRAME|WS_MAXIMIZEBOX));
        UINT dpi=GetDpiForWindow(a->window);position_count=0;
        for(HWND child=GetWindow(a->window,GW_CHILD);child&&position_count<128;child=GetWindow(child,GW_HWNDNEXT)){
            RECT rect;if(!GetWindowRect(child,&rect))return os_error();MapWindowPoints(0,a->window,(POINT*)&rect,2);
            Position *p=&positions[position_count++];p->id=GetDlgCtrlID(child);p->x=logical(rect.left,dpi);p->y=logical(rect.top,dpi);p->w=logical(rect.right-rect.left,dpi);p->h=logical(rect.bottom-rect.top,dpi);
        }
        SetWindowLongPtrW(a->window,GWLP_WNDPROC,(LONG_PTR)setup_procedure);heading(a->window);
        SendDlgItemMessageW(a->window,100,BM_SETSTYLE,BS_DEFPUSHBUTTON,TRUE);
    }else if(action==6){cancelled=0;wood_setup_poll_function(value?pump:0);}
    else return E_ARGUMENT;
    return 0;
}
int64_t wood_setup_ui_progress(int64_t done,int64_t total,const unsigned char *text,int64_t size){
    if(done<0||total<0||done>total||total>16384||size<0||size>TEXT_LIMIT||(!text&&size))return E_ARGUMENT;
    if(!setup_id)return 1;
    if(pump()<0)return 0;
    int64_t status=wood_gui_set_text(setup_id,30,text,size);if(status<0)return status;
    int base=size>=7&&text[0]=='I'?400:0;
    int span=base?550:400;
    if(size>=7&&(text[0]=='F'||text[0]=='R')){base=950;span=50;}
    int value=total>0?base+(int)(done*span/total):base;
    status=wood_setup_ui_state(setup_id,31,4,value);return status<0?status:1;
}
int64_t wood_setup_browse(int64_t id,int64_t control){
    int64_t error=0;App *a=owned(id,&error);if(!a)return error;
    HRESULT init=CoInitializeEx(0,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    if(FAILED(init))return -(int64_t)(uint32_t)init;
    IFileOpenDialog *dialog=0;HRESULT status=CoCreateInstance(&CLSID_FileOpenDialog,0,CLSCTX_INPROC_SERVER,&IID_IFileOpenDialog,(void**)&dialog);
    if(SUCCEEDED(status))status=IFileOpenDialog_SetOptions(dialog,FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR|FOS_PATHMUSTEXIST|FOS_DONTADDTORECENT);
    if(SUCCEEDED(status))status=IFileOpenDialog_Show(dialog,a->window);
    if(SUCCEEDED(status)){
        IShellItem *item=0;status=IFileOpenDialog_GetResult(dialog,&item);
        if(SUCCEEDED(status)){PWSTR selected=0;status=IShellItem_GetDisplayName(item,SIGDN_FILESYSPATH,&selected);if(SUCCEEDED(status)){if(!SetDlgItemTextW(a->window,(int)control,selected))status=HRESULT_FROM_WIN32(GetLastError());CoTaskMemFree(selected);}IShellItem_Release(item);}
    }
    if(dialog)IFileOpenDialog_Release(dialog);CoUninitialize();
    return status==HRESULT_FROM_WIN32(ERROR_CANCELLED)?0:(FAILED(status)?-(int64_t)(uint32_t)status:1);
}
int64_t wood_setup_message(const unsigned char *text,int64_t size,int64_t question){
    WCHAR *value=wide(text,size);if(!value)return os_error();
    int result=MessageBoxW(0,value,L"wood Setup",MB_TASKMODAL|(question?(MB_YESNO|MB_ICONQUESTION):(MB_OK|MB_ICONERROR)));release(value);return question?result==IDYES:0;
}
