/*
 * cpp.h — API between the C front end (parse.c / gen.c) and the C++
 * front-end sources under src/cpp/front/.
 *
 * Contract for EVERY hook below:
 *   - Called only when g_cpp_mode is set for the current input file
 *     (declared in front/parser_priv.h, defined in parse.c, set by
 *     main.c from the file extension).
 *   - Returning false / doing nothing MUST NOT consume tokens: the C
 *     path then proceeds exactly as before. This is what keeps C
 *     behavior identical — the Phase A stubs always take this route.
 *   - Returning true means the hook consumed the tokens it owns and
 *     produced the documented effect.
 *
 * Hidden-this / method-marker protocol (spec item 5; Phase B fills it):
 *   1. Field lookup fails in parse_postfix -> cpp_try_member. On a
 *      method-group hit it returns a MARKER val_t: kind = VAL_SYM
 *      (dummy sym_idx), cpp_group = gid (group ids start at 1; 0 means
 *      "not a method" — it is the memset default wherever val_t is
 *      created, so it is the safe "none" value; see back/val.h), and
 *      the OBJECT ADDRESS is saved into a cpp-module static ("pending
 *      this") by spilling it first —
 *          spill_lval(&obj); spill_ireg(&obj);
 *      — so later argument evaluation cannot clobber it.
 *   2. The postfix loop continues; on the following '(' it calls
 *      cpp_try_call BEFORE normal call handling. On a marker
 *      (cpp_group != 0) the hook must:
 *        a) pre-scan the argument list with cpp_count_call_args(lx)
 *           (balanced-paren token scan via lex_peek — does NOT consume);
 *        b) resolve group + arity -> concrete method sym idx + ftype
 *           whose params[0] is `Class *this`;
 *        c) rebuild *v as the direct callee and inject pending this as
 *           hidden argument 0 (injection seam decided in Phase B:
 *           hidden_first_arg param on the exported gen_call vs a module
 *           static consumed at gen_call entry);
 *        d) return true -> gen.c `continue`s, skipping the C path.
 *   3. Non-marker values (function pointers, plain calls) -> return
 *      false so the C call path runs unchanged.
 *   4. cpp_try_primary additionally synthesizes `this->member` values
 *      for bare member names inside method bodies (Phase B).
 *
 * POSTFIX GAP (Phase A note for later phases): the parse_primary hook
 * site returns the hook's value WITHOUT running parse_postfix (that
 * helper stays static in gen.c). A hook value that can be followed by
 * `.`/`->`/`[]`/`()` (e.g. `this`, functional casts) must either
 * consume that chain itself or the call site must be promoted to
 * `return parse_postfix(lx, v);` — a one-line, g_cpp_mode-gated hook
 * edit permitted for a later phase.
 */
#ifndef CC_CPP_H
#define CC_CPP_H

#include "core/cc.h"
#include "core/types.h"
#include "front/lexer.h"
#include "back/val.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- gen.c seams ------------------------------------------------------ */

/* parse_primary: called after `val_t v` is memset-initialized, before
 * the token switch. True -> gen.c returns *out as the primary value
 * (see POSTFIX GAP above). Phase B: bare-member fallback; Phase C:
 * nullptr/this/new/delete/casts/functional-cast. */
bool cpp_try_primary(lexer_t *lx, val_t *out);

/* parse_stmt_body: first statement of the function, before the label
 * check. True -> statement (including its ';') fully consumed.
 * Phase E: try/catch/throw. */
bool cpp_try_stmt(lexer_t *lx);

/* parse_postfix: field lookup failed (fi < 0), before the "no member"
 * error. `st` is the struct/union (or pointee for `->`), mname the
 * member name, is_arrow selects `->`. True -> *v replaced (typically
 * the method marker) and the postfix loop continues; false -> the C
 * error fires unchanged. Phase B. */
bool cpp_try_member(lexer_t *lx, val_t *v, int is_arrow,
                    const char *mname, type_t *st);

/* parse_postfix: cur is '(' before normal call handling. True -> call
 * consumed, *v is the result, gen.c continues the postfix loop;
 * false -> C path. Only method markers / cpp-only calls are handled
 * here; plain C calls must return false. Phase B. */
bool cpp_try_call(lexer_t *lx, val_t *v);

/* parse_compound: after the body loop, BEFORE symtab_pop_scope() —
 * current scope is still active (symtab_lookup_local works). Emit dtors
 * of objects leaving this scope. Phase C. */
