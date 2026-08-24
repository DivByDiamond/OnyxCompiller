/*
 * onx-run.c — OnyxOS .onx userspace emulator (host tool).
 *
 * Loads an OnyxExec v1 (.onx) binary produced by onyxcc / onyx-ld and
 * executes it on the host by interpreting RV64IMAFD machine code and
 * translating OnyxOS syscalls to host operations.
 *
 * Purpose: fast smoke-testing of compiler + libonyxc output without a
 * RISC-V CPU or QEMU. NOT a system emulator: one process, flat address
 * space, no MMU/privileges.
 *
 * Memory map (mirrors the kernel's userspace layout):
 *   0x00010000 .. segments (text/rodata/data/bss)
 *   0x01000000 .. heap (sbrk grows up)
 *   0x20000000 .. stack top (grows down), argv/env frame built by loader
 *
 * Usage:
 *   onx-run [-v] program.onx [args...]
 *
 * Exit status = program's exit code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>

/* ── .onx format (subset of include/core/onx.h) ─────────────────────── */
#define ONX_MAGIC       0x31584E4Fu
#define ONX_FLAGS_RING1 0x2u

typedef struct {
    uint64_t vaddr, filesz, memsz;
    uint32_t offset, flags, align, reserved;
} OnxSegment;

/* ── Memory: 4 KiB pages, on-demand ─────────────────────────────────── */
#define PAGE_BITS 12
#define PAGE_SIZE (1u << PAGE_BITS)
#define PAGE_MASK (PAGE_SIZE - 1)
#define ADDR_SPACE_SIZE 0x40000000ULL          /* 1 GiB, USER_TOP */
#define NPAGES (ADDR_SPACE_SIZE / PAGE_SIZE)

static uint8_t *g_pages[NPAGES];
static uint64_t g_pc;   /* forward: fault diagnostics */
static void trace_dump(void);   /* forward: crash diagnostics */

static uint8_t *page_get(uint64_t addr, bool fault_on_nomap) {
    if (addr >= ADDR_SPACE_SIZE) {
        fprintf(stderr, "onx-run: fault: address 0x%llx out of range (pc=0x%llx)\n",
                (unsigned long long)addr, (unsigned long long)g_pc);
        exit(139);
    }
    uint64_t pn = addr >> PAGE_BITS;
    if (!g_pages[pn]) {
        if (fault_on_nomap) {
            fprintf(stderr, "onx-run: fault: unmapped read at 0x%llx (pc=0x%llx)\n",
                    (unsigned long long)addr, (unsigned long long)g_pc);
            trace_dump();
            exit(139);
        }
        g_pages[pn] = (uint8_t *)calloc(1, PAGE_SIZE);
        if (!g_pages[pn]) { fprintf(stderr, "onx-run: OOM\n"); exit(133); }
    }
    return g_pages[pn];
}

static uint8_t mem_read8(uint64_t addr) {
    return page_get(addr, true)[addr & PAGE_MASK];
}
static void mem_write8(uint64_t addr, uint8_t v) {
    page_get(addr, false)[addr & PAGE_MASK] = v;
}
static uint64_t mem_read(uint64_t addr, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint64_t)mem_read8(addr + i) << (8 * i);
    return v;
}
static void mem_write(uint64_t addr, uint64_t v, int n) {
    for (int i = 0; i < n; i++) mem_write8(addr + i, (uint8_t)(v >> (8 * i)));
}
static void mem_read_buf(uint64_t addr, void *dst, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = mem_read8(addr + i);
}
static void mem_write_buf(uint64_t addr, const void *src, size_t n) {
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) mem_write8(addr + i, s[i]);
}
static size_t mem_strlen(uint64_t addr) {
    size_t n = 0;
    while (mem_read8(addr + n)) n++;
    return n;
}
static void mem_read_str(uint64_t addr, char *out, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        char c = (char)mem_read8(addr + i);
        if (!c) break;
        out[i++] = c;
    }
    out[i] = 0;
}

/* ── CPU state ──────────────────────────────────────────────────────── */
static uint64_t g_reg[32];
static uint64_t g_freg[32];      /* raw bits (float/double) */
/* g_pc defined above */
static uint64_t g_exit_code = 0;
static bool     g_exited = false;
static bool     g_verbose = false;

/* ── Host file table ────────────────────────────────────────────────── */
#define MAX_HOST_FILES 32
static int g_fds[MAX_HOST_FILES];   /* host fds; -1 = free */
static const char *g_fd_names[MAX_HOST_FILES];

static int fd_alloc(int host_fd, const char *name) {
    for (int i = 3; i < MAX_HOST_FILES; i++) {
        if (g_fds[i] == -1) {
            g_fds[i] = host_fd;
            g_fd_names[i] = name;
            return i;
        }
    }
    return -1;
}

/* ── Loader ─────────────────────────────────────────────────────────── */
static uint64_t g_entry;

