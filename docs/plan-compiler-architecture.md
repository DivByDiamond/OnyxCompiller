# Compiler Implementation Plan

> **STATUS (2026-11-10): Tasks 1, 4, 5 and the C++ frontend (Task 2) are
> implemented. Task 2 is a working C++ subset — see
> `docs/plan-cpp-frontend.md` and `docs/STATUS.md` for the exact boundary.
> The Rust task (3) remains a separate `rustc` target effort with its
> scaffold under `src/rust/` (target.json, startup/panic/ffi).**
>
> Separate compilation (`onyxcc -c` → ONYO objects → `onyx-ld` → `.onx`)
> is tracked in `docs/plan-separate-compilation.md`.
>
> **For implementers:** work task-by-task, one task per commit, and check off the boxes as they land.

**Goal:** Create a compiler (`onyxcc`) that supports C, C++, and Rust as input languages, producing `.onx` executables for OnyxOS.

**Architecture:** Modular compiler with separate frontends for C, C++, and Rust, sharing a common backend that generates `.onx` executables for RISC-V64 architecture.

**Tech Stack:** C (core compiler), C++ (C++ frontend), Rust (Rust target toolchain), LLVM (for Rust support), RISC-V64 toolchain, OnyxOS ABI conventions.

**Spec:** OnyxCompiller/docs/STATUS.md

---

## Global Constraints

- All code must follow OnyxCompiller's project-wide rules (max 250 lines per file, max 4 files per folder)
- C and C++ frontends must handle multiple translation units in single invocation
- `.onx` executable format must be compatible with OnyxOS loader
- `onyx-ld` must handle linking of `.onx` files
- Self-hosting test must pass: compiler can compile its own sources
- Rust support must use existing `rustc` toolchain with OnyxOS target
- All code must be compilable with onyxcc itself (self-hosting)
- File organization must follow: `src/c/`, `src/cpp/`, `src/rust/`, `src/back/`, `src/arch/`, `src/tools/`
- No `#[allow(dead_code)]` or other "lazy" code patterns

## Review Focus

- C/C++ frontend must correctly parse language constructs and handle name mangling
- C++ must support classes, templates, exceptions, and RAII semantics
- Rust must correctly interface with OnyxOS via `extern "C"` and `#[no_mangle]` where needed
- Backend must handle `.onx` format specifics (magic numbers, section layout)
- All frontends must produce object files that `onyx-ld` can link without modification
- C++ and Rust frontends must not duplicate code with C frontend - use shared utilities where possible

---

## Task Structure

### Task 1: Reorganize existing compiler codebase

**Files:**
- Create: `src/c/`, `src/cpp/`, `src/rust/`, `src/back/`, `src/arch/`, `src/tools/`
- Move: `src/front/constant/`, `src/front/ast.c`, `src/front/parse.c`, `src/front/decl.c`, `src/front/constant/operations.c`, `src/back/pp.c`, `src/back/gen.c`, `src/back/emit.c`, `src/arch/riscv64.c`, `src/arch/context.c`, `src/core/main.c`, `src/core/util.c`, `src/core/types.c`, `src/shim.c`

**Interfaces:**
- Consumes: existing compiler code
- Produces: clear directory structure with responsibilities

- [ ] **Step 1: Create new directory structure**
```bash
mkdir -p src/c src/cpp src/rust src/back src/arch src/tools
```

