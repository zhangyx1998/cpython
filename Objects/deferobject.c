/* DeferObject core implementation */

#include "Python.h"
#include "object.h"
#include "pyerrors.h"
#include "pytypedefs.h"

PyDoc_STRVAR(defer_doc, "DeferObject\n"
                        "(TODO) Add docs");

#define ENSURE_TYPE(OBJ, TYPE, PANIC)                                          \
    if (!Py_IS_TYPE(OBJ, TYPE))                                                \
    {                                                                          \
        PANIC;                                                                 \
    }

PyObject *PyDefer_New(PyObject *callable, unsigned char mutable)
{
    if (!PyCallable_Check(callable))
    {
        PyErr_SetString(PyExc_TypeError, "Failed to construct DeferObject: "
                                         "a non-callable object was supplied");
        return NULL;
    }

    if (PyType_Ready(&PyDefer_Type) < 0)
        return NULL;

    PyDeferObject *op;
    op = PyObject_GC_New(PyDeferObject, &PyDefer_Type);

    if (op == NULL)
        return NULL;

    op->callable = Py_NewRef(callable);
    op->mutable = mutable ? 1 : 0;
    op->args = NULL;
    op->kwargs = NULL;
    op->result = NULL;

    PyObject_GC_Track(op);
    return (PyObject *)op;
}

PyObject *_PyDefer_Observe_Impl(PyDeferObject *self)
{
    // Short-circuit if the defer-expr is already collapsed
    if (!self->mutable && self->result)
        return self->result;

    if (!PyCallable_Check(self->callable))
    {
        PyErr_Format(PyExc_RuntimeError,
                     "Failed to observe DeferObject: "
                     "%s is not callable",
                     (self->callable == NULL)
                         ? "<void>"
                         : Py_TYPE(self->callable)->tp_name);
        return NULL;
    }

    PyObject *obj;
    if (self->args || self->kwargs)
    {
        // Slow path: call the callable with the supplied arguments and keywords
        obj = PyObject_Call(self->callable, self->args, self->kwargs);
    }
    else
    {
        // Fast path: call the callable with no arguments
        obj = PyObject_CallNoArgs(self->callable);
    }

    if (obj == NULL)
        return NULL;

    // In case the returned value is another DeferObject, keep observing
    if (Py_IS_TYPE(obj, &PyDefer_Type))
        obj = _PyDefer_Observe_Impl(_Py_CAST(PyDeferObject *, obj));

    if (!self->mutable)
        self->result = obj;

    return obj;
}

/* ==================== BEGIN: static methods for defer ==================== */

PyObject *defer_reveal(PyObject *unused, PyObject *obj)
{
    ENSURE_TYPE(obj, &PyDefer_Type, return Py_None);

    if (PyType_Ready(&PyDeferExposed_Type) < 0)
        return NULL;

    PyDeferExposedObject *exposed =
        PyObject_GC_New(PyDeferExposedObject, &PyDeferExposed_Type);

    if (exposed == NULL)
        return NULL;

    exposed->ref = _Py_CAST(PyDeferObject *, Py_NewRef(obj));

    PyObject_GC_Track(exposed);
    return (PyObject *)exposed;
}

// Observe immediately (if not already) and prevent future re-evaluation.
PyObject *defer_freeze(PyObject *unused, PyObject *obj)
{
    ENSURE_TYPE(obj, &PyDefer_Type, return obj);
    _Py_CAST(PyDeferObject *, obj)->mutable = 0;
    return PyDefer_Observe(obj);
}

// Observe immediately, use cached result if appropriate.
PyObject *defer_snapshot(PyObject *unused, PyObject *obj)
{
    return PyDefer_Observe(obj);
}

// Mutable defer constructor.
PyObject *defer_Mutable(PyObject *unused, PyObject *args, PyObject *kwargs)
{
    PyObject *op = PyDefer_Type.tp_new(&PyDefer_Type, args, kwargs);
    if (op == NULL)
        return NULL;
    if (PyObject_TypeCheck(op, &PyDefer_Type))
        _Py_CAST(PyDeferObject *, op)->mutable = 1;
    else if (PyObject_TypeCheck(op, &PyDeferFactory_Type))
        _Py_CAST(PyDeferFactoryObject *, op)->mutable = 1;
    return op;
}

