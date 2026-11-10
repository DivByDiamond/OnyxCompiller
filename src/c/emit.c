/*
 * emit.c — .onx v1 emitter.
 *
 * Takes the four logical segments (.text, .rodata, .data, .bss) accumulated
 * by the code generator and writes a complete .onx v1 binary compatible
 * with OnyxKernel/kernel/src/proc/onx.rs::load().
 *
 * Segment layout produced:
 *   seg[0] = .text   @ CC_TEXT_VADDR, R+X
 *   seg[1] = .rodata @ aligned after .text, R
 *   seg[2] = .data   @ aligned after .rodata, R+W
 *   seg[3] = .bss    @ aligned after .data, R+W, filesz=0
 *
 * The kernel loader (onx.rs:41) rejects segments whose vaddr is outside
 * [USER_BASE, USER_TOP). All our segments satisfy this because we start at
 * 0x10000 and stay well below 0x40000000 for typical userspace programs.
 *
 * The kernel allocates the user stack and heap itself; we must NOT emit
 * segments for them.
 */
#include "core/compat.h"

#include "core/cc.h"

static void put_u32le(uint8_t *p, uint32_t v) {
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff;
    p[3] = (v >> 24) & 0xff;
}

static void put_u64le(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        p[i] = v & 0xff;
        v >>= 8;
    }
}

static void write_segment_v1(uint8_t *p, const OnxSegment *s) {
    put_u64le(p + 0,  s->vaddr);
    put_u64le(p + 8,  s->filesz);
    put_u64le(p + 16, s->memsz);
    put_u32le(p + 24, s->offset);
    put_u32le(p + 28, s->flags);
    put_u32le(p + 32, s->align);
    put_u32le(p + 36, s->reserved);
}

int onx_emit(const char *path,
             const cc_buf_t *text,
             const cc_buf_t *rodata,
             const cc_buf_t *data,
             uint64_t bss_size,
             uint64_t entry,
             bool ring1)
{
    /* Compute virtual addresses for the four segments. */
    uint64_t text_vaddr   = CC_TEXT_VADDR;
    uint64_t rodata_vaddr = (text_vaddr + text->size + CC_RODATA_ALIGN - 1)
                            & ~(uint64_t)(CC_RODATA_ALIGN - 1);
    uint64_t data_vaddr   = (rodata_vaddr + (rodata ? rodata->size : 0) + CC_DATA_ALIGN - 1)
                            & ~(uint64_t)(CC_DATA_ALIGN - 1);
    uint64_t bss_vaddr    = (data_vaddr + (data ? data->size : 0) + CC_BSS_ALIGN - 1)
                            & ~(uint64_t)(CC_BSS_ALIGN - 1);

    /* If a segment has zero size, we still keep its descriptor for clarity
     * but set filesz=memsz=0. The loader handles this fine. */
    OnxSegment segs[ONX_V1_MAX_SEGS];
    memset(segs, 0, sizeof(segs));

    int nsegs = 0;

    /* .text — R+X */
    segs[nsegs].vaddr   = text_vaddr;
    segs[nsegs].filesz  = text->size;
    segs[nsegs].memsz   = text->size;
    segs[nsegs].offset  = ONX_V1_HEADER_SIZE;
    segs[nsegs].flags   = ONX_VMM_R | ONX_VMM_X;
    segs[nsegs].align   = 4;
    nsegs++;

    /* .rodata — R (always present, may be empty) */
    segs[nsegs].vaddr   = rodata_vaddr;
    segs[nsegs].filesz  = rodata ? rodata->size : 0;
    segs[nsegs].memsz   = rodata ? rodata->size : 0;
    segs[nsegs].offset  = ONX_V1_HEADER_SIZE + text->size;
    segs[nsegs].flags   = ONX_VMM_R;
    segs[nsegs].align   = CC_RODATA_ALIGN;
    nsegs++;

    /* .data — R+W (may be empty) */
    segs[nsegs].vaddr   = data_vaddr;
    segs[nsegs].filesz  = data ? data->size : 0;
    segs[nsegs].memsz   = data ? data->size : 0;
    segs[nsegs].offset  = ONX_V1_HEADER_SIZE + text->size + (rodata ? rodata->size : 0);
    segs[nsegs].flags   = ONX_VMM_R | ONX_VMM_W;
    segs[nsegs].align   = CC_DATA_ALIGN;
    nsegs++;

    /* .bss — R+W, filesz=0, memsz=bss_size (zero-filled by loader) */
    if (bss_size > 0) {
        segs[nsegs].vaddr   = bss_vaddr;
        segs[nsegs].filesz  = 0;
        segs[nsegs].memsz   = bss_size;
        segs[nsegs].offset  = 0;  /* no file data */
        segs[nsegs].flags   = ONX_VMM_R | ONX_VMM_W;
        segs[nsegs].align   = CC_BSS_ALIGN;
        nsegs++;
    }

    /* Build 344-byte header. */
    uint8_t hdr[ONX_V1_HEADER_SIZE];
    memset(hdr, 0, sizeof(hdr));
    put_u32le(hdr + 0,  ONX_MAGIC);
    put_u32le(hdr + 4,  ONX_VERSION_1);
    put_u64le(hdr + 8,  entry);
    put_u32le(hdr + 16, (uint32_t)nsegs);
    put_u32le(hdr + 20, ring1 ? ONX_FLAGS_RING1 : 0);

    for (int i = 0; i < nsegs; i++) {
        write_segment_v1(hdr + ONX_V1_FIXED_HDR + i * ONX_V1_SEG_SIZE, &segs[i]);
    }

    /* Write to file. */
    FILE *f = fopen(path, "wb");
    if (!f) {
        cc_fatal("emit: cannot open %s for writing", path);
    }

    fwrite(hdr, 1, sizeof(hdr), f);
    if (text->size)   fwrite(text->data, 1, text->size, f);
    if (rodata && rodata->size) fwrite(rodata->data, 1, rodata->size, f);
    if (data && data->size)     fwrite(data->data, 1, data->size, f);

    fclose(f);

    if (g_opts.verbose) {
        fprintf(stderr, "emit: %s -> %s (entry=0x%llx, nsegs=%d, ring=%d)\n",
                g_opts.input ? g_opts.input : "<stdin>",
                path,
                (unsigned long long)entry,
                nsegs,
                ring1 ? 1 : 2);
        fprintf(stderr, "  text   vaddr=0x%06llx size=%zu\n",
                (unsigned long long)text_vaddr, text->size);
        fprintf(stderr, "  rodata vaddr=0x%06llx size=%zu\n",
                (unsigned long long)rodata_vaddr, rodata ? rodata->size : 0);
        fprintf(stderr, "  data   vaddr=0x%06llx size=%zu\n",
                (unsigned long long)data_vaddr, data ? data->size : 0);
        fprintf(stderr, "  bss    vaddr=0x%06llx size=%llu\n",
                (unsigned long long)bss_vaddr, (unsigned long long)bss_size);
    }

    return 0;
}
