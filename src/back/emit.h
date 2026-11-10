#ifndef BACK_EMIT_H
#define BACK_EMIT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* .onx v1 binary format (OnyxOS userspace executable) */

/* Magic number and version */
#define ONX_V1_MAGIC        0x4F4E5831u   /* "ONX1" */
#define ONX_V1_VERSION_1    1u

/* Header size and segment size */
#define ONX_V1_HEADER_SIZE  344u
#define ONX_V1_SEG_SIZE     40u
#define ONX_V1_FIXED_HDR    24u
#define ONX_V1_MAX_SEGS     4

/* Section flags */
#define ONX_VMM_R  0x1u
#define ONX_VMM_W  0x2u
#define ONX_VMM_X  0x4u

#define ONX_FLAGS_RING1 0x1u

/* Text segment base virtual address (matches CC_TEXT_VADDR in core/cc.h) */
#define ONX_TEXT_VADDR 0x00010000ULL

/* Section types for object files */
#define ONYO_SEC_TEXT   0u
#define ONYO_SEC_RODATA 1u
#define ONYO_SEC_DATA   2u
#define ONYO_SEC_BSS    3u

typedef struct {
    uint64_t vaddr;   /* virtual address */
    uint64_t filesz;  /* bytes in file */
    uint64_t memsz;   /* bytes in memory */
    uint32_t offset;  /* file offset */
    uint32_t flags;   /* R/W/X */
    uint32_t align;   /* alignment */
    uint32_t reserved;
} onx_segment_t;

#endif /* BACK_EMIT_H */