// Mutable defer constructor.
PyObject *defer_Immutable(PyObject *unused, PyObject *args, PyObject *kwargs)
{
    PyObject *op = PyDefer_Type.tp_new(&PyDefer_Type, args, kwargs);
    if (op == NULL)
        return NULL;
    if (PyObject_TypeCheck(op, &PyDefer_Type))
        _Py_CAST(PyDeferObject *, op)->mutable = 0;
    else if (PyObject_TypeCheck(op, &PyDeferFactory_Type))
        _Py_CAST(PyDeferFactoryObject *, op)->mutable = 0;
    return op;
}

static PyMethodDef defer_methods[] = {
    {"reveal", (PyCFunction)defer_reveal, METH_O | METH_STATIC,
     "Expose APIs of a defer object for manual manipulation."},
    {"freeze", (PyCFunction)defer_freeze, METH_O | METH_STATIC,
     "Freeze a defer object and observe immediately, "
     "return frozen value of a defer object. "
     "If a non-defer object is passed in, return as-is."},
    {"snapshot", (PyCFunction)defer_snapshot, METH_O | METH_STATIC,
     "Immediately observe a defer object, return observation result."
     "If a non-defer object is passed in, return as-is."},
    {"snapshot", (PyCFunction)defer_snapshot, METH_O | METH_STATIC,
     "Immediately observe a defer object, return observation result."
     "If a non-defer object is passed in, return as-is."},
    //  Convenience constructors
    {"Mutable", (PyCFunction)defer_Mutable,
     METH_VARARGS | METH_KEYWORDS | METH_STATIC,
     "Create a mutable defer object."},
    {"Immutable", (PyCFunction)defer_Immutable,
     METH_VARARGS | METH_KEYWORDS | METH_STATIC,
     "Create an immutable defer object."},
    {NULL, NULL} // Sentinel
};

/* ===================== END: static methods for defer ===================== */

// Core idea of the DeferObject - observation triggers evaluation
#define OBSERVE(OBJ)                                                           \
    OBJ = PyDefer_Observe(OBJ);                                                \
    if (OBJ == NULL) // OBSERVE(...) { <cond-failure> }

// Mirror a function call on DeferObject to the observed value.
// This should be used as a fallback when Py_XXX API is not available.
#define LOOKUP(self, hook, hook_type)                                          \
    hook_type hook = NULL;                                                     \
    hook = Py_TYPE(self)->tp_##hook;                                           \
    if (hook == NULL) // LOOKUP(...) { <cond-failure> }

#define ON_FAILURE(expr) expr // Helps to make the code more expressive

static PyObject *defer_getattr(PyObject *self, char *attr)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    return PyObject_GetAttrString(self, attr);
}

static int defer_setattr(PyObject *self, char *attr, PyObject *value)
{
    OBSERVE(self) ON_FAILURE(return -1);
    return PyObject_SetAttrString(self, attr, value);
}

static PyObject *defer_repr(PyObject *self)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    return PyObject_Repr(self);
}

/* =================== BEGIN: Proxy of all number methods =================== */

