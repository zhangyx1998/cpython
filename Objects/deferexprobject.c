/* DeferExpr core implementation */

#include "Python.h" // IWYU pragma: keep
#include "pycore_object.h"

PyDoc_STRVAR(defer_expr_doc, "Defer(contents)\n"
                             "--\n"
                             "\n"
                             "Create a new DeferExpr.\n"
                             "\n"
                             "  contents\n"
                             "    TODO: add doc.");

PyObject *PyDeferExpr_New(PyObject *lambda) {
  if (!PyCallable_Check(lambda)) {
    PyErr_SetString(PyExc_TypeError, "Failed to construct DeferExpr");
    return NULL;
  }
  PyDeferExprObject *op;
  op = PyObject_GC_New(PyDeferExprObject, &PyDeferExpr_Type);
  if (op != NULL) {
    op->lambda = _Py_CAST(PyFunctionObject *, Py_NewRef(lambda));
    _PyObject_GC_TRACK(op);
  }
  return (PyObject *)op;
}

PyObject *defer_expr_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  // Type must be DeferExpr_Type
  if (type != &PyDeferExpr_Type) {
    PyErr_BadInternalCall();
    return NULL;
  }
  // Use only the first positional argument as the lambda
  PyObject *lambda = NULL;
  if (!PyArg_UnpackTuple(args, "DeferExpr", 1, 1, &lambda)) {
    return NULL;
  }
  return PyDeferExpr_New(lambda);
}

static void defer_expr_dealloc(PyObject *self) {
  if (!Py_IS_TYPE(self, &PyDeferExpr_Type)) {
    PyErr_BadInternalCall();
    return;
  }
  _PyObject_GC_UNTRACK(self);
  PyDeferExprObject *defer = (PyDeferExprObject *)self;
  Py_XDECREF(defer->lambda);
  PyObject_GC_Del(self);
}

// Core idea of the DeferExpr - observation triggers evaluation
#define OBSERVE(OBJ)                                                           \
  {                                                                            \
    while (Py_IS_TYPE(OBJ, &PyDeferExpr_Type)) {                               \
      PyFunctionObject *lambda = _Py_CAST(PyDeferExprObject *, OBJ)->lambda;   \
      OBJ = PyObject_CallNoArgs(_Py_CAST(PyObject *, lambda));                 \
    }                                                                          \
  }                                                                            \
  if (OBJ == NULL) // OBSERVE(...) { <cond-failure> }

// Mirror a function call on DeferExpr to the observed value.
// This should be used as a fallback when Py_XXX API is not available.
#define LOOKUP(self, hook, hook_type)                                          \
  hook_type hook = NULL;                                                       \
  hook = Py_TYPE(self)->tp_##hook;                                             \
  if (hook == NULL) // LOOKUP(...) { <cond-failure> }

#define ON_FAILURE(expr) expr // Helps to make the code more expressive

static PyObject *defer_expr_getattr(PyObject *self, char *attr) {
  OBSERVE(self) ON_FAILURE(return NULL);
  return PyObject_GetAttrString(self, attr);
}

static int defer_expr_setattr(PyObject *self, char *attr, PyObject *value) {
  OBSERVE(self) ON_FAILURE(return -1);
  return PyObject_SetAttrString(self, attr, value);
}

static PyObject *defer_expr_repr(PyObject *self) {
  OBSERVE(self) ON_FAILURE(return NULL);
  return PyObject_Repr(self);
}

/* ==================== Proxy of all the number methods ==================== */

#define DEFER_EXPR_NB_COMMON(FUNC_TYPE, OPERAND, PANIC)                        \
  if (!PyNumber_Check(op0)) {                                                  \
    PyErr_SetString(PyExc_TypeError, "object is not numeric");                 \
    PANIC;                                                                     \
  }                                                                            \
  FUNC_TYPE operand = Py_TYPE(op0)->tp_as_number->nb_##OPERAND;                \
  if (operand == NULL) {                                                       \
    PyErr_SetString(PyExc_TypeError, #OPERAND " not supported");               \
    PANIC;                                                                     \
  }

#define DEFER_EXPR_UNARY_FUNC(OPERAND)                                         \
  static PyObject *defer_expr_nb_##OPERAND(PyObject *op0) {                    \
    OBSERVE(op0) ON_FAILURE(return NULL);                                      \
    DEFER_EXPR_NB_COMMON(unaryfunc, OPERAND, return NULL);                     \
    return operand(op0);                                                       \
  }

