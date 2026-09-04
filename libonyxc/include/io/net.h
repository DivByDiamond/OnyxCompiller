/*
 * net.h — minimal TCP client sockets + DNS resolver (libonyxc v0.5).
 *
 * Thin errno-translating wrapper over the raw _onyx_net_* syscalls
 * (OnyxKernel/kernel/src/syscall/net_sys.rs, #80-83 and #89). Outbound TCP
 * only: no listen/accept, no UDP exposed to userspace. At most 8
 * connections system-wide (kernel table is fixed-size); net_connect()
 * returns -1/ENOMEM past that. DNS lookups go through net_resolve()
 * against the DHCP-learned server (kernel net::G_DNS).
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
long _onyx_net_resolve(const char *name, size_t name_len, unsigned char ip_out[4]);

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

/* Blocking DNS A-record lookup: queries the DHCP-learned DNS server for
 * `hostname` (a NUL-terminated ASCII string) and writes the 4-byte IPv4
 * into ip_out. Returns 0 on success or -1 with errno set (EINVAL for a
 * bad name, EIO for a DNS failure or no DNS server configured, EFAULT
 * for an unreachable ip_out pointer). The kernel rejects anything
 * outside [a-zA-Z0-9.-] up front; hostnames with underscores or other
 * non-DNS characters fail with EINVAL without a network round trip. */
static inline int net_resolve(const char *hostname, unsigned char ip_out[4]) {
    size_t n = 0;
    while (hostname[n] != '\0') {
        n++;
        if (n > 253) { errno = EINVAL; return -1; }
    }
    long r = _onyx_net_resolve(hostname, n, ip_out);
    if (r < 0) { errno = (int)(-r); return -1; }
    return 0;
}

#ifdef __cplusplus
}
#endif
#endif /* _ONYX_NET_H */