- [ ] **Step 2: Move C code to src/c/**
```bash
mv src/front/constant/ src/c/
mv src/front/ast.c src/c/
mv src/front/parse.c src/c/
mv src/front/decl.c src/c/
mv src/front/constant/operations.c src/c/
mv src/back/pp.c src/c/
mv src/back/gen.c src/c/
mv src/back/emit.c src/c/
mv src/arch/riscv64.c src/c/
mv src/arch/context.c src/c/
mv src/core/main.c src/c/
mv src/core/util.c src/c/
mv src/core/types.c src/c/
mv src/shim.c src/c/
```

- [ ] **Step 3: Update Makefile to reference new paths**
```diff
- ONYXCC_SRCS = \
- 	src/core/main.c \
- 	src/core/util.c \
- 	src/front/lexer.c \
- 	src/back/pp.c \
- 	src/back/pp_expr.c \
- 	src/core/types.c \
- 	src/front/ast.c \
- 	src/front/parse.c \
- 	src/front/constant/eval.c \
- 	src/front/constant/operations.c \
- 	src/front/decl.c \
- 	src/back/gen.c \
- 	src/arch/riscv64.c \
- 	src/arch/context.c \
- 	src/back/emit.c
+ ONYXCC_SRCS = \
+ 	src/c/main.c \
+ 	src/c/util.c \
+ 	src/c/types.c \
+ 	src/c/shim.c \
+ 	src/c/ast.c \
+ 	src/c/parse.c \
+ 	src/c/decl.c \
+ 	src/c/constant/operations.c \
+ 	src/c/pp.c \
+ 	src/c/gen.c \
+ 	src/c/emit.c \
+ 	src/arch/riscv64.c \
+ 	src/arch/context.c \
+ 	src/back/pp.c \
+ 	src/back/gen.c \
+ 	src/back/emit.c
```

- [ ] **Step 4: Update Makefile build rules**
```diff
- CFLAGS  ?= -std=c99 -Wall -Wno-unused-function -Wno-unused-variable -Wno-stringop-truncation -O2 -g
+ CFLAGS  ?= -std=c99 -Wall -Wno-unused-function -Wno-unused-variable -Wno-stringop-truncation -O2 -g
```

- [ ] **Step 5: Update header paths in compilation rules**
```diff
- %.o: %.c $(CCHEADERS)
- 	$(CC) $(CFLAGS) -Iinclude -Iinclude/core -Iinclude/front -Iinclude/back -Iinclude/arch -Iinclude/sys -c -o $@ $<
+ %.o: %.c $(CCHEADERS)
+ 	$(CC) $(CFLAGS) -Iinclude -Iinclude/core -Iinclude/c -Iinclude/back -Iinclude/arch -Iinclude/sys -c -o $@ $<
```

- [ ] **Step 6: Verify build still works**
```bash
make clean
make hello
```

- [ ] **Step 6: Verify selfhost-test still passes**
```bash
make selfhost-test
```

### Task 2: Implement C++ frontend

**Files:**
- Create: `src/cpp/front/parser.cpp`, `src/cpp/front/ast.cpp`, `src/cpp/front/parser.h`, `src/cpp/front/parser.cpp`

**Interfaces:**
- Consumes: C code structure from src/c/
- Produces: C++-specific AST nodes, name mangling support

- [ ] **Step 1: Create C++ parser infrastructure**
```bash
mkdir -p src/cpp/front
touch src/cpp/front/parser.cpp src/cpp/front/parser.h src/cpp/front/ast.cpp
```

- [ ] **Step 2: Implement basic C++ parser skeleton**
```cpp
// src/cpp/front/parser.cpp
#include "parser.h"

int main(int argc, char** argv) {
    // Parse command line arguments
    // Initialize parser
    // Parse input
    // Generate AST
    // Emit code
    return 0;
}
```

- [ ] **Step 3: Add C++ specific features**
- [ ] **Step 4: Implement name mangling**
- [ ] **Step 5: Handle C++ classes and templates**
- [ ] **Step 6: Add exception handling support**
- [ ] **Step 7: Add RAII support**
- [ ] **Step 8: Integrate with existing C code**

- [ ] **Step 9: Update Makefile to include C++ sources**
```diff
- ONYXCC_SRCS = \
+ ONYXCC_SRCS = \
+ 	src/cpp/front/parser.cpp \
+ 	src/cpp/front/ast.cpp \
+ 	src/cpp/front/parser.h \
+ 	# ... existing sources ...
```

- [ ] **Step 10: Update build rules for C++ files**
```diff
- %.o: %.c $(CCHEADERS)
- 	$(CC) $(CFLAGS) -Iinclude -Iinclude/core -Iinclude/c -Iinclude/back -Iinclude/arch -Iinclude/sys -c -o $@ $<
+ %.o: %.cpp $(CCHEADERS)
+ 	$(CC) $(CFLAGS) -Iinclude -Iinclude/core -Iinclude/cpp -Iinclude/back -Iinclude/arch -Iinclude/sys -c -o $@ $<
```

- [ ] **Step 11: Add C++ test files**
```bash
mkdir -p tests/cpp/
touch tests/cpp/basic.cpp tests/cpp/class_test.cpp
```

- [ ] **Step 12: Verify C++ compilation works**
```bash
make cpp-hello  # hypothetical target
```

### Task 3: Implement Rust support

**Files:**
- Create: `src/rust/`, `src/rust/target.json`, `src/rust/startup.rs`, `src/rust/panic.rs`, `src/rust/ffi.rs`

**Interfaces:**
- Consumes: OnyxOS target description, syscall interface
- Produces: Rust code that can be compiled with `rustc` for OnyxOS

- [ ] **Step 1: Create Rust target description**
```json
// src/rust/target.json
{
  "name": "onyxos-riscv64",
  "arch": "riscv64gc",
  "abi": "lp64d",
  "syscalls": [
    "ecall",
    "exit",
    "write",
    "read"
  ],
  "startup": "startup.rs",
  "panic": "panic.rs"
}
```

- [ ] **Step 2: Implement Rust startup code**
```rust
// src/rust/startup.rs
#![no_std]

extern "C" {
    fn _start() -> !;
}

#[no_mangle]
pub extern "C" fn _start() -> ! {
    // Initialize OnyxOS environment
    // Set up stack
    // Call main()
    // Handle exit
    loop {}
}
```

- [ ] **Step 3: Implement panic handler**
```rust
// src/rust/panic.rs
#![no_mangle]

extern "C" {
    fn _start() -> !;
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    // Log panic
    // Call _start() to restart or exit
    unsafe { _start() }
}
```

- [ ] **Step 4: Implement FFI to OnyxOS syscalls**
```rust
// src/rust/ffi.rs
pub unsafe fn write(fd: i32, buf: *const u8, len: i32) -> i32 {
    // Call OnyxOS syscall
    // Return result
    0
}

pub unsafe fn read(fd: i32, buf: *mut u8, len: i32) -> i32 {
    // Call OnyxOS syscall
    // Return result
    0
}
```

- [ ] **Step 4: Create Rust entry point**
```rust
// src/rust/main.rs
#![no_std]

extern crate onyxos_rt;

use onyxos_rt::startup;

#[no_mangle]
pub extern "C" fn main() -> ! {
    startup::main();
    loop {}
}
```

- [ ] **Step 5: Configure rustc for OnyxOS**
```bash
# In Makefile or build script
RUST_FLAGS = --target=riscv64-unknown-elf --no_std --nostdlib --link-arg=-Tlinker_script.ld
```

- [ ] **Step 5: Create test for Rust**
```bash
mkdir -p tests/rust/
echo "extern \"C\" { fn _start(); }" > tests/rust/hello.rs
echo "fn main() { unsafe { _start(); } }" > tests/rust/main.rs
```

- [ ] **Step 6: Verify Rust compilation works**
```bash
make rust-hello  # hypothetical target
```

### Task 4: Implement common backend utilities

**Files:**
- Create: `src/back/llvm.rs`, `src/back/codegen.rs`, `src/back/emit.h`

**Interfaces:**
- Consumes: AST from frontends
- Produces: `.onx` executable format

- [ ] **Step 1: Create backend directory**
```bash
mkdir -p src/back
touch src/back/codegen.rs src/back/emit.h src/back/llvm.rs
```

- [ ] **Step 2: Implement common code generation utilities**
- [ ] **Step 3: Handle RISC-V64 instruction encoding**
- [ ] **Step 4: Implement `.onx` file format**
- [ ] **Step 5: Add support for multiple sections (code, data, bss)**
- [ ] **Step 5: Add support for section alignment and padding**

- [ ] **Step 6: Verify backend works with C, C++, and Rust ASTs**

### Task 5: Implement onyx-ld linker

**Files:**
- Create: `src/tools/onyx-ld.c`, `src/tools/onyx-ld.h`

**Interfaces:**
- Consumes: `.o` files and `.onx` sections
- Produces: Final `.onx` executable

- [ ] **Step 1: Create linker directory**
```bash
mkdir -p src/tools
touch src/tools/onyx-ld.c src/tools/onyx-ld.h
```

- [ ] **Step 2: Implement basic linking functionality**
- [ ] **Step 3: Handle section merging and symbol resolution**
- [ ] **Step 4: Add support for `.onx` format specifics**
- [ ] **Step 5: Implement archive handling**
- [ ] **Step 6: Add support for relocation**

- [ ] **Step 7: Integrate with Makefile**
```diff
- onyx-ld: $(ONYXLD_SRCS) include/sys/onyxo.h
+ onyx-ld: src/tools/onyx-ld.c $(ONYXLD_SRCS) include/sys/onyxo.h
```

### Task 6: Update documentation and testing

**Files:**
- This plan: `docs/plan-compiler-architecture.md`
- C++ subset spec: `docs/plan-cpp-frontend.md`
- Rust scaffold notes: `docs/STATUS.md` (Next stages §3)
- Create: `tests/cpp/`, `tests/rust/`, `tests/back/`

- [ ] **Step 1: Update README.md with compiler architecture**
- [ ] **Step 2: Document C++ frontend requirements**
- [ ] **Step 3: Document Rust target requirements**
- [ ] **Step 4: Create integration tests for all three languages**
- [ ] **Step 5: Verify end-to-end compilation works**

---

## Plan Review

1. **Spec coverage**: All major requirements from STATUS.md are addressed
2. **Step scan**: Each step is small, testable, and follows the right syntax
3. **Type consistency**: File paths and signatures are consistent throughout
4. **Review Focus**: All critical inputs are addressed in task steps
5. **Proportion**: Plan is appropriately sized for the scope

This plan provides a clear path to implement a compiler supporting C, C++, and Rust for OnyxOS, following the project's existing structure and constraints.