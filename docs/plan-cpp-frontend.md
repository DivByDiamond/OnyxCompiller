# C++ Frontend Implementation — Phase Spec (A–G)

> **STATUS (2026-11-10): all phases A–G are implemented and verified**
> (`make check`: 66 C syntax + 42 integration + 5 driver + 15 C++
> fixtures PASS; `make selfhost-test` PASS). Deferred items, kept honest
> in `docs/STATUS.md`: multiple/virtual inheritance, RTTI/dynamic_cast,
> class templates and deduction, virtual destructors, unwinding
> destructors, lambdas, constexpr, operator overloading, std::lib.
>
> **For implementers:** each phase is a self-contained task; run the full verification suite before moving on.
> Read this whole spec first, then implement ONLY your phase. Run
> verification before finishing. Rules: `info.md` (≤250 lines/file,
> ≤4 files/folder — subfolders count separately, no inline tests,
> `// SAFETY:` not applicable — this is C99, but every non-obvious
> invariant gets a comment), `docs/STATUS.md` limits stay honest.

**Goal:** working C++ subset for OnyxCC: classes, methods, ctors/dtors,
RAII, inheritance (single + multiple, non-virtual bases), virtual
functions + vtables, namespaces, references, new/delete, exceptions
(setjmp/longjmp lowering), simple function templates. All end-to-end:
`.cpp` in → `.onx` out → runs on `tools/onx-run`.

**Honesty boundary (document in STATUS.md at Phase G):** no RTTI
(dynamic_cast → clear error), no virtual inheritance, no multiple
inheritance *virtual* dispatch across bases (virtual methods work with
single inheritance and first-base-of-multiple), no exception-unwinding
dtors (longjmp skips local dtors — document), no class templates, no
std::lib, no function-try-blocks, no lambdas with captures yet
(defaultless lambda → may parse or error, choose and document).

## Architecture (read this before anything)

Single-pass tcc-style: expressions compile DURING parsing. Parser =
`src/c/parse.c` (top level) + static machinery in `src/c/gen.c`
(statements/expressions, 4337 lines — do NOT grow it; hooks only).
Types: `include/core/types.h` (`type_t`, `TY_STRUCT` = class storage,
tags via `tags_install/tags_lookup`). Symbols: flat `symtab_*` in
`include/front/ast.h`.

C++ code lives in C99 (NOT .cpp — must call gen internals freely):

```
include/back/val.h            — val_t + VAL_* MOVED out of gen.c (Phase A)
include/cpp/cpp.h             — API between gen.c/parse.c and cpp module
src/cpp/front/parse_class.c   — class bodies, methods, access, inheritance (B)
src/cpp/front/parse_expr.c    — new/delete/this/nullptr/casts/functional-cast (C)
src/cpp/front/parse_stmt.c    — try/catch/throw lowering (E)
src/cpp/front/parse_ns.c      — namespaces, using, out-of-line defs, templates (D/F)
src/cpp/tables/mangle.c       — Itanium-lite name mangling (B)
src/cpp/tables/method_table.c — class→methods, overloads by arity (B)
src/cpp/tables/template_table.c — function templates: name, span, params (F)
libonyxc/src/eh/eh.c          — exception runtime: push/pop/throw (E)
tests/cpp/*.cpp               — fixtures (every phase adds some)
scripts/cpp_runner.sh         — runs cpp fixtures via onyxcc + onx-run (B+)
```

Existing: `src/cpp/{parser.cpp,parser.h,ast.cpp}` stay (file driver +
C++ AST record structs from ast.cpp — Phase B may move those structs
into `include/cpp/cpp_ast.h` as C if needed; if so, delete ast.cpp
stubs and update Makefile).

### Key mechanics

1. **Mode flag:** `extern bool g_cpp_mode;` declared in
   `include/front/parser_priv.h`, defined in `src/c/parse.c`.
   `src/c/main.c` file loop (input → pp → `lex_init` line ~309 →
   `parse_translation_unit` line ~326): before processing file N set
   `g_cpp_mode = has_cpp_ext(input)` (`.cpp .cc .cxx .C` — case-sensitive
   `.C` optional; choose and document). libonyxc `.c` files → false.
   Mode must be correct per file (multi-file builds mix C and C++).

2. **val.h move (Phase A):** cut `val_t` + `VAL_*` enum (gen.c ~lines
   38–66) into `include/back/val.h` (includes `core/types.h`), gen.c
   `#include "back/val.h"`, gen.h includes val.h so cpp code sees it.

