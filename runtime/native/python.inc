/* Optional CPython 3.14 bridge. Opaque supported C API only: no Python headers,
   PyObject layout assumptions, C++ runtime, or Python dependency for Core.
   External modules are trusted in-process code, not a sandbox. */
typedef void PyValue;
typedef void PyConfig;
typedef struct { WOwner owner; PyValue *value; } WPython;
typedef struct {
    void *library,*thread;
    int state; /* 0 cold, 1 initializing, 2 running, 3 closed/failed */
    WInt active,objects;
    WLock gate;
#define PY_API(ret,name,args) ret (*name) args
    PY_API(PyConfig*,PyInitConfig_Create,(void));
    PY_API(void,PyInitConfig_Free,(PyConfig*));
    PY_API(int,PyInitConfig_SetInt,(PyConfig*,const char*,int64_t));
    PY_API(int,PyInitConfig_SetStr,(PyConfig*,const char*,const char*));
    PY_API(int,PyInitConfig_SetStrList,(PyConfig*,const char*,size_t,char*const*));
    PY_API(int,PyInitConfig_GetError,(PyConfig*,const char**));
    PY_API(int,Py_InitializeFromInitConfig,(PyConfig*));
    PY_API(int,Py_IsInitialized,(void));
    PY_API(const char*,Py_GetVersion,(void));
    PY_API(void*,PyEval_SaveThread,(void));
    PY_API(void,PyEval_RestoreThread,(void*));
    PY_API(int,PyGILState_Ensure,(void));
    PY_API(void,PyGILState_Release,(int));
    PY_API(int,Py_FinalizeEx,(void));
    PY_API(void,Py_IncRef,(PyValue*));
    PY_API(void,Py_DecRef,(PyValue*));
    PY_API(PyValue*,Py_GetConstant,(unsigned));
    PY_API(PyValue*,PyImport_ImportModule,(const char*));
    PY_API(PyValue*,PyObject_GetAttrString,(PyValue*,const char*));
    PY_API(PyValue*,PyObject_Call,(PyValue*,PyValue*,PyValue*));
    PY_API(PyValue*,PyObject_Type,(PyValue*));
    PY_API(PyValue*,PyObject_Str,(PyValue*));
    PY_API(int,PyObject_IsTrue,(PyValue*));
    PY_API(PyValue*,PyNumber_Index,(PyValue*));
    PY_API(PyValue*,PyErr_GetRaisedException,(void));
    PY_API(PyValue*,PyErr_Occurred,(void));
    PY_API(void,PyErr_Clear,(void));
    PY_API(PyValue*,PyLong_FromLongLong,(long long));
    PY_API(long long,PyLong_AsLongLong,(PyValue*));
    PY_API(PyValue*,PyFloat_FromDouble,(double));
    PY_API(double,PyFloat_AsDouble,(PyValue*));
    PY_API(PyValue*,PyUnicode_FromStringAndSize,(const char*,intptr_t));
    PY_API(const char*,PyUnicode_AsUTF8AndSize,(PyValue*,intptr_t*));
    PY_API(PyValue*,PyBytes_FromStringAndSize,(const char*,intptr_t));
    PY_API(int,PyBytes_AsStringAndSize,(PyValue*,char**,intptr_t*));
    PY_API(PyValue*,PyList_New,(intptr_t));
    PY_API(intptr_t,PyList_Size,(PyValue*));
    PY_API(PyValue*,PyList_GetItem,(PyValue*,intptr_t));
    PY_API(int,PyList_SetItem,(PyValue*,intptr_t,PyValue*));
    PY_API(PyValue*,PyTuple_New,(intptr_t));
    PY_API(intptr_t,PyTuple_Size,(PyValue*));
    PY_API(PyValue*,PyTuple_GetItem,(PyValue*,intptr_t));
    PY_API(int,PyTuple_SetItem,(PyValue*,intptr_t,PyValue*));
    PY_API(PyValue*,PyDict_New,(void));
    PY_API(int,PyDict_SetItem,(PyValue*,PyValue*,PyValue*));
    PY_API(int,PyDict_Next,(PyValue*,intptr_t*,PyValue**,PyValue**));
    PyValue *types[9];
#undef PY_API
} Python;
static Python py;

static void py_decref(PyValue *value) { if(value) py.Py_DecRef(value); }
/* Fetch first, so formatting an exception never leaves the original indicator
   pending. A failing __str__ gets a bounded, non-recursive fallback. */