static void load_onx(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "onx-run: cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (fread(buf, 1, sz, f) != (size_t)sz) {
        fprintf(stderr, "onx-run: short read\n"); exit(1);
    }
    fclose(f);

    if (sz < 24) { fprintf(stderr, "onx-run: too small\n"); exit(1); }
    uint32_t magic = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                     ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    if (magic != ONX_MAGIC) {
        fprintf(stderr, "onx-run: bad magic 0x%x (not an .onx v1 file)\n", magic);
        exit(1);
    }
    uint32_t nsegs = (uint32_t)buf[16] | ((uint32_t)buf[17] << 8) |
                     ((uint32_t)buf[18] << 16) | ((uint32_t)buf[19] << 24);
    g_entry = 0;
    for (int i = 0; i < 8; i++) g_entry |= (uint64_t)buf[8 + i] << (8 * i);
    if (nsegs == 0 || nsegs > 8) {
        fprintf(stderr, "onx-run: bad nsegs %u\n", nsegs); exit(1);
    }
    if (g_entry == 0) {
        fprintf(stderr, "onx-run: entry=0 (no _start; link libonyxc start)\n");
        exit(1);
    }

    for (uint32_t s = 0; s < nsegs; s++) {
        const uint8_t *sh = buf + 24 + s * 40;
        OnxSegment seg;
        seg.vaddr = 0; seg.filesz = 0; seg.memsz = 0;
        for (int i = 0; i < 8; i++) {
            seg.vaddr  |= (uint64_t)sh[i] << (8 * i);
            seg.filesz |= (uint64_t)sh[8 + i] << (8 * i);
            seg.memsz  |= (uint64_t)sh[16 + i] << (8 * i);
        }
        seg.offset = (uint32_t)sh[24] | ((uint32_t)sh[25] << 8) |
                     ((uint32_t)sh[26] << 16) | ((uint32_t)sh[27] << 24);
        for (uint64_t a = seg.vaddr; a < seg.vaddr + seg.memsz; a += PAGE_SIZE) {
            page_get(a, false);
        }
        if (seg.filesz) {
            mem_write_buf(seg.vaddr, buf + seg.offset, seg.filesz);
        }
        if (g_verbose) {
            printf("[onx-run] seg %u: vaddr=0x%llx filesz=%llu memsz=%llu off=%u\n",
                   s, (unsigned long long)seg.vaddr,
                   (unsigned long long)seg.filesz,
                   (unsigned long long)seg.memsz, seg.offset);
        }
    }
    free(buf);
}

/* ── Stack + argv frame (mirrors kernel onx/argv.rs) ────────────────── */
static uint64_t g_brk = 0;      /* current program break */

static void setup_stack(int argc, char **argv) {
    uint64_t sp = 0x20000000ULL - 4096;
    uint64_t argv_str[64];
    int n = argc < 63 ? argc : 63;
    for (int i = 0; i < n; i++) {
        size_t l = strlen(argv[i]) + 1;
        sp -= l;
        mem_write_buf(sp, argv[i], l);
        argv_str[i] = sp;
    }
    sp &= ~0xFULL;
    /* Layout (growing down from the strings):
     *   [rnd block 16B][auxv 32B][envp NULL 8][argv NULL 8][argv n×8][argc 8]
     * Total = 16+32+8+8+8n+8 = 72+8n. rnd sits at the BOTTOM, guaranteed
     * below every slot (the old placement aliased argv[1] for argc=2 and
     * corrupted it with random bytes). */
    sp -= 16 + 32 + 8 + 8 + (n + 1) * 8;
    sp &= ~0xFULL;
    sp &= ~0xFULL;
    uint64_t frame = sp;
    uint64_t p = frame;
    mem_write(p, (uint64_t)n, 8); p += 8;
    for (int i = 0; i < n; i++) { mem_write(p, argv_str[i], 8); p += 8; }
    mem_write(p, 0, 8); p += 8;          /* argv NULL */
    mem_write(p, 0, 8); p += 8;          /* envp NULL */
    uint64_t rnd = frame - 16;      /* 16-byte entropy block BELOW the frame */
    for (int i = 0; i < 16; i++) mem_write8(rnd + i, (uint8_t)(rand() & 0xff));
    mem_write(p, 25, 8); p += 8; mem_write(p, rnd, 8); p += 8;
    mem_write(p, 0, 8); p += 8; mem_write(p, 0, 8);

    g_reg[2] = frame;           /* sp  */
    g_reg[10] = (uint64_t)n;    /* a0 = argc */
    g_reg[11] = frame + 8;      /* a1 = &argv[0] */

    if (getenv("ONYXRUN_FRAME")) {
        fprintf(stderr, "FRAME @ %llx (argc=%d):\n",
                (unsigned long long)frame, n);
        for (int k = 0; k < n + 6; k++) {
            uint64_t v = mem_read(frame + k * 8, 8);
            fprintf(stderr, "  [+%3d] %016llx", k * 8, (unsigned long long)v);
            if (v >= 0x10000 && v < 0x20000000) {
                char s[33];
                mem_read_str(v, s, sizeof(s));
                fprintf(stderr, "  -> \"%s\"", s);
            }
            fprintf(stderr, "\n");
        }
    }
}

/* ── Syscalls (OnyxKernel ABI) ──────────────────────────────────────── */
#define SYS_write   1
#define SYS_read    2
#define SYS_exit    3
#define SYS_yield   4
#define SYS_getpid  5
#define SYS_brk     6
#define SYS_mmap    7
#define SYS_open    8
#define SYS_close   9
#define SYS_lseek  10
#define SYS_stat   11
#define SYS_sbrk   13
#define SYS_munmap 34
#define SYS_dup    35
#define SYS_chdir  39
#define SYS_getcwd 40
#define SYS_truncate2 71
#define SYS_ftruncate 72
#define SYS_access 42
#define SYS_gettimeofday 43
#define SYS_fcntl  44
#define SYS_nanosleep 49
#define SYS_fstat  50
#define SYS_ioctl  53
#define SYS_execve 58
#define SYS_clock_gettime 64
#define SYS_clock_getres 65
#define SYS_isatty 66
#define SYS_getentropy 67
#define SYS_unlink 37
#define SYS_rename 38

#define O_ACCMODE_LINUX 3

static int host_oflags(int onyx_flags) {
    int acc = onyx_flags & O_ACCMODE_LINUX;
    int f = 0;
    if (acc == 1) f = O_WRONLY;
    else if (acc == 2) f = O_RDWR;
    else f = O_RDONLY;
    if (onyx_flags & 0x40)  f |= O_CREAT;
    if (onyx_flags & 0x80)  f |= O_EXCL;
    if (onyx_flags & 0x200) f |= O_TRUNC;
    if (onyx_flags & 0x400) f |= O_APPEND;
    return f;
}