3. **gen.c exports (Phase A)** — remove `static`, declare in
   `include/back/gen.h`: `parse_expr`, `materialize`, `load_value`,
   `spill_lval`, `spill_ireg`, `gen_call`. Keep signatures.

4. **Hooks in gen.c (created Phase A as stubs, filled by later phases):**
   - `parse_primary`: at switch top — `if (g_cpp_mode && cpp_try_primary(lx, &v)) return v;`
     (also covers T_KW_NEW/THIS/NULLPTR/static_cast and functional-cast `Foo(1,2)`
     when ident lookup fails — cpp returns false → fall through to normal path).
   - `parse_stmt_body`: at switch top — `if (g_cpp_mode && cpp_try_stmt(lx)) return;`
     (T_KW_TRY/T_KW_THROW; statement incl. `;` consumed by cpp).
   - `parse_postfix` member-access (after field lookup fails, before
     "no member" error): `if (g_cpp_mode && cpp_try_member(lx, &v, is_arrow, mname, st)) ...;`
     cpp returns method marker val (see below) or reports error.
   - `parse_postfix` `T_LPAREN` branch: before normal handling —
     `if (g_cpp_mode && cpp_try_call(lx, &v)) continue;` (method/overload
     resolution + hidden-this injection; cpp uses exported gen_call).
   - `parse_compound`: after loop, before `symtab_pop_scope()` →
     `if (g_cpp_mode) cpp_scope_exit(lx);`
   - `T_KW_RETURN` case (~2986): after expr parse, before jump-to-epilogue
     → `if (g_cpp_mode) cpp_return_unwind(lx);`
   - `gen_func_body` start: `if (g_cpp_mode) cpp_func_begin();`
     (resets per-function ctor/dtor registry; also cpp_state for `this`).
   - Local decl branch in `parse_stmt_body` (is_type_start ~2645): after
     symbol install+init — `if (g_cpp_mode) cpp_local_decl(lx, type, sym_idx, ...);`
     (Phase C: ctor call + dtor registration; signature decided in A stub:
     `void cpp_local_decl(lexer_t *lx, type_t *t, int sym_idx, bool had_init);` —
     A ships it as no-op, C fills it. Also: local `Foo x(1,2);` — declarator
     sees '(' → function suffix; cpp mode must intercept BEFORE
     parse_declarator treats it as function type: A adds guard hook in the
     local-decl path: if g_cpp_mode && next-after-name is '(' && type is
     class tag → cpp_local_ctor_decl(...) — design the exact seam yourself,
     minimal gen.c delta, document it in cpp.h.)

5. **Method marker (val_t):** val.h adds to val_t: `int cpp_group;`
   (method-group id, -1 = not a method). `cpp_try_member` on hit sets
   `v.kind = VAL_SYM` (dummy sym_idx), `v.cpp_group = gid`,
   saves object address into cpp-module pending-this (cpp calls
   materialize/spill on it — object lvalue must survive: cpp does
   `spill_lval(&obj); spill_ireg(&obj);` stores to module static, then
   returns marker). `cpp_try_call` on marker: `cpp_count_call_args(lx)`
   (token pre-scan of balanced parens counting top-level commas — in
   cpp module, uses `lex_peek`), resolves group+arity → sym idx +
   method ftype (params[0] = `Class* this`), rebuilds `v` as direct
   callee, injects pending this as arg0. Injection detail: gen_call
   parses args and assigns a0.. — read gen_call (line ~910) and choose:
   either add `val_t *hidden_first_arg` param to gen_call (changes 3-4
   call sites — allowed, gen.c signature churn kept minimal) or module
   static consumed at gen_call entry. Pick one, document in cpp.h.

