/*
 * start_cc.c — program entry point for programs compiled by OnyxCC.
 *
 * Identical contract to start.c, but written without inline assembly:
 * OnyxCC passes a0/a1 (argc/argv) as regular function parameters, so
 * declaring _start with parameters is enough. This file is the one
 * auto-linked by `onyxcc` when building userspace programs.
 *
 * Stack layout (set up by the kernel's onx/argv.rs):
 *   a0 = argc, a1 = &argv[0]
 */
#include "onyxc.h"

extern int main(int argc, char **argv);

char **__onyx_argv = 0;
char **environ    = 0;

/* argc in a0, argv in a1 — parameters pick up the registers directly. */
void _start(long argc, char **argv) {
    /* If argc is 0 (old v0.3 kernel contract), synthesize a minimal
     * argv[0] so libc consumers don't crash. */
    static char progname[] = "onyx-program";
    static char *default_argv[2] = { 0, 0 };
    if (argc == 0 || argv == 0) {
        default_argv[0] = progname;
        argc = 1;
        argv = default_argv;
    }

    __onyx_argv = argv;
    /* environ lives just past argv's NULL terminator. */
    environ = &argv[argc + 1];

    int ret = main((int)argc, argv);
    exit(ret);
}
