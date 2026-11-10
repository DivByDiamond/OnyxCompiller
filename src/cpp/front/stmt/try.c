/*
 * stmt/try.c — `try`/`catch` lowering onto the setjmp chain.
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
#include "arch/context.h"
#include "front/decl.h"

#define CPP_JMPBUF_SIZE 216

void cpp_bind_catch(int64_t objslot, type_t *ctype, const char *name) {
    if (!name[0]) return;
    bool is_ref = (ctype->kind == TY_PTR && ctype->is_ref);
    type_t *lt = is_ref ? ctype : ctype;
    uint64_t sz = type_sizeof(lt);
    if (sz == 0) sz = 8;
    int scope = g_cur_scope > 0 ? g_cur_scope : 1;
    int lidx = symtab_install_local(name, SYM_LOCAL_VAR, lt, scope);
    g_func.cur_offset += (int)sz;
    g_func.cur_offset = (g_func.cur_offset + 7) & ~7;
    int64_t off = -(int64_t)g_func.cur_offset;
    if (g_func.cur_offset > g_func.frame_size)
        g_func.frame_size = g_func.cur_offset;
    g_locals[lidx].offset = off;
    rv_ld(RV_T0, RV_FP, (int)objslot);
    if (is_ref) {
        rv_sd(RV_T0, RV_FP, (int)off);  /* reference: store the pointer */
    } else if (lt->kind == TY_STRUCT) {
        for (uint64_t k = 0; k < sz; k++) {
            rv_lbu(RV_T1, RV_T0, (int)k);
            rv_sb(RV_T1, RV_FP, (int)(off + (int64_t)k));
        }
    } else {
        rv_sd(RV_T0, RV_FP, (int)off);
    }
}

void cpp_parse_try(lexer_t *lx) {
    static int serial;
    char lbl_catch[32], lbl_done[32], lbl_next[32];
    snprintf(lbl_catch, sizeof(lbl_catch), "cpp_catch_%d", serial);
    snprintf(lbl_done, sizeof(lbl_done), "cpp_done_%d", serial);
    int saved = g_eh_depth;
    serial++;
    lex_next(lx);                       /* try */

    int64_t env = cpp_frameslot(CPP_JMPBUF_SIZE, 16);
    int64_t frame = cpp_frameslot(16, 16);
    int push_sym = cpp_eh_sym("__onyx_eh_push", &ty_void,
                          type_make_ptr(&ty_void), type_make_ptr(&ty_void));
    int pop_sym = cpp_eh_sym("__onyx_eh_pop", &ty_void, NULL, NULL);

    cpp_emit_call_slots(push_sym, frame, env);
    rv_addi_imm(RV_T2, RV_FP, (int)env);
    rv_setjmp(RV_T2);                   /* result in T0 */
    uint32_t br_catch = gen_emit_bne(RV_T0, RV_ZERO);
    g_eh_depth = saved + 1;

    parse_stmt(lx);                     /* try body */

    g_eh_depth = saved;
    cpp_emit_call(pop_sym);
    gen_jump_to(lbl_done);

    gen_label_here(lbl_catch);
    gen_patch_here(br_catch);
    cpp_emit_call(pop_sym);
    g_eh_depth = saved;

    int obj_sym = cpp_eh_sym("__onyx_eh_obj", type_make_ptr(&ty_void), NULL, NULL);
    int tag_sym = cpp_eh_sym("__onyx_eh_tag", &ty_int, NULL, NULL);
    int64_t objslot = cpp_frameslot(8, 8);
    int64_t tagslot = cpp_frameslot(8, 8);
    cpp_emit_call(obj_sym);
    rv_sd(RV_A0, RV_FP, (int)objslot);
    cpp_emit_call(tag_sym);
    rv_sd(RV_A0, RV_FP, (int)tagslot);

    bool has_catch_all = false;
    while (lx->cur.kind == T_KW_CATCH) {
        snprintf(lbl_next, sizeof(lbl_next), "cpp_next_%d", serial++);
        lex_next(lx);                   /* catch */
        parse_expect(T_LPAREN, "expected '(' after catch");
        bool catch_all = false;
        type_t *ctype = NULL;
        char bname[CC_MAX_IDENT] = {0};
        if (lx->cur.kind == T_ELLIPSIS) {
            catch_all = true;
            has_catch_all = true;
            lex_next(lx);
        } else {
            type_t *base = NULL;
            bool s = false, e = false, t = false, i = false;
            char ftag[CC_MAX_IDENT] = {0};
            if (!parse_decl_spec(lx, &base, &s, &e, &t, &i, ftag)) {
                parse_error("expected catch type");
                break;
            }
            ctype = parse_declarator(lx, base, bname, sizeof(bname));
        }
        parse_expect(T_RPAREN, "expected ')' after catch clause");

        uint32_t br_skip = 0;
        if (!catch_all) {
            type_t *tagt = (ctype->kind == TY_PTR) ? ctype->base : ctype;
            rv_ld(RV_T0, RV_FP, (int)tagslot);
            rv_addi_imm(RV_T1, RV_ZERO, cpp_tag_for_type(tagt));
            br_skip = gen_emit_bne(RV_T0, RV_T1);
        }
        symtab_push_scope();
        if (!catch_all) cpp_bind_catch(objslot, ctype, bname);
        parse_stmt(lx);                 /* clause body */
        symtab_pop_scope();
        gen_jump_to(lbl_done);
        if (!catch_all) {
            gen_label_here(lbl_next);
            gen_patch_here(br_skip);
        }
    }

    if (!has_catch_all) {
        int re_sym = cpp_eh_sym("__onyx_eh_rethrow", &ty_void, NULL, NULL);
        cpp_emit_call(re_sym);
    }
    gen_label_here(lbl_done);
}

