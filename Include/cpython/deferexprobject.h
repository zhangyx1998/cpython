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

    // If "collapsible" is set to true, first observation result will be
    // cached and returned on subsequent observations
    unsigned char collapsible; // (bool)

    // An callable can either be
    // 1. a zero-argument lambda function (created by PyAST_DeferStmt)
    // 2. a vanilla python function that can be called without arguments
    // 3. a callable object that can be called without arguments
    PyObject *callable;

    // The result of the first observation
    // this is only valid if collapse is set to true
    PyObject *result;

} PyDeferExprObject;

typedef struct
{
    PyObject_HEAD;
    PyDeferExprObject *ref;
} PyDeferExprExposedObject;

// This is the stealth proxy - the actual DeferExpr object
// It will never reveal itself in python (all observations are proxied)
PyAPI_DATA(PyTypeObject) PyDeferExpr_Type;

// Name "DeferExpr" in Python points to this version of DeferExpr (Exposed)
// It provides a set of interface methods to interact with a stealth DeferExpr
// It is a plain object. No proxy is done for it.
// Attributes "callable", "collapse" and "result" (when available) are exposed.
// ---
// Notice that it holds a reference to the stealth object, not a snapshot.
// This means the attributes will change as the stealth object changes.
PyAPI_DATA(PyTypeObject) PyDeferExprExposed_Type;

PyAPI_FUNC(PyObject *)
    PyDeferExpr_New(PyObject *callable, unsigned char collapsible);
PyAPI_FUNC(PyObject *) PyDeferExpr_Observe(PyObject *obj);

// built-in functions and documentations
PyObject *builtin_expose(PyObject *, PyObject *);
PyObject *builtin_freeze(PyObject *, PyObject *);
PyObject *builtin_snapshot(PyObject *, PyObject *);

#define builtin_expose_doc                                                     \
    "expose()\n"                                                               \
    "--\n"                                                                     \
    "Expose the DeferExpr object for direct manipulation"

#define builtin_freeze_doc                                                     \
    "freeze()\n"                                                               \
    "--\n"                                                                     \
    "Freeze the DeferExpr object, preventing future re-evaluation"

#define builtin_snapshot_doc                                                   \
    "snapshot()\n"                                                             \
    "--\n"                                                                     \
    "Observe the DeferExpr object, using cached result if appropriate"

#ifdef __cplusplus
}
#endif
#endif /* !Py_DEFEREXPROBJECT_H */
