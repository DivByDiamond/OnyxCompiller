<p align="center">
  <img src="https://img.shields.io/badge/platform-RISC--V%2064--bit-green" alt="RISC-V 64">
  <img src="https://img.shields.io/badge/MMU-Sv39-yellow" alt="Sv39 MMU">
  <img src="https://img.shields.io/badge/license-GPL--3.0-red" alt="GPL-3.0">
</p>

<h1 align="center">OnyxKernel</h1>

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
</pre>
</p>

# OnyxCC — C/C++ → RISC-V64 → .onx compiler for OnyxOS

**Status:** MVP — компилирует runnable hello-world `.onx` для OnyxOS.
Доступны две сборки компилятора: native Linux-бинарник (для разработки
вне ОС) и `.onx` для запуска внутри OnyxOS.

OnyxCC — это single-pass компилятор C (с заделом под C++) для RISC-V64,
написанный на чистом C. Цель — self-hosting: компилятор работает на
OnyxOS и умеет собирать сам себя, а также всю userspace-часть ОС.

Проект написан с нуля, вдохновлён архитектурой tcc (tiny C compiler):
минимум памяти, линейное время компиляции, без IR и тяжёлых
оптимизаций. Запускается на платах с 512 МБ ОЗУ.

## Возможности (MVP)

- ✅ Лексер C99 + расширения (`__attribute__`, `__asm__`)
- ✅ Препроцессор: `#include`, `#define` (object-like), `#if/#ifdef/#ifndef/#elif/#else/#endif`, `#pragma once`, `defined()`
- ✅ Парсер C: функции, параметры, локальные переменные, массивы, указатели, struct/union/enum
- ✅ Выражения: арифметика, сравнения, логика, битовые операции, `?:`, `,`, вызовы функций, `sizeof`, casts, `&`, `*`, `++`, `--`
- ✅ Control flow: `if`/`else`, `while`, `for`, `return`, `break`/`continue` (частично)
- ✅ Codegen RISC-V64 (RV64IMA): полный набор инструкций I + M extensions, B/J-типы с label fixups
- ✅ Вызов функций по стандартному RISC-V calling convention (a0–a7, ra, sp, fp)
- ✅ Запись `.onx` v1 (344-байт заголовок + сегменты) — формат совместим с OnyxKernel `onx::load()`
- ✅ Встроенные `__ecall0..3(n, [a, [b, [c]]])` для syscalls без inline asm
- ✅ libonyxc: `_start`, `printf`, `puts`, `malloc`/`free`, `string`, `ctype`, syscall wrappers

## Что НЕ работает (пока)

- ❌ C++ фронтенд (структура заголовков готова, парсера нет)
- ✅ Полная variadic arguments (`...`) — `va_start` / `va_arg` / `va_end` builtins,
  a0–a7 save area в прологе variadic-функции
- ❌ Линковка нескольких `.c` файлов в один `.onx` (MVP — один translation unit)
- ❌ Float/double в codegen (типы есть, инструкции F/D не эмитятся)
- ✅ `switch`/`case`/`default` — linear compare chain, `break` поддержан
- ✅ `goto` и labels — как backward, так и forward jumps
- ✅ `&&`/`||` с short-circuit семантикой (исправлен баг, когда RHS парсился
  дважды и ломал поток токенов)
- ❌ Глобальные инициализаторы с массивами/строками (только скаляры)
- ❌ Compound literals, designated initializers
- ❌ Inline assembly (заменено на `__ecallN` builtins)
- ❌ Self-hosting: компилятор пока что не может скомпилировать сам себя
  (требует доработки парсера — не хватает нескольких C-конструкций)

## Архитектура

```
OnyxCC
├── include/              # Заголовки
│   ├── cc.h              # Общие типы, опции, буферы
│   ├── onx.h             # .onx формат (синхронизирован с OnyxKernel)
│   ├── syscalls.h        # OnyxOS syscall ABI
│   ├── lexer.h           # Токены
│   ├── pp.h              # Препроцессор
│   ├── types.h           # Система типов
│   ├── ast.h             # AST nodes + symbol table
│   ├── parse.h           # Парсер top-level
│   ├── gen.h             # Codegen (single-pass)
│   ├── riscv64.h         # Энкодеры инструкций
│   └── emit.h            # .onx writer
├── src/                  # Реализация компилятора (~2500 строк C)
│   ├── main.c            # CLI driver
│   ├── util.c            # Диагностика, арена, буферы, string pool
│   ├── lexer.c           # Лексер
│   ├── pp.c              # Препроцессор
│   ├── types.c           # Типы
│   ├── ast.c             # AST + symbol table
│   ├── parse.c           # Top-level parser
│   ├── gen.c             # Парсер выражений/стейтментов + codegen
│   ├── riscv64.c         # Кодирование инструкций
│   └── emit.c            # .onx v1 emitter
├── libonyxc/             # Минимальная libc (как musl, но компактнее)
│   ├── include/          # stdio.h, stdlib.h, string.h, ctype.h, onyxc.h
│   └── src/
│       ├── start.c       # _start → main → exit
│       ├── syscalls.c    # Обёртки над ecall
│       ├── stdio.c       # printf/puts/putchar
│       ├── stdlib.c      # malloc/sbrk/exit/atoi/strtol
│       ├── string.c      # strlen/strcpy/strcmp/...
│       └── ctype.c       # isalpha/isdigit/...
├── tests/
│   ├── hello.c           # Hello world с printf
│   └── hello_full.c      # Self-contained: _start + syscalls + main
└── Makefile
```

### Конвейер компиляции

```
input.c
   │
   ▼  pp.c
preprocessed.c   (макросы раскрыты, #include вставлены)
   │
   ▼  lexer.c
token stream
   │
   ▼  parse.c + gen.c (single-pass)
g_text / g_rodata / g_data / g_bss   (машинный код + сегменты)
   │
   ▼  emit.c
hello.onx   (готов к запуску в OnyxKernel)
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

### На OnyxOS (self-hosting, TODO)

Когда компилятор станет полностью self-hosting:

```bash
onyxcc -I /usr/onyxc/include -o program.onx program.c
```

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

### v0.2 — Self-hosting foundation
- [ ] Полная поддержка `switch`/`case`
- [ ] `goto` и метки
- [ ] Полноценные глобальные инициализаторы (массивы, строки)
- [ ] Variadic arguments (для `printf`-семейства)
- [ ] Float/double в codegen (F/D расширения RISC-V)
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
