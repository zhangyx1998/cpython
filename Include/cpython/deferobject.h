/* Defer object interface */
#ifndef Py_DEFEROBJECT_H
#define Py_DEFEROBJECT_H

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{

    PyObject_HEAD;

    // If "mutable" is set to False, first observation result will be cached
    // and reused on subsequent observations.
    // Otherwise, each observation will re-evaluate the callable.
    unsigned char mutable; // (bool, default: True)

    // An callable can either be
    // 1. a zero-argument lambda function (created by PyAST_DeferStmt)
    // 2. a vanilla function (crated in python using Defer(fn, *args, **kwargs))
    // 3. a callable object created in similar way as (2)
    PyObject *callable;

    // TODO: when only positional arguments are present, we can use vectorcall

    // Optional arguments and keyword arguments objects for the callable
    // If no arguments are supplied, they will be set to NULL.
    // NOTE: They must either be NULL or a valid pointer /together/.
    PyObject *args;
    PyObject *kwargs;

    // The result of the first observation
    // this is only valid if mutable is set to False
    PyObject *result;

} PyDeferObject;

// This is the stealth proxy - the actual DeferExpr object
// It will never reveal itself in python (all observations are proxied)
PyAPI_DATA(PyTypeObject) PyDefer_Type;

// A DeferExposed object provides a set of interface methods to interact with a
// stealth defer object.
// Attributes "callable", "mutable" and "result" (when available) are exposed.
// ---
// For defer objects constructed with arguments, "args" and "kwargs" are also
// exposed.
// ---
// Note: it holds a reference to the stealth object, not a "snapshot".
// This means the attributes are "living" - they may change themselves upon
// observation or be modified from another reference to the same defer object.
// This also means that all changes will be applied to the defer object.
typedef struct
{
    PyObject_HEAD;
    PyDeferObject *ref;
} PyDeferExposedObject;

PyAPI_DATA(PyTypeObject) PyDeferExposed_Type;

// A factory is created when you write `defer(..., *args, **kwargs)` in python
// It is primarily intended to work as an decorator which can be used to create
// a defer object from a function definition.

typedef struct
{
    PyObject_HEAD;
    unsigned char mutable;
    PyObject *args;
    PyObject *kwargs;
} PyDeferFactoryObject;

PyAPI_DATA(PyTypeObject) PyDeferFactory_Type;

// Exposed API functions

PyAPI_FUNC(PyObject *) PyDefer_New(PyObject *callable, unsigned char mutable);

PyObject *_PyDefer_Observe_Impl(PyDeferObject *);

static inline PyAPI_FUNC(PyObject *) PyDefer_Observe(PyObject *obj)
{
    // Inline the fast path - most objects are not DeferExpr
    if (!Py_IS_TYPE(obj, &PyDefer_Type))
        return obj;
    // Otherwise, call the actual observe implementation
    else
        return _PyDefer_Observe_Impl(_Py_CAST(PyDeferObject *, obj));
}

#ifdef __cplusplus
}
#endif
#endif /* !Py_DEFEROBJECT_H */