static void do_stat_fill(uint64_t st_buf, struct stat *st) {
    /* Layout must match onyxc.h struct stat. */
    mem_write(st_buf + 0,   (uint64_t)st->st_dev, 8);
    mem_write(st_buf + 8,   (uint64_t)st->st_ino, 8);
    mem_write(st_buf + 16,  (uint64_t)st->st_mode, 4);
    mem_write(st_buf + 20,  (uint64_t)st->st_nlink, 4);
    mem_write(st_buf + 24,  (uint64_t)st->st_uid, 4);
    mem_write(st_buf + 28,  (uint64_t)st->st_gid, 4);
    mem_write(st_buf + 32,  0, 4);
    mem_write(st_buf + 40,  (uint64_t)st->st_rdev, 8);
    mem_write(st_buf + 48,  (uint64_t)st->st_size, 8);
    mem_write(st_buf + 56,  (uint64_t)st->st_blksize, 8);
    mem_write(st_buf + 64,  (uint64_t)st->st_blocks, 8);
    mem_write(st_buf + 72,  (uint64_t)st->st_atime, 8);
    mem_write(st_buf + 80,  0, 8);
    mem_write(st_buf + 88,  (uint64_t)st->st_mtime, 8);
    mem_write(st_buf + 96,  0, 8);
    mem_write(st_buf + 104, (uint64_t)st->st_ctime, 8);
    mem_write(st_buf + 112, 0, 8);
}

static long do_syscall(void) {
    uint64_t nr = g_reg[17];       /* a7 */
    uint64_t a0 = g_reg[10], a1 = g_reg[11], a2 = g_reg[12];

    switch (nr) {
        case SYS_write: {
            int fd = (int)a0;
            if (getenv("ONYXRUN_TRACE")) {
                fprintf(stderr, "SYSCALL write(fd=%d, buf=%llx, len=%llu) ra=%llx\n",
                        fd, (unsigned long long)a1, (unsigned long long)a2,
                        (unsigned long long)g_reg[1]);
            }
            uint8_t *tmp = (uint8_t *)malloc(a2 ? a2 : 1);
            mem_read_buf(a1, tmp, a2);
            long n;
            if (fd == 1) { n = (long)fwrite(tmp, 1, a2, stdout); fflush(stdout); }
            else if (fd == 2) { n = (long)fwrite(tmp, 1, a2, stderr); fflush(stderr); }
            else if (fd >= 0 && fd < MAX_HOST_FILES && g_fds[fd] >= 0) {
                n = (long)write(g_fds[fd], tmp, a2);
            } else n = -9;
            free(tmp);
            return n;
        }
        case SYS_read: {
            int fd = (int)a0;
            if (fd == 0) {
                int c = fgetc(stdin);
                if (c == EOF) return 0;
                mem_write8(a1, (uint8_t)c);
                return 1;
            }
            if (fd >= 0 && fd < MAX_HOST_FILES && g_fds[fd] >= 0) {
                uint8_t *tmp = (uint8_t *)malloc(a2 ? a2 : 1);
                long n = (long)read(g_fds[fd], tmp, a2);
                if (n > 0) mem_write_buf(a1, tmp, n);
                free(tmp);
                return n;
            }
            return -9;
        }
        case SYS_exit:
            if (getenv("ONYXRUN_TRACE")) {
                fprintf(stderr, "SYSCALL exit(%llu)\n", (unsigned long long)a0);
            }
            g_exit_code = a0;
            g_exited = true;
            if (getenv("ONYXRUN_TRACE")) trace_dump();
            return 0;
        case SYS_yield:
            return 0;
        case SYS_getpid:
            return (long)getpid();
        case SYS_brk: {
            if (a0 == 0) return g_brk;
            if (a0 > g_brk && a0 < 0x10000000ULL) {
                for (uint64_t a = g_brk; a < a0; a += PAGE_SIZE) page_get(a, false);
                g_brk = a0;
            }
            return g_brk;
        }
        case SYS_sbrk: {
            int64_t inc = (int64_t)a0;
            uint64_t old = g_brk;
            uint64_t nb = (inc >= 0) ? g_brk + (uint64_t)inc
                                     : (g_brk > (uint64_t)(-inc)) ? g_brk - (uint64_t)(-inc) : 0x1000000;
            if (nb > 0x10000000ULL) return (long)-12;
            for (uint64_t a = old; a < nb; a += PAGE_SIZE) page_get(a, false);
            g_brk = nb;
            return (long)old;
        }
        case SYS_mmap: {
            if (a1 > 0x10000000ULL) return (long)-12;
            for (uint64_t a = a0; a < a0 + a1; a += PAGE_SIZE) page_get(a, false);
            return (long)a0;
        }
        case SYS_munmap:
            return 0;
        case SYS_open: {
            char path[1024];
            mem_read_str(a0, path, sizeof(path));
            int host_fd = open(path, host_oflags((int)a1), 0644);
            if (host_fd < 0) return (long)-2;
            int fd = fd_alloc(host_fd, strdup(path));
            if (fd < 0) { close(host_fd); return (long)-24; }
            return fd;
        }
        case SYS_close: {
            int fd = (int)a0;
            if (fd >= 3 && fd < MAX_HOST_FILES && g_fds[fd] >= 0) {
                close(g_fds[fd]);
                g_fds[fd] = -1;
                return 0;
            }
            return -9;
        }
        case SYS_lseek: {
            int fd = (int)a0;
            if (fd >= 0 && fd < MAX_HOST_FILES && g_fds[fd] >= 0) {
                return (long)lseek(g_fds[fd], (off_t)(int64_t)a1, (int)a2);
            }
            return -9;
        }
        case SYS_stat: {
            char path[1024];
            mem_read_str(a0, path, sizeof(path));
            struct stat st;
            if (stat(path, &st) != 0) return -2;
            do_stat_fill(a1, &st);
            return 0;
        }
        case SYS_fstat: {
            int fd = (int)a0;
            if (fd >= 0 && fd < MAX_HOST_FILES && g_fds[fd] >= 0) {
                struct stat st;
                if (fstat(g_fds[fd], &st) != 0) return -2;
                do_stat_fill(a1, &st);
                return 0;
            }
            if (fd == 1 || fd == 2) {
                mem_write(a1 + 16, 0020620, 4);   /* S_IFCHR */
                mem_write(a1 + 48, 0, 8);
                return 0;
            }
            return -9;
        }
        case SYS_fcntl:
            return 0;
        case SYS_dup: {
            int fd = (int)a0;
            if (fd >= 0 && fd < MAX_HOST_FILES && g_fds[fd] >= 0) {
                return fd_alloc(dup(g_fds[fd]), g_fd_names[fd]);
            }
            return -9;
        }
        case SYS_gettimeofday: {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            if (a0) {
                mem_write(a0, (uint64_t)tv.tv_sec, 8);
                mem_write(a0 + 8, (uint64_t)tv.tv_usec, 8);
            }
            return 0;
        }
        case SYS_clock_gettime: {
            struct timespec ts;
            clock_gettime(a0 == 1 ? CLOCK_MONOTONIC : CLOCK_REALTIME, &ts);
            mem_write(a1, (uint64_t)ts.tv_sec, 8);
            mem_write(a1 + 8, (uint64_t)ts.tv_nsec, 8);
            return 0;
        }
        case SYS_clock_getres: {
            mem_write(a1, 0, 8);
            mem_write(a1 + 8, 1000, 8);
            return 0;
        }
        case SYS_nanosleep: {
            uint64_t sec = mem_read(a0, 8);
            uint64_t nsec = mem_read(a0 + 8, 8);
            struct timespec ts = { (time_t)sec, (long)nsec };
            nanosleep(&ts, NULL);
            return 0;
        }
        case SYS_ioctl: {
            uint64_t req = a1;
            if (req == 0x5401) {          /* TCGETS */
                for (int i = 0; i < 60; i++) mem_write8(a2 + i, 0);
                mem_write(a2 + 12, 010 | 02, 4);   /* ECHO | ICANON */
                return 0;
            }
            if (req == 0x5402) return 0;  /* TCSETS */
            if (req == 0x5413) {          /* TIOCGWINSZ */
                mem_write(a2, 24, 2);
                mem_write(a2 + 2, 80, 2);
                return 0;
            }
            return 0;
        }
        case SYS_isatty:
            return 1;
        case SYS_ftruncate: {
            int fd = (int)a0;
            if (fd >= 0 && fd < MAX_HOST_FILES && g_fds[fd] >= 0)
                return ftruncate(g_fds[fd], (off_t)(int64_t)a1) == 0 ? 0 : -1;
            return -9;
        }
        case SYS_truncate2: {
            char path[1024];
            mem_read_str(a0, path, sizeof(path));
            return truncate(path, (off_t)(int64_t)a1) == 0 ? 0 : -1;
        }
        case SYS_getcwd: {
            char tmp[1024];
            if (!getcwd(tmp, sizeof(tmp))) return -1;
            size_t l = strlen(tmp);
            if (l + 1 > a1) return -34;
            mem_write_buf(a0, tmp, l + 1);
            return (long)l;
        }
        case SYS_chdir: {
            char path[1024];
            mem_read_str(a0, path, sizeof(path));
            return chdir(path) == 0 ? 0 : -2;
        }
        case SYS_unlink: {
            char path[1024];
            mem_read_str(a0, path, sizeof(path));
            return unlink(path) == 0 ? 0 : -2;
        }
        case SYS_rename: {
            char oldp[1024], newp[1024];
            mem_read_str(a0, oldp, sizeof(oldp));
            mem_read_str(a1, newp, sizeof(newp));
            return rename(oldp, newp) == 0 ? 0 : -2;
        }
        case SYS_access: {
            char path[1024];
            mem_read_str(a0, path, sizeof(path));
            return access(path, (int)a1 == 0 ? F_OK : (int)a1) == 0 ? 0 : -2;
        }
        case SYS_getentropy: {
            for (uint64_t i = 0; i < a1; i++) {
                mem_write8(a0 + i, (uint8_t)(rand() >> 8));
            }
            return 0;
        }
        default:
            fprintf(stderr, "onx-run: unimplemented syscall %llu\n",
                    (unsigned long long)nr);
            return (long)-38;   /* -ENOSYS */
    }
}

