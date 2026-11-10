# Compiler status and next stages

This describes the current compiler, not a claim of C or C++ conformance.
The supported C99-oriented subset can build the unmodified Lua 5.5.1
interpreter and bytecode compiler against libonyxc. OnyxApps retains the
checksum-pinned MIT-licensed upstream archive and offline build wrappers.

## Current foundation

The driver accepts multiple C translation units, resolves shared symbols
and mangles file-local statics. The input limit includes automatically
linked libc sources; `-N` reserves the entire capacity for user inputs.
Driver tests derive the actual limits from `include/core/cc.h`.

The preprocessor handles function/object macros, multiline invocations,
argument prescan, stringification, token pasting, variadic arguments,
conditional arithmetic and `defined`. It remains a bounded implementation:
limited buffers, expansion depth and rescan passes are not standard macro
hide-set semantics.

Integer constant evaluation now has target widths, signed/unsigned
conversions, boolean casts and conditional/logical short circuiting. It
supports useful `sizeof` and builtin `offsetof` forms. Unknown identifiers,
zero divisors and invalid shift counts receive diagnostics. Target signed
overflow deliberately wraps instead of triggering host-language undefined
behavior; this is not a complete C constant-expression constraint checker.

The RV64 backend and runtime regressions cover pointers and compound
assignments, signed loads/conversions, aggregates, global initializers,
function pointers, floating-point values, switch dispatch, stack arguments,
variadic calls, string arrays and large addresses. Passing individual tests
does not imply that every interaction or ABI corner case is implemented.

libonyxc includes a reusable allocator, buffered stdio, floating formatting
and parsing, environment/process helpers, locale, math and nonlocal jumps.
Native sanitizer harnesses exercise selected allocator, arena, stdio,
process, formatting and string-to-double paths. Host emulator checks do
not establish equivalent kernel or hardware behavior for those services.

Lua smoke tests execute the CLI, math, pure Lua `require`, reported errors,
garbage collection and luac bytecode roundtrips. Native shared-library
loading is disabled in the generic Lua port. These results do not establish
that the complete Lua test suite or every standard-library operation passes.

## Material limitations

- There is no C++ frontend. Classes, overload resolution, templates,
  exceptions, RAII, the C++ runtime and the standard library remain work.
- `onyxcc -c` does not yet emit relocatable objects. `onyx-ld` understands
  the private ONYO object format and Unix archives, but its existence does
  not provide a complete separate-compilation pipeline. Use multi-file
  compilation to produce `.onx` directly.
- Aggregate calls/returns and variadic calls use the compiler's current
  private conventions. Interoperability with independently compiled
  RISC-V toolchains is not established; ordinary register names alone do
  not make these conventions a standard platform ABI.
- `long double` uses 16-byte storage with binary64 arithmetic and does not
  provide genuine extended precision.
- Arbitrary `sizeof(expression)` and unevaluated-expression side effects
  are incomplete. Compound literals, full designated initialization,
  C99 diagnostics and other untested language constructs remain gaps.
- Inline assembly is restricted; use documented syscall builtins for the
  supported runtime paths. There is no optimizing IR, complete debug-info
  pipeline or general-purpose system linker compatibility guarantee.
- `selfhost-test` verifies source acceptance. Building `onyxcc_self.onx`
  only verifies artifact emission. `tests/selfhost/run.py` separately runs
  that compiler, uses it to compile a freestanding program, and executes
  the result. Repeated compiler bootstrap stages and compiling the whole
  C library require additional validation.

## Validation boundaries

`make check` builds host tools, checks frontend fixtures with
`-fsyntax-only`, compiles and executes runtime fixtures with `onx-run`, and
checks multi-file driver bounds. Runtime tests verify both exit status and
expected output. `make check-native` runs the native sanitizer harnesses.
`python3 tests/driver/run.py --sanitize` adds an instrumented host compiler
for driver bounds checks. These checks require a host C compiler, Python 3,
Bash, coreutils and binutils; sanitizer harnesses require ASan/UBSan support.

The compiler and `.onx` emulator are developed together, so agreement
between them is not sufficient evidence of ISA or ABI correctness. Compare
against independent compilers and run appropriate QEMU/OC2R integration
checks before claiming target-system support for a new behavior.

## C++ frontend (subset)

