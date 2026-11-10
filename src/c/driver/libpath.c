/*
 * driver/libpath.c — locating libonyxc and auto-linking it.
 *
 * Unless -nostdlib or -c, the driver appends the standard library sources
 * (start_cc + core libc) to the translation-unit list and registers the
 * default include paths, so `onyxcc -o prog.onx prog.c` works without any
 * manual -I or extra .c files. ONYXCC_LIB overrides the library directory.
 */
#include "core/compat.h"
#ifndef CC_FREESTANDING
#include <unistd.h>   /* readlink */
#endif

#include "core/cc.h"
#include "front/driver.h"

void driver_autolink_libonyxc(void) {
    if (g_opts.nostdlib || g_opts.compile_only) return;

    static char libdir[CC_MAX_PATH * 2];
    const char *env_lib = getenv("ONYXCC_LIB");
    if (env_lib) {
        snprintf(libdir, sizeof(libdir), "%s", env_lib);
    } else {
        /* Locate libonyxc relative to the executable:
         *   <exe-dir>/libonyxc          (repo layout: OnyxCompiller/)
         *   <exe-dir>/../libonyxc       (installed alongside)
         *   /usr/onyxc/lib              (system install) */
        char exepath[CC_MAX_PATH * 2] = {0};
        ssize_t n = -1;
#ifndef CC_FREESTANDING
        n = readlink("/proc/self/exe", exepath, sizeof(exepath) - 1);
#endif
        libdir[0] = 0;
        if (n > 0) {
            exepath[n] = 0;
            char *slash = strrchr(exepath, '/');
            if (slash) *slash = 0;
            static const char *cands[] = {
                "/libonyxc",
                "/../libonyxc",
                "/../lib/libonyxc",
            };
            for (int i = 0; i < 3 && libdir[0] == 0; i++) {
                char probe_dir[CC_MAX_PATH * 3];
                snprintf(probe_dir, sizeof(probe_dir), "%s%s/src/core/start_cc.c",
                         exepath, cands[i]);
                FILE *f = fopen(probe_dir, "rb");
                if (f) {
                    fclose(f);
                    snprintf(libdir, sizeof(libdir), "%s%s", exepath, cands[i]);
                }
            }
        }
        if (libdir[0] == 0) {
            snprintf(libdir, sizeof(libdir), "/usr/onyxc/lib");
        }
    }

    /* Sanity: start_cc.c must exist there. */
    char probe[CC_MAX_PATH * 3];
    snprintf(probe, sizeof(probe), "%s/src/core/start_cc.c", libdir);
    FILE *f = fopen(probe, "rb");
    if (!f) {
        if (g_opts.verbose) {
            fprintf(stderr, "onyxcc: libonyxc not found at %s "
                    "(set ONYXCC_LIB); skipping auto-link\n", libdir);
        }
        return;
    }
    fclose(f);

    /* Keep this list in sync with CC_LIB_SOURCE_COUNT and with the copy in
     * scripts/sepcomp-runner.sh (separate-compilation test). */
    static const char *libsrcs[] = {
        "/src/core/start_cc.c",
        "/src/core/syscalls.c",
        "/src/io/stdio.c",
        "/src/io/stdlib.c",
        "/src/memory/alloc.c",
        "/src/io/string.c",
        "/src/io/strerror.c",
        "/src/ctype/ctype.c",
        "/src/io/time.c",
        "/src/io/termios.c",
        "/src/io/math.c",
        "/src/math/ieee.c",
        "/src/locale/locale.c",
        "/src/process/runtime.c",
        "/src/format/float.c",
        "/src/convert/strtod.c",
        "/src/eh/eh.c",
    };
    static char libpaths[CC_LIB_SOURCE_COUNT][CC_MAX_PATH * 2];
    for (int i = 0; i < CC_LIB_SOURCE_COUNT; i++) {
        snprintf(libpaths[i], sizeof(libpaths[i]), "%s%s", libdir, libsrcs[i]);
        g_opts.input_files[g_opts.n_input_files++] = libpaths[i];
    }

    /* Default include paths for the library headers. */
    static char inc1[CC_MAX_PATH * 2], inc2[CC_MAX_PATH * 2], inc3[CC_MAX_PATH * 2];
    snprintf(inc1, sizeof(inc1), "%s/include/core", libdir);
    snprintf(inc2, sizeof(inc2), "%s/include/io", libdir);
    snprintf(inc3, sizeof(inc3), "%s/include/ctype", libdir);
    const char *defaults[3] = { inc1, inc2, inc3 };
    for (int i = 0; i < 3; i++) {
        bool dup = false;
        for (int j = 0; j < g_opts.n_include_paths; j++) {
            if (strcmp(g_opts.include_paths[j], defaults[i]) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup && g_opts.n_include_paths < 16) {
            g_opts.include_paths[g_opts.n_include_paths++] = defaults[i];
        }
    }
    if (g_opts.verbose) {
        fprintf(stderr, "onyxcc: auto-linking libonyxc from %s\n", libdir);
    }
}
