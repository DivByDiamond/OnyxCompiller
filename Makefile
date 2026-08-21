# OnyxCC — C/C++ → RISC-V64 → .onx compiler for OnyxOS
#
# Build targets:
#   make                 # build host onyxcc (Linux/x86_64)
#   make hello           # compile tests/hello_full.c → tests/hello_full.onx
#   make onyxcc-riscv    # cross-compile onyxcc → onyxcc.riscv.elf (for OnyxOS)
#   make onyxcc-onx      # convert onyxcc.riscv.elf → onyxcc.onx
#   make all-targets     # all of the above
#   make libonyxc        # build libonyxc (host verification)
#   make selfhost-test   # compile onyxcc sources with onyxcc (stage-1 verification)
#   make selfhost        # full onyxcc self-build → onyxcc_self.onx (experimental)
#   make test            # compile and dump hello.onx header
#   make clean
#
# Host toolchain: any C compiler (gcc/clang) on Linux.
# Cross toolchain: clang-19 + lld (auto-detected).

CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wno-unused-function -Wno-unused-variable -Wno-stringop-truncation -O2 -g
LDFLAGS ?=

ONYXCC_SRCS = \
    src/core/main.c \
    src/core/util.c \
    src/front/lexer.c \
    src/back/pp.c \
    src/core/types.c \
    src/front/ast.c \
    src/front/parse.c \
    src/back/gen.c \
    src/arch/riscv64.c \
    src/back/emit.c

ONYXCC_OBJS = $(ONYXCC_SRCS:.c=.o)

ONYXCC = onyxcc
LIBONYXC = libonyxc/libonyxc.a
ONYXLD = onyx-ld

# Cross-compilation toolchain for OnyxOS build.
CLANG        ?= clang-19
LLD          ?= ld.lld-19
ELF2ONX      ?= /home/z/my-project/onyx/OnyxKernel/target/release/elf2onx
RISCV_FLAGS  = --target=riscv64-unknown-elf -march=rv64gc -mabi=lp64d -mcmodel=medany \
	-Os -ffunction-sections -fdata-sections \
	-ffreestanding -nostdlib -fno-builtin -Iinclude -Wall -Wno-unused-function \
	-Wno-unused-variable -Wno-unused-but-set-variable -Wno-incompatible-pointer-types

SELFHOST_FLAGS = -DCC_FREESTANDING -I include -I include/core -I include/front -I include/back -I include/arch -I include/sys

.PHONY: all clean test libonyxc hello onyxcc-riscv onyxcc-onx all-targets selfhost-test selfhost test-runner onyx-ld

all: $(ONYXCC) $(ONYXLD)

# ── onyx-ld — OnyxOS linker ──────────────────────────────────────────
# Reads .o object files (and .a archives) and produces a .onx binary.
# Compiled natively on Linux/x86_64 for use as a host-side tool.
ONYXLD_SRCS = src/tools/onyx-ld.c

$(ONYXLD): $(ONYXLD_SRCS) include/sys/onyxo.h
	$(CC) $(CFLAGS) -Iinclude -Iinclude/sys -o $@ $(ONYXLD_SRCS) $(LDFLAGS)
	@ls -la $@
	@echo "--- onyx-ld ready"

$(ONYXCC): $(ONYXCC_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -Iinclude -Iinclude/core -Iinclude/front -Iinclude/back -Iinclude/arch -Iinclude/sys -c -o $@ $<

# Cross-compile onyxcc itself to RISC-V64 ELF (for OnyxOS).
# Uses clang-19 + lld with a freestanding libc shim (src/shim.c).
onyxcc-riscv: onyxcc.riscv.elf

onyxcc.riscv.elf: $(ONYXCC_SRCS) src/core/shim.c linker_onyx.ld
	$(CLANG) $(RISCV_FLAGS) \
	-Wl,-T,linker_onyx.ld -Wl,--gc-sections -Wl,--strip-all -Wl,-n \
	$(ONYXCC_SRCS) src/core/shim.c -o $@
	@ls -la $@
	@echo "--- onyxcc.riscv.elf ready. Convert to .onx with: make onyxcc-onx"

# Convert onyxcc.riscv.elf → onyxcc.onx using OnyxKernel's elf2onx tool.
onyxcc-onx: onyxcc.riscv.elf
	@if [ ! -x "$(ELF2ONX)" ]; then \
	echo "Error: elf2onx not found at $(ELF2ONX)"; \
	echo "Build it: cd ../OnyxKernel && cargo build --release -p onyx_tools"; \
	exit 1; \
	fi
	$(ELF2ONX) --ring=1 onyxcc.riscv.elf onyxcc.onx
	@ls -la onyxcc.onx
	@echo "--- onyxcc.onx ready — drop into OnyxFS image to run on OnyxOS"

# Compile libonyxc as a host-side static archive (verification only).
libonyxc:
	$(MAKE) -C libonyxc CC=$(CC) CFLAGS="$(CFLAGS)"

# Build hello.onx using our onyxcc.
hello: $(ONYXCC)
	./$(ONYXCC) -v -I libonyxc/include/core -I libonyxc/include/io -I libonyxc/include/ctype -o tests/hello_full.onx tests/hello_full.c

# Dump .onx header for inspection.
test: hello
	@echo "--- hello_full.onx header ---"
	@od -A x -t x1z -v -N 64 tests/hello_full.onx
	@echo "--- file size ---"
	@wc -c tests/hello_full.onx

all-targets: $(ONYXCC) hello onyxcc-onx
	@echo "All targets built."
	@echo "  Linux binary:   $(ONYXCC)"
	@echo "  RISC-V ELF:     onyxcc.riscv.elf"
	@echo "  OnyxOS binary:  onyxcc.onx"
	@echo "  Test program:   tests/hello_full.onx"

# Self-hosting test: compile onyxcc's own sources using onyxcc itself.
# This verifies the compiler can compile its own code (stage-1).
selfhost-test: $(ONYXCC)
	@echo "=== Self-hosting test: compiling onyxcc sources with onyxcc ==="
	./$(ONYXCC) $(SELFHOST_FLAGS) -o /dev/null $(ONYXCC_SRCS)
	@echo "=== All source files compiled successfully ==="

# Full self-hosting build (stage-1): produces an .onx binary.
selfhost: $(ONYXCC)
	./$(ONYXCC) $(SELFHOST_FLAGS) -o onyxcc_self.onx -e _start $(ONYXCC_SRCS) src/core/shim.c
	@echo "--- onyxcc_self.onx ready ---"
	@ls -la onyxcc_self.onx

test-runner: $(ONYXCC)
	@bash scripts/test_runner.sh

clean:
	rm -f $(ONYXCC) $(ONYXCC_OBJS) src/core/*.o src/front/*.o src/back/*.o src/arch/*.o tests/*.onx onyxcc.riscv.elf onyxcc.onx onyxcc_self.onx
	rm -rf cc/
