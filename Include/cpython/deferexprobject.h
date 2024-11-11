/* Defer object interface */
#ifndef Py_DEFEREXPROBJECT_H
#define Py_DEFEREXPROBJECT_H

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{

    PyObject_HEAD;

    // If "collapsing" is set to true, first observation result will be
    // cached and returned on subsequent observations
    char collapsing; // (bool)

    // An callable can either be
    // 1. a zero-argument lambda function (created by PyAST_DeferStmt)
    // 2. a vanilla python function that can be called without arguments
    // 3. a callable object that can be called without arguments
    PyObject *callable;

    // The result of the first observation
    // this is only valid if collapsing is set to true
    PyObject *result;

} PyDeferExprObject;

PyAPI_DATA(PyTypeObject) PyDeferExpr_Type;

#define PyDeferExpr_Check(op) Py_IS_TYPE((op), &PyDeferExpr_Type)

PyAPI_FUNC(PyObject *) PyDeferExpr_New(PyObject *);
PyAPI_FUNC(PyObject *) PyDeferExpr_Observe(PyObject *);

#ifdef __cplusplus
}
#endif
#endif /* !Py_DEFEREXPROBJECT_H */