#define DEFER_EXPR_NB_COMMON(FUNC_TYPE, OPERAND, PANIC)                        \
    if (!PyNumber_Check(op0))                                                  \
    {                                                                          \
        PyErr_SetString(PyExc_TypeError, "object is not numeric");             \
        PANIC;                                                                 \
    }                                                                          \
    FUNC_TYPE operand = Py_TYPE(op0)->tp_as_number->nb_##OPERAND;              \
    if (operand == NULL)                                                       \
    {                                                                          \
        PyErr_SetString(PyExc_TypeError, #OPERAND " not supported");           \
        PANIC;                                                                 \
    }

#define DEFER_EXPR_UNARY_FUNC(OPERAND)                                         \
    static PyObject *defer_nb_##OPERAND(PyObject *op0)                         \
    {                                                                          \
        OBSERVE(op0) ON_FAILURE(return NULL);                                  \
        DEFER_EXPR_NB_COMMON(unaryfunc, OPERAND, return Py_NotImplemented);    \
        return operand(op0);                                                   \
    }

#define DEFER_EXPR_BINARY_FUNC(OPERAND)                                        \
    static PyObject *defer_nb_##OPERAND(PyObject *op0, PyObject *op1)          \
    {                                                                          \
        OBSERVE(op0) ON_FAILURE(return NULL);                                  \
        OBSERVE(op1) ON_FAILURE(return NULL);                                  \
        DEFER_EXPR_NB_COMMON(binaryfunc, OPERAND, return Py_NotImplemented);   \
        return operand(op0, op1);                                              \
    }

#define DEFER_EXPR_TERNARY_FUNC(OPERAND)                                       \
    static PyObject *defer_nb_##OPERAND(PyObject *op0, PyObject *op1,          \
                                        PyObject *op2)                         \
    {                                                                          \
        OBSERVE(op0) ON_FAILURE(return NULL);                                  \
        OBSERVE(op1) ON_FAILURE(return NULL);                                  \
        OBSERVE(op2) ON_FAILURE(return NULL);                                  \
        DEFER_EXPR_NB_COMMON(ternaryfunc, OPERAND, return Py_NotImplemented);  \
        return operand(op0, op1, op2);                                         \
    }

DEFER_EXPR_BINARY_FUNC(add);
DEFER_EXPR_BINARY_FUNC(subtract);
DEFER_EXPR_BINARY_FUNC(multiply);
DEFER_EXPR_BINARY_FUNC(remainder);
DEFER_EXPR_BINARY_FUNC(divmod);
DEFER_EXPR_TERNARY_FUNC(power);
DEFER_EXPR_UNARY_FUNC(negative);
DEFER_EXPR_UNARY_FUNC(positive);
DEFER_EXPR_UNARY_FUNC(absolute);
static int defer_nb_bool(PyObject *self)
{
    OBSERVE(self) ON_FAILURE(return -1);
    return PyObject_IsTrue(self);
}
DEFER_EXPR_UNARY_FUNC(invert);
DEFER_EXPR_BINARY_FUNC(lshift);
DEFER_EXPR_BINARY_FUNC(rshift);
DEFER_EXPR_BINARY_FUNC(and);
DEFER_EXPR_BINARY_FUNC(xor);
DEFER_EXPR_BINARY_FUNC(or);
DEFER_EXPR_UNARY_FUNC(int);
DEFER_EXPR_UNARY_FUNC(float);
DEFER_EXPR_BINARY_FUNC(inplace_add);
DEFER_EXPR_BINARY_FUNC(inplace_subtract);
DEFER_EXPR_BINARY_FUNC(inplace_multiply);
DEFER_EXPR_BINARY_FUNC(inplace_remainder);
DEFER_EXPR_TERNARY_FUNC(inplace_power);
DEFER_EXPR_BINARY_FUNC(inplace_lshift);
DEFER_EXPR_BINARY_FUNC(inplace_rshift);
DEFER_EXPR_BINARY_FUNC(inplace_and);
DEFER_EXPR_BINARY_FUNC(inplace_xor);
DEFER_EXPR_BINARY_FUNC(inplace_or);
DEFER_EXPR_BINARY_FUNC(floor_divide);
DEFER_EXPR_BINARY_FUNC(true_divide);
DEFER_EXPR_BINARY_FUNC(inplace_floor_divide);
DEFER_EXPR_BINARY_FUNC(inplace_true_divide);
DEFER_EXPR_UNARY_FUNC(index);
DEFER_EXPR_BINARY_FUNC(matrix_multiply);
DEFER_EXPR_BINARY_FUNC(inplace_matrix_multiply);

static PyNumberMethods defer_as_number = {
    defer_nb_add,
    defer_nb_subtract,
    defer_nb_multiply,
    defer_nb_remainder,
    defer_nb_divmod,
    defer_nb_power,
    defer_nb_negative,
    defer_nb_positive,
    defer_nb_absolute,
    defer_nb_bool,
    defer_nb_invert,
    defer_nb_lshift,
    defer_nb_rshift,
    defer_nb_and,
    defer_nb_xor,
    defer_nb_or,
    defer_nb_int,
    NULL, /* the slot formerly known as nb_long */
    defer_nb_float,
    defer_nb_inplace_add,
    defer_nb_inplace_subtract,
    defer_nb_inplace_multiply,
    defer_nb_inplace_remainder,
    defer_nb_inplace_power,
    defer_nb_inplace_lshift,
    defer_nb_inplace_rshift,
    defer_nb_inplace_and,
    defer_nb_inplace_xor,
    defer_nb_inplace_or,
    defer_nb_floor_divide,
    defer_nb_true_divide,
    defer_nb_inplace_floor_divide,
    defer_nb_inplace_true_divide,
    defer_nb_index,
    defer_nb_matrix_multiply,
    defer_nb_inplace_matrix_multiply,
};

