/* Separate-compilation fixture: global, static and .bss symbols.
 * Compiled on its own with `onyxcc -N -c` and linked with main.c. */

/* Regression: a prototype that is never called must NOT be exported as an
 * undefined symbol by `-c` (the linker rejects objects with undefined
 * symbols, even unused ones). */
extern int sep_never_defined(void);

int sep_add(int a, int b) { return a + b; }

static int sep_local(int x) { return x * 2; }

int sep_double(int x) { return sep_local(x); }

int sep_global = 40;

int sep_bss;

static int sep_seed = 2;

int sep_seed_get(void) { return sep_seed; }
