# Separate Compilation (Task 1 of the compiler plan)

**Goal:** `onyxcc -c src.c -o src.o` emits a relocatable ONYO object;
`onyx-ld` links any number of them (plus static archives) into a `.onx`
that runs identically to a single-invocation build.

Status: **implemented and verified** (2026-11-11). `make check` runs
`scripts/sepcomp-runner.sh`, which compiles all 17 libonyxc sources plus
the `tests/separate/` fixtures into separate objects, links them with
`onyx-ld`, executes the result, and asserts that duplicate globals and
undefined symbols are rejected. `make selfhost-test` stays green.

---

## Current state

- `onyxcc -c` emits ONYO v1 objects; `onyx-ld` links them into `.onx`.
- As of 2026-11-15 (`c3d2bd7`), archive members are selected lazily using unresolved
  globals and the entry symbol, with a fixed point within each archive.
  Archive order matters; repeat earlier archives to resolve cycles.
- GNU/BSD long names are supported. Index metadata is skipped; no index,
  weak symbols, COMDAT, section GC or thin archives are implemented.
- Malformed archive framing and recognized ONYO members are rejected,
  including malformed members which would otherwise remain unselected.
- Format definition: `include/sys/onyxo.h` (header 64 B, section 40 B,
  symbol 32 B, reloc 24 B; string table after the tables; section data last).

## ONYO object contract (writer = compiler, reader = onyx-ld)

One object per translation unit. No libonyxc auto-link in `-c` mode.

### Versions and weak binding (2026-11-15, `4845c52`)

ONYO v1 remains the compiler's output. The linker also accepts v2, which
keeps all table layouts and relocation numbers unchanged and adds symbol
flag `ONYO_SYM_WEAK` (`0x80`). Weak symbols require v2, a nonempty name,
GLOBAL without LOCAL and exactly one of DEFINED/UNDEF. Unsupported symbol
flag bits are rejected. A v1 linker rejects v2 rather than silently
misinterpreting weak binding. This does not change ONX executable versions.

Strong definitions win over weak ones regardless of input order; two
strong definitions remain an error. The first selected weak definition
wins over later weak definitions. All global relocations, including those
from the defining object, use the winning definition. Local symbols keep
object-private binding. An unresolved weak reference has value zero before
adding its relocation addend; callers must check optional function pointers.

An undefined weak reference does not pull archive members. A strong
reference (or the entry root) can pull a weak definition; an existing weak
definition satisfies a name and does not itself demand a stronger archive
member. Members pulled for other exports can supply overriding strong
definitions. An entry definition is always required, even when an undefined
entry symbol is marked weak. A defined weak entry is valid.

As of 2026-11-15 (`f9d10cb`), `onyxcc -c` accepts GNU weak attributes on file-scope functions and data,
keeps weak binding across redeclarations and emits v2 only when an emitted
symbol is weak. Ordinary objects and unreferenced extern declarations keep
v1. Static/typedef weak declarations are rejected. Direct executable builds
with weak symbols require `-c` followed by `onyx-ld`; syntax-only is allowed.
The explicit object tests in `tests/weak/` still patch metadata to cover
malformed inputs. `tests/weak/source/` checks real attribute emission and
linked execution without modifying object metadata.

### Sections (fixed order, all four always emitted)

| idx | name      | align | contents                    |
|-----|-----------|-------|-----------------------------|
| 0   | `.text`   | 4     | `g_text`                    |
| 1   | `.rodata` | 16    | `g_rodata`                  |
| 2   | `.data`   | 16    | `g_data`                    |
| 3   | `.bss`    | 16    | no bytes, `size = g_bss_size` |

`data_off` = absolute offset of the section bytes in the file (0 for
`.bss`). Layout: header, section headers, symbols, relocs, string table,
then text/rodata/data bytes.

### Symbols

First four entries are section symbols (needed as reloc targets for
rodata references): `.text`, `.rodata`, `.data`, `.bss`, each
`DEFINED|LOCAL|DATA`, `section_idx` = its index, `value = 0`.

Then every `g_globals[i]` with kind `SYM_FUNCTION` or `SYM_GLOBAL_VAR`:

| symbol | flags | section | value |
|---|---|---|---|
| defined function | `DEFINED\|FUNC` + `GLOBAL`/`LOCAL` | 0 | `text_off` |
| declared function (no body) | `UNDEF\|FUNC\|GLOBAL` | -1 | 0 |
| var with initializer (`.data`) | `DEFINED\|DATA` + `GLOBAL`/`LOCAL` | 2 | `text_off` |
| allocated var without initializer (`.bss`) | `DEFINED\|DATA` + `GLOBAL`/`LOCAL` | 3 | `text_off` |
| `extern` declaration only | `UNDEF\|DATA\|GLOBAL` | -1 | 0 |

