/*
 * strerror.c — translate errno codes to strings (libonyxc v0.5).
 *
 * Also implements perror() and the sigset ops.
 */
#include "onyxc.h"

char *strerror(int err) {
    static char buf[32];
    switch (err) {
        case 0:           return "Success";
        case EPERM:       return "Operation not permitted";
        case ENOENT:      return "No such file or directory";
        case ESRCH:       return "No such process";
        case EINTR:       return "Interrupted system call";
        case EIO:         return "Input/output error";
        case ENXIO:       return "No such device or address";
        case E2BIG:       return "Argument list too long";
        case ENOEXEC:     return "Exec format error";
        case EBADF:       return "Bad file descriptor";
        case ECHILD:      return "No child processes";
        case EAGAIN:      return "Resource temporarily unavailable";
        case ENOMEM:      return "Cannot allocate memory";
        case EACCES:      return "Permission denied";
        case EFAULT:      return "Bad address";
        case EBUSY:       return "Device or resource busy";
        case EEXIST:      return "File exists";
        case EXDEV:       return "Invalid cross-device link";
        case ENODEV:      return "No such device";
        case ENOTDIR:     return "Not a directory";
        case EISDIR:      return "Is a directory";
        case EINVAL:      return "Invalid argument";
        case ENFILE:      return "Too many open files in system";
        case EMFILE:      return "Too many open files";
        case ENOTTY:      return "Inappropriate ioctl for device";
        case ETXTBSY:     return "Text file busy";
        case EFBIG:       return "File too large";
        case ENOSPC:      return "No space left on device";
        case ESPIPE:      return "Illegal seek";
        case EROFS:       return "Read-only file system";
        case EMLINK:      return "Too many links";
        case EPIPE:       return "Broken pipe";
        case EDOM:        return "Numerical argument out of domain";
        case ERANGE:      return "Numerical result out of range";
        case EDEADLK:     return "Resource deadlock avoided";
        case ENAMETOOLONG: return "File name too long";
        case ENOSYS:      return "Function not implemented";
        case ENOTEMPTY:   return "Directory not empty";
        case ELOOP:       return "Too many levels of symbolic links";
        case ENOTSUP:     return "Operation not supported";
        default:
            snprintf(buf, sizeof(buf), "Unknown error %d", err);
            return buf;
    }
}

void perror(const char *s) {
    if (s && *s) {
        fprintf(stderr, "%s: ", s);
    }
    fprintf(stderr, "%s\n", strerror(errno));
}

/* ── sigset_t operations ───────────────────────────────────────────── */
int sigemptyset(sigset_t *set) {
    if (!set) { errno = EFAULT; return -1; }
    *set = 0;
    return 0;
}

int sigfillset(sigset_t *set) {
    if (!set) { errno = EFAULT; return -1; }
    *set = (sigset_t)-1;
    return 0;
}

int sigaddset(sigset_t *set, int sig) {
    if (!set || sig <= 0 || sig >= 32) { errno = EINVAL; return -1; }
    *set |= (sigset_t)1 << sig;
    return 0;
}

int sigdelset(sigset_t *set, int sig) {
    if (!set || sig <= 0 || sig >= 32) { errno = EINVAL; return -1; }
    *set &= ~((sigset_t)1 << sig);
    return 0;
}

int sigismember(const sigset_t *set, int sig) {
    if (!set || sig <= 0 || sig >= 32) { errno = EINVAL; return -1; }
    return (*set & ((sigset_t)1 << sig)) ? 1 : 0;
}
