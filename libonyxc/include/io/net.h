/*
 * net.h — minimal TCP client sockets (libonyxc v0.5).
 *
 * Thin errno-translating wrapper over the raw _onyx_net_* syscalls
 * (OnyxKernel/kernel/src/syscall/net_sys.rs, #80-83, #89). Outbound TCP
 * only: no listen/accept, no UDP. net_resolve() does a blocking DNS A-record
 * lookup; net_connect() still takes a raw IPv4, so callers resolve first.
 * At most 8 connections system-wide (kernel table is fixed-size);
 * net_connect() returns -1/ENOMEM past that.
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

long _onyx_net_resolve(const char *name, unsigned char ip_out[4]);

/* Resolves a hostname to an IPv4 address via the kernel's built-in DNS
 * resolver (blocking, single A-record query against the DHCP-learned
 * server). Writes 4 raw bytes to ip_out on success, or returns -1/errno
 * (e.g. ETIMEDOUT / EIO if no reply arrives). */
static inline int net_resolve(const char *name, unsigned char ip_out[4]) {
    long r = _onyx_net_resolve(name, ip_out);
    if (r < 0) { errno = (int)(-r); return -1; }
    return 0;
}

#ifdef __cplusplus
}
#endif
#endif /* _ONYX_NET_H */