/* ==================== END: Proxy of all number methods ==================== */

static Py_hash_t defer_hash(PyObject *self)
{
    OBSERVE(self) ON_FAILURE(return -1);
    return PyObject_Hash(self);
}

static PyObject *defer_call(PyObject *self, PyObject *args, PyObject *kwargs)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    return PyObject_Call(self, args, kwargs);
}

static PyObject *defer_str(PyObject *self)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    return PyObject_Str(self);
}

static PyObject *defer_getattro(PyObject *self, PyObject *attr)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    return PyObject_GetAttr(self, attr);
}

static int defer_setattro(PyObject *self, PyObject *attr, PyObject *value)
{
    OBSERVE(self) ON_FAILURE(return -1);
    return PyObject_SetAttr(self, attr, value);
}

// Special: not a proxied method
// Traverse enclosed objects for garbage collection
static int defer_traverse(PyDeferObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->callable);
    Py_VISIT(self->args);
    Py_VISIT(self->kwargs);
    Py_VISIT(self->result);
    return 0;
}

// Special: not a proxied method
// Clear our reference to the callable object
// User might gain access to it using tools from the inspect module
static int defer_clear(PyDeferObject *self)
{
    Py_CLEAR(self->callable);
    Py_CLEAR(self->result);
    return 0;
}

static PyObject *defer_richcompare(PyObject *self, PyObject *other, int op)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    return PyObject_RichCompare(self, other, op);
}

static PyObject *defer_iter(PyObject *self)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    return PyObject_GetIter(self);
}

static PyObject *defer_iternext(PyObject *self)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    LOOKUP(self, iternext, iternextfunc)
    ON_FAILURE({
        PyErr_SetString(PyExc_TypeError, "object is not iterable");
        return NULL;
    });
    return iternext(self);
}

static PyObject *defer_descr_get(PyObject *self, PyObject *obj, PyObject *type)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    LOOKUP(self, descr_get, descrgetfunc)
    ON_FAILURE({
        PyErr_SetString(PyExc_TypeError, "object has no descriptor getter");
        return NULL;
    });
    return descr_get(self, obj, type);
}

static int defer_descr_set(PyObject *self, PyObject *obj, PyObject *value)
{
    OBSERVE(self) ON_FAILURE(return -1);
    LOOKUP(self, descr_set, descrsetfunc)
    ON_FAILURE({
        PyErr_SetString(PyExc_TypeError, "object has no descriptor setter");
        return -1;
    });
    return descr_set(self, obj, value);
}

// By default, we re-evaluate a defer object on every observation
static const unsigned char mutable_default = 0;

PyObject *defer_new(PyTypeObject *unused, PyObject *args, PyObject *kwargs)
{
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    // We need at least one positional argument - the callable
    if (nargs < 1)
    {
        PyErr_SetString(PyExc_TypeError, "DeferObject requires at least one "
                                         "positional argument - the callable");
        return NULL;
    }
    // Get the callable
    PyObject *callable = PyTuple_GET_ITEM(args, 0);
    // If callable is ellipsis, return DeferFactory
    if (callable == Py_Ellipsis)
    {
        if (PyType_Ready(&PyDeferFactory_Type) < 0)
        {
            PyErr_SetString(PyExc_SystemError,
                            "Failed to initialize type DeferFactory");
            return NULL;
        }
        PyDeferFactoryObject *op =
            PyObject_GC_New(PyDeferFactoryObject, &PyDeferFactory_Type);
        if (op == NULL)
        {
            PyErr_SetString(PyExc_SystemError, "Failed allocate DeferFactory");
            return NULL;
        }
        PyObject_GC_Track(op);
        op->mutable = mutable_default;
        if (nargs > 1)
            op->args = PyTuple_GetSlice(args, 1, nargs);
        else
            op->args = NULL;
        if (kwargs && PyDict_Size(kwargs) > 0)
            op->kwargs = PyDict_Copy(kwargs);
        else
            op->kwargs = NULL;
        return (PyObject *)op;
    }
    // Construct a DeferObject
    PyDeferObject *op = (PyDeferObject *)PyDefer_New(callable, mutable_default);
    if (op == NULL)
        return NULL;
    // Check if we have any additional args/kwargs to assign to it
    if (nargs > 1)
        op->args = PyTuple_GetSlice(args, 1, nargs);
    if (kwargs && PyDict_Size(kwargs) > 0)
        op->kwargs = PyDict_Copy(kwargs);
    // Return the constructed object
    return (PyObject *)op;
}