/* ── Crash trace: ring buffer of last 24 executed instructions ─────── */
static uint64_t g_trace_pc[24];
static uint32_t g_trace_insn[24];
static int g_trace_idx;

static void trace_record(uint64_t pc, uint32_t insn) {
    g_trace_pc[g_trace_idx] = pc;
    g_trace_insn[g_trace_idx] = insn;
    g_trace_idx = (g_trace_idx + 1) % 24;
}

static void trace_dump(void) {
    fprintf(stderr, "--- last instructions ---\n");
    for (int k = 0; k < 24; k++) {
        int i = (g_trace_idx + k) % 24;
        if (g_trace_pc[i] == 0 && k > 0) continue;
        fprintf(stderr, "  pc=0x%08llx  insn=0x%08x\n",
                (unsigned long long)g_trace_pc[i], g_trace_insn[i]);
    }
}

/* ── RV64IMAFD interpreter ──────────────────────────────────────────── */

static uint32_t fetch32(uint64_t pc) {
    return (uint32_t)mem_read8(pc) | ((uint32_t)mem_read8(pc + 1) << 8) |
           ((uint32_t)mem_read8(pc + 2) << 16) | ((uint32_t)mem_read8(pc + 3) << 24);
}

static inline int64_t sx(uint64_t v, int bits) {
    uint64_t m = 1ULL << (bits - 1);
    return (int64_t)((v ^ m) - m);
}

union fval {
    float f;
    double d;
    uint32_t u32;
    uint64_t u64;
};

static float freg_f(int i) { union fval v; v.u64 = (uint32_t)g_freg[i]; return v.f; }
static double freg_d(int i) { union fval v; v.u64 = g_freg[i]; return v.d; }
static void set_freg_f(int i, float f) { union fval v; v.f = f; g_freg[i] = v.u32; }
static void set_freg_d(int i, double d) { union fval v; v.d = d; g_freg[i] = v.u64; }

