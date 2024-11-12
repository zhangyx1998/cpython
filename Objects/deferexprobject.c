/* DeferExpr core implementation */

#include "Python.h" // IWYU pragma: keep
#include "pycore_object.h"
#include "pyerrors.h"
#include "pytypedefs.h"
#include "refcount.h"

PyDoc_STRVAR(defer_expr_doc, "DeferExpr\n"
                             "(TODO) Add docs");

#define ENSURE_TYPE(OBJ, TYPE, PANIC)                                          \
    if (!Py_IS_TYPE(OBJ, TYPE))                                                \
    {                                                                          \
        PANIC;                                                                 \
    }

PyObject *PyDeferExpr_New(PyObject *callable, unsigned char collapsible)
{
    if (!PyCallable_Check(callable))
    {
        PyErr_SetString(PyExc_TypeError, "Failed to construct DeferExpr: "
                                         "a non-callable object was supplied");
        return NULL;
    }

    PyDeferExprObject *op;
    op = PyObject_GC_New(PyDeferExprObject, &PyDeferExpr_Type);

    if (op == NULL)
        return NULL;

    op->callable = Py_NewRef(callable);
    op->collapsible = collapsible;
    op->result = NULL;

    PyObject_GC_Track(op);
    return (PyObject *)op;
}

PyObject *PyDeferExpr_Observe(PyObject *obj)
{
    ENSURE_TYPE(obj, &PyDeferExpr_Type, return obj);
    PyDeferExprObject *self = _Py_CAST(PyDeferExprObject *, obj);
    // Short-circuit if the defer-expr is already collapsed
    if (self->collapsible && self->result != NULL)
        return self->result;

    obj = PyObject_CallNoArgs(_Py_CAST(PyObject *, self->callable));
    obj = PyDeferExpr_Observe(obj);

    if (self->collapsible)
        self->result = obj;

    return obj;
}

// Core idea of the DeferExpr - observation triggers evaluation
#define OBSERVE(OBJ)                                                           \
    OBJ = PyDeferExpr_Observe(OBJ);                                            \
    if (OBJ == NULL) // OBSERVE(...) { <cond-failure> }

// Mirror a function call on DeferExpr to the observed value.
// This should be used as a fallback when Py_XXX API is not available.
#define LOOKUP(self, hook, hook_type)                                          \
    hook_type hook = NULL;                                                     \
    hook = Py_TYPE(self)->tp_##hook;                                           \
    if (hook == NULL) // LOOKUP(...) { <cond-failure> }

#define ON_FAILURE(expr) expr // Helps to make the code more expressive

static PyObject *defer_expr_getattr(PyObject *self, char *attr)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    return PyObject_GetAttrString(self, attr);
}

static int defer_expr_setattr(PyObject *self, char *attr, PyObject *value)
{
    OBSERVE(self) ON_FAILURE(return -1);
    return PyObject_SetAttrString(self, attr, value);
}

static PyObject *defer_expr_repr(PyObject *self)
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
    static PyObject *defer_expr_nb_##OPERAND(PyObject *op0)                    \
    {                                                                          \
        OBSERVE(op0) ON_FAILURE(return NULL);                                  \
        DEFER_EXPR_NB_COMMON(unaryfunc, OPERAND, return NULL);                 \
        return operand(op0);                                                   \
    }

#define DEFER_EXPR_BINARY_FUNC(OPERAND)                                        \
    static PyObject *defer_expr_nb_##OPERAND(PyObject *op0, PyObject *op1)     \
    {                                                                          \
        OBSERVE(op0) ON_FAILURE(return NULL);                                  \
        OBSERVE(op1) ON_FAILURE(return NULL);                                  \
        DEFER_EXPR_NB_COMMON(binaryfunc, OPERAND, return NULL);                \
        return operand(op0, op1);                                              \
    }