static void defer_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    defer_clear((PyDeferObject *)self);
    PyObject_GC_Del(self);
}

PyTypeObject PyDefer_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) //
    "defer",
    sizeof(PyDeferObject),
    (Py_ssize_t)0,
    (destructor)defer_dealloc,
    (Py_ssize_t)0,
    (getattrfunc)defer_getattr,
    (setattrfunc)defer_setattr,
    (PyAsyncMethods *)0,
    (reprfunc)defer_repr,
    (PyNumberMethods *)&defer_as_number,
    (PySequenceMethods *)0,
    (PyMappingMethods *)0,
    (hashfunc)defer_hash,
    (ternaryfunc)defer_call,
    (reprfunc)defer_str,
    (getattrofunc)defer_getattro,
    (setattrofunc)defer_setattro,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    defer_doc,
    (traverseproc)defer_traverse,
    (inquiry)defer_clear,
    (richcmpfunc)defer_richcompare,
    (Py_ssize_t)0,
    (getiterfunc)defer_iter,
    (iternextfunc)defer_iternext,
    (PyMethodDef *)defer_methods,
    (PyMemberDef *)0,
    (PyGetSetDef *)0,
    (PyTypeObject *)0,
    (PyObject *)0,
    (descrgetfunc)defer_descr_get,
    (descrsetfunc)defer_descr_set,
    (Py_ssize_t)0,
    (initproc)0,
    (allocfunc)0,
    (newfunc)defer_new,
    (freefunc)0,
};

// Exposed version of the DeferObject through `DeferObject.expose()` method

static int defer_exposed_traverse(PyDeferExposedObject *self, visitproc visit,
                                  void *arg)
{
    Py_VISIT(self->ref);
    return 0;
}

static int defer_exposed_clear(PyDeferExposedObject *self)
{
    Py_CLEAR(self->ref);
    return 0;
}

static void defer_exposed_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    defer_exposed_clear((PyDeferExposedObject *)self);
    PyObject_GC_Del(self);
}

