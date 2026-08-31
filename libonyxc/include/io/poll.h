/*
 * poll.h — poll(2) readiness multiplexer (libonyxc v0.5).
 *
 * Kernel counterpart: OnyxKernel kernel/src/syscall/poll_sys.rs (SYS_poll).
 * Onyx deviation from Linux: fds are 64-bit (idx, epoch) tokens, so
 * `fd` is `long` and the struct is 16 bytes with no padding — the kernel
 * stages raw byte copies of this array, so keep the layout in sync.
 */
#ifndef _ONYX_POLL_H
#define _ONYX_POLL_H

#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/* poll() events / revents bits (Linux values). */
#define POLLIN      0x001
#define POLLOUT     0x004
#define POLLERR     0x008
#define POLLHUP     0x010
#define POLLNVAL    0x020

struct pollfd {
    long fd;        /* Onyx fd token, or -1 to ignore this entry */
    int  events;    /* requested events (POLLIN/POLLOUT) */
    int  revents;   /* returned events (kernel-filled) */
};

/* poll(fds, nfds, timeout_ms): returns the number of entries with nonzero
 * revents, 0 on timeout, -1 with errno on failure. timeout < 0 blocks
 * indefinitely, 0 returns immediately. */
long _onyx_poll(void *fds, unsigned long nfds, int timeout);

static inline int poll(struct pollfd *fds, unsigned long nfds, int timeout) {
    long r = _onyx_poll(fds, nfds, timeout);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}

#ifdef __cplusplus
}
#endif

#endif /* _ONYX_POLL_H */
