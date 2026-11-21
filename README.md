<p align="center">
<pre class="not-prose" style="text-align:center;font-family:monospace;">
    ███████
  ███▒▒▒▒▒███
 ███     ▒▒███ ████████   █████ ████ █████ █████
▒███      ▒███▒▒▒███▒▒▒███ ▒▒███ ▒███ ▒▒███ ▒▒███
▒███      ▒███ ▒███ ▒███  ▒███ ▒███  ▒▒▒█████▒
▒▒███     ███  ▒███ ▒███  ▒███ ▒███   ███▒▒▒███
 ▒▒▒███████▒   ████ █████ ▒▒███████  █████ █████
   ▒▒▒▒▒▒▒    ▒▒▒▒ ▒▒▒▒▒   ▒▒▒▒▒███ ▒▒▒▒▒ ▒▒▒▒▒
                           ███ ▒███
                          ▒▒██████
                           ▒▒▒▒▒▒
      ███████                                           ████████        ████████
    ███▒▒▒▒▒███                                       ███▒▒▒▒▒▒███    ███▒▒▒▒▒▒███
   ███     ▒▒███  ████████   █████ ████  █████ █████ ███             ███
  ▒███      ▒███ ▒▒███▒▒███  ▒███ ▒███   ▒███ ▒▒███  ███             ███
  ▒███      ▒███  ▒███ ▒███  ▒███ ▒███   ▒▒▒█████▒   ███             ███
  ▒▒███     ███   ▒███ ▒███  ▒███ ▒███    ███▒▒▒███   ███             ███
   ▒▒▒███████▒    ████ █████ ▒▒███████   █████ █████  ███▒▒▒▒▒▒███▒   ███▒▒▒▒▒▒███▒
     ▒▒▒▒▒▒▒      ▒▒▒▒ ▒▒▒▒▒  ▒▒▒▒▒███   ▒▒▒▒ ▒▒▒▒▒    ▒▒▒▒▒▒▒▒▒▒      ▒▒▒▒▒▒▒▒▒▒
</pre>
</p>

<p align="center"><em>Compiler onyxcc and linker onyx-ld: C99 (and a C++ subset) in, RISC-V64 .onx executables out</em></p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-RISC--V%2064--bit-green" alt="RISC-V 64-bit">
  <img src="https://img.shields.io/badge/language-C99-orange" alt="C99">
  <img src="https://img.shields.io/badge/target-OnyxOS-blue" alt="OnyxOS">
  <img src="https://img.shields.io/badge/license-GPL--3.0-red" alt="GPL-3.0">
</p>

----