#define DEFER_EXPR_TERNARY_FUNC(OPERAND)                                       \
    static PyObject *defer_expr_nb_##OPERAND(PyObject *op0, PyObject *op1,     \
                                             PyObject *op2)                    \
    {                                                                          \
        OBSERVE(op0) ON_FAILURE(return NULL);                                  \
        OBSERVE(op1) ON_FAILURE(return NULL);                                  \
        OBSERVE(op2) ON_FAILURE(return NULL);                                  \
        DEFER_EXPR_NB_COMMON(ternaryfunc, OPERAND, return NULL);               \
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
static int defer_expr_nb_bool(PyObject *self)
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

static PyNumberMethods defer_expr_as_number = {
    defer_expr_nb_add,
    defer_expr_nb_subtract,
    defer_expr_nb_multiply,
    defer_expr_nb_remainder,
    defer_expr_nb_divmod,
    defer_expr_nb_power,
    defer_expr_nb_negative,
    defer_expr_nb_positive,
    defer_expr_nb_absolute,
    defer_expr_nb_bool,
    defer_expr_nb_invert,
    defer_expr_nb_lshift,
    defer_expr_nb_rshift,
    defer_expr_nb_and,
    defer_expr_nb_xor,
    defer_expr_nb_or,
    defer_expr_nb_int,
    NULL, /* the slot formerly known as nb_long */
    defer_expr_nb_float,
    defer_expr_nb_inplace_add,
    defer_expr_nb_inplace_subtract,
    defer_expr_nb_inplace_multiply,
    defer_expr_nb_inplace_remainder,
    defer_expr_nb_inplace_power,
    defer_expr_nb_inplace_lshift,
    defer_expr_nb_inplace_rshift,
    defer_expr_nb_inplace_and,
    defer_expr_nb_inplace_xor,
    defer_expr_nb_inplace_or,
    defer_expr_nb_floor_divide,
    defer_expr_nb_true_divide,
    defer_expr_nb_inplace_floor_divide,
    defer_expr_nb_inplace_true_divide,
    defer_expr_nb_index,
    defer_expr_nb_matrix_multiply,
    defer_expr_nb_inplace_matrix_multiply,
};

/* ==================== END: Proxy of all number methods ==================== */

static Py_hash_t defer_expr_hash(PyObject *self)
{
    OBSERVE(self) ON_FAILURE(return -1);
    return PyObject_Hash(self);
}

static PyObject *defer_expr_call(PyObject *self, PyObject *args,
                                 PyObject *kwargs)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    return PyObject_Call(self, args, kwargs);
}

static PyObject *defer_expr_str(PyObject *self)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    return PyObject_Str(self);
}

static PyObject *defer_expr_getattro(PyObject *self, PyObject *attr)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    return PyObject_GetAttr(self, attr);
}

static int defer_expr_setattro(PyObject *self, PyObject *attr, PyObject *value)
{
    OBSERVE(self) ON_FAILURE(return -1);
    return PyObject_SetAttr(self, attr, value);
}

// Special: not a proxied method
// Traverse enclosed objects for garbage collection
static int defer_expr_traverse(PyDeferExprObject *self, visitproc visit,
                               void *arg)
{
    Py_VISIT(self->callable);
    Py_VISIT(self->result);
    return 0;
}

// Special: not a proxied method
// Clear our reference to the callable object
// User might gain access to it using tools from the inspect module
static int defer_expr_clear(PyDeferExprObject *self)
{
    Py_CLEAR(self->callable);
    Py_CLEAR(self->result);
    return 0;
}

static PyObject *defer_expr_richcompare(PyObject *self, PyObject *other, int op)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    return PyObject_RichCompare(self, other, op);
}

static PyObject *defer_expr_iter(PyObject *self)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    return PyObject_GetIter(self);
}

static PyObject *defer_expr_iternext(PyObject *self)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    LOOKUP(self, iternext, iternextfunc)
    ON_FAILURE({
        PyErr_SetString(PyExc_TypeError, "object is not iterable");
        return NULL;
    });
    return iternext(self);
}