`LOCAL` is used for `static` file-scope objects/functions and for the
generated `__sl_*` backing symbols of function-local statics. The linker
must never add LOCAL symbols to the global namespace.

### Relocations

The compiler fixup tables map one-to-one:

| compiler fixup | relocs emitted | section | target | addend |
|---|---|---|---|---|
| `g_fixups` (lui+addi) | `HI20` @patch_lui, `LO12_I` @patch_addi | 0 | referenced symbol | `extra_off` |
| `g_rodata_fixups` (lui+addi) | `HI20`, `LO12_I` | 0 | `.rodata` section symbol | `rodata_off` |
| `g_data_rodata_fixups` | `R_ONYO_64` | 2 | `.rodata` section symbol | `rodata_off` |
| `g_data_sym_fixups` | `R_ONYO_64` | 2 | referenced symbol | 0 |

No other reloc types are produced today (direct calls are `la`+`jalr`,
branches are resolved intra-function during parsing).

## Required compiler changes

1. `sym_t`: add `bool is_static` (set by `gen_decl` for `static`
   functions/variables and for the `__sl_*` backing symbol) and
   `bool allocated` (true when a `SYM_GLOBAL_VAR` actually reserves space
   in `.data` or `.bss`; distinguishes `int x;` from `extern int x;`).
2. `gen.c`: set those flags; export read-only accessors for the four
   fixup tables (or a small `back/reloc.h` view).
3. New `src/back/obj.c` + `include/back/obj.h`:
   `int obj_emit(const char *path)` writes the ONYO object above. It must
   not call `gen_finalize` and must not mutate gen state.
4. `main.c`: add `-c/--compile`; in this mode require exactly one input,
   skip libonyxc auto-link, parse, then `obj_emit` instead of
   `gen_finalize` + `onx_emit`. Default output `a.o`.
5. Per-file independence: `-c` runs one translation unit per process, so
   the existing one-shot init is enough; do not accumulate state.

## Required linker changes

1. Relocation resolution: if the target symbol is `DEFINED` in its own
   object, use its address directly (covers LOCAL symbols and section
   symbols). Only `UNDEF` targets are looked up in the global table.
2. Keep LOCAL symbols out of the global table (verify) and report clear
   diagnostics for duplicate globals / undefined symbols.
3. BSS symbols and sections: address from `g_bss_vaddr + data_off +
   value`; reject relocations inside `.bss`.
4. Alignment: section alignment from the object (`4` text, `16` rest).
5. Keep the existing file split under 250 lines: introduce
   `src/tools/ld/` (`parse.c`, `link.c`, `emit.c`, `ld_internal.h`) with
   `src/tools/onyx-ld.c` reduced to CLI wiring.

## Tests (`tests/separate/` + `scripts/sepcomp-runner.sh`)

Runner steps:
1. `onyxcc -N -c` each libonyxc source listed in `src/c/main.c` → `*.o`;
2. `onyxcc -N -c` each `tests/separate/*.c` fixture → `*.o`;
3. `onyx-ld -o prog.onx -e _start <all objects>`;
4. `tools/onx-run prog.onx` and compare stdout with `// EXPECT:` lines.

Negative fixtures: duplicate global definition and undefined symbol must
make `onyx-ld` fail (non-zero exit).

## Task split (parallel, disjoint files)

| Task | Owner files | Scope |
|---|---|---|
| A | `include/front/ast.h`, `src/c/gen.c`, `include/back/gen.h`, `include/back/obj.h`, `src/back/obj.c`, `src/c/main.c`, `include/core/cc.h` | object emission + driver `-c` |
| B | `src/tools/onyx-ld.c`, `src/tools/onyx-ld.h`, `src/tools/ld/*` | linker correctness + split |
| C | `tests/separate/*`, `scripts/sepcomp-runner.sh` | fixtures + runner |

Makefile wiring (`src/back/obj.c`, `src/tools/ld/*.c`, `check` hook) is
done centrally after A/B/C land, to avoid concurrent edits.

## Verification

```sh
make -j1 onyxcc onyx-ld
bash scripts/sepcomp-runner.sh          # new: object round-trip + link + run
make check                              # existing suites must stay green
make selfhost-test                      # compiler still compiles itself
```
