/* Independent optional CPython embedding oracle. This test alone uses Python
   headers/hosted C; neither is required by the Wood compiler or native bridge. */
#include <Python.h>
#include <stdio.h>

/* No Wood runtime or Python script compilation participates in this variant.
   Use the same public calls and release every owned temporary explicitly. */
static PyObject *invoke(PyObject *owner,const char *name,PyObject *arguments) {
    PyObject *function=PyObject_GetAttrString(owner,name),*keywords=PyDict_New();
    PyObject *result=function&&keywords?PyObject_Call(function,arguments,keywords):NULL;
    Py_XDECREF(function); Py_XDECREF(keywords); return result;
}
static int numpy_capi(void) {
    PyObject *numpy=PyImport_ImportModule("numpy"),*list=PyList_New(4);
    if(!numpy || !list) { Py_XDECREF(numpy); Py_XDECREF(list); return 1; }
    for(Py_ssize_t i=0;i<4;++i) PyList_SetItem(list,i,PyFloat_FromDouble((double)i+1));
    PyObject *args=PyTuple_Pack(1,list),*array=invoke(numpy,"array",args);
    Py_DECREF(args); Py_DECREF(list);
    if(!array) { Py_DECREF(numpy); return 1; }
    PyObject *shape=Py_BuildValue("(ii)",2,2); args=PyTuple_Pack(1,shape); Py_DECREF(shape);
    PyObject *matrix=invoke(array,"reshape",args); Py_DECREF(args);
    if(!matrix) { Py_DECREF(array); Py_DECREF(numpy); return 1; }
    args=PyTuple_New(0); PyObject *rows=invoke(matrix,"tolist",args); Py_DECREF(args); Py_XDECREF(rows);
    args=PyTuple_Pack(1,matrix); PyObject *sum=invoke(numpy,"sum",args); Py_DECREF(args);
    int status=!sum || PyFloat_AsDouble(sum)!=10.0; Py_XDECREF(sum);
    args=PyTuple_Pack(2,matrix,matrix); PyObject *product=invoke(numpy,"matmul",args); Py_DECREF(args);
    if(product) { args=PyTuple_New(0); rows=invoke(product,"tolist",args); Py_DECREF(args); Py_XDECREF(rows); }
    else status=1;
    Py_XDECREF(product); Py_DECREF(matrix);
    shape=PyLong_FromLong(3); args=PyTuple_Pack(1,shape); Py_DECREF(shape);
    PyObject *bad=invoke(array,"reshape",args); Py_DECREF(args);
    if(bad) { Py_DECREF(bad); status=1; }
    PyObject *error=PyErr_GetRaisedException();
    if(!error) status=1;
    else {
        PyObject *type=PyObject_Type(error),*name=type?PyObject_GetAttrString(type,"__name__"):NULL,*text=PyObject_Str(error);
        Py_ssize_t size; if(name) (void)PyUnicode_AsUTF8AndSize(name,&size);
        if(text) (void)PyUnicode_AsUTF8AndSize(text,&size);
        Py_XDECREF(text); Py_XDECREF(name); Py_XDECREF(type); Py_DECREF(error); PyErr_Clear();
    }
    Py_DECREF(array); Py_DECREF(numpy);
    if(PyErr_Occurred()) { PyErr_Clear(); status=1; }
    if(!status) puts("Independent CPython NumPy C-API oracle passed");
    return status;
}

int main(int argc,char **argv) {
    if(argc!=3 && argc<6) return 2;
    PyInitConfig *config=PyInitConfig_Create();
    if(!config) return 2;
    int status=PyInitConfig_SetStr(config,"program_name",argv[1]);
    if(!status) status=PyInitConfig_SetStr(config,"executable",argv[1]);
    if(!status) status=PyInitConfig_SetInt(config,"install_signal_handlers",0);
    if(argc>=6) {
        if(!status) status=PyInitConfig_SetStr(config,"prefix",argv[3]);
        if(!status) status=PyInitConfig_SetStr(config,"base_prefix",argv[4]);
        const char *off[]={"configure_c_stdio","site_import","use_environment","user_site_directory","write_bytecode","parse_argv"};
        for(unsigned i=0;i<sizeof(off)/sizeof(off[0]) && !status;++i) status=PyInitConfig_SetInt(config,off[i],0);
        if(!status) status=PyInitConfig_SetStrList(config,"module_search_paths",(size_t)argc-5,argv+5);
        if(!status) status=PyInitConfig_SetInt(config,"module_search_paths_set",1);
    }
    if(!status) status=Py_InitializeFromInitConfig(config);
    PyInitConfig_Free(config);
    if(status<0) return 2;
    if(strcmp(argv[2],"--numpy-capi")==0) status=numpy_capi();
    else {
        FILE *source=fopen(argv[2],"rb");
        if(!source) { Py_FinalizeEx(); return 2; }
        status=PyRun_SimpleFile(source,argv[2]);
        fclose(source);
    }
    if(Py_FinalizeEx()<0) return 2;
    return status?1:0;
}
