/*
 * net.h — minimal TCP client sockets (libonyxc v0.5).
 *
 * Thin errno-translating wrapper over the raw _onyx_net_* syscalls
 * (OnyxKernel/kernel/src/syscall/net_sys.rs, #80-83). Outbound TCP only:
 * no listen/accept, no UDP, no DNS (callers pass a resolved IPv4 the same
 * way tools like `curl --resolve` do). At most 8 connections system-wide
 * (kernel table is fixed-size); net_connect() returns -1/ENOMEM past that.
 */
#ifndef _ONYX_NET_H
#define _ONYX_NET_H

#include <stddef.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

long _onyx_net_connect(const unsigned char ip[4], int port);
long _onyx_net_send(long conn_id, const void *buf, size_t n);
long _onyx_net_recv(long conn_id, void *buf, size_t n);
long _onyx_net_close(long conn_id);

/* Opens a TCP connection to ip:port (ip = 4 raw bytes, e.g. {93,184,216,34}).
 * Returns a conn_id >= 0 on success, or -1 with errno set. */
static inline int net_connect(const unsigned char ip[4], int port) {
    long r = _onyx_net_connect(ip, port);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}

/* Sends len bytes; returns bytes written (may be < len), or -1/errno. */
static inline long net_send(int conn_id, const void *buf, size_t len) {
    long r = _onyx_net_send(conn_id, buf, len);
    if (r < 0) { errno = (int)(-r); return -1; }
    return r;
}

/* Reads up to len bytes; returns bytes read (0 = no data yet / non-blocking
 * gap, caller should retry), or -1/errno. */
static inline long net_recv(int conn_id, void *buf, size_t len) {
    long r = _onyx_net_recv(conn_id, buf, len);
    if (r < 0) { errno = (int)(-r); return -1; }
    return r;
}

/* Closes the connection. Always succeeds. */
static inline void net_close(int conn_id) {
    _onyx_net_close(conn_id);
}

#ifdef __cplusplus
}
#endif
#endif /* _ONYX_NET_H */