A C++ frontend now lives under `src/cpp/` and is compiled into `onyxcc`
itself (C99 sources; no separate binary). `.cpp`, `.cc` and `.cxx` inputs
switch the parser into C++ mode per translation unit (`g_cpp_mode`); C
files keep the plain C path unchanged. End-to-end fixtures live in
`tests/cpp/` and run through `scripts/cpp_runner.sh` as part of
`make check`.

Supported today:

- `class` / `struct` with access specifiers, data members, inline and
  out-of-line member functions, constructors (incl. `Counter::Counter`)
  and destructors; method overloading resolved by argument count;
  Itanium-lite name mangling (`_ZN3Foo3barEi`).
- Single non-virtual inheritance with flattened base fields and base
  pointer upcast; virtual functions with a vtable in `.data`, vptr store
  in every constructor, and indirect dispatch through base pointers.
- RAII for locals: automatic constructor call, destructors at scope exit
  and before `return` (the return value is preserved across dtor calls).
- `new` / `delete` (single objects; array forms rejected), `this`,
  `nullptr`, functional casts `T(args)`, `T&` references (auto-deref at
  use sites, pointer-passed arguments).
- Flattened namespaces (`namespace N { ... }`, `using namespace N;`,
  `N::name`, `A::B::name`).
- Exceptions via a setjmp/longjmp handler chain in
  `libonyxc/src/eh/eh.c`: `throw`, `try`/`catch (T& e)`,
  `catch (int)`, `catch (...)`, rethrow; uncaught exceptions terminate
  with exit code 134.
- Function templates with explicit type arguments
  (`maxv<int>(a, b)`), instantiated by textual substitution and compiled
  at top level after the current declaration.

## Next stages

1. Finish and test the C separate-compilation pipeline: relocatable object
   emission, symbol visibility, archive selection, relocations and an
   explicit ABI. Expand C conformance/negative tests and libc behavior.
2. Extend the C++ frontend beyond the subset above: multiple and virtual
   inheritance with pointer adjustment, RTTI/`dynamic_cast`, class
   templates and deduction, virtual destructors, exception unwinding that
   runs destructors of skipped scopes, lambdas, `constexpr`, operator
   overloading and standard-library integration. C compatibility fixes are
   foundation work, not completed C++ support.
3. Treat Rust as a separate target/runtime effort using the existing
   `rustc` toolchain: target description, startup, allocation, panic and
   linking support. Extending OnyxCC's C parser does not implement Rust.

## C++ material limitations (explicit)

- No multiple inheritance (only one base, non-virtual) and no virtual
  inheritance; RTTI, `dynamic_cast` and `typeid` are not implemented.
- No class templates; function templates require explicit type arguments,
  simple type spellings and a definition before use; nested
  `A<B<C>>` type arguments are unsupported.
- Constructor initializer lists, default member initializers, bitfields
  in classes and nested classes are rejected with diagnostics.
- Destructors are not run when a longjmp unwinds scopes; throwing from a
  destructor during return unwinding is unsupported.
- Namespaces are flattened: two namespaces cannot declare the same member
  name, and `using N::x;` aliases are not tracked.
- References are represented as flagged pointers (`type_t::is_ref`);
  reference return types and references to temporaries are not supported.
- Temporary objects from functional casts are not destroyed automatically.
- The C++ frontend shares the single-pass C backend: no optimizing IR, no
  separate-compilation objects, and diagnostics follow the C parser's
  error-recovery model.

## Source organization

Constant operands and arithmetic live under `src/front/constant/`; libc
responsibilities use directories such as `memory`, `format`, `convert`,
`locale` and `process`. Prefer focused C modules with at most 250 lines and
four source files per folder. Existing large parser/backend files still
need incremental extraction; do not claim the repository already meets
those limits everywhere. Vendored upstream archives preserve their source
layout and license rather than mechanically rewriting external projects.

C++ frontend layout:

- `src/cpp/front/` — `parse_class.c` (class bodies, layout, vtables),
  `parse_method.c` (member declarations/definitions, mangling calls),
  `parse_call.c` (method markers, hidden `this`, `new`/`delete`, refs),
  `parse_ns.c` (flattened namespaces), `parse_stmt.c` (exceptions),
  `parse_tmpl.c` (function templates), `hooks.c` (RAII registry, ctor
  prologue, remaining hooks).
- `src/cpp/tables/` — `mangle.c`, `method_table.c`, `class_table.c`
  (records, access control).
- `include/cpp/` — `cpp.h` (gen.c/parse.c seam), `cpp_front.h`
  (internal API), `mangle.h`.
- `libonyxc/src/eh/eh.c` — exception runtime (auto-linked).