#define DEFER_EXPR_BINARY_FUNC(OPERAND)                                        \
  static PyObject *defer_expr_nb_##OPERAND(PyObject *op0, PyObject *op1) {     \
    OBSERVE(op0) ON_FAILURE(return NULL);                                      \
    OBSERVE(op1) ON_FAILURE(return NULL);                                      \
    DEFER_EXPR_NB_COMMON(binaryfunc, OPERAND, return NULL);                    \
    return operand(op0, op1);                                                  \
  }

#define DEFER_EXPR_TERNARY_FUNC(OPERAND)                                       \
  static PyObject *defer_expr_nb_##OPERAND(PyObject *op0, PyObject *op1,       \
                                           PyObject *op2) {                    \
    OBSERVE(op0) ON_FAILURE(return NULL);                                      \
    OBSERVE(op1) ON_FAILURE(return NULL);                                      \
    OBSERVE(op2) ON_FAILURE(return NULL);                                      \
    DEFER_EXPR_NB_COMMON(ternaryfunc, OPERAND, return NULL);                   \
    return operand(op0, op1, op2);                                             \
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
static int defer_expr_nb_bool(PyObject *self) {
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

/* ================== END Proxy of all the number methods ================== */

static Py_hash_t defer_expr_hash(PyObject *self) {
  OBSERVE(self) ON_FAILURE(return -1);
  return PyObject_Hash(self);
}

static PyObject *defer_expr_call(PyObject *self, PyObject *args,
                                 PyObject *kwargs) {
  OBSERVE(self) ON_FAILURE(return NULL);
  return PyObject_Call(self, args, kwargs);
}

static PyObject *defer_expr_str(PyObject *self) {
  OBSERVE(self) ON_FAILURE(return NULL);
  return PyObject_Str(self);
}

static PyObject *defer_expr_getattro(PyObject *self, PyObject *attr) {
  OBSERVE(self) ON_FAILURE(return NULL);
  return PyObject_GetAttr(self, attr);
}

static int defer_expr_setattro(PyObject *self, PyObject *attr,
                               PyObject *value) {
  OBSERVE(self) ON_FAILURE(return -1);
  return PyObject_SetAttr(self, attr, value);
}

static int defer_expr_traverse(PyObject *self, visitproc visit, void *arg) {
  OBSERVE(self) ON_FAILURE(return -1);
  Py_VISIT(self);
  return 0;
}

// Special
static int defer_expr_clear(PyObject *self) {
  // Clear our reference to the lambda function
  // User might gain access to it using tools from the inspect module
  if (!Py_IS_TYPE(self, &PyDeferExpr_Type)) {
    PyErr_BadInternalCall();
    return -1;
  }
  PyDeferExprObject *defer = (PyDeferExprObject *)self;
  Py_CLEAR(defer->lambda);
  return 0;
}

static PyObject *defer_expr_richcompare(PyObject *self, PyObject *other,
                                        int op) {
  OBSERVE(self) ON_FAILURE(return NULL);
  return PyObject_RichCompare(self, other, op);
}

static PyObject *defer_expr_iter(PyObject *self) {
  OBSERVE(self) ON_FAILURE(return NULL);
  return PyObject_GetIter(self);
}

static PyObject *defer_expr_iternext(PyObject *self) {
  OBSERVE(self) ON_FAILURE(return NULL);
  LOOKUP(self, iternext, iternextfunc)
  ON_FAILURE({
    PyErr_SetString(PyExc_TypeError, "object is not iterable");
    return NULL;
  });
  return iternext(self);
}

static PyObject *defer_expr_descr_get(PyObject *self, PyObject *obj,
                                      PyObject *type) {
  OBSERVE(self) ON_FAILURE(return NULL);
  LOOKUP(self, descr_get, descrgetfunc)
  ON_FAILURE({
    PyErr_SetString(PyExc_TypeError, "object is not a descriptor");
    return NULL;
  });
  return descr_get(self, obj, type);
}

static int defer_expr_descr_set(PyObject *self, PyObject *obj,
                                PyObject *value) {
  OBSERVE(self) ON_FAILURE(return -1);
  LOOKUP(self, descr_set, descrsetfunc)
  ON_FAILURE({
    PyErr_SetString(PyExc_TypeError, "object is not a descriptor");
    return -1;
  });
  return descr_set(self, obj, value);
}

PyTypeObject PyDeferExpr_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "DeferExpr",
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
