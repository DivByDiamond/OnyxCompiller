/*
 * mangle.h — Itanium-lite symbol mangling (spec item 6, Phase B).
 *
 * Everything the class front end needs to name a symbol:
 *
 *   free fn  foo(int)      -> _Z3fooi
 *   method   Foo::bar(int) -> _ZN3Foo3barEi
 *   ctor     Foo(int)      -> _ZN3FooC1Ei
 *   dtor     Foo::~Foo()   -> _ZN3FooD1Ev
 *
 * "_Z" + 'N' + length-prefixed scope components + 'E' + one type code
 * per declared parameter (the hidden `this` is NOT part of the name —
 * arity everywhere in the module means "declared args, no this").
 * Constructors mangle as C1 (complete object ctor), destructors as D1.
 *
 * Output capacity is CC_MAX_IDENT (64) because sym_t.name is that
 * small: overflow reports cc_error and returns false — names are never
 * silently truncated (truncated names collide).
 */
#ifndef CC_CPP_MANGLE_H
#define CC_CPP_MANGLE_H

#include "core/cc.h"
#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mangle a member of class `cls` spelled `name`. Picks the scheme from
 * the name: `name == cls` -> constructor (C1), leading '~' -> destructor
 * (D1), anything else -> nested method name. `nargs` declared args are
 * taken from ftype->params[0..nargs-1] (call BEFORE inserting the
 * hidden this parameter). Returns false on overflow (cc_error emitted);
 * `out` is always NUL-terminated.
 */
bool cpp_mangle_member(char *out, size_t out_sz, const char *cls,
                       const char *name, type_t *ftype, int nargs);

/*
 * Mangle a free function `_Z<len>name<args>` (spec item 6). Phase B has
 * no caller — free functions keep raw C names so mixed C/C++ linking
 * stays untouched; shipped for Phase D+ namespaces.
 */
bool cpp_mangle_free(char *out, size_t out_sz, const char *name,
                     type_t *ftype, int nargs);

#ifdef __cplusplus
}
#endif

#endif /* CC_CPP_MANGLE_H */
