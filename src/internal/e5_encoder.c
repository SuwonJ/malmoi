/* Optional adapter. The default library does not compile or link Python. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "e5_encoder.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MalmoiE5 { PyObject *instance; };

static int encode(void *userdata, const char *text, float *out, size_t dimension) {
    MalmoiE5 *e=(MalmoiE5 *)userdata;
    PyGILState_STATE gil=PyGILState_Ensure();
    PyObject *result=PyObject_CallMethod(e->instance,"encode","s",text);
    PyObject *values=result ? PySequence_Fast(result,"Expected vector") : NULL;
    size_t i;
    int rc=MALMOI_CONFIG;
    if (values && (size_t)PySequence_Fast_GET_SIZE(values)==dimension) {
        rc=MALMOI_OK;
        for (i=0;i<dimension;++i) {
            double value=PyFloat_AsDouble(PySequence_Fast_GET_ITEM(values,(Py_ssize_t)i));
            if (PyErr_Occurred() || !isfinite(value) || fabs(value)>1.00001) {
                rc=MALMOI_CONFIG; break;
            }
            out[i]=(float)value;
        }
    }
    if (PyErr_Occurred()) PyErr_Print();
    Py_XDECREF(values);Py_XDECREF(result);
    PyGILState_Release(gil);
    return rc;
}

void malmoi_e5_close(MalmoiE5 *e) {
    if (e) {
        PyGILState_STATE gil=PyGILState_Ensure();
        Py_XDECREF(e->instance);
        PyGILState_Release(gil);
        free(e);
    }
}

int malmoi_e5_open(MalmoiE5 **out, const char *database, const char *model,
                   const char *space, MalmoiEncoder *encoder) {
    const char *script=getenv("MALMOI_ENCODER_SCRIPT");
    PyObject *globals=NULL,*name=NULL,*executed=NULL,*klass,*instance=NULL;
    PyObject *json=NULL,*descriptor=NULL,*dumps=NULL,*args=NULL,*kwargs=NULL,*serialized=NULL;
    PyGILState_STATE gil;
    int rc=MALMOI_CONFIG;
    *out=NULL;
    if (!script || !script[0]) script="tools/embed_query.py";
    /* Initialize once on the opening thread. Never finalize an interpreter
     * owned by another host, or reload native ORT/NumPy modules after close.
     * As with environment configuration, initial opens must be serialized. */
    if (!Py_IsInitialized()) {
        PyConfig config;
        PyStatus status;
        PyConfig_InitPythonConfig(&config);
        config.install_signal_handlers=0;
        status=PyConfig_SetBytesString(&config,&config.program_name,MALMOI_PYTHON_EXECUTABLE);
        if (!PyStatus_Exception(status)) status=Py_InitializeFromConfig(&config);
        if (PyStatus_Exception(status))
            fprintf(stderr,"E5 Python initialization failed: %s\n",status.err_msg ? status.err_msg : "unknown");
        PyConfig_Clear(&config);
        if (PyStatus_Exception(status)) return rc;
        (void)PyEval_SaveThread();
    }
    gil=PyGILState_Ensure();
    globals=PyDict_New();name=PyUnicode_FromString("malmoi_e5_encoder");
    if (!globals || !name || PyDict_SetItemString(globals,"__name__",name)<0) goto done;
    /* Python opens the script, avoiding Windows CRT FILE* incompatibility. */
    {
        PyObject *builtins=PyEval_GetBuiltins();
        PyObject *open_fn=PyDict_GetItemString(builtins,"open");
        PyObject *stream=open_fn ? PyObject_CallFunction(open_fn,"ss",script,"rb") : NULL;
        PyObject *source=stream ? PyObject_CallMethod(stream,"read",NULL) : NULL;
        PyObject *closed=stream ? PyObject_CallMethod(stream,"close",NULL) : NULL;
        Py_XDECREF(closed);Py_XDECREF(stream);
        if (source) {
            if (PyBytes_Check(source))
                executed=PyRun_String(PyBytes_AS_STRING(source),Py_file_input,globals,globals);
            Py_DECREF(source);
        }
    }
    if (!executed) goto done;
    klass=PyDict_GetItemString(globals,"QueryEncoder");
    if (!klass) goto done;
    instance=PyObject_CallFunction(klass,"ss",database,model);
    if (!instance) goto done;
    /* QueryEncoder validates revision, hashes, pooling, prefix and dimension.
     * Also compare its full canonical descriptor with the C loader's space. */
    json=PyImport_ImportModule("json");
    descriptor=PyObject_GetAttrString(instance,"space");
    dumps=json ? PyObject_GetAttrString(json,"dumps") : NULL;
    args=descriptor ? PyTuple_Pack(1,descriptor) : NULL;
    kwargs=Py_BuildValue("{s:O,s:O,s:(ss)}","ensure_ascii",Py_False,"sort_keys",Py_True,
                         "separators",",",":");
    if (dumps && args && kwargs) serialized=PyObject_Call(dumps,args,kwargs);
    if (!serialized) goto done;
    {
        const char *actual=PyUnicode_AsUTF8(serialized);
        if (!actual) goto done;
        if (strcmp(actual,space)) { rc=MALMOI_SPACE; goto done; }
    }
    *out=(MalmoiE5 *)calloc(1,sizeof(MalmoiE5));
    if (!*out) {rc=MALMOI_MEMORY;goto done;}
    (*out)->instance=instance;instance=NULL;
    encoder->encode=encode;encoder->userdata=*out;encoder->space=space;
    rc=MALMOI_OK;
done:
    if (PyErr_Occurred()) PyErr_Print();
    Py_XDECREF(serialized);Py_XDECREF(kwargs);Py_XDECREF(args);
    Py_XDECREF(dumps);Py_XDECREF(descriptor);Py_XDECREF(json);
    Py_XDECREF(instance);Py_XDECREF(executed);Py_XDECREF(name);Py_XDECREF(globals);
    PyGILState_Release(gil);
    return rc;
}
