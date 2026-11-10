[![OnyxCompiller CI](https://github.com/DivByDiamond/OnyxCompiller/actions/workflows/ci.yml/badge.svg)](https://github.com/DivByDiamond/OnyxCompiller/actions/workflows/ci.yml)

# OnyxCC

OnyxCC is a compiler written in C for a growing C99-oriented subset,
producing RISC-V64 `.onx` executables for OnyxOS. It automatically links
libonyxc and supports multiple C translation units in one invocation.
A C++ subset frontend is built in (classes, methods, RAII, virtual
functions, namespaces, references, new/delete, exceptions and function
templates); see [the status and remaining work](docs/STATUS.md) for the
exact boundary — this is not full C++ conformance. There is no Rust
frontend; Rust remains a separate `rustc`-based target effort.

## Build and use

A Linux host needs a C compiler, Make and the usual development tools:

```sh
make
./onyxcc -o hello.onx hello.c
./onyxcc -o application.onx main.c helper.c
```

Common options:

```sh
./onyxcc -I include -DDEBUG=1 -o application.onx main.c
./onyxcc -N -e _start -o freestanding.onx startup.c
./onyxcc --ring1 -o service.onx service.c
./onyxcc -fsyntax-only -N source.c
```

`-N` disables automatic libc linkage. Ordinary applications use the default
ring; `--ring1` is reserved for services needing that privilege. The total
input capacity includes libc translation units; driver tests check the
configured bounds. `ONYXCC_LIB` selects the libonyxc directory when the
compiler is installed away from its source tree.

`onyx-ld` builds with the compiler and reads private ONYO objects/archives.
The compiler does not yet emit those objects with `-c`; multi-file input
is the current application build path.

## Check behavior

```sh
make check                  # frontend acceptance, executed fixtures, driver bounds
make check-native           # selected libc/arena paths under host ASan/UBSan
python3 tests/driver/run.py --sanitize
make selfhost-test          # compiler sources accepted; does not run the result
```

`make check` builds `onyxcc`, `onyx-ld` and `tools/onx-run` from their
sources as required. CI forces a fresh host-tool build because tracked
prebuilt executables cannot validate current sources. Runtime fixtures
check exit status and expected output. The host emulator executes one
process and translates syscalls; it is not a kernel/MMU emulator.

For the sibling Lua ports:

```sh
make -C ../OnyxApps lua luac ONYXCC="$PWD/onyxcc"
python3 ../OnyxApps/apps/lua/tests/smoke.py --runner tools/onx-run \
  --lua ../OnyxApps/build/lua.onx --luac ../OnyxApps/build/luac.onx
```

OnyxApps verifies a pinned vendored archive and builds offline. It is not
needed for the compiler's default CI checks. QEMU, OC2R and other target
validation remain separate from these host checks.

## Compiler artifacts and cross-building

```sh
make selfhost               # experimental onyxcc_self.onx emission
make onyxcc-riscv           # clang/lld cross-build of the compiler
make onyxcc-onx             # convert the cross-built ELF using elf2onx
```

The cross-build needs the configured Clang/LLD tools and OnyxKernel's
`elf2onx`; override `ELF2ONX` for the local checkout. A self-hosted artifact
being emitted does not establish that it runs correctly or can bootstrap
another working compiler. Those are separate unfinished validation goals.

## C++ support

`.cpp` / `.cc` / `.cxx` inputs are parsed in C++ mode; `.c` inputs keep the
plain C path. The frontend is part of `onyxcc` itself (no separate binary)
and is covered by `make check` through `scripts/cpp_runner.sh`, which
compiles each `tests/cpp/*.cpp`, runs it in `tools/onx-run` and compares
stdout (negative fixtures must be rejected).

```sh
./onyxcc -I libonyxc/include/core -I libonyxc/include/io \
         -o app.onx app.cpp
```

Supported subset: classes/structs with access control, member functions,
constructors/destructors, method overloading by arity, single inheritance,
virtual functions with vtables, RAII for locals, `new`/`delete`, `this`,
`nullptr`, `T&` references, flattened namespaces, exceptions
(`throw`/`try`/`catch`, `catch(...)`, rethrow) and function templates with
explicit type arguments. Multiple/virtual inheritance, RTTI, class
templates, lambdas and unwinding destructors are not implemented; see
[docs/STATUS.md](docs/STATUS.md) for the full list.

## Layout

- `src/core`: driver, buffers, types and the freestanding shim.
- `src/front`: lexer/declarations/parser; `constant/` separates typed
  constant operands from arithmetic and conditional evaluation.
- `src/back`: preprocessing, immediate code generation and `.onx` emission.
- `src/arch`: RV64 instruction encoding and context support.
- `src/cpp/`: C++ subset frontend (`front/` parsers, `tables/` mangling,
  method and class records) — built into `onyxcc` as C99 sources.
- `libonyxc`: headers and runtime modules grouped by responsibility;
  `src/eh/eh.c` is the C++ exception runtime.
- `tests`: frontend fixtures, executed programs, driver tests, C++
  fixtures (`tests/cpp/`) and native sanitizer harnesses.

The current backend emits instructions during parsing. There is no
optimizing intermediate representation. Keep new modules focused and
follow the project's file/folder limits; existing large files require
incremental cleanup rather than assertions that all source is conforming.

## License and related projects

GPL-3.0; see [LICENSE](LICENSE). Lua retains its upstream MIT license in
OnyxApps. OnyxKernel defines the syscall and executable ABI; OnyxOS builds
images; OnyxApps hosts optional applications. The repository's existing
`OnyxCompiller` spelling is intentional.
