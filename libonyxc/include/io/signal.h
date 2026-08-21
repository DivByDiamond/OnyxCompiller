/*
 * signal.h — POSIX signals (libonyxc v0.5).
 *
 * Provides standard signal constants, struct sigaction, sigset_t ops,
 * and the wrappers for kill / raise / sigaction / sigprocmask / alarm.
 */
#ifndef _ONYX_SIGNAL_H
#define _ONYX_SIGNAL_H

#include <stddef.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Signal numbers — must match kernel abi.rs. */
#define SIGHUP      1
#define SIGINT      2
#define SIGQUIT     3
#define SIGILL      4
#define SIGTRAP     5
#define SIGABRT     6
#define SIGBUS      7
#define SIGFPE      8
#define SIGKILL     9
#define SIGUSR1     10
#define SIGSEGV     11
#define SIGUSR2     12
#define SIGPIPE     13
#define SIGALRM     14
#define SIGTERM     15
#define SIGCHLD     17
#define SIGCONT     18
#define SIGSTOP     19
#define SIGTSTP     20
#define NSIG        32

/* sigprocmask `how` */
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

typedef long sigset_t;

/* SIG_DFL/IGN/ERR — typed as function pointers but stored as integers.
 * We use sentinel values that the kernel recognizes. */
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;
    unsigned long sa_flags;
    void (*sa_restorer)(void);
};

#define SA_NOCLDSTOP 0x1
#define SA_NODEFER   0x2
#define SA_RESTART   0x4

/* Raw kernel syscall — declared in onyxc.h. */
long _onyx_kill(int pid, int sig);
long _onyx_sigaction(int sig, const void *act, void *oldact);
long _onyx_sigprocmask(int how, const void *set, void *oldset);
long _onyx_sigreturn(void);
long _onyx_getpid(void);

/* Inline wrappers with errno handling. */
static inline int kill(int pid, int sig) {
    long r = _onyx_kill(pid, sig);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int raise(int sig) { return kill((int)_onyx_getpid(), sig); }
static inline int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact) {
    long r = _onyx_sigaction(sig, act, oldact);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}
static inline int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    long r = _onyx_sigprocmask(how, set, oldset);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}

int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int sig);
int sigdelset(sigset_t *set, int sig);
int sigismember(const sigset_t *set, int sig);

/* alarm — uses signal SIGALRM; minimal implementation: return 0. */
unsigned int alarm(unsigned int sec);

#ifdef __cplusplus
}
#endif

#endif /* _ONYX_SIGNAL_H */
