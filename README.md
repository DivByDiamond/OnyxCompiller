[![OnyxCompiller CI](https://github.com/DivByDiamond/OnyxCompiller/actions/workflows/ci.yml/badge.svg)](https://github.com/DivByDiamond/OnyxCompiller/actions/workflows/ci.yml)

<p align="center">
  <img src="https://img.shields.io/badge/platform-RISC--V%2064--bit-green" alt="RISC-V 64">
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

# OnyxCC — C/C++ → RISC-V64 → .onx compiler for OnyxOS

**Status:** v0.6 — полноценная разработка userspace-софта: автолинковка libonyxc,
function-like макросы, termios/math/assert в libc, FP-арифметика с varargs,
интеграционные тесты запуска (onx-run эмулятор), 64/64 компиляция + 10/10 runtime.
Компилятор собирается как:
  - native Linux-бинарник (для разработки вне ОС)
  - `.onx` для запуска внутри OnyxOS (self-hosting)

OnyxCC — это single-pass компилятор C (с заделом под C++) для RISC-V64,
написанный на чистом C. Цель — self-hosting: компилятор работает на
OnyxOS и умеет собирать сам себя, а также всю userspace-часть ОС.
Stage-1 уже достигнут: `make selfhost` собирает `onyxcc_self.onx`
(11 source files → один self-contained .onx бинарь, готовый к запуску
внутри OnyxOS).

Проект написан с нуля, вдохновлён архитектурой tcc (tiny C compiler):
минимум памяти, линейное время компиляции, без IR и тяжёлых
оптимизаций. Запускается на платах с 512 МБ ОЗУ.

## Возможности (v0.5)

- ✅ Лексер C99 + расширения (`__attribute__`, `__asm__`)
- ✅ Препроцессор: `#include`, `#define` (object-like), `#if/#ifdef/#ifndef/#elif/#else/#endif`, `#pragma once`, `defined()`
- ✅ Парсер C: функции, параметры, локальные переменные, массивы, указатели, struct/union/enum
- ✅ Выражения: арифметика, сравнения, логика, битовые операции, `?:`, `,`, вызовы функций, `sizeof`, casts, `&`, `*`, `++`, `--`
- ✅ Control flow: `if`/`else`, `while`, `for`, `return`, `break`/`continue` (частично)
- ✅ Codegen RISC-V64 (RV64IMA): полный набор инструкций I + M extensions, B/J-типы с label fixups
- ✅ Вызов функций по стандартному RISC-V calling convention (a0–a7, ra, sp, fp)
- ✅ Запись `.onx` v1 (344-байт заголовок + сегменты) — формат совместим с OnyxKernel `onx::load()`
- ✅ Встроенные `__ecall0..3(n, [a, [b, [c]]])` для syscalls без inline asm
- ✅ **Multi-file compilation** — `onyxcc -o prog.onx main.c util.c lib.c` (до 16 .c файлов, shared symbol table с per-file static mangling)
- ✅ **Self-hosting (stage-1)** — `make selfhost` → `onyxcc_self.onx` (компилятор компилирует свои 11 source files в один бинарь)
- ✅ `switch`/`case`/`default` (linear compare chain с поддержкой `break`)
- ✅ `goto` и labels (как backward, так и forward jumps)
- ✅ Float/double в codegen (RV64 F/D расширения: fadd/fsub/fmul/fdiv, fld/fsd, fmv.w.x/fmv.x.w, fcvt.*)
- ✅ Полные variadic args (`...`) — `va_start`/`va_arg`/`va_end` builtins
- ✅ `&&`/`||` с short-circuit семантикой
- ✅ Глобальные инициализаторы массивов/строк (включая brace-elided)
- ✅ libonyxc v0.5: `_start`, `printf`/`fprintf`/`sprintf`/`snprintf`/`sscanf`, `FILE*` buffered I/O (`fopen`/`fread`/`fwrite`/`fgets`/`fputs`/`fseek`/`ftell`/`feof`/`getline`), `errno`/`strerror`/`perror`, `time`/`gmtime`/`strftime`/`clock_gettime`/`nanosleep`, signal sets (`sigaction`/`sigprocmask`), `qsort`/`bsearch`, более 30 string functions, full ctype
- ✅ **`onyx-ld` linker** (src/tools/onyx-ld.c) — отдельный тул для линковки `.o` объектников (формат `ONYO`) и `.a` архивов (стандартный Unix `ar`) в финальный `.onx`. Поддерживает RISC-V релокации: `R_ONYO_64`/`R_ONYO_32`/`R_ONYO_HI20`/`R_ONYO_LO12_I`/`R_ONYO_LO12_S`/`R_ONYO_PCREL_HI20`/`R_ONYO_PCREL_LO12_I`/`R_ONYO_PCREL_LO12_S`/`R_ONYO_JAL`/`R_ONYO_BRANCH`.

## Что нового в v0.6

- ✅ **Автолинковка libonyxc** — `onyxcc -o prog.onx prog.c` просто работает:
  libc подхватывается автоматически (start/syscalls/stdio/stdlib/string/
  ctype/time/termios/math), include-пути регистрируются сами.
  `-nostdlib` / `-N` отключает.
- ✅ **Function-like макросы** — `#x` (stringize), `a##b` (paste),
  `__VA_ARGS__`, GNU-расширение `, ##__VA_ARGS__`.
- ✅ **Указатели на функции** — параметы, локальные/глобальные переменные,
  struct-поля, typedef'ы, касты, `va_arg(ap, int (*)(void))`.
- ✅ **Float/double полностью** — F/D-кодировки по спеке, FP-параметры в
  fa0-fa7, FP varargs (отдельная save-area), тернарники/составные
  присваивания, `double`-возврат, int↔double конверсии.
- ✅ **Критические фиксы кодгена** — фрейм (fp=верх фрейма, эпилоги
  патчатся), spill-система для lhs/rhs, порядок загрузки операндов,
  switch case-label dispatch, `~x` теперь XOR (не −1!), 8-й/16-ричные
  литералы, `[3][4]` размерности.
- ✅ **`onx-run`** (tools/onx-run.c) — эмулятор RV64IMAFD на хосте:
  запуск .onx без QEMU, трассировка (ONYXRUN_TRACE/WATCH/FRAME).
- ✅ **libonyxc v0.6** — `termios.h` (tcgetattr/tcsetattr/cfmakeraw),
  `math.h` (sqrt/pow/exp/log/sin/cos/tan/atan2/floor/ceil/fmod soft-float),
  `assert.h`, `__func__`/`__FILE__`, exit() сбрасывает stdio.
- ✅ **Интеграционные тесты** (scripts/integration-runner.sh) — 10 тестов
  компиляция+запуск+сравнение вывода: 10/10 PASS.

## Что НЕ работает (пока)

- ❌ C++ фронтенд (структура заголовков готова, парсера нет)
- ❌ Режим `onyxcc -c` (эмиссия `.o` с релокациями вместо resolved адресов) — пока не реализован в `gen.c`/`emit.c`. Линковщик `onyx-ld` готов принимать `.o` файлы, но компилятор их ещё не эмитит. **Workaround**: используйте multi-file режим `onyxcc -o prog.onx a.c b.c c.c`.
- ❌ Compound literals, designated initializers
- ❌ Inline assembly (заменено на `__ecallN` builtins)
- ❌ Запуск на реальном Milk-V Duo S (проверено только в QEMU через OnyxOS)
- ❌ C++ templates / classes / namespaces
- ❌ Оптимизации (const propagation, dead code elimination) — single-pass без IR

## Архитектура

```
OnyxCC
├── include/              # Заголовки
│   ├── core/cc.h         # Общие типы, опции, буферы
│   ├── core/compat.h     # Совместимость Linux/OnyxOS freestanding
│   ├── sys/onyxo.h       # .o объектный формат (ONYO magic, version 1)
│   ├── sys/onyx.h        # .onx формат (синхронизирован с OnyxKernel)
│   ├── sys/syscalls.h    # OnyxOS syscall ABI
│   ├── front/lexer.h    # Токены
│   ├── back/pp.h         # Препроцессор
│   ├── core/types.h      # Система типов
│   ├── front/ast.h       # AST nodes + symbol table
│   ├── front/parse.h     # Парсер top-level
│   ├── back/gen.h        # Codegen (single-pass)
│   ├── arch/riscv64.h    # Энкодеры инструкций
│   └── back/emit.h       # .onx writer
├── src/
│   ├── core/             # main.c, util.c, types.c, shim.c (freestanding libc shim)
│   ├── front/            # lexer.c, ast.c, parse.c
│   ├── back/             # pp.c, gen.c (~3400 строк), emit.c
│   ├── arch/             # riscv64.c
│   └── tools/            # onyx-ld.c — отдельный линковщик
├── libonyxc/             # libc v0.5
│   ├── include/          # onyxc.h, stdio.h, stdlib.h, string.h, ctype.h,
│   │                     # time.h, signal.h, errno.h, fcntl.h, unistd.h, limits.h
│   ├── src/
│   │   ├── core/         # start.c (_start), syscalls.c (ecall wrappers)
│   │   ├── io/           # stdio.c (FILE*/printf/scanf), stdlib.c (malloc/qsort),
│   │   │                 # string.c (40+ funcs), strerror.c, time.c
│   │   └── ctype/        # ctype.c
│   └── tests/            # libc_smoke.c (11 tests PASS) + linux_stubs.c
├── tests/                # 58+ тестовых C программ
└── Makefile              # all / hello / onyxcc-riscv / onyxcc-onx / libonyxc /
                          # selfhost-test / selfhost / test-runner
```

### Конвейер компиляции

```
input1.c input2.c ...
   │
   ▼  pp.c (per file)
preprocessed.c   (макросы раскрыты, #include вставлены)
   │
   ▼  lexer.c (per file)
token stream
   │
   ▼  parse.c + gen.c (single-pass, shared symbol table)
   │  ── g_text, g_rodata, g_data, g_bss (накапливаются между файлами)
   │  ── static symbols получают per-file mangling чтобы избежать коллизий
   │
   ▼  gen_finalize(entry_sym) — после всех файлов
resolved addresses, label fixups applied
   │
   ▼  emit.c
output.onx  (344-байт заголовок + сегменты text/rodata/data/bss)
```

Подход single-pass: во время парсинга выражений сразу генерируется
RISC-V код. AST строится только для top-level declarations и
statements; выражения идут напрямую в кодогенератор. Это даёт
линейное время компиляции и минимальное потребление памяти.

### Формат .onx

`include/onx.h` синхронизирован с
`OnyxKernel/kernel/src/proc/onx.rs::load()` и
`OnyxKernel/core/src/formats/header.rs`. Используется v1 (344-байтный
заголовок, до 8 сегментов). Подробнее — в комментарии к `onx.h`.

## Сборка

### На хост-машине (Linux/x86_64) — для разработки вне OnyxOS

```bash
cd OnyxCC
make            # собирает ./onyxcc (Linux x86_64 ELF)
make hello      # собирает tests/hello_full.onx
make test       # печатает заголовок .onx
```

### Cross-компиляция для OnyxOS — собственно `.onx` компилятор

```bash
make onyxcc-riscv    # clang-19 → onyxcc.riscv.elf (RISC-V64 ELF)
make onyxcc-onx      # elf2onx  → onyxcc.onx (OnyxOS ring-1 binary)
```

Требуется `clang-19` + `lld-19` (для RISC-V target) и `elf2onx`
из `OnyxKernel/target/release/elf2onx`. Полный pipeline:

```bash
# 1. Собрать elf2onx (один раз):
cd ../OnyxKernel && cargo build --release -p onyx_tools

# 2. Собрать onyxcc.onx:
cd ../OnyxCC && make onyxcc-onx
```

### Self-hosting (stage-1 — работает!)

Компилятор может скомпилировать сам себя (stage-1, все 11 source-файлов):

```bash
make selfhost-test   # компиляция всех исходников → /dev/null (проверка)
make selfhost        # компиляция → onyxcc_self.onx (178KB)
```

Результат — `onyxcc_self.onx`, бинарник OnyxCC, скомпилированный самим OnyxCC.
Для запуска требуется OnyxOS (QEMU или Milk-V Duo S).

## Использование

```bash
# Базовая компиляция
onyxcc -o hello.onx hello.c

# С явным entry point
onyxcc -e _start -o hello.onx hello.c

# Ring 1 (root space)
onyxcc --ring1 -o service.onx service.c

# Include path и макросы
onyxcc -I /usr/onyxc/include -DDEBUG=1 -o prog.onx prog.c
```

## Roadmap

### v0.2 — Self-hosting foundation ✅
- [x] Полная поддержка `switch`/`case`  ✅
- [x] `goto` и метки  ✅
- [x] Глобальные инициализаторы (массивы, строки, struct arrays)  ✅
- [x] Variadic arguments (для `printf`-семейства)  ✅
- [x] Self-hosting: компилятор компилирует сам себя (stage-1)  ✅
- [x] Float/double в codegen (F/D расширения RISC-V)  ✅ (riscv64.c:318-375)
- [ ] Линковка нескольких `.c` в один `.onx`
- [ ] **Milestone:** onyxcc компилирует сам себя

### v0.3 — C++ фронтенд
- [ ] Парсер C++ (классы, namespaces, перегрузка, ссылки)
- [ ] Шаблоны (минимальные)
- [ ] RAII / деструкторы
- [ ] `new`/`delete`
- [ ] **Milestone:** hello.cpp компилируется

### v0.4 — Production-ready
- [ ] Оптимизации (constant folding, dead code elimination)
- [ ] Inline expansion для маленьких функций
- [ ] Отладочная информация (DWARF или собственный формат)
- [ ]_picolibc-совместимость_
- [ ] **Milestone:** libonyxc + onyxcc собираются на OnyxOS под OnyxOS

## Интеграция с OnyxOS

`OnyxOS/Makefile` может быть расширен:

```makefile
# in OnyxOS/Makefile
ONYXCC ?= onyxcc
ONYXCC_INCLUDE ?= $(ONYXCC_DIR)/libonyxc/include

bin:
        $(ONYXCC) --ring1 -o bin/init.onx init/init.c
        $(ONYXCC) -o bin/login.onx init/login.c
        $(ONYXCC) -o bin/osh.onx init/osh.c
```

## Лицензия

GPLv3 (в соответствии с лицензией всего проекта OnyxOS).

## Связанные репозитории

- [OnyxOS](https://github.com/loki5512344/OnyxOS) — основная ОС, документация
- [OnyxBoot](https://github.com/loki5512344/OnyxBoot) — загрузчик на C++
- [OnyxKernel](https://github.com/loki5512344/OnyxKernel) — ядро на Rust, формат `.onx`