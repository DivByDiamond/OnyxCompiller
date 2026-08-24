/*
 * onx.h — OnyxExec (.onx) binary format, synced with OnyxKernel.
 *
 * Two versions are supported by the kernel loader (kernel/src/proc/onx.rs):
 *   v1 — fixed 8-segment header, 344 bytes total
 *   v2 — dynamic segments, 32-byte fixed + nsegs*48
 *
 * OnyxCC emits v1 by default: simpler, smaller header overhead for typical
 * userspace programs which rarely need >8 PT_LOAD-equivalent segments.
 *
 * Layout (v1):
 *   [0..4]    magic   = 0x31584E4F  ("ONX1" little-endian)
 *   [4..8]    version = 1
 *   [8..16]   entry   (virtual address)
 *   [16..20]  nsegs
 *   [20..24]  flags   (bit1 = RING1)
 *   [24..344] 8 segment descriptors, 40 bytes each:
 *       [0..8]   vaddr
 *       [8..16]  filesz
 *       [16..24] memsz
 *       [24..28] offset (in file)
 *       [28..32] flags  (VMM_R | VMM_W | VMM_X)
 *       [32..36] align
 *       [36..40] reserved
 *   [344..]   raw segment data, concatenated
 *
 * The kernel requires segment vaddr to be in [USER_BASE, USER_TOP).
 * USER_BASE = 0x10000, USER_TOP = 0x40000000 (1GB).
 * Standard userspace layout (matches init/linker.ld):
 *   .text    @ 0x00010000
 *   .rodata  follows .text
 *   .data    follows .rodata
 *   .bss     follows .data (filesz < memsz, zero-filled by loader)
 *   heap     @ 0x01000000 (managed by sbrk, allocated by kernel)
 *   ustack   @ 0x20000000 (top, grown down by kernel)
 */
#ifndef ONYX_ONX_H
#define ONYX_ONX_H

#ifndef CC_FREESTANDING
#include <stdint.h>
#include <stddef.h>
#endif

#define ONX_MAGIC            0x31584E4Fu   /* 'ONX1' LE */
#define ONX_VERSION_1        1u
#define ONX_VERSION_2        2u
#define ONX_FLAGS_RING1      0x2u
#define ONX_FLAGS_COMPRESSED 0x4u   /* v2 only */

#define ONX_VMM_R  (1u << 1)
#define ONX_VMM_W  (1u << 2)
#define ONX_VMM_X  (1u << 3)

#define ONX_V1_MAX_SEGS    8
#define ONX_V1_FIXED_HDR   24
#define ONX_V1_SEG_SIZE    40
#define ONX_V1_HEADER_SIZE (ONX_V1_FIXED_HDR + ONX_V1_MAX_SEGS * ONX_V1_SEG_SIZE) /* 344 */

#define ONX_USER_BASE  0x00010000uLL
#define ONX_USER_TOP   0x40000000uLL
#define ONX_PAGE_SIZE  4096u

#pragma pack(push, 1)
typedef struct {
    uint64_t vaddr;
    uint64_t filesz;
    uint64_t memsz;
    uint32_t offset;
    uint32_t flags;
    uint32_t align;
    uint32_t reserved;
} OnxSegment;
#pragma pack(pop)

/* Segment descriptor is 40 bytes in v1 (verified by host compiler at build time). */

#endif /* ONYX_ONX_H */