static int32_t f2i32(float f) {
    if (f != f) return 0x7fffffff;
    if (f >= 2147483647.0f) return 0x7fffffff;
    if (f <= -2147483648.0f) return (int32_t)0x80000000;
    return (int32_t)f;
}
static int64_t f2i64(double d) {
    if (d != d) return 0x7fffffffffffffffLL;
    if (d >= 9223372036854775295.0) return 0x7fffffffffffffffLL;
    if (d <= -9223372036854775808.0) return (int64_t)0x8000000000000000LL;
    return (int64_t)d;
}

static void illegal(uint32_t insn) {
    fprintf(stderr, "onx-run: illegal instruction 0x%08x at pc=0x%llx\n",
            insn, (unsigned long long)g_pc);
    trace_dump();
    exit(132);
}

static int g_trace_regs = 0;

static void step(void) {
    uint32_t insn = fetch32(g_pc);
    trace_record(g_pc, insn);
    if (g_trace_regs) {
        fprintf(stderr, "pc=%08llx t0=%llx t1=%llx t2=%llx a0=%llx a1=%llx a2=%llx sp=%llx fp=%llx f0=%llx f1=%llx fa0=%llx fa1=%llx\n",
            (unsigned long long)g_pc, (unsigned long long)g_reg[5],
            (unsigned long long)g_reg[6], (unsigned long long)g_reg[7],
            (unsigned long long)g_reg[10], (unsigned long long)g_reg[11],
            (unsigned long long)g_reg[12],
            (unsigned long long)g_reg[2], (unsigned long long)g_reg[8],
            (unsigned long long)g_freg[0], (unsigned long long)g_freg[1],
            (unsigned long long)g_freg[10], (unsigned long long)g_freg[11]);
    }
    uint32_t opcode = insn & 0x7F;
    uint32_t rd = (insn >> 7) & 0x1F;
    uint32_t funct3 = (insn >> 12) & 0x7;
    uint32_t rs1 = (insn >> 15) & 0x1F;
    uint32_t rs2 = (insn >> 20) & 0x1F;
    uint32_t funct7 = (insn >> 25) & 0x7F;
    uint64_t npc = g_pc + 4;

    switch (opcode) {
        case 0x37: /* LUI */
            g_reg[rd] = (uint64_t)(int32_t)(insn & 0xFFFFF000u);
            break;
        case 0x17: /* AUIPC */
            g_reg[rd] = g_pc + (uint64_t)(int32_t)(insn & 0xFFFFF000u);
            break;
        case 0x6F: { /* JAL */
            int64_t imm = sx(((insn >> 31) & 1) << 20 |
                             ((insn >> 12) & 0xFF) << 12 |
                             ((insn >> 20) & 1) << 11 |
                             ((insn >> 21) & 0x3FF) << 1, 21);
            if (rd) g_reg[rd] = npc;
            npc = g_pc + imm;
            break;
        }
        case 0x67: { /* JALR */
            int64_t imm = sx((insn >> 20) & 0xFFF, 12);
            uint64_t t = (g_reg[rs1] + imm) & ~1ULL;
            if (rd) g_reg[rd] = npc;
            npc = t;
            break;
        }
        case 0x63: { /* branches */
            int64_t imm = sx(((insn >> 31) & 1) << 12 |
                             ((insn >> 7) & 1) << 11 |
                             ((insn >> 25) & 0x3F) << 5 |
                             ((insn >> 8) & 0xF) << 1, 13);
            bool take = false;
            switch (funct3) {
                case 0: take = g_reg[rs1] == g_reg[rs2]; break;
                case 1: take = g_reg[rs1] != g_reg[rs2]; break;
                case 4: take = (int64_t)g_reg[rs1] <  (int64_t)g_reg[rs2]; break;
                case 5: take = (int64_t)g_reg[rs1] >= (int64_t)g_reg[rs2]; break;
                case 6: take = g_reg[rs1] <  g_reg[rs2]; break;
                case 7: take = g_reg[rs1] >= g_reg[rs2]; break;
                default: illegal(insn);
            }
            if (take) npc = g_pc + imm;
            break;
        }
        case 0x03: { /* loads */
            int64_t imm = sx((insn >> 20) & 0xFFF, 12);
            uint64_t addr = g_reg[rs1] + imm;
            switch (funct3) {
                case 0: g_reg[rd] = (uint64_t)(int64_t)(int8_t)mem_read8(addr); break;
                case 1: g_reg[rd] = (uint64_t)(int64_t)(int16_t)mem_read(addr, 2); break;
                case 2: g_reg[rd] = (uint64_t)(int64_t)(int32_t)mem_read(addr, 4); break;
                case 3: g_reg[rd] = mem_read(addr, 8); break;
                case 4: g_reg[rd] = mem_read8(addr); break;
                case 5: g_reg[rd] = mem_read(addr, 2); break;
                case 6: g_reg[rd] = mem_read(addr, 4); break;
                default: illegal(insn);
            }
            if (g_trace_regs) {
                fprintf(stderr, "    load[%llx] -> r%d=%llx\n",
                        (unsigned long long)addr, rd, (unsigned long long)g_reg[rd]);
            }
            break;
        }
        case 0x23: { /* stores */
            int64_t imm = sx(((insn >> 25) & 0x7F) << 5 | ((insn >> 7) & 0x1F), 12);
            uint64_t addr = g_reg[rs1] + imm;
            switch (funct3) {
                case 0: mem_write8(addr, (uint8_t)g_reg[rs2]); break;
                case 1: mem_write(addr, g_reg[rs2], 2); break;
                case 2: mem_write(addr, g_reg[rs2], 4); break;
                case 3: mem_write(addr, g_reg[rs2], 8); break;
                default: illegal(insn);
            }
            if (g_trace_regs) {
                fprintf(stderr, "    store[%llx] <- r%d=%llx\n",
                        (unsigned long long)addr, rs2, (unsigned long long)g_reg[rs2]);
            }
            {
                const char *wp = getenv("ONYXRUN_WATCH");
                if (wp) {
                    uint64_t wa = strtoull(wp, NULL, 16);
                    if (addr <= wa && wa < addr + 8) {
                        fprintf(stderr, "WATCH: pc=%llx store[%llx..%llx] <- %llx (watch %llx)\n",
                                (unsigned long long)g_pc, (unsigned long long)addr,
                                (unsigned long long)(addr+8),
                                (unsigned long long)g_reg[rs2], wa);
                    }
                }
            }
            break;
        }
        case 0x13: { /* OP-IMM */
            int64_t imm = sx((insn >> 20) & 0xFFF, 12);
            uint64_t shamt = (insn >> 20) & 0x3F;
            switch (funct3) {
                case 0: g_reg[rd] = g_reg[rs1] + imm; break;
                case 2: g_reg[rd] = ((int64_t)g_reg[rs1] < imm) ? 1 : 0; break;
                case 3: g_reg[rd] = (g_reg[rs1] < (uint64_t)imm) ? 1 : 0; break;
                case 4: g_reg[rd] = g_reg[rs1] ^ imm; break;
                case 6: g_reg[rd] = g_reg[rs1] | imm; break;
                case 7: g_reg[rd] = g_reg[rs1] & imm; break;
                case 1: g_reg[rd] = g_reg[rs1] << shamt; break;
                case 5:
                    if ((insn >> 26) == 0x20) g_reg[rd] = (uint64_t)((int64_t)g_reg[rs1] >> shamt);
                    else g_reg[rd] = g_reg[rs1] >> shamt;
                    break;
                default: illegal(insn);
            }
            break;
        }
        case 0x33: { /* OP */
            if (funct7 == 0x01) { /* RV64M */
                int64_t a = (int64_t)g_reg[rs1], b = (int64_t)g_reg[rs2];
                switch (funct3) {
                    case 0: g_reg[rd] = g_reg[rs1] * g_reg[rs2]; break;
                    case 1: {
                        __int128 r = (__int128)a * (__int128)b;
                        g_reg[rd] = (uint64_t)(r >> 64);
                        break;
                    }
                    case 2: {
                        __int128 r = (__int128)a * (__int128)(uint64_t)g_reg[rs2];
                        g_reg[rd] = (uint64_t)(r >> 64);
                        break;
                    }
                    case 3: {
                        unsigned __int128 r = (unsigned __int128)g_reg[rs1] *
                                              (unsigned __int128)g_reg[rs2];
                        g_reg[rd] = (uint64_t)(r >> 64);
                        break;
                    }
                    case 4:
                        if (b == 0) g_reg[rd] = ~0ULL;
                        else if (a == INT64_MIN && b == -1) g_reg[rd] = (uint64_t)INT64_MIN;
                        else g_reg[rd] = (uint64_t)(a / b);
                        break;
                    case 5:
                        if (g_reg[rs2] == 0) g_reg[rd] = ~0ULL;
                        else g_reg[rd] = g_reg[rs1] / g_reg[rs2];
                        break;
                    case 6:
                        if (b == 0) g_reg[rd] = g_reg[rs1];
                        else if (a == INT64_MIN && b == -1) g_reg[rd] = 0;
                        else g_reg[rd] = (uint64_t)(a % b);
                        break;
                    case 7:
                        if (g_reg[rs2] == 0) g_reg[rd] = g_reg[rs1];
                        else g_reg[rd] = g_reg[rs1] % g_reg[rs2];
                        break;
                }
            } else if (funct7 == 0x20) {
                if (funct3 == 0) g_reg[rd] = g_reg[rs1] - g_reg[rs2];
                else if (funct3 == 5) g_reg[rd] = (uint64_t)((int64_t)g_reg[rs1] >> (g_reg[rs2] & 63));
                else illegal(insn);
            } else {
                switch (funct3) {
                    case 0: g_reg[rd] = g_reg[rs1] + g_reg[rs2]; break;
                    case 1: g_reg[rd] = g_reg[rs1] << (g_reg[rs2] & 63); break;
                    case 2: g_reg[rd] = ((int64_t)g_reg[rs1] < (int64_t)g_reg[rs2]) ? 1 : 0; break;
                    case 3: g_reg[rd] = (g_reg[rs1] < g_reg[rs2]) ? 1 : 0; break;
                    case 4: g_reg[rd] = g_reg[rs1] ^ g_reg[rs2]; break;
                    case 5: g_reg[rd] = g_reg[rs1] >> (g_reg[rs2] & 63); break;
                    case 6: g_reg[rd] = g_reg[rs1] | g_reg[rs2]; break;
                    case 7: g_reg[rd] = g_reg[rs1] & g_reg[rs2]; break;
                }
            }
            break;
        }
        case 0x1B: { /* OP-IMM-32 */
            int64_t imm = sx((insn >> 20) & 0xFFF, 12);
            uint64_t shamt = (insn >> 20) & 0x3F;
            int64_t a = (int64_t)(int32_t)(uint32_t)g_reg[rs1];
            switch (funct3) {
                case 0: g_reg[rd] = (uint64_t)(int32_t)(a + (int32_t)imm); break;
                case 1: g_reg[rd] = (uint64_t)(int32_t)((uint32_t)a << shamt); break;
                case 5:
                    if ((insn >> 26) == 0x20) g_reg[rd] = (uint64_t)(int32_t)(a >> shamt);
                    else g_reg[rd] = (uint64_t)(int32_t)((uint32_t)a >> shamt);
                    break;
                default: illegal(insn);
            }
            break;
        }
        case 0x3B: { /* OP-32 */
            int64_t a = (int64_t)(int32_t)(uint32_t)g_reg[rs1];
            int64_t b = (int64_t)(int32_t)(uint32_t)g_reg[rs2];
            if (funct7 == 0x01) {
                switch (funct3) {
                    case 0: g_reg[rd] = (uint64_t)(int32_t)((int32_t)a * (int32_t)b); break;
                    case 4:
                        if ((int32_t)b == 0) g_reg[rd] = ~0ULL;
                        else if ((int32_t)a == INT32_MIN && (int32_t)b == -1) g_reg[rd] = (uint64_t)(int32_t)INT32_MIN;
                        else g_reg[rd] = (uint64_t)(int32_t)((int32_t)a / (int32_t)b);
                        break;
                    case 5:
                        if ((uint32_t)g_reg[rs2] == 0) g_reg[rd] = ~0ULL;
                        else g_reg[rd] = (uint64_t)(int32_t)((uint32_t)g_reg[rs1] / (uint32_t)g_reg[rs2]);
                        break;
                    case 6:
                        if ((int32_t)b == 0) g_reg[rd] = (uint64_t)(int32_t)a;
                        else if ((int32_t)a == INT32_MIN && (int32_t)b == -1) g_reg[rd] = 0;
                        else g_reg[rd] = (uint64_t)(int32_t)((int32_t)a % (int32_t)b);
                        break;
                    case 7:
                        if ((uint32_t)g_reg[rs2] == 0) g_reg[rd] = g_reg[rs1];
                        else g_reg[rd] = (uint64_t)(int32_t)((uint32_t)g_reg[rs1] % (uint32_t)g_reg[rs2]);
                        break;
                    default: illegal(insn);
                }
            } else if (funct7 == 0x20) {
                if (funct3 == 0) g_reg[rd] = (uint64_t)(int32_t)(a - b);
                else if (funct3 == 5) g_reg[rd] = (uint64_t)(int32_t)(a >> (b & 31));
                else illegal(insn);
            } else {
                switch (funct3) {
                    case 0: g_reg[rd] = (uint64_t)(int32_t)(a + b); break;
                    case 1: g_reg[rd] = (uint64_t)(int32_t)((uint32_t)g_reg[rs1] << (g_reg[rs2] & 31)); break;
                    case 5: g_reg[rd] = (uint64_t)(int32_t)((uint32_t)g_reg[rs1] >> (g_reg[rs2] & 31)); break;
                    default: illegal(insn);
                }
            }
            break;
        }
        case 0x0F: /* FENCE */
            break;
        case 0x73: { /* SYSTEM */
            if (insn == 0x00000073) {   /* ECALL */
                g_reg[10] = (uint64_t)do_syscall();
                if (g_exited) { exit((int)g_exit_code); }
            } else if (insn == 0x00100073) {  /* EBREAK */
                fprintf(stderr, "onx-run: ebreak at 0x%llx\n", (unsigned long long)g_pc);
                exit(133);
            } else {
                illegal(insn);
            }
            break;
        }
        /* ── F/D extension ─────────────────────────────────────────── */
        case 0x07: { /* FP loads: FLW / FLD */
            int64_t imm = sx((insn >> 20) & 0xFFF, 12);
            uint64_t addr = g_reg[rs1] + imm;
            if (funct3 == 2) g_freg[rd] = mem_read(addr, 4);
            else if (funct3 == 3) g_freg[rd] = mem_read(addr, 8);
            else illegal(insn);
            break;
        }
        case 0x27: { /* FP stores: FSW / FSD */
            int64_t imm = sx(((insn >> 25) & 0x7F) << 5 | ((insn >> 7) & 0x1F), 12);
            uint64_t addr = g_reg[rs1] + imm;
            if (funct3 == 2) mem_write(addr, g_freg[rs2], 4);
            else if (funct3 == 3) mem_write(addr, g_freg[rs2], 8);
            else illegal(insn);
            {
                const char *wp = getenv("ONYXRUN_WATCH");
                if (wp) {
                    uint64_t wa = strtoull(wp, NULL, 16);
                    if (addr <= wa && wa < addr + 8) {
                        fprintf(stderr, "WATCH: pc=%llx fstore[%llx..%llx] <- f%d=%llx (watch %llx)\n",
                                (unsigned long long)g_pc, (unsigned long long)addr,
                                (unsigned long long)(addr+8), rs2,
                                (unsigned long long)g_freg[rs2], wa);
                    }
                }
            }
            break;
        }
        case 0x53: { /* FP ops — spec-correct funct7 dispatch */
            uint32_t f7 = funct7;
            uint32_t rs2f = rs2;   /* rs2 field: conversion source subtype */
            /* FSGNJ family: S=0x10, D=0x11; funct3 0/1/2 = SGNJ/SGNJN/SGNJX */
            if (f7 == 0x10 || f7 == 0x11) {
                int is_d = (f7 == 0x11);
                if (is_d) {
                    uint64_t a = g_freg[rs1], b = g_freg[rs2f];
                    uint64_t sign = 0x8000000000000000ULL;
                    uint64_t r;
                    switch (funct3) {
                        case 0: r = (a & ~sign) | (b & sign); break;
                        case 1: r = (a & ~sign) | (~b & sign); break;
                        default: r = a ^ (b & sign); break;
                    }
                    g_freg[rd] = r;
                } else {
                    uint32_t a = (uint32_t)g_freg[rs1], b = (uint32_t)g_freg[rs2f];
                    uint32_t sign = 0x80000000u;
                    uint32_t r;
                    switch (funct3) {
                        case 0: r = (a & ~sign) | (b & sign); break;
                        case 1: r = (a & ~sign) | (~b & sign); break;
                        default: r = a ^ (b & sign); break;
                    }
                    g_freg[rd] = r;
                }
                break;
            }
            /* FMIN/FMAX: S=0x28, D=0x29 */
            if (f7 == 0x28 || f7 == 0x29) {
                if (f7 == 0x28) {
                    float a = freg_f(rs1), b = freg_f(rs2f);
                    set_freg_f(rd, (funct3 == 0) ?
                        (a < b ? a : b) : (a > b ? a : b));
                } else {
                    double a = freg_d(rs1), b = freg_d(rs2f);
                    set_freg_d(rd, (funct3 == 0) ?
                        (a < b ? a : b) : (a > b ? a : b));
                }
                break;
            }
            /* FCMP: S=0x50, D=0x51; funct3 2=EQ 1=LT 0=LE */
            if (f7 == 0x50 || f7 == 0x51) {
                bool r = false;
                if (f7 == 0x50) {
                    float a = freg_f(rs1), b = freg_f(rs2f);
                    switch (funct3) {
                        case 2: r = (a == b); break;
                        case 1: r = (a < b); break;
                        default: r = (a <= b); break;
                    }
                } else {
                    double a = freg_d(rs1), b = freg_d(rs2f);
                    switch (funct3) {
                        case 2: r = (a == b); break;
                        case 1: r = (a < b); break;
                        default: r = (a <= b); break;
                    }
                }
                g_reg[rd] = r ? 1 : 0;
                break;
            }
            /* Arithmetic: FADD 0x00/0x01, FSUB 0x04/0x05, FMUL 0x08/0x09,
             * FDIV 0x0C/0x0D, FSQRT 0x58/0x59 (rs2 must be 0). */
            switch (f7) {
                case 0x00: set_freg_f(rd, freg_f(rs1) + freg_f(rs2f)); break;
                case 0x01: set_freg_d(rd, freg_d(rs1) + freg_d(rs2f)); break;
                case 0x04: set_freg_f(rd, freg_f(rs1) - freg_f(rs2f)); break;
                case 0x05: set_freg_d(rd, freg_d(rs1) - freg_d(rs2f)); break;
                case 0x08: set_freg_f(rd, freg_f(rs1) * freg_f(rs2f)); break;
                case 0x09: set_freg_d(rd, freg_d(rs1) * freg_d(rs2f)); break;
                case 0x0C: set_freg_f(rd, freg_f(rs1) / freg_f(rs2f)); break;
                case 0x0D: set_freg_d(rd, freg_d(rs1) / freg_d(rs2f)); break;
                case 0x58: set_freg_f(rd, freg_f(rs1) < 0 ?
                    -(0.0f/0.0f) : __builtin_sqrtf(freg_f(rs1))); break;
                case 0x59: set_freg_d(rd, freg_d(rs1) < 0 ?
                    -(0.0/0.0) : __builtin_sqrt(freg_d(rs1))); break;
                default: {
                    /* Conversions and moves. */
                    switch (f7) {
                        case 0x60:  /* FCVT.W.S (rs2f: 0=W 1=WU 2=L 3=LU) */
                            if (rs2f == 0) g_reg[rd] = (uint64_t)(int64_t)(int32_t)f2i32(freg_f(rs1));
                            else g_reg[rd] = (uint64_t)(uint32_t)f2i32(freg_f(rs1));
                            break;
                        case 0x61:  /* FCVT.W.D */
                            if (rs2f == 0) g_reg[rd] = (uint64_t)(int64_t)(int32_t)f2i64(freg_d(rs1));
                            else g_reg[rd] = (uint64_t)(uint32_t)f2i64(freg_d(rs1));
                            break;
                        case 0x68:  /* FCVT.S.W / WU / L / LU */
                            switch (rs2f) {
                                case 0: set_freg_f(rd, (float)(int32_t)(uint32_t)g_reg[rs1]); break;
                                case 1: set_freg_f(rd, (float)(uint32_t)g_reg[rs1]); break;
                                case 2: set_freg_f(rd, (float)(int64_t)g_reg[rs1]); break;
                                default: set_freg_f(rd, (float)g_reg[rs1]); break;
                            }
                            break;
                        case 0x69:  /* FCVT.D.W / WU / L / LU */
                            switch (rs2f) {
                                case 0: set_freg_d(rd, (double)(int32_t)(uint32_t)g_reg[rs1]); break;
                                case 1: set_freg_d(rd, (double)(uint32_t)g_reg[rs1]); break;
                                case 2: set_freg_d(rd, (double)(int64_t)g_reg[rs1]); break;
                                default: set_freg_d(rd, (double)g_reg[rs1]); break;
                            }
                            break;
                        case 0x20:  /* FCVT.S.D (rs2f=1) */
                            set_freg_f(rd, (float)freg_d(rs1));
                            break;
                        case 0x21:  /* FCVT.D.S (rs2f=0) */
                            set_freg_d(rd, (double)freg_f(rs1));
                            break;
                        case 0x70:  /* FMV.X.W */
                            g_reg[rd] = (uint32_t)g_freg[rs1];
                            break;
                        case 0x78:  /* FMV.W.X */
                            g_freg[rd] = (uint32_t)g_reg[rs1];
                            break;
                        case 0x71:  /* FMV.X.D */
                            g_reg[rd] = g_freg[rs1];
                            break;
                        case 0x79:  /* FMV.D.X */
                            g_freg[rd] = g_reg[rs1];
                            break;
                        default:
                            illegal(insn);
                    }
                }
            }
            break;
        }
        default:
            illegal(insn);
    }
    g_reg[0] = 0;
    g_pc = npc;
}