OnyxCC is a compiler written in C99 for a growing C99-oriented subset. It
produces RISC-V64 `.onx` executables for
[OnyxOS](https://github.com/DivByDiamond/OnyxOS); OnyxKernel defines the
syscall and executable ABI that the output depends on. The compiler links
libonyxc automatically and accepts several C translation units in one
invocation.

A C++ subset frontend is built in: classes, methods, RAII, virtual
functions, namespaces, references, `new`/`delete`, exceptions and function
templates. See [the status and remaining work](docs/STATUS.md) for the
exact boundary; this is not full C++ conformance. There is no Rust
frontend. Rust remains a separate `rustc`-based target effort.

----

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

`-N` disables automatic libc linkage. Ordinary applications use the
default ring; `--ring1` is reserved for services that need the privilege.
The total input capacity includes libc translation units; driver tests
check the configured bounds. `ONYXCC_LIB` selects the libonyxc directory
when the compiler is installed away from its source tree.

`onyx-ld` builds with the compiler and reads private ONYO objects and
archives. `onyxcc -c` emits those objects, one translation unit per
invocation; see the Separate compilation section below.

----

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

The 2026-11-15 baseline passed 66 frontend, 48 runtime, 5 driver, 15 C++,
5 separate-compilation and seven native sanitizer suites
([docs/STATUS.md](docs/STATUS.md)).

For the sibling Lua ports:

```sh
make -C ../OnyxApps lua luac ONYXCC="$PWD/onyxcc"
python3 ../OnyxApps/apps/lua/tests/smoke.py --runner tools/onx-run \
  --lua ../OnyxApps/build/lua.onx --luac ../OnyxApps/build/luac.onx
```

OnyxApps verifies a pinned vendored archive and builds offline. It is not
needed for the compiler's default CI checks. QEMU, OC2R and other target
validation remain separate from these host checks.

----

## Compiler artifacts and cross-building

```sh
make selfhost               # experimental onyxcc_self.onx emission
make onyxcc-riscv           # clang/lld cross-build of the compiler
make onyxcc-onx             # convert the cross-built ELF using elf2onx
```

The cross-build needs the configured Clang/LLD tools and OnyxKernel's
`elf2onx`; override `ELF2ONX` for the local checkout. A self-hosted
artifact being emitted does not establish that it runs correctly or can
bootstrap another working compiler. Those are separate unfinished
validation goals.

----

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

----

## Separate compilation

`onyxcc -c` emits a relocatable ONYO object per translation unit and
`onyx-ld` links objects (and static archives) into a `.onx`:

```sh
./onyxcc -c -I libonyxc/include/core -o main.o main.c
./onyxcc -c -I libonyxc/include/core -o util.o util.c
./onyx-ld -o prog.onx -e _start main.o util.o
```

`-c` takes exactly one input file, does not auto-link libonyxc, and writes
`a.o` by default. The pipeline is covered by `make check`
(`scripts/sepcomp-runner.sh`, fixtures in `tests/separate/`); the object
format and relocation contract are documented in
[docs/plan-separate-compilation.md](docs/plan-separate-compilation.md).
Current limits: one translation unit per `-c` invocation, linear
archive extraction (no symbol index), `-e` must name a GLOBAL symbol.

----

## Layout

- `src/core`: driver, buffers, types and the freestanding shim.
- `src/front`: lexer/declarations/parser; `constant/` separates typed
  constant operands from arithmetic and conditional evaluation.
- `src/back`: preprocessing, immediate code generation and `.onx` emission.
- `src/arch`: RV64 instruction encoding and context support.
- `src/cpp/`: C++ subset frontend (`front/` parsers, `tables/` mangling,
  method and class records), built into `onyxcc` as C99 sources.
- `libonyxc`: headers and runtime modules grouped by responsibility;
  `src/eh/eh.c` is the C++ exception runtime.
- `tests`: frontend fixtures, executed programs, driver tests, C++
  fixtures (`tests/cpp/`) and native sanitizer harnesses.

The current backend emits instructions during parsing. There is no
optimizing intermediate representation. Keep new modules focused and
follow the project's file/folder limits; existing large files require
incremental cleanup rather than assertions that all source is conforming.

----

## Ecosystem

| Repository | Role | Language |
|---|---|---|
| [OnyxOS](https://github.com/DivByDiamond/OnyxOS) | Ecosystem: build, disk image, docs | Bash / Python |
| [OnyxKernel](https://github.com/DivByDiamond/OnyxKernel) | RISC-V 64 kernel (Sv39, 3 rings) | Rust |
| [OnyxBoot](https://github.com/DivByDiamond/OnyxBoot) | Bootloader (M-mode, FDT) | C++20 |
| [OnyxShell](https://github.com/DivByDiamond/OnyxShell) | User shell `osh` | Rust |
| [OnyxCompiller](https://github.com/DivByDiamond/OnyxCompiller) | C99 compiler `onyxcc` + linker | C99 |
| [OnyxApps](https://github.com/DivByDiamond/OnyxApps) | Userland applications | C99 |
| [OnyxDesktop](https://github.com/DivByDiamond/OnyxDesktop) | Desktop environment `onyxdesk` | C99 |

----

## License

GPL-3.0; see [LICENSE](LICENSE). Lua retains its upstream MIT license in
OnyxApps. The repository's existing `OnyxCompiller` spelling is intentional.

----

## Русская версия

OnyxCC: компилятор, написанный на C99 для расширяющегося подмножества
C99. Он создаёт исполняемые файлы `.onx` для RISC-V64 и
[OnyxOS](https://github.com/DivByDiamond/OnyxOS); системные вызовы и ABI
исполняемых файлов, от которых зависит вывод компилятора, задаёт
OnyxKernel. Компилятор линкует libonyxc автоматически и принимает
несколько единиц трансляции C за один вызов.

Встроен фронтенд подмножества C++: классы, методы, RAII, виртуальные
функции, пространства имён, ссылки, `new`/`delete`, исключения и шаблоны
функций. Точная граница возможностей описана в
[docs/STATUS.md](docs/STATUS.md); полной совместимости с C++ нет.
Фронтенда Rust нет, Rust остаётся отдельной задачей поверх `rustc`.

----

## Сборка и использование

Хосту на Linux нужны компилятор C, Make и стандартные инструменты
разработки:

```sh
make
./onyxcc -o hello.onx hello.c
./onyxcc -o application.onx main.c helper.c
```

Типовые опции:

```sh
./onyxcc -I include -DDEBUG=1 -o application.onx main.c
./onyxcc -N -e _start -o freestanding.onx startup.c
./onyxcc --ring1 -o service.onx service.c
./onyxcc -fsyntax-only -N source.c
```

`-N` отключает автоматическую линковку libc. Обычные приложения собираются
в кольце по умолчанию, `--ring1` зарезервирована для сервисов, которым
нужны такие привилегии. Суммарный лимит входных данных включает единицы
трансляции libc; фактические границы проверяются тестами драйвера.
Переменная `ONYXCC_LIB` выбирает каталог libonyxc, когда компилятор
установлен отдельно от дерева исходников.

`onyx-ld` собирается вместе с компилятором и читает приватные объекты и
архивы ONYO. `onyxcc -c` выдаёт такие объекты, по одной единице
трансляции за вызов; см. раздел про раздельную компиляцию ниже.

----

## Поведение проверок

```sh
make check                  # приёмка фронтенда, исполняемые фикстуры, границы драйвера
make check-native           # выбранные пути libc/arena под host ASan/UBSan
python3 tests/driver/run.py --sanitize
make selfhost-test          # исходники компилятора принимаются; результат не запускается
```

`make check` при необходимости собирает `onyxcc`, `onyx-ld` и
`tools/onx-run` из их исходников. CI принудительно пересобирает
хост-инструменты: отслеживаемые готовые бинарники не могут валидировать
текущие исходники. Исполняемые фикстуры проверяют код возврата и ожидаемый
вывод. Хост-эмулятор запускает один процесс и транслирует системные
вызовы; это не эмулятор ядра или MMU.

Базлайн от 2026-11-15: 66 наборов фронтенда, 48 исполняемых, 5 драйверных,
15 для C++, 5 для раздельной компиляции и семь наборов нативных
санитайзеров ([docs/STATUS.md](docs/STATUS.md)).

Для соседних портов Lua:

```sh
make -C ../OnyxApps lua luac ONYXCC="$PWD/onyxcc"
python3 ../OnyxApps/apps/lua/tests/smoke.py --runner tools/onx-run \
  --lua ../OnyxApps/build/lua.onx --luac ../OnyxApps/build/luac.onx
```

OnyxApps проверяет вендорский архив с закреплённой контрольной суммой и
собирается офлайн. Для стандартных CI-проверок компилятора это не
требуется. Проверки QEMU, OC2R и остальных целей остаются отдельными от
этих хост-проверок.

----

## Артефакты компилятора и кросс-сборка

```sh
make selfhost               # экспериментальная генерация onyxcc_self.onx
make onyxcc-riscv           # кросс-сборка компилятора через clang/lld
make onyxcc-onx             # конвертация кросс-собранного ELF через elf2onx
```

Для кросс-сборки нужны настроенные инструменты Clang/LLD и `elf2onx` из
OnyxKernel; путь к локальному чекауту переопределяется через `ELF2ONX`. То,
что самохостный артефакт сгенерирован, не доказывает, что он корректно
работает или способен бутстрапнуть другой рабочий компилятор. Это отдельные
незавершённые цели валидации.

----

## Поддержка C++

Входы `.cpp` / `.cc` / `.cxx` парсятся в режиме C++, входы `.c` идут по
обычному пути C. Фронтенд встроен в сам `onyxcc` (отдельного бинарника
нет) и покрывается `make check` через `scripts/cpp_runner.sh`: скрипт
компилирует каждый файл из `tests/cpp/*.cpp`, запускает его в
`tools/onx-run` и сравнивает stdout (негативные фикстуры обязаны
отвергаться).

```sh
./onyxcc -I libonyxc/include/core -I libonyxc/include/io \
         -o app.onx app.cpp
```

Поддерживаемое подмножество: классы и структуры с контролем доступа,
функции-члены, конструкторы и деструкторы, перегрузка методов по числу
аргументов, одиночное наследование, виртуальные функции с vtable, RAII
для локальных переменных, `new`/`delete`, `this`, `nullptr`, ссылки `T&`,
уплощённые пространства имён, исключения (`throw`/`try`/`catch`,
`catch(...)`, повторный throw) и шаблоны функций с явными
типами-аргументами. Множественное и виртуальное наследование, RTTI,
шаблоны классов, лямбды и вызов деструкторов при раскрутке стека не
реализованы; полный список в [docs/STATUS.md](docs/STATUS.md).

----

## Раздельная компиляция

`onyxcc -c` выдаёт перемещаемый объект ONYO на каждую единицу трансляции,
а `onyx-ld` линкует объекты (и статические архивы) в `.onx`:

```sh
./onyxcc -c -I libonyxc/include/core -o main.o main.c
./onyxcc -c -I libonyxc/include/core -o util.o util.c
./onyx-ld -o prog.onx -e _start main.o util.o
```

`-c` принимает ровно один входной файл, не линкует libonyxc автоматически
и по умолчанию пишет `a.o`. Конвейер покрывается `make check`
(`scripts/sepcomp-runner.sh`, фикстуры в `tests/separate/`); формат
объектов и контракт релокаций описаны в
[docs/plan-separate-compilation.md](docs/plan-separate-compilation.md).
Текущие ограничения: одна единица трансляции за вызов `-c`, линейное
извлечение из архива (без индекса символов), `-e` обязана
указывать на GLOBAL-символ.

----

## Структура

- `src/core`: драйвер, буферы, типы и freestanding-заглушка.
- `src/front`: лексер/декларации/парсер; `constant/` отделяет типизированные
  константные операнды от арифметики и условных вычислений.
- `src/back`: препроцессинг, непосредственная генерация кода и эмиссия `.onx`.
- `src/arch`: кодирование инструкций RV64 и поддержка контекста.
- `src/cpp/`: фронтенд подмножества C++ (`front/` парсеры, `tables/`
  манглинг, записи методов и классов), встроен в `onyxcc` как C99-код.
- `libonyxc`: заголовки и модули рантайма, сгруппированные по
  ответственности; `src/eh/eh.c` это рантайм исключений C++.
- `tests`: фикстуры фронтенда, исполняемые программы, тесты драйвера,
  фикстуры C++ (`tests/cpp/`) и нативные санитайзер-харнесы.

Текущий бэкенд выдаёт инструкции во время парсинга. Оптимизирующего
промежуточного представления нет. Новые модули держите сфокусированными и
соблюдайте файловые лимиты проекта; существующие большие файлы требуют
постепенной чистки, а не заявлений о конформности всего исходника.

----

## Экосистема

| Repository | Role | Language |
|---|---|---|
| [OnyxOS](https://github.com/DivByDiamond/OnyxOS) | Ecosystem: build, disk image, docs | Bash / Python |
| [OnyxKernel](https://github.com/DivByDiamond/OnyxKernel) | RISC-V 64 kernel (Sv39, 3 rings) | Rust |
| [OnyxBoot](https://github.com/DivByDiamond/OnyxBoot) | Bootloader (M-mode, FDT) | C++20 |
| [OnyxShell](https://github.com/DivByDiamond/OnyxShell) | User shell `osh` | Rust |
| [OnyxCompiller](https://github.com/DivByDiamond/OnyxCompiller) | C99 compiler `onyxcc` + linker | C99 |
| [OnyxApps](https://github.com/DivByDiamond/OnyxApps) | Userland applications | C99 |
| [OnyxDesktop](https://github.com/DivByDiamond/OnyxDesktop) | Desktop environment `onyxdesk` | C99 |

----

## Лицензия

GPL-3.0, см. [LICENSE](LICENSE). Lua сохраняет исходную лицензию MIT в
OnyxApps. Написание `OnyxCompiller` с двумя `l` сохранено намеренно.
