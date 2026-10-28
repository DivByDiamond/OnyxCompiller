/* Process helpers for Onyx's script-path shell and raw-exit-code wait ABI. */
#include "onyxc.h"
#include <signal.h>

static unsigned long script_counter;

static void script_name(char *name) {
    const char *hex = "0123456789abcdef";
    unsigned long pid = (unsigned long)_onyx_getpid();
    unsigned long serial = script_counter++;
    int i;
    memcpy(name, "/tmp/s", 6);
    for (i = 0; i < 8; ++i) name[6 + i] = hex[(pid >> (28 - 4 * i)) & 15];
    name[14] = '-';
    for (i = 0; i < 16; ++i) name[15 + i] = hex[(serial >> (60 - 4 * i)) & 15];
    name[31] = 0;
}

static int script_write(int fd, const char *data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        long n = _onyx_write(fd, data + sent, length - sent);
        if (n == -EINTR) continue;
        if (n <= 0) { errno = n < 0 ? (int)-n : EIO; return -1; }
        sent += (size_t)n;
    }
    return 0;
}

int system(const char *command) {
    char path[32];
    char *argv[3];
    long fd = -1, child, waited, ring, r;
    int attempt, status = 0, failure = 0;
    if (!command) {
        r = _onyx_access("/bin/osh", X_OK);
        if (r < 0) errno = (int)-r;
        return r == 0;
    }
    ring = _onyx_getring();
    if (ring < 0) { errno = (int)-ring; return -1; }
    if (ring > 2) { errno = EINVAL; return -1; }
    for (attempt = 0; attempt < 128; ++attempt) {
        script_name(path);
        fd = _onyx_open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) break;
        if (fd != -EEXIST) { errno = (int)-fd; return -1; }
    }
    if (fd < 0) { errno = EEXIST; return -1; }
    if (script_write((int)fd, command, strlen(command)) < 0 ||
        script_write((int)fd, "\n", 1) < 0) failure = errno;
    r = _onyx_close((int)fd);
    if (r < 0 && !failure) failure = (int)-r;
    if (!failure) {
        argv[0] = "/bin/osh";
        argv[1] = path;
        argv[2] = NULL;
        child = _onyx_spawn("/bin/osh", argv, (int)ring);
        if (child < 0) failure = (int)-child;
        else {
            do {
                waited = _onyx_waitpid((int)child, &status, 0);
                /* Current kernel returns ENOENT after parking a waiter;
                 * retry once scheduled again, as well as on interruption. */
            } while (waited == -EINTR || waited == -ENOENT);
            if (waited < 0) failure = (int)-waited;
            else if (waited != child) failure = ECHILD;
        }
    }
    r = _onyx_unlink(path);
    if (r < 0 && !failure) failure = (int)-r;
    if (failure) { errno = failure; return -1; }
    /* waitpid supplies an unencoded exit code. Signal termination is also
     * flattened to 128+signal by the kernel and cannot be distinguished. */
    return (status & 255) << 8;
}

onyx_sighandler_t signal(int number, onyx_sighandler_t handler) {
    struct sigaction action, previous;
    if (handler == SIG_ERR) { errno = EINVAL; return SIG_ERR; }
    action.sa_handler = handler;
    action.sa_mask = 0;
    action.sa_flags = 0;
    action.sa_restorer = NULL;
    if (sigaction(number, &action, &previous) < 0) return SIG_ERR;
    return previous.sa_handler;
}
