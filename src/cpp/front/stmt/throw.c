/*
 * stmt/throw.c — `throw` lowering and the statement dispatch.
 */
#include "core/compat.h"

#include "core/cc.h"
#include "core/types.h"
#include "front/lexer.h"
#include "front/ast.h"
#include "front/parser_priv.h"
#include "back/gen.h"
#include "arch/riscv64.h"
#include "cpp/cpp_front.h"
#include "cpp/cpp.h"

void cpp_parse_throw(lexer_t *lx) {
    lex_next(lx);                       /* throw */
    int throw_sym = cpp_eh_sym("__onyx_eh_throw", &ty_void,
                           type_make_ptr(&ty_void), &ty_int);
    if (lx->cur.kind == T_SEMI) {       /* rethrow */
        int re_sym = cpp_eh_sym("__onyx_eh_rethrow", &ty_void, NULL, NULL);
        cpp_emit_call(re_sym);
        lex_next(lx);
        return;
    }
    val_t v = parse_expr(lx);
    parse_expect(T_SEMI, "expected ';' after throw");
    int tag = cpp_tag_for_type(v.type);
    if (v.type && v.type->kind == TY_STRUCT) {
        uint64_t sz = type_sizeof(v.type);
        int64_t tmp = cpp_frameslot(sz, 8);
        materialize(&v, RV_T0);         /* object address */
        for (uint64_t k = 0; k < sz; k++) {
            rv_lbu(RV_T1, RV_T0, (int)k);
            rv_sb(RV_T1, RV_FP, (int)(tmp + (int64_t)k));
        }
        rv_addi_imm(RV_A0, RV_FP, (int)tmp);
    } else {
        load_value(&v, RV_T0);
        rv_mv(RV_A0, RV_T0);
    }
    rv_addi_imm(RV_A1, RV_ZERO, tag);
    cpp_emit_call(throw_sym);
}

/* Bind the caught object to a local inside the clause body. */
bool cpp_parse_exception_stmt(lexer_t *lx) {
    if (lx->cur.kind == T_KW_THROW) {
        cpp_parse_throw(lx);
        return true;
    }
    if (lx->cur.kind == T_KW_TRY) {
        cpp_parse_try(lx);
        return true;
    }
    return false;
}

