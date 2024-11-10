/* Defer object interface */

#ifndef Py_LIMITED_API
#ifndef Py_DEFEREXPROBJECT_H
#define Py_DEFEREXPROBJECT_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  PyObject_HEAD
      /* Content of the defer or NULL when empty */
      PyFunctionObject *lambda;
} PyDeferExprObject;

PyAPI_DATA(PyTypeObject) PyDeferExpr_Type;

#define PyDeferExpr_Check(op) Py_IS_TYPE((op), &PyDeferExpr_Type)

PyAPI_FUNC(PyObject *) PyDeferExpr_New(PyObject *);

#ifdef __cplusplus
}
#endif
#endif /* !Py_TUPLEOBJECT_H */
#endif /* Py_LIMITED_API */
