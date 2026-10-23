#include "../include/sys/syscall_numbers.h"
typedef unsigned long size_t;

long __ecall2(long n, long a, long b);
long __ecall3(long n, long a, long b, long c);

static void write_str(const char *s) {
    long n = 0;
    while (s[n]) n = n + 1;
    __ecall3(SYS_write, 1, (long)s, n);
}

static void write_udec(unsigned long v) {
    char buf[24];
    int p = 23;
    buf[p] = 0;
    p = p - 1;
    if (v == 0) {
        buf[p] = '0';
        p = p - 1;
    }
    while (v > 0) {
        buf[p] = '0' + (int)(v % 10);
        p = p - 1;
        v = v / 10;
    }
    __ecall3(SYS_write, 1, (long)(buf + p + 1), 23 - p);
}

static void exit_proc(int code) {
    __ecall2(SYS_exit, code, 0);
    for (;;) { }
}

void _start(void) {
    unsigned char ip[4];
    ip[0] = 10; ip[1] = 0; ip[2] = 2; ip[3] = 2;
    char buf[256];
    int cid;
    long n;
    int i;
    int j;

    write_str("TCP test: connecting to 10.0.2.2:7777...\n");

    cid = (int)__ecall3(SYS_net_connect, (long)ip, 7777, 0);
    if (cid < 0) {
        write_str("FAIL: connect returned ");
        write_udec((unsigned long)(-cid));
        write_str("\n");
        exit_proc(1);
    }

    write_str("OK: connected (cid=");
    write_udec((unsigned long)cid);
    write_str(")\n");

    n = __ecall3(SYS_net_send, cid, (long)"Hello from OnyxOS!\n", 19);
    if (n < 0) {
        write_str("FAIL: send returned ");
        write_udec((unsigned long)(-n));
        write_str("\n");
        __ecall2(SYS_net_close, cid, 0);
        exit_proc(1);
    }

    write_str("OK: sent ");
    write_udec((unsigned long)n);
    write_str(" bytes\n");

    write_str("Waiting for response...\n");
    i = 0;
    while (i < 500) {
        n = __ecall3(SYS_net_recv, cid, (long)buf, 255);
        if (n > 0) {
            buf[n] = 0;
            write_str("OK: received ");
            write_udec((unsigned long)n);
            write_str(" bytes: ");
            write_str(buf);
            write_str("\n");
            break;
        }
        j = 0;
        while (j < 50000) {
            j = j + 1;
        }
        i = i + 1;
    }

    if (i >= 500) {
        write_str("Timeout: no response\n");
    }

    __ecall2(SYS_net_close, cid, 0);
    write_str("Connection closed\n");
    exit_proc(0);
}