static void py_error(void *ctx) {
    PyValue *error=py.PyErr_GetRaisedException(),*type=0,*name=0,*text=0;
    JBuffer message={ctx,0};
    if(error) { type=py.PyObject_Type(error); if(type) name=py.PyObject_GetAttrString(type,"__name__"); text=py.PyObject_Str(error); }
    intptr_t count=0; const char *bytes=name?py.PyUnicode_AsUTF8AndSize(name,&count):0;
    jb_word(&message,"Python ");
    if(bytes) jb_add(&message,bytes,count>1024?1024:count); else jb_word(&message,"exception");
    py.PyErr_Clear(); count=0; bytes=text?py.PyUnicode_AsUTF8AndSize(text,&count):0;
    if(bytes) { jb_word(&message,": "); jb_add(&message,bytes,count>8192?8192:count); }
    py_decref(text); py_decref(name); py_decref(type); py_decref(error); py.PyErr_Clear();
    WStr result={0}; jb_finish(&message,&result);
    if(!wd_rt_unwinding(ctx)) wd_rt_error_value(ctx,"R4031",5,&result);
    wd_m_str_drop(&result);
}
static int py_finite(double value) { return (n_bits(value)&UINT64_C(0x7fffffffffffffff))<UINT64_C(0x7ff0000000000000); }
static WJson *py_field(void *ctx,WJson *info,const char *name) { return d_field(ctx,info,name); }
static int py_find(void *ctx,WStr *exe) {
    WStr key=d_literal("WOOD_PYTHON"); d_environment(ctx,exe,&key);
    if(exe->size) {
        if(!wd_m_path_is_absolute(ctx,exe)) fail(ctx,"R4030","WOOD_PYTHON must name an absolute trusted Python executable");
        else if(p_kind(ctx,exe,1)!=1) fail(ctx,"R4030","WOOD_PYTHON executable does not exist or is not a regular file");
        return !wd_rt_unwinding(ctx);
    }
    wd_m_str_drop(exe); *exe=(WStr){0};
    { /* A self-contained application: 'wood build --bundle-python' puts CPython beside it. */
#ifdef _WIN32
        WStr bundled=d_literal("wood-python/python.exe");
#else
        WStr bundled=d_literal("wood-python/bin/python3");
#endif
        WStr self={0},parent={0},candidate={0}; d_executable(ctx,&self);
        if(!wd_rt_unwinding(ctx)) wd_m_path_parent(ctx,&parent,&self);
        if(!wd_rt_unwinding(ctx)) wd_m_path_join(ctx,&candidate,&parent,&bundled);
        int found=!wd_rt_unwinding(ctx) && p_kind(ctx,&candidate,1)==1;
        wd_m_str_drop(&self); wd_m_str_drop(&parent);
        if(found) { *exe=candidate; return 1; }
        wd_m_str_drop(&candidate);
        if(wd_rt_unwinding(ctx)) return 0;
    }
    key=d_literal("VIRTUAL_ENV"); WStr directory={0}; d_environment(ctx,&directory,&key);
#ifdef _WIN32
    WStr leaf=d_literal("Scripts/python.exe"); const char separator=';'; const char *binary="python.exe";
#else
    WStr leaf=d_literal("bin/python3"); const char separator=':'; const char *binary="python3";
#endif
    if(directory.size) {
        if(!wd_m_path_is_absolute(ctx,&directory)) fail(ctx,"R4030","VIRTUAL_ENV must be absolute");
        else wd_m_path_join(ctx,exe,&directory,&leaf);
        if(!wd_rt_unwinding(ctx) && p_kind(ctx,exe,1)!=1) fail(ctx,"R4030","VIRTUAL_ENV Python executable is unavailable");
        wd_m_str_drop(&directory); return !wd_rt_unwinding(ctx);
    }
    wd_m_str_drop(&directory); key=d_literal("PATH"); WStr paths={0}; d_environment(ctx,&paths,&key);
    WInt at=0;
    for(WInt i=0;paths.size && i<=paths.size && !exe->size && !wd_rt_unwinding(ctx);++i) if(i==paths.size || paths.data[i]==separator) {
        WStr parent={paths.data+at,i-at,0},name=d_literal(binary),candidate={0}; at=i+1;
        /* Deliberately exclude CWD and relative PATH entries. */
        if(parent.size && wd_m_path_is_absolute(ctx,&parent)) {
            wd_m_path_join(ctx,&candidate,&parent,&name);
            if(p_kind(ctx,&candidate,1)==1) { *exe=candidate; candidate=(WStr){0}; }
            wd_m_str_drop(&candidate);
        }
    }
    wd_m_str_drop(&paths);
    if(!exe->size && !wd_rt_unwinding(ctx)) fail(ctx,"R4030","CPython unavailable; set WOOD_PYTHON to an absolute CPython 3.14 executable");
    return !wd_rt_unwinding(ctx);
}
static WJson *py_probe(void *ctx) {
    /* Probe only the selected executable, without shell/source-directory imports.
       sys.path comes from this interpreter's own isolated venv resolution. */
    const char *script="import sys,sysconfig,os,json; print(json.dumps({'version':list(sys.version_info[:2]),'gil':bool(sysconfig.get_config_var('Py_GIL_DISABLED')),'exe':sys.executable,'prefix':sys.prefix,'base':sys.base_prefix,'library':os.path.realpath(os.path.join(sys.base_prefix,'python314.dll') if os.name=='nt' else next((c for c in (os.path.join(sysconfig.get_config_var('LIBDIR') or '',sysconfig.get_config_var('LDLIBRARY') or ''),os.path.join(sys.base_prefix,'lib',sysconfig.get_config_var('LDLIBRARY') or '')) if os.path.isfile(c)),'')),'paths':[p for p in sys.path if os.path.isabs(p)]}))";
    WStr exe={0},temp={0},log={0},empty={0},leaf=d_literal("probe.json"),text={0};
    WJson *result=0; WSeq *args=0;
    if(!py_find(ctx,&exe)) goto done;
    d_temp(ctx,&temp,&empty); if(wd_rt_unwinding(ctx)) goto done;
    wd_m_path_join(ctx,&log,&temp,&leaf);
    args=wd_m_seq_new(ctx,sizeof(WStr),0,0,wd_m_str_retain,wd_m_str_drop);
    WStr values[]={exe,d_literal("-I"),d_literal("-c"),d_literal(script)};
    for(unsigned i=0;i<4 && !wd_rt_unwinding(ctx);++i) wd_m_seq_append(ctx,&args,values+i);
    WInt status=d_process(ctx,args,&log,15000,65536,0);
    if(!wd_rt_unwinding(ctx) && status) fail(ctx,"R4030","selected Python probe failed; CPython 3.14 with a shared library is required");
    if(!wd_rt_unwinding(ctx)) { wd_m_read_text(ctx,&text,&log); result=wd_m_json_parse(ctx,&text); }
done:
    /* Cleanup needs an independent error context on Windows, where an existing
       failure intentionally prevents the driver from performing filesystem work. */
    if(temp.size) {
        WInt size=wd_rt_context_size(); void *clean=wd_platform_alloc((WSize)size);
        if(clean) { zero_bytes(clean,(WSize)size); d_release(clean,&temp); wd_rt_context_clear(clean); wd_platform_free(clean,(WSize)size); }
        else d_release(ctx,&temp);
    }
    wd_m_release(args); wd_m_str_drop(&exe); wd_m_str_drop(&temp); wd_m_str_drop(&log); wd_m_str_drop(&text);
    return result;
}
static int py_initialize(void *ctx) {
    WJson *info=py_probe(ctx); PyConfig *config=0; int ok=0,attempted=0;
    if(wd_rt_unwinding(ctx)) goto done;
    WJson *version=py_field(ctx,info,"version"),*gil=py_field(ctx,info,"gil");
    if(!version || version->kind!=5 || wd_m_seq_len(version->as.array)!=2 ||
       (*(WJson**)wd_m_seq_read(version->as.array,0))->as.integer!=3 ||
       (*(WJson**)wd_m_seq_read(version->as.array,1))->as.integer!=14 ||
       !gil || gil->kind!=1 || gil->as.integer) { fail(ctx,"R4030","interop requires normal GIL-enabled CPython 3.14; other versions and free-threaded builds are unsupported"); goto done; }
    WStr library=d_text(ctx,info,"library");
    if(!wd_m_path_is_absolute(ctx,&library) || p_kind(ctx,&library,1)!=1) { fail(ctx,"R4030","selected CPython shared library is unavailable"); goto done; }
#ifdef _WIN32
    WOwner *owner=0; unsigned short *wide=p_wide(ctx,&library,&owner);
    if(wide) py.library=LoadLibraryExW(wide,0,0x900);
    wd_m_release(owner);
#else
    if(dlopen && dlsym && dlclose && pthread_create && pthread_join) py.library=dlopen((char*)library.data,0x102); /* NOW | GLOBAL: extension-module symbols */
#endif
    if(!py.library) { fail(ctx,"R4030","cannot load selected CPython shared library"); goto done; }
#define PY_LOAD(name) do { *(void**)(&py.name)=optional_symbol(py.library,#name); if(!py.name) { fail(ctx,"R4030","missing supported CPython API: " #name); goto done; } } while(0)
    PY_LOAD(PyInitConfig_Create); PY_LOAD(PyInitConfig_Free); PY_LOAD(PyInitConfig_SetInt); PY_LOAD(PyInitConfig_SetStr);
    PY_LOAD(PyInitConfig_SetStrList); PY_LOAD(PyInitConfig_GetError); PY_LOAD(Py_InitializeFromInitConfig);
    PY_LOAD(Py_IsInitialized); PY_LOAD(Py_GetVersion); PY_LOAD(PyEval_SaveThread); PY_LOAD(PyEval_RestoreThread);
    PY_LOAD(PyGILState_Ensure); PY_LOAD(PyGILState_Release); PY_LOAD(Py_FinalizeEx); PY_LOAD(Py_IncRef); PY_LOAD(Py_DecRef);
    PY_LOAD(Py_GetConstant); PY_LOAD(PyImport_ImportModule); PY_LOAD(PyObject_GetAttrString); PY_LOAD(PyObject_Call);
    PY_LOAD(PyObject_Type); PY_LOAD(PyObject_Str); PY_LOAD(PyObject_IsTrue); PY_LOAD(PyNumber_Index);
    PY_LOAD(PyErr_GetRaisedException); PY_LOAD(PyErr_Occurred); PY_LOAD(PyErr_Clear);
    PY_LOAD(PyLong_FromLongLong); PY_LOAD(PyLong_AsLongLong); PY_LOAD(PyFloat_FromDouble); PY_LOAD(PyFloat_AsDouble);
    PY_LOAD(PyUnicode_FromStringAndSize); PY_LOAD(PyUnicode_AsUTF8AndSize); PY_LOAD(PyBytes_FromStringAndSize); PY_LOAD(PyBytes_AsStringAndSize);
    PY_LOAD(PyList_New); PY_LOAD(PyList_Size); PY_LOAD(PyList_GetItem); PY_LOAD(PyList_SetItem);
    PY_LOAD(PyTuple_New); PY_LOAD(PyTuple_Size); PY_LOAD(PyTuple_GetItem); PY_LOAD(PyTuple_SetItem);
    PY_LOAD(PyDict_New); PY_LOAD(PyDict_SetItem); PY_LOAD(PyDict_Next);
#undef PY_LOAD
    const char *types[]={"","PyBool_Type","PyLong_Type","PyFloat_Type","PyUnicode_Type","PyList_Type","PyTuple_Type","PyDict_Type","PyBytes_Type"};
    /* None is identified through Py_GetConstant, never its private type. */
    for(unsigned i=1;i<9;++i) { py.types[i]=optional_symbol(py.library,types[i]); if(!py.types[i]) { fail(ctx,"R4030","missing CPython builtin type"); goto done; } }
    const char *v=py.Py_GetVersion();
    if(!v || v[0]!='3' || v[1]!='.' || v[2]!='1' || v[3]!='4' || v[4]!='.' || py.Py_IsInitialized()) { fail(ctx,"R4030","incompatible or already initialized CPython library"); goto done; }
    config=py.PyInitConfig_Create(); if(!config) { fail(ctx,"R4030","cannot allocate CPython initialization configuration"); goto done; }
    const char *fields[]={"program_name","executable","prefix","base_prefix"}; const char *keys[]={"exe","exe","prefix","base"};
    for(unsigned i=0;i<4;++i) { WStr value=d_text(ctx,info,keys[i]); if(wd_rt_unwinding(ctx) || py.PyInitConfig_SetStr(config,fields[i],(char*)value.data)<0) goto config_error; }
    const char *off[]={"install_signal_handlers","configure_c_stdio","site_import","use_environment","user_site_directory","write_bytecode","parse_argv"};
    for(unsigned i=0;i<7;++i) if(py.PyInitConfig_SetInt(config,off[i],0)<0) goto config_error;
    WJson *paths=py_field(ctx,info,"paths");
    if(!paths || paths->kind!=5 || wd_m_seq_len(paths->as.array)>256) { fail(ctx,"R4030","invalid Python search path probe"); goto done; }
    char *entries[256]; size_t count=(size_t)wd_m_seq_len(paths->as.array);
    for(size_t i=0;i<count;++i) { WJson *path=*(WJson**)wd_m_seq_read(paths->as.array,(WInt)i); if(path->kind!=4 || !p_valid(ctx,&path->as.string)) goto done; entries[i]=(char*)path->as.string.data; }
    if(py.PyInitConfig_SetStrList(config,"module_search_paths",count,entries)<0 || py.PyInitConfig_SetInt(config,"module_search_paths_set",1)<0) goto config_error;
    attempted=1;
    if(py.Py_InitializeFromInitConfig(config)<0) goto config_error;
    py.thread=py.PyEval_SaveThread(); ok=1; goto done;
config_error: {
    const char *error=0; py.PyInitConfig_GetError(config,&error);
    fail(ctx,"R4030",error?error:"CPython isolated initialization failed");
}
done:
    if(config) py.PyInitConfig_Free(config);
    wd_m_release(info);
    /* Never unload an interpreter after initialization has been attempted. */
    if(!ok && !attempted && py.library) { optional_close(py.library); py.library=0; }
    return ok;
}
static int py_enter(void *ctx,int *gil) {
    if(wd_rt_unwinding(ctx)) return 0;
    wd_m_threads_init(); w_lock(&py.gate);
    if(py.state==0) {
        if(wd_m_worker_thread()) { w_unlock(&py.gate); fail(ctx,"R4030","initialize Python on the main Wood thread before spawning workers"); return 0; }
        py.state=1; w_unlock(&py.gate); int ok=py_initialize(ctx); w_lock(&py.gate); py.state=ok?2:3;
    }
    if(py.state!=2) { w_unlock(&py.gate); if(!wd_rt_unwinding(ctx)) fail(ctx,"R4030","Python interpreter is initializing, closed, or previously failed"); return 0; }
    ++py.active; w_unlock(&py.gate);
    *gil=py.PyGILState_Ensure();
    if(wd_m_task_cancelled()) { fail(ctx,"R4022","task cancelled"); py.PyGILState_Release(*gil); w_lock(&py.gate); --py.active; w_unlock(&py.gate); return 0; }
    return 1;
}
static void py_leave(int gil) { py.PyGILState_Release(gil); w_lock(&py.gate); --py.active; w_unlock(&py.gate); }
static PyValue *py_get(void *ctx,WPython *object) { if(!object || !object->value) { fail(ctx,"R4032","Python object is closed"); return 0; } return object->value; }
static void py_destroy(void *p) {
    WPython *object=p;
    if(object->value) {
        /* Protect the entire GIL API interval, including its final release:
           zero live Python values alone does not prove no call is in flight. */
        w_lock(&py.gate); ++py.active; w_unlock(&py.gate);
        /* A live wrapper prevents finalization. Publish invalidation before
           DECREF, which can invoke arbitrary Python finalizers. */
        int gil=py.PyGILState_Ensure(); PyValue *value=object->value; object->value=0; py_decref(value); py.PyErr_Clear();
        w_lock(&py.gate); --py.objects; w_unlock(&py.gate); py_leave(gil);
    }
}
static WPython *py_wrap(void *ctx,PyValue *value) {
    if(!value) { if(!wd_rt_unwinding(ctx)) py_error(ctx); return 0; }
    WPython *result=(WPython*)allocate(ctx,sizeof(WPython),py_destroy);
    if(!result) { py_decref(value); return 0; }
    result->value=value; w_lock(&py.gate); ++py.objects; w_unlock(&py.gate); return result;
}
void wd_m_py_check(void *ctx,void *object) { int gil; if(py_enter(ctx,&gil)) { (void)py_get(ctx,object); py_leave(gil); } }
void *wd_m_py_import(void *ctx,const WStr *name) {
    int gil; if(!py_enter(ctx,&gil)) return 0;
    WPython *result=0; WStr terminated={0};
    if(p_valid(ctx,name)) p_copy(ctx,&terminated,name->data,name->size);
    if(!wd_rt_unwinding(ctx)) result=py_wrap(ctx,py.PyImport_ImportModule((char*)terminated.data));
    wd_m_str_drop(&terminated);
    py_leave(gil); return result;
}
void *wd_m_py_attr(void *ctx,void *object,const WStr *name) {
    int gil; if(!py_enter(ctx,&gil)) return 0;
    PyValue *value=py_get(ctx,object); WPython *result=0;
    WStr terminated={0};
    if(value && p_valid(ctx,name)) p_copy(ctx,&terminated,name->data,name->size);
    if(value && !wd_rt_unwinding(ctx)) result=py_wrap(ctx,py.PyObject_GetAttrString(value,(char*)terminated.data));
    wd_m_str_drop(&terminated);
    py_leave(gil); return result;
}
static PyValue *py_arguments(void *ctx,WSeq *values) {
    WInt count=wd_m_seq_len(values); PyValue *tuple=py.PyTuple_New((intptr_t)count); if(!tuple) { py_error(ctx); return 0; }
    for(WInt i=0;i<count && !wd_rt_unwinding(ctx);++i) {
        PyValue *value=py_get(ctx,*(WPython**)wd_m_seq_read(values,i));
        if(value) { py.Py_IncRef(value); if(py.PyTuple_SetItem(tuple,(intptr_t)i,value)<0) py_error(ctx); }
    }
    if(wd_rt_unwinding(ctx)) { py_decref(tuple); return 0; } return tuple;
}
void *wd_m_py_tuple(void *ctx,WSeq *values) { int gil; if(!py_enter(ctx,&gil)) return 0; WPython *result=py_wrap(ctx,py_arguments(ctx,values)); py_leave(gil); return result; }
void *wd_m_py_call(void *ctx,void *object,WSeq *arguments,WMap *keywords) {
    int gil; if(!py_enter(ctx,&gil)) return 0;
    PyValue *callable=py_get(ctx,object),*args=0,*kwargs=0; WPython *result=0;
    if(callable) args=py_arguments(ctx,arguments);
    if(args) kwargs=py.PyDict_New();
    if(args && !kwargs) py_error(ctx);
    for(WInt i=0;i<wd_m_map_len(keywords) && !wd_rt_unwinding(ctx);++i) {
        WStr *key=wd_m_map_entry(keywords,i,0); PyValue *value=py_get(ctx,*(WPython**)wd_m_map_entry(keywords,i,1));
        PyValue *name=py.PyUnicode_FromStringAndSize((char*)key->data,(intptr_t)key->size);
        if(!name) py_error(ctx);
        if(value && name && py.PyDict_SetItem(kwargs,name,value)<0) py_error(ctx);
        py_decref(name);
    }
    if(!wd_rt_unwinding(ctx)) result=py_wrap(ctx,py.PyObject_Call(callable,args,kwargs));
    py_decref(kwargs); py_decref(args); py.PyErr_Clear(); py_leave(gil); return result;
}
static PyValue *py_from_json(void *ctx,WJson *value,WInt depth) {
    if(wd_rt_unwinding(ctx)) return 0;
    if(depth>64 || !value) { fail(ctx,"R4032","Python conversion exceeds nesting limit"); return 0; }
    PyValue *result=0;
    switch(value->kind) {
    case 0: result=py.Py_GetConstant(0); break;
    case 1: result=py.Py_GetConstant(value->as.integer?2:1); break;
    case 2: result=py.PyLong_FromLongLong(value->as.integer); break;
    case 3: result=py.PyFloat_FromDouble(value->as.floating); break;
    case 4: result=py.PyUnicode_FromStringAndSize((char*)value->as.string.data,(intptr_t)value->as.string.size); break;
    case 5:
        result=py.PyList_New((intptr_t)wd_m_seq_len(value->as.array));
        for(WInt i=0;result && i<wd_m_seq_len(value->as.array) && !wd_rt_unwinding(ctx);++i) {
            PyValue *child=py_from_json(ctx,*(WJson**)wd_m_seq_read(value->as.array,i),depth+1);
            if(child && py.PyList_SetItem(result,(intptr_t)i,child)<0) py_error(ctx);
        } break;
    case 6:
        result=py.PyDict_New();
        for(WInt i=0;result && i<wd_m_map_len(value->as.object) && !wd_rt_unwinding(ctx);++i) {
            WStr *key=wd_m_map_entry(value->as.object,i,0);
            PyValue *name=py.PyUnicode_FromStringAndSize((char*)key->data,(intptr_t)key->size);
            PyValue *child=py_from_json(ctx,*(WJson**)wd_m_map_entry(value->as.object,i,1),depth+1);
            if(name && child && py.PyDict_SetItem(result,name,child)<0) py_error(ctx);
            if(!name && !wd_rt_unwinding(ctx)) py_error(ctx);
            py_decref(child); py_decref(name);
        } break;
    default: fail(ctx,"R4032","unsupported native JSON conversion");
    }
    if(!result && !wd_rt_unwinding(ctx)) py_error(ctx);
    if(wd_rt_unwinding(ctx)) { py_decref(result); return 0; } return result;
}
void *wd_m_py_value(void *ctx,WJson *value) { int gil; if(!py_enter(ctx,&gil)) return 0; WPython *result=py_wrap(ctx,py_from_json(ctx,value,0)); py_leave(gil); return result; }
static void py_utf8(void *ctx,WStr *out,PyValue *value) {
    intptr_t size=0; const char *bytes=py.PyUnicode_AsUTF8AndSize(value,&size);
    if(!bytes) py_error(ctx); else p_copy(ctx,out,(const unsigned char*)bytes,(WInt)size);
}
static WJson *py_to_json(void *ctx,PyValue *value,PyValue **ancestors,WInt depth,WInt *budget) {
    if(wd_rt_unwinding(ctx)) return 0;
    if(depth>64 || --*budget<0) { fail(ctx,"R4032","Python conversion exceeds 64 levels or 1000000 nodes"); return 0; }
    for(WInt i=0;i<depth;++i) if(ancestors[i]==value) { fail(ctx,"R4032","cyclic Python values cannot become JSON"); return 0; }
    ancestors[depth]=value;
    PyValue *none=py.Py_GetConstant(0); int null=value==none; py_decref(none);
    if(null) return j_new(ctx,0);
    PyValue *type=py.PyObject_Type(value); int kind=-1;
    for(int i=1;i<8;++i) if(type==py.types[i]) kind=i;
    py_decref(type);
    if(kind<0) { fail(ctx,"R4032","Python JSON conversion accepts exact builtin scalars, lists, tuples and string-key dictionaries; convert library objects explicitly"); return 0; }
    WJson *result=j_new(ctx,kind==6?5:kind==7?6:kind); if(!result) return 0;
    if(kind==1) result->as.integer=py.PyObject_IsTrue(value);
    if(kind==2) result->as.integer=py.PyLong_AsLongLong(value);
    if(kind==3) { result->as.floating=py.PyFloat_AsDouble(value); if(!py_finite(result->as.floating)) fail(ctx,"R4032","Python JSON numbers must be finite"); }
    if(kind==4) py_utf8(ctx,&result->as.string,value);
    if(kind==5 || kind==6) {
        result->as.array=wd_m_seq_new(ctx,sizeof(WJson*),0,0,j_retain_slot,j_drop_slot);
        intptr_t count=kind==5?py.PyList_Size(value):py.PyTuple_Size(value);
        if(count>*budget) fail(ctx,"R4032","Python conversion exceeds node limit");
        for(intptr_t i=0;i<count && !wd_rt_unwinding(ctx);++i) {
            PyValue *child=kind==5?py.PyList_GetItem(value,i):py.PyTuple_GetItem(value,i);
            WJson *item=py_to_json(ctx,child,ancestors,depth+1,budget);
            if(item) { wd_m_seq_append(ctx,&result->as.array,&item); wd_m_release(item); }
        }
    }
    if(kind==7) {
        result->as.object=j_map(ctx); intptr_t position=0; PyValue *key=0,*child=0;
        while(!wd_rt_unwinding(ctx) && py.PyDict_Next(value,&position,&key,&child)) {
            JPair item={0}; PyValue *typekey=py.PyObject_Type(key); int valid=typekey==py.types[4]; py_decref(typekey);
            if(!valid) { fail(ctx,"R4032","Python JSON dictionary keys must be strings"); break; }
            py_utf8(ctx,&item.key,key); item.value=py_to_json(ctx,child,ancestors,depth+1,budget);
            if(!wd_rt_unwinding(ctx)) { WJson **slot=wd_m_map_edit(ctx,&result->as.object,&item.key,1); if(slot) { *slot=item.value; wd_m_retain(item.value); } }
            j_pair_drop(&item);
        }
    }
    if(py.PyErr_Occurred() && !wd_rt_unwinding(ctx)) py_error(ctx);
    if(wd_rt_unwinding(ctx)) { wd_m_release(result); return 0; } return result;
}
void *wd_m_py_json(void *ctx,void *object) {
    int gil; if(!py_enter(ctx,&gil)) return 0;
    PyValue *value=py_get(ctx,object),*ancestors[65]; WInt budget=1000000;
    WJson *result=value?py_to_json(ctx,value,ancestors,0,&budget):0; py.PyErr_Clear(); py_leave(gil); return result;
}
void *wd_m_py_bytes(void *ctx,const WStr *bytes) { int gil; if(!py_enter(ctx,&gil)) return 0; WPython *result=py_wrap(ctx,py.PyBytes_FromStringAndSize((char*)bytes->data,(intptr_t)bytes->size)); py_leave(gil); return result; }
void wd_m_py_to_bytes(void *ctx,WStr *out,void *object) {
    *out=(WStr){0}; int gil; if(!py_enter(ctx,&gil)) return;
    PyValue *value=py_get(ctx,object); char *bytes=0; intptr_t size=0;
    if(value) { if(py.PyBytes_AsStringAndSize(value,&bytes,&size)<0) py_error(ctx); else p_copy(ctx,out,(unsigned char*)bytes,(WInt)size); }
    py_leave(gil);
}
void wd_m_py_text(void *ctx,WStr *out,void *object) {
    *out=(WStr){0}; int gil; if(!py_enter(ctx,&gil)) return;
    PyValue *value=py_get(ctx,object),*text=value?py.PyObject_Str(value):0;
    if(text) py_utf8(ctx,out,text); else if(!wd_rt_unwinding(ctx)) py_error(ctx);
    py_decref(text); py.PyErr_Clear(); py_leave(gil);
}
WInt wd_m_py_int(void *ctx,void *object) {
    int gil; if(!py_enter(ctx,&gil)) return 0; PyValue *value=py_get(ctx,object),*index=value?py.PyNumber_Index(value):0; WInt result=0;
    if(index) result=(WInt)py.PyLong_AsLongLong(index);
    if((!index || py.PyErr_Occurred()) && !wd_rt_unwinding(ctx)) py_error(ctx);
    py_decref(index); py.PyErr_Clear(); py_leave(gil); return result;
}
double wd_m_py_float(void *ctx,void *object) {
    int gil; if(!py_enter(ctx,&gil)) return 0; PyValue *value=py_get(ctx,object); double result=value?py.PyFloat_AsDouble(value):0;
    if(py.PyErr_Occurred() && !wd_rt_unwinding(ctx)) py_error(ctx);
    py.PyErr_Clear(); py_leave(gil); return result;
}
WInt wd_m_py_bool(void *ctx,void *object) {
    int gil; if(!py_enter(ctx,&gil)) return 0; PyValue *value=py_get(ctx,object); int result=value?py.PyObject_IsTrue(value):0;
    if(result<0 && !wd_rt_unwinding(ctx)) py_error(ctx); py.PyErr_Clear(); py_leave(gil); return result>0;
}
WInt wd_m_py_closed(void *ctx,void *object) {
    (void)ctx; WPython *value=object; if(!value) return 1;
    w_lock(&py.gate);
    if(py.state!=2) { w_unlock(&py.gate); return 1; }
    ++py.active; w_unlock(&py.gate);
    int gil=py.PyGILState_Ensure(); WInt result=!value->value; py_leave(gil); return result;
}
void wd_m_py_close(void *ctx,void *object) {
    WPython *value=object; if(!value) return;
    /* Resource cleanup must run during Wood unwinding and cancellation. Do not
       use py_enter(), whose ordinary-call guard deliberately rejects both. */
    w_lock(&py.gate);
    if(py.state!=2) { w_unlock(&py.gate); return; }
    ++py.active; w_unlock(&py.gate);
    int gil=py.PyGILState_Ensure();
    if(value->value) {
        PyValue *old=value->value; value->value=0; py_decref(old); py.PyErr_Clear();
        w_lock(&py.gate); --py.objects; w_unlock(&py.gate);
    }
    py_leave(gil); (void)ctx;
}
void wd_m_py_finish(void *ctx) {
    w_lock(&py.gate);
    if(py.state==0 || py.state==3) { w_unlock(&py.gate); return; }
    if(py.state!=2 || py.active || py.objects || wt_id()!=__atomic_load_n(&wt_primary_id,__ATOMIC_ACQUIRE)) {
        w_unlock(&py.gate); if(!wd_rt_unwinding(ctx)) fail(ctx,"R4032","close all Python objects and join workers before main-thread shutdown"); return;
    }
    py.state=3; w_unlock(&py.gate);
    py.PyEval_RestoreThread(py.thread); py.thread=0;
    if(py.Py_FinalizeEx()<0 && !wd_rt_unwinding(ctx)) fail(ctx,"R4031","Python shutdown failed");
    /* Keep the shared library mapped: extension destructors may run at process
       exit. Reinitialization and interpreter switching are deliberately refused. */
}
