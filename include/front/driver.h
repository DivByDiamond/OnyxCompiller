/*
 * driver.h — internal API of the OnyxCC driver split.
 *
 * main.c owns the pipeline order; these helpers own the CLI table
 * (driver/opts.c), per-file mode plus `-c` object compilation
 * (driver/compile.c) and libonyxc discovery/auto-link (driver/libpath.c).
 */
#ifndef CC_DRIVER_H
#define CC_DRIVER_H

#include "core/compat.h"
#include "core/cc.h"

/* CLI surface (driver/opts.c). */
void driver_usage(void);
const char *driver_optstring(void);
struct option *driver_long_options(void);

/* Per-file language mode and `-c` (driver/compile.c). */
bool driver_is_cpp_source(const char *path);
int driver_compile_object(const char *input, const char *output);

/* Append libonyxc sources + include paths to g_opts unless -nostdlib or
 * -c (driver/libpath.c). */
void driver_autolink_libonyxc(void);

#endif /* CC_DRIVER_H */