void cpp_scope_exit(lexer_t *lx);

/* T_KW_RETURN: after `;` is consumed, BEFORE the epilogue emission.
 * At this point the return value is already placed in a0/fa0 (or the
 * aggregate return slot) — the implementation must preserve a0/fa0
 * across any dtor calls it emits. Emits dtors of ALL registered locals
 * in reverse creation order. Phase C. */
void cpp_return_unwind(lexer_t *lx);

/* gen_func_body: right after symtab_enter_function(). Resets the
 * per-function ctor/dtor registry and cpp `this` state. Phase C. */
void cpp_func_begin(void);

/* parse_stmt_body local-decl: called once per declarator AFTER symbol
 * install and init emission (typedef/extern/static declarators bypass
 * it; a declarator handled by cpp_local_ctor_decl also bypasses it).
 * sym_idx is the symtab_install_local index — it may be 0/uninstalled
 * when stack allocation was deferred (incomplete-array init), so the
 * implementation must check `t` before using sym_idx.
 * NOT called for `Foo x(1,2);` — that path goes through
 * cpp_local_ctor_decl instead. Phase C: dtor registration.
 * Final Phase A signature (spec item 4, unchanged):
 *     void cpp_local_decl(lexer_t *lx, type_t *t, int sym_idx, bool had_init); */
void cpp_local_decl(lexer_t *lx, type_t *t, int sym_idx, bool had_init);

/* parse.c seam, AFTER each top-level declaration: compile C++ template
 * bodies queued while that declaration was parsed (top-level only, so a
 * body never splices into the active function's instruction stream). */
void cpp_drain_pending_templates(void);

/* parse.c seam (top of parse_top_level): true -> the whole top-level
 * construct (class/template/namespace/out-of-line `X::y` decl) consumed
 * by the cpp module. Phase B: class; Phase D: namespace; F: template. */
bool cpp_top_level(lexer_t *lx);

/* parse.c seam, AFTER parse_decl_spec() succeeded and BEFORE the C
 * declarator loop. Handles out-of-line member definitions whose return
 * type was already parsed (`int Foo::bar() {...}`) and constructor /
 * destructor definitions (`Foo::Foo(...)`, `Foo::~Foo()`) where the
 * "type" parsed by decl-spec was actually the class name.
 * True -> the whole declaration was consumed. */
bool cpp_try_out_of_line(lexer_t *lx, type_t *base);

/* parse_postfix, member FOUND in st->fields (fi >= 0), before the
 * access is used. Emits the private/protected diagnostic when the
 * current context may not touch the member; otherwise a no-op.
 * Phase B. */
void cpp_check_field_access(lexer_t *lx, const char *mname, type_t *st);

/* Class type for a bare C++ name — only meaningful in .cpp files (class
 * tags are intentionally NOT global typedefs). NULL when not a class. */
type_t *cpp_type_by_name(const char *name);

/* gen_func_body, after parameters are installed in the frame: a class
 * constructor stores its vtable address into `this->__vptr` (Phase D). */
void cpp_method_prologue(lexer_t *lx);

/* ---- local-decl ctor seam (spec item 4) ------------------------------ *
 * `Foo x(1,2);` — parse_declarator would swallow "(...)" as function
 * params and error on the argument tokens. Seam (gen.c local-decl path,
 * inside the declarator loop, BEFORE parse_declarator runs): cur is the
 * declarator name (T_IDENT), next has been lex_peek'd as '('.
 *     cpp_is_ctor_call_site: may this base type start a ctor call?
 *         (Phase C: true only for class tags with a ctor.)
 *     cpp_local_ctor_decl: parse `name ( args )` and emit the ctor
 *         call + install the local; returns true if it consumed the
 *         declarator (caller then expects ';' and ends the statement).
 * Both false in Phase A -> the normal C declarator path runs. */
bool cpp_is_ctor_call_site(lexer_t *lx, type_t *t);
bool cpp_local_ctor_decl(lexer_t *lx, type_t *t);

/* ---- helpers ---------------------------------------------------------- */

/* cpp module pre-scan for cpp_try_call: count top-level ',' inside the
 * upcoming balanced paren group WITHOUT consuming tokens (lex_peek
 * only). Returns -1 when cur is not '(' (Phase A stub). Phase B. */
int cpp_count_call_args(lexer_t *lx);

#ifdef __cplusplus
}
#endif

#endif /* CC_CPP_H */