/* ── main ───────────────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    const char *prog = NULL;
    int argi = 1;
    if (argc > 1 && strcmp(argv[1], "-v") == 0) { g_verbose = true; argi = 2; }
    if (argi >= argc) {
        fprintf(stderr, "usage: onx-run [-v] program.onx [args...]\n");
        return 2;
    }
    prog = argv[argi];

    for (int i = 0; i < MAX_HOST_FILES; i++) g_fds[i] = -1;
    g_fds[0] = 0; g_fd_names[0] = "<stdin>";
    g_fds[1] = 1; g_fd_names[1] = "<stdout>";
    g_fds[2] = 2; g_fd_names[2] = "<stderr>";

    srand((unsigned)time(NULL));
    load_onx(prog);
    setup_stack(argc - argi, argv + argi);

    /* Pre-map a 1 MiB stack region below the stack top (the kernel maps
     * the whole ustack area up front; userspace may read untouched slots
     * that must read as zero rather than fault). */
    for (uint64_t a = 0x20000000ULL - 0x100000; a < 0x20000000ULL; a += PAGE_SIZE) {
        page_get(a, false);
    }

    {
        const char *tr = getenv("ONYXRUN_TRACE");
        if (tr && atoi(tr) >= 2) g_trace_regs = 1;
    }
    g_brk = 0x01000000ULL;
    g_pc = g_entry;

    uint64_t steps = 0;
    while (!g_exited) {
        step();
        steps++;
        if (steps > 2000000000ULL) {
            fprintf(stderr, "onx-run: step limit exceeded (infinite loop?)\n");
            return 124;
        }
    }
    return (int)g_exit_code;
}