static PyObject *defer_expr_descr_get(PyObject *self, PyObject *obj,
                                      PyObject *type)
{
    OBSERVE(self) ON_FAILURE(return NULL);
    LOOKUP(self, descr_get, descrgetfunc)
    ON_FAILURE({
        PyErr_SetString(PyExc_TypeError, "object has no descriptor getter");
        return NULL;
    });
    return descr_get(self, obj, type);
}

static int defer_expr_descr_set(PyObject *self, PyObject *obj, PyObject *value)
{
    OBSERVE(self) ON_FAILURE(return -1);
    LOOKUP(self, descr_set, descrsetfunc)
    ON_FAILURE({
        PyErr_SetString(PyExc_TypeError, "object has no descriptor setter");
        return -1;
    });
    return descr_set(self, obj, value);
}

PyObject *defer_expr_new(PyTypeObject *type_unused, PyObject *args,
                         PyObject *kwargs)
{
    // Use the first positional argument as the lambda
    PyObject *callable = NULL;
    if (!PyArg_UnpackTuple(args, "callable", 1, 1, &callable))
        return NULL;

    // Check optional keyword-only argument "collapsible"
    PyObject *collapsible = NULL;
    if (kwargs != NULL)
        collapsible = PyDict_GetItemString(kwargs, "collapsible");

    if (collapsible == NULL || !PyObject_IsTrue(collapsible))
        return PyDeferExpr_New(callable, 0);
    else
        return PyDeferExpr_New(callable, 1);
}

static void defer_expr_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    defer_expr_clear((PyDeferExprObject *)self);
    PyObject_GC_Del(self);
}

PyTypeObject PyDeferExpr_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) //
    "DeferExpr",
    sizeof(PyDeferExprObject),
    (Py_ssize_t)0,
    (destructor)defer_expr_dealloc,
    (Py_ssize_t)0,
    (getattrfunc)defer_expr_getattr,
    (setattrfunc)defer_expr_setattr,
    (PyAsyncMethods *)0,
    (reprfunc)defer_expr_repr,
    (PyNumberMethods *)&defer_expr_as_number,
    (PySequenceMethods *)0,
    (PyMappingMethods *)0,
    (hashfunc)defer_expr_hash,
    (ternaryfunc)defer_expr_call,
    (reprfunc)defer_expr_str,
    (getattrofunc)defer_expr_getattro,
    (setattrofunc)defer_expr_setattro,
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    defer_expr_doc,
    (traverseproc)defer_expr_traverse,
    (inquiry)defer_expr_clear,
    (richcmpfunc)defer_expr_richcompare,
    (Py_ssize_t)0,
    (getiterfunc)defer_expr_iter,
    (iternextfunc)defer_expr_iternext,
    (PyMethodDef *)0,
    (PyMemberDef *)0,
    (PyGetSetDef *)0,
    (PyTypeObject *)0,
    (PyObject *)0,
    (descrgetfunc)defer_expr_descr_get,
    (descrsetfunc)defer_expr_descr_set,
    (Py_ssize_t)0,
    (initproc)0,
    (allocfunc)0,
    (newfunc)defer_expr_new,
    (freefunc)0,
};

// Exposed version of the DeferExpr through `DeferExpr.expose()` method

static int defer_expr_exposed_traverse(PyDeferExprExposedObject *self,
                                       visitproc visit, void *arg)
{
    Py_VISIT(self->ref);
    return 0;
}

static int defer_expr_exposed_clear(PyDeferExprExposedObject *self)
{
    Py_CLEAR(self->ref);
    return 0;
}

static void defer_expr_exposed_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    defer_expr_exposed_clear((PyDeferExprExposedObject *)self);
    PyObject_GC_Del(self);
}

/* ================== BEGIN: builtin methods for DeferExpr ================== */

