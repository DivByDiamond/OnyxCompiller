/*
 * emit.c — .onx executable writer for onyx-ld.
 *
 * The header is a fixed 344-byte block holding the entry address and up
 * to four program segments (text, rodata, data, bss), followed by the
 * segment contents in the same order.
 */

#include <stdio.h>
#include <string.h>

#include "ld_internal.h"

#define ONX_V1_MAGIC        0x31584E4F   /* "ONX1" LE */
#define ONX_V1_VERSION_1    1
#define ONX_V1_HEADER_SIZE  344
#define ONX_V1_SEG_SIZE     40
#define ONX_V1_FIXED_HDR    24
#define ONX_VMM_R           0x1
#define ONX_VMM_W           0x2
#define ONX_VMM_X           0x4

typedef struct {
    uint64_t vaddr, filesz, memsz;
    uint32_t offset, flags, align, reserved;
} seg_t;

int onyx_ld_emit_onx(const char *path) {
    int entry_idx = ld_find_global_sym(g_ld_opts.entry_sym);
    if (entry_idx < 0) {
        fprintf(stderr, "onyx-ld: entry symbol '%s' is undefined\n", g_ld_opts.entry_sym);
        return -1;
    }
    uint64_t entry;
    if (!ld_sym_vaddr(&g_global_syms[entry_idx], &entry)) {
        fprintf(stderr, "onyx-ld: entry symbol '%s' has an invalid section\n",
                g_ld_opts.entry_sym);
        return -1;
    }

    seg_t segs[4];
    int nsegs = 0;
    segs[nsegs++] = (seg_t){ g_text_vaddr, g_out_text_size, g_out_text_size,
                             ONX_V1_HEADER_SIZE, ONX_VMM_R | ONX_VMM_X, 4, 0 };
    segs[nsegs++] = (seg_t){ g_rodata_vaddr, g_out_rodata_size, g_out_rodata_size,
                             ONX_V1_HEADER_SIZE + (uint32_t)g_out_text_size,
                             ONX_VMM_R, 16, 0 };
    if (g_out_data_size > 0) {
        segs[nsegs++] = (seg_t){ g_data_vaddr, g_out_data_size, g_out_data_size,
                                 ONX_V1_HEADER_SIZE +
                                     (uint32_t)(g_out_text_size + g_out_rodata_size),
                                 ONX_VMM_R | ONX_VMM_W, 16, 0 };
    }
    if (g_out_bss_size > 0) {
        segs[nsegs++] = (seg_t){ g_bss_vaddr, 0, g_out_bss_size, 0,
                                 ONX_VMM_R | ONX_VMM_W, 16, 0 };
    }

    uint8_t hdr[ONX_V1_HEADER_SIZE];
    memset(hdr, 0, sizeof(hdr));
    ld_wr_u32(hdr + 0, ONX_V1_MAGIC);
    ld_wr_u32(hdr + 4, ONX_V1_VERSION_1);
    ld_wr_u64(hdr + 8, entry);
    ld_wr_u32(hdr + 16, (uint32_t)nsegs);
    ld_wr_u32(hdr + 20, g_ld_opts.ring1 ? ONYX_LD_FLAG_RING1 : 0);
    for (int i = 0; i < nsegs; i++) {
        uint8_t *p = hdr + ONX_V1_FIXED_HDR + i * ONX_V1_SEG_SIZE;
        ld_wr_u64(p + 0, segs[i].vaddr);
        ld_wr_u64(p + 8, segs[i].filesz);
        ld_wr_u64(p + 16, segs[i].memsz);
        ld_wr_u32(p + 24, segs[i].offset);
        ld_wr_u32(p + 28, segs[i].flags);
        ld_wr_u32(p + 32, segs[i].align);
        ld_wr_u32(p + 36, segs[i].reserved);
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "onyx-ld: cannot open %s for writing\n", path);
        return -1;
    }
    int ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    if (ok && g_out_text_size > 0)
        ok = fwrite(g_out_text, 1, g_out_text_size, f) == g_out_text_size;
    if (ok && g_out_rodata_size > 0)
        ok = fwrite(g_out_rodata, 1, g_out_rodata_size, f) == g_out_rodata_size;
    if (ok && g_out_data_size > 0)
        ok = fwrite(g_out_data, 1, g_out_data_size, f) == g_out_data_size;
    if (fclose(f) != 0) ok = 0;
    if (!ok) {
        fprintf(stderr, "onyx-ld: failed writing %s\n", path);
        return -1;
    }

    if (g_ld_opts.verbose) {
        fprintf(stderr, "onyx-ld: emitted %s\n", path);
        fprintf(stderr, "  entry  = 0x%llx\n", (unsigned long long)entry);
        fprintf(stderr, "  text   = 0x%06llx size=%zu\n",
                (unsigned long long)g_text_vaddr, g_out_text_size);
        fprintf(stderr, "  rodata = 0x%06llx size=%zu\n",
                (unsigned long long)g_rodata_vaddr, g_out_rodata_size);
        fprintf(stderr, "  data   = 0x%06llx size=%zu\n",
                (unsigned long long)g_data_vaddr, g_out_data_size);
        fprintf(stderr, "  bss    = 0x%06llx size=%llu\n",
                (unsigned long long)g_bss_vaddr, (unsigned long long)g_out_bss_size);
    }
    return 0;
}
