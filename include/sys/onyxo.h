/*
 * onyxo.h — OnyxOS Object File Format (.o) and Static Archive (.a)
 *
 * The .o format is a minimal relocatable object format designed for the
 * OnyxCC → onyx-ld → .onx pipeline. It is simpler than ELF: no DWARF,
 * no section flags beyond R/W/X, no per-section relocations table (one
 * global reloc table is enough for our use cases).
 *
 * Layout (all integers little-endian):
 *
 *   0x00  magic[4]      = "ONYO"
 *   0x04  version       = 1            (u32)
 *   0x08  flags         = 0            (u32)  [bit 0 = RING1]
 *   0x0C  n_sections     (u32)
 *   0x10  n_symbols      (u32)
 *   0x14  n_relocs       (u32)
 *   0x18  strtab_size    (u32)
 *   0x1C  entry_sym_off  (u32)  -- 0xFFFFFFFF if no entry symbol
 *   0x20  reserved[32]
 *
 *   [Section header: 40 bytes each]
 *     0x00  name_off      (u32)   -- offset into strtab
 *     0x04  type          (u32)   -- 0=text 1=rodata 2=data 3=bss
 *     0x08  flags         (u32)   -- 0x1=R, 0x2=W, 0x4=X
 *     0x0C  align         (u32)
 *     0x10  size          (u64)
 *     0x18  data_off      (u64)   -- offset from start of file
 *
 *   [Symbol entry: 32 bytes each]
 *     0x00  name_off      (u32)
 *     0x04  flags        (u32)  -- 0x1=defined, 0x2=undef, 0x4=global,
 *                                  0x8=local, 0x10=function, 0x20=data,
 *                                  0x40=entry
 *     0x08  section_idx   (s32)  -- -1 if undef
 *     0x0C  reserved      (u32)
 *     0x10  value         (u64)  -- offset within section (if defined)
 *     0x18  size          (u64)
 *
 *   [Reloc entry: 24 bytes each]
 *     0x00  section_idx   (u32)  -- which section the patch is in
 *     0x04  type          (u32)  -- reloc type (see R_ONYO_*)
 *     0x08  offset        (u64)  -- offset within that section
 *     0x10  sym_idx       (u32)  -- index into the symbol table
 *     0x14  addend        (s32)  -- added to symbol value
 *
 *   [String table: nstrtab_size bytes, NUL-terminated entries]
 *
 *   [Section data: raw bytes, in section-header order]
 *
 * Reloc types (RISC-V subset):
 *   R_ONYO_64              -- 8-byte absolute address
 *   R_ONYO_32              -- 4-byte absolute address
 *   R_ONYO_HI20            -- lui imm20 (bits 31:12 of sym value + 0x800)
 *   R_ONYO_LO12_I          -- addi imm12 (bits 11:0) for I-type
 *   R_ONYO_LO12_S          -- sw imm12 (bits 11:0) for S-type
 *   R_ONYO_PCREL_HI20     -- auipc imm20 (PC-relative upper 20)
 *   R_ONYO_PCREL_LO12_I   -- addi imm12 (PC-relative lower 12) I-type
 *   R_ONYO_PCREL_LO12_S   -- sw imm12 (PC-relative lower 12) S-type
 *   R_ONYO_JAL            -- jal imm21 (function call)
 *   R_ONYO_BRANCH         -- beq/bne imm13 (conditional branch)
 *
 * The OnyxOS static archive format (.a) is the standard Unix `ar` format:
 *   "!<arch>\n" magic, followed by file headers and file data, where each
 *   file is a .o object. See onyx-ld source for parsing details.
 */
#ifndef ONYXO_H
#define ONYXO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ONYO_MAGIC0 'O'
#define ONYO_MAGIC1 'N'
#define ONYO_MAGIC2 'Y'
#define ONYO_MAGIC3 'O'
#define ONYO_MAGIC 0x4F594E4F  /* "ONYO" little-endian = 0x4F,0x59,0x4E,0x4F */

#define ONYO_VERSION_1  1

#define ONYO_FLAG_RING1 0x1

#define ONYO_HDR_SIZE        64
#define ONYO_SECTION_HDR_SIZE 40
#define ONYO_SYMBOL_SIZE     32
#define ONYO_RELOC_SIZE      24

/* Section types. */
#define ONYO_SEC_TEXT   0
#define ONYO_SEC_RODATA 1
#define ONYO_SEC_DATA   2
#define ONYO_SEC_BSS    3

/* Section flags. */
#define ONYO_SEC_R  0x1
#define ONYO_SEC_W  0x2
#define ONYO_SEC_X  0x4

/* Symbol flags. */
#define ONYO_SYM_DEFINED  0x01
#define ONYO_SYM_UNDEF    0x02
#define ONYO_SYM_GLOBAL  0x04
#define ONYO_SYM_LOCAL   0x08
#define ONYO_SYM_FUNC    0x10
#define ONYO_SYM_DATA    0x20
#define ONYO_SYM_ENTRY   0x40

/* Reloc types. */
#define R_ONYO_64            1
#define R_ONYO_32            2
#define R_ONYO_HI20          3
#define R_ONYO_LO12_I        4
#define R_ONYO_LO12_S        5
#define R_ONYO_PCREL_HI20    6
#define R_ONYO_PCREL_LO12_I  7
#define R_ONYO_PCREL_LO12_S  8
#define R_ONYO_JAL          9
#define R_ONYO_BRANCH       10

/* Special "no entry" sentinel for the entry_sym_off field. */
#define ONYO_NO_ENTRY 0xFFFFFFFFu

#ifdef __cplusplus
}
#endif

#endif /* ONYXO_H */