6. **Mangling (Itanium-lite, mangle.c):** `_Z` + nested scope:
   `Foo::bar(int)` → `_ZN3Foo3barEi`; free fn `foo(int)` → `_Z3fooi`;
   ctor `_ZN3FooC1Ev` (C1 complete-ctor only), dtor `_ZN3FooD1Ev`;
   type codes: v i l m c h s t j b d f P(pointer) K(const) R(reference→P
   subset). `sym_t.name` cap = CC_MAX_IDENT(64) — truncate + error if
   overflow (cc_error, don't silently collide). Statics: keep existing
   file-prefix mangling semantics (methods never static-file-mangled).

7. **Class representation:** data members → one `type_t` `TY_STRUCT`,
   `tag = ClassName`, installed via `tags_install` so `Foo x;` works
   (`is_type_start`/`parse_type_spec` already resolve tags). Virtual or
   polymorphic: first 8 bytes = vptr field (name like `__vptr`, inserted
   as field 0). Bases: fields of base struct laid out first (offset 0,
   base size rounded to align), then derived members; multiple bases
   concatenated. Access check: field/method records store access level;
   lookup from outside a member-function context checks public (error
   otherwise: `cc_error("'%s' is private in '%s'", ...)`). `struct` =
   public default, `class` = private default.

8. **Method compilation:** inline in class body or out-of-line
   `Ret Class::method(args) { ... }` → build `decl_t` D_FUNC_DEF with
   MANGLED name, ftype with implicit `Class *this` as params[0]
   (`this` installed as local sym in symtab — gen_func_body already
   installs params by name, so name params[0] "this"), call `gen_decl`.
   Inside method body, bare `field` → `this->field` fallback: implement
   by cpp hook in primary ident-not-found? No — simpler: cpp hook in
   `cpp_try_primary` when ident is unknown local/global AND current
   method context has member of that name → emit `this->name` path.
   Track current class context in cpp module (pushed at method compile).

9. **Out-of-line / namespace seam (parse.c):** `parse_top_level` first
   checks (g_cpp_mode): `T_KW_CLASS` / `T_KW_TEMPLATE` /
   `T_KW_NAMESPACE` / `T_KW_TRY`(top-level = error) → cpp handlers;
   `T_IDENT` + peek `T_SCOPE` (`::`) → out-of-line member fn or
   qualified symbol → `cpp_parse_qualified(lx)`. Return bool: handled.
   `::` token: lexer emits single `T_SCOPE` for `::` (never split colons).

10. **Exceptions runtime (E):** `libonyxc/src/eh/eh.c` + header
    `libonyxc/include/...` (match existing header layout; stdio.h lives
    in include/io — so `include/eh/eh.h`? follow libonyxc's own
    include/ tree, look at Makefile). API: `__onyx_eh_push(jmp_buf*)`,
    `__onyx_eh_pop(void)`, `__onyx_eh_throw(void *obj, int tag)` (stores
    + longjmps; if no handler → print "terminate: uncaught exception"
    + `exit(134)`), `__onyx_eh_state()` returns current (obj, tag).
    Bump `CC_LIB_SOURCE_COUNT` (include/core/cc.h) and `libsrcs[]` in
    main.c. Tag ids: per class tag name → stable positive int (hash or
    sequential at class definition); primitives get small reserved ids.
    Lowering: `try {A} catch (T e) {B}` ≈
    `{ jmp_buf jb; void *e_ptr; if (!__onyx_eh_push(&jb)) { A; __onyx_eh_pop(); }
      else { __onyx_eh_pop(); state=e_ptr...; if (match) {B} else rethrow; } }` —
    setjmp idiom: `if (setjmp(jb)) {catch-path} else {try-path}` using the
    existing `__builtin_onyx_setjmp`/`longjmp` codegen in parse_primary
    (reuse — do NOT add new builtins). catch(...) → always match.
    `throw e;` → `__onyx_eh_throw(&obj_storage, tag)` (storage: class
    object copied to a cpp-module static 4KiB buffer? throw by value:
    copy object bytes to static buffer via memcpy call or inline sd
    loop (rv_sd exists); pointer/reference throw → pass pointer as-is.
    Choose: static 64KiB ring (document size) — single exception at a
    time is fine for subset.

11. **Templates (F):** function templates only. Record in template
    table: name, param names (from `template<class T, ...>`), byte span
    [start,end) of the full decl in `lx->src`. Use site: primary/postfix
    sees `name<` where name in table → parse concrete type args until
    `>` (handle nested `>>` — lexer gives T_SHR: split-by-rescan or
    require space `> >`; pick simple: treat T_SHR after type-arg as two
    `>` — you control relexing, easiest: substitute text then lex fresh).
    Instantiate: copy span to buffer, textual-replace param identifiers
    (word-boundary), create fresh `lexer_t` over the substituted source
    with filename `(template <name> instantiated)` — then run
    `parse_translation_unit`-equivalent top-level parse on it (the
    snippet is a complete decl). Mangled name for specialization:
    `_Z<name><I<typecodes>E...>` approximated as `_Z<name>S_<argcode>_...`
    — your call, must be unique per instantiation. Parse-time: only
    at explicit `name<...>` use (no deduction — document).

12. **References (C):** `T& x` in declarators (parse_declarator —
    cpp mode): treat `&` like `*` producing `type_make_ptr(t)` BUT mark
    the DECLARED SYMBOL `is_ref=true` → add `bool is_ref;` to `sym_t`
    (ast.h). Use site (primary ident, local sym is_ref): load pointer
    value then return VAL_LVAL at that address (auto-deref). Passing to
    calls: `f(r)` passes pointee address? NO — C++ passes reference =
    pointer value... if r is `int&`, `f(r)` passes *pointer* value
    (the address stored) — which is what passing `int*` to `int*` param
    does; but f declared `void f(int&)` → its param type was converted
    to `int*` at parse → types match (address passed). ✓. Assignments
    `r = 5` → auto-deref makes LVAL → store through. Member `r.x` →
    is_ref var: primary returns LVAL-of-pointee? Careful: `Foo& f` use
    as object: `f.method()` — if primary yields loaded POINTEE lvalue,
    `->` vs `.` mismatch: C++ `r.x` and `r->x` BOTH valid on ref/ptr...
    subset: make is_ref primary return the POINTER VALUE (VAL_REG/SPILL
    typed `T*`) and require `.` on refs be treated as `->`?? that breaks
    `r = 5`. Decision: primary on is_ref returns POINTED-TO lvalue
    (VAL_LVAL) for scalar/primitive refs; for class-type refs return
    pointer-typed value so `->` works and `.` — handle in postfix: if
    v was produced from is_ref class object (mark via type? no...)
    — SIMPLER UNIFORM RULE: refs always represent as pointer T* in
    symbol type; auto-deref ONLY on plain assignment target and scalar
    load contexts is complex. FINAL RULE (choose this): is_ref symbol
    stores T*; primary ALWAYS loads the pointer → v is VAL_REG typed
    T* ; postfix `.` on it → treat as `->` (allow dot on T* in cpp mode:
    field offset same as arrow — one-line in member hook); assignment
    `r = v` → v is binary LHS: assign path expects lvalue... make
    primary detect: if is_ref AND next token is `=` (assignment op) →
    return lvalue-through-pointer (load ptr → VAL_LVAL). Handle binary
    ops reading r (`r + 1`) → primary returns pointer?? WRONG for int&.
    UGH. => For NON-class refs: return dereferenced LVAL always (so
    arithmetic/assignment work; `&r` → address-of lval = original
    pointer ✓; `r = 5` ✓; passing r to `f(int&)`→ param is T* : call
    passes... VAL_LVAL passed to arg materialization loads VALUE not
    address ✗). Resolve: f(int&) params — in cpp mode when param type
    is pointer BECAUSE declared ref → at call site we must pass ADDRESS
    OF LVAL. So call-arg emission must know arg was reference-declared.
    => param func_param_t has no flag... add `bool is_ref` to
    `func_param_t` (types.h — small additive change), call site: for
    VAL_LVAL args where corresponding param is_ref → materialize address
    instead of value. That's a gen_call arg-loop tweak (~5 lines,
    gated on param flag, C mode never sets flag → no behavior change).
    Class-type refs: primary returns... same uniform: for class is_ref,
    primary returns deref'd LVAL (VAL_LVAL at object addr) typed T:
    `.` works naturally, `->` on LVAL — arrow expects pointer value:
    arrow path does load_value → for VAL_LVAL loads VALUE at addr —
    would need addr instead... `Foo* p; p->x` works today because p is
    SYM/REG holding pointer. For ref class: `r->x`?? if r is object
    reference, C++ allows both `.` and `->`. Add to postfix arrow
    branch: if v is VAL_LVAL of struct type (came from is_ref or plain
    object) → treat as already-address (VAL_LVAL addr IS object addr) →
    just do member offset (same as '.'). Actually cleanest: `->` on
    VAL_LVAL struct = identical to `.` (address already in reg).
    Today's arrow on struct-typed-LVAL would misbehave anyway; the
    hook: in arrow branch, if `v.kind==VAL_LVAL && v.type is struct`
    → skip load (address already there). Wait — but for `Foo *p; p->x`
    where p is VAL_SYM: v.type = T*, load_value gives pointer value,
    correct. For VAL_LVAL holding struct (reference object) → no load.
    Both work with kind+type check. ✓.

13. **RAII specifics (C):** ctor call at decl: default ctor →
    `gen_call(mangled_C1_sym, ftype_no_params, callee=address-of-local)`.
    With args `Foo x(1,2)`: intercept seam (4: local decl) parses
    `( args )` via gen machinery (exported parse_expr) then gen_call
    with hidden this. Dtor registry: cpp-side array of
    {scope_depth, sym_idx (local obj), dtor_sym, dtor_ftype} reset per
    function (cpp_func_begin), max 64/function (cc_fatal over).
    cpp_scope_exit: emit dtors for entries with scope_depth == current
    g_cur_scope (BEFORE pop — call order in parse_compound: after loop,
    pass g_cur_scope; emit while current scope still active so
    symtab_lookup_local works). cpp_return_unwind: emit dtors of ALL
    registered entries in REVERSE creation order (skip current scope
    handled? return exits everything → all entries). Virtual dtor:
    via vtable (D phase), non-virtual → direct call. this pointer for
    dtor = address-of-local (materialize VAL_SYM → reg).

14. **new/delete (C):** `new T` / `new T(args)` → emit call to
    `malloc(n)` (sym lookup "malloc" — auto-linked libonyxc), then ctor
    with hidden this = result (a0 holds return — after gen_call, value
    in ret reg; need to pass as callee for ctor call — read gen_call
    return convention: val_t ret reg a0 ✓). `delete p` → evaluate p,
    call dtor with this=p, then `free(p)`. Arrays `new T[n]` → error
    "not yet supported" (document). `delete[]` → error.

15. **Virtual dispatch (D):** polymorphic class → vtable in rodata:
    array of function addresses (uint64 each), slot i = i-th virtual
    method in DECLARATION order (base slots first for MI first-base —
    subset: slot table = own class's full ordered list, bases merged in
    declaration order — implement single-level: derived overrides reuse
    base slot index when signature matches, else append). Ctor emits:
    `vaddr_of_vtable → store at [this+0]`. Method call where ftype
    declares virtual AND static type is polymorphic → indirect:
    load [obj+0] (vptr), load [vptr + slot*8], jalr. Where: cpp_try_call
    decides (method record has `is_virtual` + slot). Non-virtual or
    exact-class static dispatch → direct. Dtor: virtual classes' dtor
    called through vtable slot too (dtor slot = reserved after methods).
    Vtable emission: cpp module appends to a buffer; hook into rodata —
    gen.c has rodata fixups; add export `uint32_t cc_strpool_add` exists
    (rodata append) — can REUSE cc_strpool_add for vtable bytes? It adds
    bytes to rodata ✓ (returns offset) — then emit `lui+addi` rodata
    address with fixup — cpp needs fixup mechanism: rodata fixups array
    is gen.c-static... provide export `void gen_rodata_addr(uint32_t
    rodata_off, int reg)` (wraps rv_lui/rv_addi/fixup record — 10 lines
    in gen.c, declared gen.h) — add in Phase D.

16. **Namespaces (D):** `namespace N { decls }` — flatten: maintain
    prefix stack; symbols registered as `N::name` internally? symtab
    lookup by raw name "name" must resolve inside ns — subset: inside
    ns body, first install as `N::name` mangled, also record alias:
    method/out-of-line `N::Foo::m` handled by qualified parser.
    Name resolution inside namespace: cpp module keeps
    "current ns prefix" and try-hooks prepend prefix when looking up
    failed raw name (prefix + "::" + name). `using namespace N;` at
    top-level: record fallback order; lookup tries current, then each
    using-ns. Global (no ns) always fallback. Keep a lookup API:
    `int cpp_lookup_global(const char *name)` used by primary hook when
    raw lookup fails. `using N::x;` → alias record (name → N::name).
    Nested ns supported (prefix stack).

## Phase list (execute in order, one subagent each)

**Phase A — Foundation.** lexer `T_SCOPE` + keywords
(nullptr, constexpr, explicit, dynamic_cast, final as context keyword →
skip final), mode flag + main.c ext detection, val.h move, gen.c
exports, ALL hook sites with stub cpp functions (`src/cpp/front/` +
`include/cpp/cpp.h`: cpp_try_primary, cpp_try_stmt, cpp_try_member,
cpp_try_call, cpp_scope_exit, cpp_return_unwind, cpp_func_begin,
cpp_local_decl, cpp_top_level → parse.c seam, cpp_count_call_args).
Stubs return false/no-op. Makefile: compile new .c files, fix clean.
**Verify: `make clean && make && make check && make selfhost-test`
ALL GREEN** (C paths unaffected: g_cpp_mode false everywhere).

**Phase B — Classes core + methods + calls.** class/struct body parse
(access, data members, method decls inline+out-of-line, ctor/dtor
decls), mangle.c, method_table.c (group + arity resolve),
cpp_try_member/cpp_try_call real impl (hidden this injection), bare
member → this-> fallback in primary hook, access checks, tags_install
so `Foo x;` parses. Tests: `tests/cpp/class_basic.cpp`,
`method_call.cpp`, `inherit_basic.cpp` (single inheritance, field
offsets, upcast pointer identity), `access_error.cpp` (negative:
must FAIL compile, checked by runner). Runner `scripts/cpp_runner.sh`
+ hook into `make check` (Makefile: extend `check:` recipe with
`bash scripts/cpp_runner.sh`).

**Phase C — RAII + new/delete + references.** local ctor seam (4),
ctor/dtor emission, scope exit + return unwind, `new`/`delete`
(non-array), `T&` sugar + func_param_t.is_ref + gen_call arg tweak +
postfix `->`-on-struct-LVAL rule. Tests: `raii_order.cpp` (ctor/dtor
order print), `return_unwind.cpp`, `newdelete.cpp`, `refs.cpp`.

**Phase D — Namespaces + virtual + MI.** parse_ns.c (ns flatten,
using), virtual vtables (gen_rodata_addr export), vptr in ctors,
indirect virtual calls, MI field layout + upcasts + override slot
reuse (single-vtable simple scheme). Tests: `namespace_basic.cpp`,
`virtual_dispatch.cpp` (base ptr → derived override), `mi_fields.cpp`.

**Phase E — Exceptions.** libonyxc eh.c + header + lib list bump,
parse_stmt.c lowering, type tags, catch(...), nested try, throw
value/pointer, uncaught → terminate. Tests: `throw_catch.cpp`,
`catch_all.cpp`, `nested_try.cpp`. Document: dtors skipped on unwind.

**Phase F — Function templates.** template_table.c, span record,
explicit-arg instantiation, relex+substitute, specialization naming.
Tests: `template_fn.cpp` (max<int>, identity<string?> — string = const
char* ok). `>>` in type args must work (`Vec<Vec<int>>` N/A — function
templates: `f<pair<int,int>>` rare; require lexer T_SHR split handled).

**Phase G — Docs + final.** Update `README.md` (C++ section: what's
supported, how to build), `docs/STATUS.md` (Next stages: C++ frontend
item 2 → status, Material limitations: explicit subset list + what's
deferred: RTTI/dynamic_cast, virtual inheritance, class templates,
unwind dtors, std::lib, lambdas...), plan checkboxes in
`docs/plan-compiler-architecture.md`. Full
verification suite run + paste results.

## Verification (EVERY phase ends with all of these green)

```
cd OnyxCompiller
make clean && make                 # builds onyxcc + onyx-ld
make check                         # 66+42+driver tests + cpp runner (B+)
make selfhost-test                 # C sources still self-compile
```

New .cpp fixtures must: compile without error (negative tests must
FAIL), run on `tools/onx-run` (build via `make tools/onx-run` or it's
built by check), exit 0, stdout match expected (script asserts).

## Anti-goals

- No rewriting gen.c architecture; no IR; no growing gen.c > +150 lines
  TOTAL across all phases (hooks + exports only — if you need more,
  you are designing wrong: push logic into src/cpp/).
- No changes to C behavior: every gen.c edit must be gated on
  g_cpp_mode (or additive field defaulting to false/0).
- No inline tests in src/ files; no `#[allow...]` equivalents (no
  empty `#pragma GCC diagnostic` suppressions — fix warnings properly
  or use existing CFLAGS suppressions only).
- Every `src/cpp/front/*.c` file ≤250 lines; if a phase overflows —
  split by responsibility into another file (respect 4-per-folder).
- Do not touch other repos (OnyxKernel/OS etc). libonyxc lives inside
  this repo — allowed.