#define GET(ATTR)                                                              \
    else if (strcmp(attr, #ATTR) == 0 && self->ref->ATTR)                      \
    {                                                                          \
        return Py_NewRef(self->ref->ATTR);                                     \
    }

static PyObject *defer_exposed_getattr(PyDeferExposedObject *self, char *attr)
{
    if (strcmp(attr, "mutable") == 0)
        return (self->ref->mutable) ? (Py_True) : (Py_False);
    GET(callable)
    GET(args)
    GET(kwargs)
    GET(result)
    // No such attribute
    PyErr_Format(PyExc_AttributeError, "<%s object> has no attribute \"%s\"",
                 self->ob_base.ob_type->tp_name, attr);
    return NULL;
}

#undef GET

#define SET(ATTR)                                                              \
    else if (strcmp(attr, #ATTR) == 0)                                         \
    {                                                                          \
        Py_XDECREF(self->ref->ATTR);                                           \
        self->ref->ATTR = Py_XNewRef(value);                                   \
        return 0;                                                              \
    }

static int defer_exposed_setattr(PyDeferExposedObject *self, char *attr,
                                 PyObject *value)
{
    if (strcmp(attr, "mutable") == 0)
    {
        self->ref->mutable = Py_IsTrue(value) ? 1 : 0;
        return 0;
    }
    SET(callable)
    SET(args)
    SET(kwargs)
    SET(result)
    // No such attribute
    PyErr_Format(PyExc_AttributeError, "<%s object> has no attribute \"%s\"",
                 self->ob_base.ob_type->tp_name, attr);
    return -1;
}

#undef SET

PyTypeObject PyDeferExposed_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) //
    "DeferExposed",
    sizeof(PyDeferExposedObject),
    (Py_ssize_t)0,
    (destructor)defer_exposed_dealloc,
    (Py_ssize_t)0,
    (getattrfunc)defer_exposed_getattr,
    (setattrfunc)defer_exposed_setattr,
    (PyAsyncMethods *)0,
    (reprfunc)0,
    (PyNumberMethods *)0,
    (PySequenceMethods *)0,
    (PyMappingMethods *)0,
    (hashfunc)0,
    (ternaryfunc)0,
    (reprfunc)0,
    (getattrofunc)0,
    (setattrofunc)0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    defer_doc,
    (traverseproc)defer_exposed_traverse,
    (inquiry)defer_exposed_clear,
    (richcmpfunc)0,
    (Py_ssize_t)0,
    (getiterfunc)0,
    (iternextfunc)0,
    (PyMethodDef *)0,
    (PyMemberDef *)0,
    (PyGetSetDef *)0,
    (PyTypeObject *)0,
    (PyObject *)0,
    (descrgetfunc)0,
    (descrsetfunc)0,
    (Py_ssize_t)0,
    (initproc)0,
    (allocfunc)0,
    (newfunc)0,
    (freefunc)0,
};

// DeferFactory:
// Incomplete defer construct: defer(..., *args, **kwargs)

static int defer_factory_traverse(PyDeferFactoryObject *self, visitproc visit,
                                  void *arg)
{
    Py_VISIT(self->args);
    Py_VISIT(self->kwargs);
    return 0;
}

static int defer_factory_clear(PyDeferFactoryObject *self)
{
    Py_CLEAR(self->args);
    Py_CLEAR(self->kwargs);
    return 0;
}

static void defer_factory_dealloc(PyDeferFactoryObject *self)
{
    PyObject_GC_UnTrack(self);
    defer_factory_clear(self);
    PyObject_GC_Del(self);
}

static PyObject *defer_factory_call(PyDeferFactoryObject *self, PyObject *args,
                                    PyObject *kwargs)
{
    // Only accepts one single positional argument
    if (PyTuple_GET_SIZE(args) != 1 || kwargs != NULL)
    {
        PyErr_SetString(PyExc_TypeError, "DeferFactory requires exactly one "
                                         "positional argument - the callable");
        return NULL;
    }
    PyDeferObject *op =
        (PyDeferObject *)PyDefer_New(PyTuple_GET_ITEM(args, 0), 1);
    if (op == NULL)
        return NULL;
    op->mutable = self->mutable;
    op->args = Py_XNewRef(self->args);
    op->kwargs = Py_XNewRef(self->kwargs);
    return (PyObject *)op;
}

PyTypeObject PyDeferFactory_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) //
    "DeferFactory",
    sizeof(PyDeferFactoryObject),
    (Py_ssize_t)0,
    (destructor)defer_factory_dealloc,
    (Py_ssize_t)0,
    (getattrfunc)0,
    (setattrfunc)0,
    (PyAsyncMethods *)0,
    (reprfunc)0,
    (PyNumberMethods *)0,
    (PySequenceMethods *)0,
    (PyMappingMethods *)0,
    (hashfunc)0,
    (ternaryfunc)defer_factory_call,
    (reprfunc)0,
    (getattrofunc)0,
    (setattrofunc)0,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    defer_doc,
    (traverseproc)defer_factory_traverse,
    (inquiry)defer_factory_clear,
    (richcmpfunc)0,
    (Py_ssize_t)0,
    (getiterfunc)0,
    (iternextfunc)0,
    (PyMethodDef *)0,
    (PyMemberDef *)0,
    (PyGetSetDef *)0,
    (PyTypeObject *)0,
    (PyObject *)0,
    (descrgetfunc)0,
    (descrsetfunc)0,
    (Py_ssize_t)0,
    (initproc)0,
    (allocfunc)0,
    (newfunc)0,
    (freefunc)0,
};