PyObject *builtin_expose(PyObject *unused, PyObject *obj)
{
    ENSURE_TYPE(obj, &PyDeferExpr_Type, return Py_None);

    PyDeferExprExposedObject *exposed =
        PyObject_GC_New(PyDeferExprExposedObject, &PyDeferExprExposed_Type);

    if (exposed == NULL)
        return NULL;

    exposed->ref = _Py_CAST(PyDeferExprObject *, Py_NewRef(obj));

    PyObject_GC_Track(exposed);
    return (PyObject *)exposed;
}

// Observe immediately (if not already) and prevent future re-evaluation.
PyObject *builtin_freeze(PyObject *unused, PyObject *obj)
{
    ENSURE_TYPE(obj, &PyDeferExpr_Type, return obj);
    _Py_CAST(PyDeferExprObject *, obj)->collapsible = 1;
    return PyDeferExpr_Observe(obj);
}

// Observe immediately, use cached result if appropriate.
PyObject *builtin_snapshot(PyObject *unused, PyObject *obj)
{
    return PyDeferExpr_Observe(obj);
}

/* =================== END: builtin methods for DeferExpr =================== */

#define GETTER(NAME)                                                           \
    static PyObject *defer_expr_exposed_get_##NAME(                            \
        PyDeferExprExposedObject *self, void *unused)

#define SETTER(NAME)                                                           \
    static int defer_expr_exposed_set_##NAME(PyDeferExprExposedObject *self,   \
                                             PyObject *value, void *unused)

GETTER(collapsible)
{
    if (self->ref->collapsible)
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}

SETTER(collapsible)
{
    if (Py_IsTrue(value))
        self->ref->collapsible = 1;
    else
        self->ref->collapsible = 0;
    return 0;
}

GETTER(callable)
{
    PyObject *res = Py_XNewRef(self->ref->callable);
    if (res == NULL)
    {
        PyErr_SetString(PyExc_AttributeError,
                        "attribute <callable> does not exist");
        return NULL;
    }
    return res;
}

SETTER(callable)
{
    if (value != NULL && !PyCallable_Check(value))
    {
        PyErr_SetString(PyExc_TypeError, "value must be callable");
        return -1;
    }
    Py_XDECREF(self->ref->callable);
    self->ref->callable = Py_XNewRef(value);
    return 0;
}

GETTER(result)
{
    PyObject *res = self->ref->result;
    if (res == NULL)
    {
        PyErr_SetString(PyExc_AttributeError,
                        "attribute <result> does not exist");
        return NULL;
    }
    return Py_NewRef(res);
}

SETTER(result)
{
    // A frozen DeferExpr's result may be manually tweaked
    // as long as user knows what they are doing
    Py_XDECREF(self->ref->result);
    self->ref->result = Py_XNewRef(value);
    return 0;
}

static PyGetSetDef DeferExpr_getsetlist[] = {
    {"collapsible", (getter)defer_expr_exposed_get_collapsible,
     (setter)defer_expr_exposed_set_collapsible},
    {"callable", (getter)defer_expr_exposed_get_callable,
     (setter)defer_expr_exposed_set_callable},
    {"result", (getter)defer_expr_exposed_get_result,
     (setter)defer_expr_exposed_set_result},
    {NULL, NULL, NULL, NULL, NULL},
};

PyTypeObject PyDeferExprExposed_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) //
    "DeferExprExposed",
    sizeof(PyDeferExprExposedObject),
    (Py_ssize_t)0,
    (destructor)defer_expr_exposed_dealloc,
    (Py_ssize_t)0,
    (getattrfunc)0,
    (setattrfunc)0,
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
    defer_expr_doc,
    (traverseproc)defer_expr_exposed_traverse,
    (inquiry)defer_expr_exposed_clear,
    (richcmpfunc)0,
    (Py_ssize_t)0,
    (getiterfunc)0,
    (iternextfunc)0,
    (PyMethodDef *)0,
    (PyMemberDef *)0,
    (PyGetSetDef *)DeferExpr_getsetlist,
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
