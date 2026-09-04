[![OnyxCompiller CI](https://github.com/DivByDiamond/OnyxCompiller/actions/workflows/ci.yml/badge.svg)](https://github.com/DivByDiamond/OnyxCompiller/actions/workflows/ci.yml)

<p align="center">
  <img src="https://img.shields.io/badge/platform-RISC--V%2064--bit-green" alt="RISC-V 64">
  <img src="https://img.shields.io/badge/language-C99-orange" alt="C99">
  <img src="https://img.shields.io/badge/MMU-Sv39-yellow" alt="Sv39 MMU">
  <img src="https://img.shields.io/badge/license-GPL--3.0-red" alt="GPL-3.0">
</p>


<p align="center">
<pre class="not-prose" style="text-align:center;font-family:monospace;">
    ███████                                                                          
  ███▒▒▒▒▒███                                                                        
 ███     ▒▒███ ████████   █████ ████ █████ █████                                     
▒███      ▒███▒▒███▒▒███ ▒▒███ ▒███ ▒▒███ ▒▒███                                      
▒███      ▒███ ▒███ ▒███  ▒███ ▒███  ▒▒▒█████▒                                       
▒▒███     ███  ▒███ ▒███  ▒███ ▒███   ███▒▒▒███                                      
 ▒▒▒███████▒   ████ █████ ▒▒███████  █████ █████                                     
   ▒▒▒▒▒▒▒    ▒▒▒▒ ▒▒▒▒▒   ▒▒▒▒▒███ ▒▒▒▒▒ ▒▒▒▒▒                                      
                           ███ ▒███                                                  
                          ▒▒██████                                                   
                           ▒▒▒▒▒▒                                                    
   █████████                                      ███  ████  ████                    
  ███▒▒▒▒▒███                                    ▒▒▒  ▒▒███ ▒▒███                    
 ███     ▒▒▒   ██████  █████████████   ████████  ████  ▒███  ▒███   ██████  ████████ 
▒███          ███▒▒███▒▒███▒▒███▒▒███ ▒▒███▒▒███▒▒███  ▒███  ▒███  ███▒▒███▒▒███▒▒███
▒███         ▒███ ▒███ ▒███ ▒███ ▒███  ▒███ ▒███ ▒███  ▒███  ▒███ ▒███████  ▒███ ▒▒▒ 
▒▒███     ███▒███ ▒███ ▒███ ▒███ ▒███  ▒███ ▒███ ▒███  ▒███  ▒███ ▒███▒▒▒   ▒███     
 ▒▒█████████ ▒▒██████  █████▒███ █████ ▒███████  █████ █████ █████▒▒██████  █████    
  ▒▒▒▒▒▒▒▒▒   ▒▒▒▒▒▒  ▒▒▒▒▒ ▒▒▒ ▒▒▒▒▒  ▒███▒▒▒  ▒▒▒▒▒ ▒▒▒▒▒ ▒▒▒▒▒  ▒▒▒▒▒▒  ▒▒▒▒▒     
                                       ▒███                                          
                                       █████                                         
                                      ▒▒▒▒▒                                          
</pre>
</p>

<p align="center"><em>A self-hosting C compiler for RISC-V 64-bit, targeting OnyxOS</em></p>

----

OnyxCC (`onyxcc`) is a single-pass C99 compiler for RISC-V 64-bit, written in
freestanding-compatible C. It compiles to the OnyxExec (`.onx`) format used
by [OnyxKernel](https://github.com/DivByDiamond/OnyxKernel), and is
self-hosting: `onyxcc` can compile its own source into a `.onx` binary that
runs on OnyxOS and rebuilds the compiler again from there.

Architecturally it takes after tcc (tiny C compiler): minimal memory use,
linear-time compilation, no intermediate representation, no heavyweight
optimization passes. It is designed to run comfortably on boards with as
little as 512 MB of RAM.

Part of the [OnyxOS](https://github.com/DivByDiamond/OnyxOS) ecosystem.

----

## Key Features

- Single-pass codegen - expressions are compiled directly to RISC-V machine code as they are parsed; no IR
- C99 lexer and parser - functions, parameters, locals, arrays, pointers, `struct`/`union`/`enum`, `switch`/`case`, `goto`/labels
- Preprocessor - `#include`, object- and function-like `#define` (`#`/`##`, `__VA_ARGS__`), `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif`, `#pragma once`, `defined()`
- RV64IMAFD codegen - full I/M integer extensions plus F/D floating point (soft- and hard-float paths), standard calling convention (a0-a7, ra, sp, fp)
- Multi-file compilation - `onyxcc -o prog.onx main.c util.c lib.c` with a shared symbol table and per-file static-symbol mangling
- Self-hosting - `make selfhost` compiles the compiler's own sources into `onyxcc_self.onx`
- `onyx-ld` linker - links `.o` object files (`ONYO` format) and `.a` archives into a final `.onx`, with a full set of RISC-V relocations (`R_ONYO_64/32/HI20/LO12_I/LO12_S/PCREL_HI20/PCREL_LO12_I/PCREL_LO12_S/JAL/BRANCH`)
- `onx-run` emulator - runs `.onx` binaries on the host without QEMU, with instruction/frame tracing for debugging codegen
- Automatic libc linking - `onyxcc -o prog.onx prog.c` links `libonyxc` (start/syscalls/stdio/stdlib/string/ctype/time/termios/math) automatically; `-nostdlib`/`-N` disables it
- `libonyxc` - a C99 libc for OnyxOS: buffered `stdio` (`printf` family, `FILE*` I/O), `stdlib`, 40+ `string` functions, full `ctype`, `time`/`strftime`/`clock_gettime`/`nanosleep`, `errno`/`strerror`, signal sets, `termios` (raw mode), soft-float `math.h`, `assert.h`
- Integration tests - a compile + run + compare-output harness (`scripts/integration-runner.sh`) over the test suite

## Not yet implemented

- C++ front end (header layout is in place; no parser yet)
- `onyxcc -c` (emitting relocatable `.o` files instead of resolved addresses) - `onyx-ld` already accepts `.o` input, but the compiler doesn't emit it yet; use multi-file mode (`onyxcc -o prog.onx a.c b.c c.c`) as a workaround
- Compound literals, designated initializers
- Inline assembly (use the `__ecallN` builtins instead)
- Optimization passes (constant folding, dead code elimination) - single-pass, no IR to optimize over
- Testing on real Milk-V Duo S hardware (verified in QEMU via OnyxOS only)

----

## Architecture

```
OnyxCC
├── include/
│   ├── core/cc.h          # Shared types, options, buffers
│   ├── core/compat.h      # Linux / OnyxOS freestanding compatibility
│   ├── sys/onyxo.h        # .o object format (ONYO magic)
│   ├── sys/onyx.h         # .onx format (kept in sync with OnyxKernel)
│   ├── sys/syscalls.h     # OnyxOS syscall ABI
│   ├── front/lexer.h      # Tokens
│   ├── back/pp.h          # Preprocessor
│   ├── core/types.h       # Type system
│   ├── front/ast.h        # AST nodes + symbol table
│   ├── front/parse.h      # Top-level parser
│   ├── back/gen.h         # Single-pass codegen
│   ├── arch/riscv64.h     # Instruction encoders
│   └── back/emit.h        # .onx writer
├── src/
│   ├── core/              # main.c, util.c, types.c, shim.c (freestanding libc shim)
│   ├── front/              # lexer.c, ast.c, parse.c
│   ├── back/               # pp.c, gen.c, emit.c
│   ├── arch/                # riscv64.c
│   └── tools/               # onyx-ld.c - the standalone linker
├── libonyxc/               # libc
│   ├── include/            # onyxc.h, stdio.h, stdlib.h, string.h, ctype.h,
│   │                       # time.h, signal.h, errno.h, fcntl.h, unistd.h, limits.h
│   ├── src/
│   │   ├── core/           # start.c (_start), syscalls.c (ecall wrappers)
│   │   ├── io/              # stdio.c, stdlib.c, string.c, strerror.c, time.c
│   │   └── ctype/           # ctype.c
│   └── tests/               # libc smoke tests + Linux stubs
├── tests/                   # test C programs
└── Makefile
```

### Compilation Pipeline

```
input1.c input2.c ...
   │
   ▼  pp.c (per file)
preprocessed.c   (macros expanded, #include inlined)
   │
   ▼  lexer.c (per file)
token stream
   │
   ▼  parse.c + gen.c (single-pass, shared symbol table)
   │  ── g_text, g_rodata, g_data, g_bss accumulate across files
   │  ── static symbols get per-file mangling to avoid collisions
   │
   ▼  gen_finalize(entry_sym) - after all files are processed
resolved addresses, label fixups applied
   │
   ▼  emit.c
output.onx  (header + text/rodata/data/bss segments)
```

### The `.onx` format

`include/sys/onyx.h` is kept in sync with
`OnyxKernel/kernel/src/proc/onx/` and `OnyxKernel/core/src/formats/header.rs`.
See the comment block at the top of `onyx.h` for the exact byte layout.

----

## Building

### On the host (Linux/x86_64) - for development outside OnyxOS

```console
$ make            # builds ./onyxcc (native Linux x86_64 ELF)
$ make hello      # builds tests/hello_full.onx
$ make test       # prints an .onx header
```

### Cross-compiling for OnyxOS - the `.onx` compiler itself

```console
$ make onyxcc-riscv    # clang -> onyxcc.riscv.elf (RISC-V 64 ELF)
$ make onyxcc-onx      # elf2onx -> onyxcc.onx (OnyxOS ring-1 binary)
```

Requires `clang`/`lld` with RISC-V target support and `elf2onx` from
`OnyxKernel/target/release/elf2onx`. Full pipeline:

```console
# 1. Build elf2onx once:
$ cd ../OnyxKernel && cargo build --release -p onyx_tools

# 2. Build onyxcc.onx:
$ cd ../OnyxCompiller && make onyxcc-onx
```

### Self-hosting

The compiler can compile its own source tree:

```console
$ make selfhost-test   # compile everything to /dev/null, sanity check only
$ make selfhost        # compile -> onyxcc_self.onx
```

Running `onyxcc_self.onx` requires OnyxOS (QEMU or real hardware).

----

## Usage

```console
$ onyxcc -o hello.onx hello.c                  # basic compilation
$ onyxcc -e _start -o hello.onx hello.c        # explicit entry point
$ onyxcc --ring1 -o service.onx service.c      # ring 1 (root space) binary
$ onyxcc -I /usr/onyxc/include -DDEBUG=1 -o prog.onx prog.c
```

----

## Integration with OnyxOS

`OnyxOS/Makefile` can invoke `onyxcc` directly for userland binaries:

```makefile
ONYXCC ?= onyxcc
ONYXCC_INCLUDE ?= $(ONYXCC_DIR)/libonyxc/include

bin:
	$(ONYXCC) --ring1 -o bin/init.onx init/init.c
	$(ONYXCC) -o bin/login.onx init/login.c
	$(ONYXCC) -o bin/osh.onx init/osh.c
```

Optional userspace applications (built with `onyxcc` against `libonyxc`) live
in [OnyxApps](https://github.com/DivByDiamond/OnyxApps).

----

## Related Projects

| Project | Description |
|---------|-------------|
| [OnyxOS](https://github.com/DivByDiamond/OnyxOS) | Meta-repository: build orchestration, docs, image assembly |
| [OnyxBoot](https://github.com/DivByDiamond/OnyxBoot) | RISC-V bootloader (C++20) |
| [OnyxKernel](https://github.com/DivByDiamond/OnyxKernel) | RISC-V kernel (Rust), defines the `.onx` format |
| [OnyxApps](https://github.com/DivByDiamond/OnyxApps) | Userspace applications built with this compiler |

For planned work, see [OnyxKernel/todo.md](https://github.com/DivByDiamond/OnyxKernel/blob/main/todo.md).

----

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
