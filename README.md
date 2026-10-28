[![OnyxCompiller CI](https://github.com/DivByDiamond/OnyxCompiller/actions/workflows/ci.yml/badge.svg)](https://github.com/DivByDiamond/OnyxCompiller/actions/workflows/ci.yml)

# OnyxCC

OnyxCC is a compiler written in C for a growing C99-oriented subset,
producing RISC-V64 `.onx` executables for OnyxOS. It automatically links
libonyxc and supports multiple C translation units in one invocation.

The compiler now builds the standard Lua 5.5.1 interpreter and luac port,
with emulator smoke checks covering CLI execution, math, pure Lua modules,
errors, garbage collection and bytecode roundtrips. This is a substantial
C compatibility milestone, not full C or C++ support. There is no C++
frontend yet. See [the status and remaining work](docs/STATUS.md) for
language, ABI, runtime and self-hosting limitations.

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

## Layout

- `src/core`: driver, buffers, types and the freestanding shim.
- `src/front`: lexer/declarations/parser; `constant/` separates typed
  constant operands from arithmetic and conditional evaluation.
- `src/back`: preprocessing, immediate code generation and `.onx` emission.
- `src/arch`: RV64 instruction encoding and context support.
- `libonyxc`: headers and runtime modules grouped by responsibility.
- `tests`: frontend fixtures, executed programs, driver tests and native
  sanitizer harnesses.

The current backend emits instructions during parsing. There is no
optimizing intermediate representation. Keep new modules focused and
follow the project's file/folder limits; existing large files require
incremental cleanup rather than assertions that all source is conforming.

## License and related projects

GPL-3.0; see [LICENSE](LICENSE). Lua retains its upstream MIT license in
OnyxApps. OnyxKernel defines the syscall and executable ABI; OnyxOS builds
images; OnyxApps hosts optional applications. The repository's existing
`OnyxCompiller` spelling is intentional.
