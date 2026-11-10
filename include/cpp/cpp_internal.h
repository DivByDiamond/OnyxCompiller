/*
 * cpp_internal.h — helpers shared between the C++ front submodules
 * (expr/, decl/, stmt/). Pulled out of cpp_front.h to keep each header
 * within the project's file-size budget; included at the end of it.
 */
#ifndef CC_CPP_INTERNAL_H
#define CC_CPP_INTERNAL_H

#include "core/cc.h"
#include "core/types.h"
#include "front/lexer.h"
#include "back/val.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- internal helpers shared between the front-* submodules ---------- */

val_t cpp_local_val(int lidx);
void cpp_consume_parens(lexer_t *lx);
bool cpp_bare_member(lexer_t *lx, val_t *out);
bool cpp_parse_new(lexer_t *lx, val_t *out);
bool cpp_parse_delete(lexer_t *lx, val_t *out);
bool cpp_functional_cast(lexer_t *lx, type_t *ct, val_t *out);

void cpp_insert_this(type_t *ft, type_t *cls, bool is_static);
void cpp_compile_method(lexer_t *lx, int ci, const char *mangled, type_t *ft);
bool cpp_override_is_virtual(int ci, const char *name, int arity);
void cpp_parse_class_body(lexer_t *lx, bool is_struct);

int64_t cpp_frameslot(uint64_t bytes, uint64_t align);
int cpp_eh_sym(const char *name, type_t *ret, type_t *a0, type_t *a1);
void cpp_emit_call(int sym);
void cpp_emit_call_slots(int sym, int64_t a0slot, int64_t a1slot);
void cpp_bind_catch(int64_t objslot, type_t *ctype, const char *name);
void cpp_parse_throw(lexer_t *lx);
void cpp_parse_try(lexer_t *lx);
extern int g_eh_depth;
void cpp_queue_pending_template(char *buf, size_t len);


#ifdef __cplusplus
}
#endif

#endif /* CC_CPP_INTERNAL_H */
