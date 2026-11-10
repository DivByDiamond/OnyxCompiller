/*
 * src/back/llvm.rs — RISC-V64 instruction encoding utilities.
 *
 * Mirrors src/arch/riscv64.c in Rust. Provides:
 *   - I/R/S/B/U/J-type instruction encoders
 *   - Fixup handling for branches/jumps
 *   - .onx section layout helpers
 *   - Relocation types (R_ONYO_*)
 *
 * Intended to be compiled as a library (rustc --crate-type lib) or standalone.
 */

#![allow(dead_code)]

use core::fmt;

/// RISC-V register numbers (matching arch/riscv64.h)
mod reg {
    pub const ZERO: u8 = 0;
    pub const RA: u8 = 1;
    pub const SP: u8 = 2;
    pub const GP: u8 = 3;
    pub const TP: u8 = 4;
    pub const T0: u8 = 5;
    pub const T1: u8 = 6;
    pub const T2: u8 = 7;
    pub const S0: u8 = 8; // FP alias
    pub const S1: u8 = 9;
    pub const A0: u8 = 10;
    pub const A1: u8 = 11;
    pub const A2: u8 = 12;
    pub const A3: u8 = 13;
    pub const A4: u8 = 14;
    pub const A5: u8 = 15;
    pub const A6: u8 = 16;
    pub const A7: u8 = 17;
    pub const S2: u8 = 18;
    pub const S3: u8 = 19;
    pub const S4: u8 = 20;
    pub const S5: u8 = 21;
    pub const S6: u8 = 22;
    pub const S7: u8 = 23;
    pub const S8: u8 = 24;
    pub const S9: u8 = 25;
    pub const S10: u8 = 26;
    pub const S11: u8 = 27;
    pub const T3: u8 = 28;
    pub const T4: u8 = 29;
    pub const T5: u8 = 30;
    pub const T6: u8 = 31;

    pub const FP: u8 = S0;

    pub const FT0: u8 = 0;
    pub const FT1: u8 = 1;
    pub const FT2: u8 = 2;
    pub const FT3: u8 = 3;
    pub const FT4: u8 = 4;
    pub const FT5: u8 = 5;
    pub const FT6: u8 = 6;
    pub const FT7: u8 = 7;
    pub const FS0: u8 = 8;
    pub const FS1: u8 = 9;
    pub const FA0: u8 = 10;
    pub const FA1: u8 = 11;
    pub const FA2: u8 = 12;
    pub const FA3: u8 = 13;
    pub const FA4: u8 = 14;
    pub const FA5: u8 = 15;
    pub const FA6: u8 = 16;
    pub const FA7: u8 = 17;
    pub const FS2: u8 = 18;
    pub const FS3: u8 = 19;
    pub const FS4: u8 = 20;
    pub const FS5: u8 = 21;
    pub const FS6: u8 = 22;
    pub const FS7: u8 = 23;
    pub const FS8: u8 = 24;
    pub const FS9: u8 = 25;
    pub const FS10: u8 = 26;
    pub const FS11: u8 = 27;
    pub const FT8: u8 = 28;
    pub const FT9: u8 = 29;
    pub const FT10: u8 = 30;
    pub const FT11: u8 = 31;
}

/// RISC-V instruction opcode constants
mod opcode {
    pub const OP_IMM: u32 = 0x13;
    pub const OP_IMM_32: u32 = 0x1B;
    pub const OP: u32 = 0x33;
    pub const OP_32: u32 = 0x3B;
    pub const OP_LOAD: u32 = 0x03;
    pub const OP_FLOAD: u32 = 0x07;
    pub const OP_STORE: u32 = 0x23;
    pub const OP_FSTORE: u32 = 0x27;
    pub const OP_BRANCH: u32 = 0x63;
    pub const OP_JALR: u32 = 0x67;
    pub const OP_JAL: u32 = 0x6F;
    pub const OP_LUI: u32 = 0x37;
    pub const OP_AUIPC: u32 = 0x17;
    pub const OP_FP: u32 = 0x53;
    pub const OP_SYSTEM: u32 = 0x73;
}

/// Instruction encoders (emit 32-bit LE into buffer)
mod enc {
    use crate::reg::*;
    use crate::opcode::*;

    #[inline]
    pub fn emit32(buf: &mut Vec<u8>, insn: u32) {
        buf.extend_from_slice(&insn.to_le_bytes());
    }

    #[inline]
    pub fn addi(buf: &mut Vec<u8>, rd: u8, rs1: u8, imm: i32) {
        let insn = ((imm as u32 & 0xFFF) << 20)
            | ((rs1 as u32 & 0x1F) << 15)
            | (0u32 << 12)
            | ((rd as u32 & 0x1F) << 7)
            | OP_IMM;
        emit32(buf, insn);
    }

    #[inline]
    pub fn addi_imm(buf: &mut Vec<u8>, rd: u8, rs1: u8, imm: i64) {
        if imm >= -2048 && imm <= 2047 {
            addi(buf, rd, rs1, imm as i32);
            return;
        }
        if rs1 == ZERO {
            load_immediate(buf, rd, imm);
            return;
        }
        let temp = if rs1 == T6 { T5 } else { T6 };
        load_immediate(buf, temp, imm);
        add(buf, rd, rs1, temp);
    }

    #[inline]
    fn load_immediate(buf: &mut Vec<u8>, rd: u8, mut value: i64) {
        if value >= -2048 && value <= 2047 {
            addi(buf, rd, ZERO, value as i32);
            return;
        }
        let low = ((value & 4095) ^ 2048) - 2048;
        let high = value >> 12;
        let carry = if low < 0 { 1 } else { 0 };
        load_immediate(buf, rd, high + carry);
        slli(buf, rd, rd, 12);
        if low != 0 { addi(buf, rd, rd, low as i32); }
    }

    #[inline]
    pub fn lui(buf: &mut Vec<u8>, rd: u8, imm20: u32) {
        let insn = ((imm20 & 0xFFFFF) << 12)
            | ((rd as u32 & 0x1F) << 7)
            | OP_LUI;
        emit32(buf, insn);
    }

    #[inline]
    pub fn auipc(buf: &mut Vec<u8>, rd: u8, imm20: u32) {
        let insn = ((imm20 & 0xFFFFF) << 12)
            | ((rd as u32 & 0x1F) << 7)
            | OP_AUIPC;
        emit32(buf, insn);
    }

    #[inline]
    pub fn add(buf: &mut Vec<u8>, rd: u8, rs1: u8, rs2: u8) {
        emit_r_type(buf, 0x0, 0x00, rd, rs1, rs2);
    }

    #[inline]
    pub fn sub(buf: &mut Vec<u8>, rd: u8, rs1: u8, rs2: u8) {
        emit_r_type(buf, 0x0, 0x20, rd, rs1, rs2);
    }

    #[inline]
    pub fn mul(buf: &mut Vec<u8>, rd: u8, rs1: u8, rs2: u8) {
        emit_r_type(buf, 0x0, 0x01, rd, rs1, rs2);
    }

    #[inline]
    pub fn div(buf: &mut Vec<u8>, rd: u8, rs1: u8, rs2: u8) {
        emit_r_type(buf, 0x4, 0x01, rd, rs1, rs2);
    }

    #[inline]
    pub fn sll(buf: &mut Vec<u8>, rd: u8, rs1: u8, rs2: u8) {
        emit_r_type(buf, 0x1, 0x00, rd, rs1, rs2);
    }

    #[inline]
    pub fn srl(buf: &mut Vec<u8>, rd: u8, rs1: u8, rs2: u8) {
        emit_r_type(buf, 0x5, 0x00, rd, rs1, rs2);
    }

    #[inline]
    pub fn sra(buf: &mut Vec<u8>, rd: u8, rs1: u8, rs2: u8) {
        emit_r_type(buf, 0x5, 0x20, rd, rs1, rs2);
    }

    #[inline]
    pub fn slt(buf: &mut Vec<u8>, rd: u8, rs1: u8, rs2: u8) {
        emit_r_type(buf, 0x2, 0x00, rd, rs1, rs2);
    }

    #[inline]
    pub fn sltu(buf: &mut Vec<u8>, rd: u8, rs1: u8, rs2: u8) {
        emit_r_type(buf, 0x3, 0x00, rd, rs1, rs2);
    }

    #[inline]
    fn emit_r_type(buf: &mut Vec<u8>, funct3: u32, funct7: u32, rd: u8, rs1: u8, rs2: u8) {
        let insn = (funct7 << 25)
            | ((rs2 as u32 & 0x1F) << 20)
            | ((rs1 as u32 & 0x1F) << 15)
            | ((funct3 & 0x7) << 12)
            | ((rd as u32 & 0x1F) << 7)
            | OP;
        emit32(buf, insn);
    }

    #[inline]
    pub fn lb(buf: &mut Vec<u8>, rd: u8, rs1: u8, imm: i32) { emit_i_load(buf, 0x0, rd, rs1, imm); }
    #[inline]
    pub fn lh(buf: &mut Vec<u8>, rd: u8, rs1: u8, imm: i32) { emit_i_load(buf, 0x1, rd, rs1, imm); }
    #[inline]
    pub fn lw(buf: &mut Vec<u8>, rd: u8, rs1: u8, imm: i32) { emit_i_load(buf, 0x2, rd, rs1, imm); }
    #[inline]
    pub fn ld(buf: &mut Vec<u8>, rd: u8, rs1: u8, imm: i32) { emit_i_load(buf, 0x3, rd, rs1, imm); }
    #[inline]
    pub fn lbu(buf: &mut Vec<u8>, rd: u8, rs1: u8, imm: i32) { emit_i_load(buf, 0x4, rd, rs1, imm); }
    #[inline]
    pub fn lhu(buf: &mut Vec<u8>, rd: u8, rs1: u8, imm: i32) { emit_i_load(buf, 0x5, rd, rs1, imm); }
    #[inline]
    pub fn lwu(buf: &mut Vec<u8>, rd: u8, rs1: u8, imm: i32) { emit_i_load(buf, 0x6, rd, rs1, imm); }

    fn emit_i_load(buf: &mut Vec<u8>, funct3: u32, rd: u8, rs1: u8, imm: i32) {
        if imm < -2048 || imm > 2047 {
            let temp = if rs1 == T6 { T5 } else { T6 };
            addi_imm(buf, temp, rs1, imm as i64);
            emit_i_load(buf, funct3, rd, temp, 0);
            return;
        }
        let insn = ((imm as u32 & 0xFFF) << 20)
            | ((rs1 as u32 & 0x1F) << 15)
            | ((funct3 & 0x7) << 12)
            | ((rd as u32 & 0x1F) << 7)
            | OP_LOAD;
        emit32(buf, insn);
    }

    #[inline]
    pub fn sb(buf: &mut Vec<u8>, rs2: u8, rs1: u8, imm: i32) { emit_s_store(buf, 0x0, rs2, rs1, imm); }
    #[inline]
    pub fn sh(buf: &mut Vec<u8>, rs2: u8, rs1: u8, imm: i32) { emit_s_store(buf, 0x1, rs2, rs1, imm); }
    #[inline]
    pub fn sw(buf: &mut Vec<u8>, rs2: u8, rs1: u8, imm: i32) { emit_s_store(buf, 0x2, rs2, rs1, imm); }
    #[inline]
    pub fn sd(buf: &mut Vec<u8>, rs2: u8, rs1: u8, imm: i32) { emit_s_store(buf, 0x3, rs2, rs1, imm); }

    fn emit_s_store(buf: &mut Vec<u8>, funct3: u32, rs2: u8, rs1: u8, imm: i32) {
        if imm < -2048 || imm > 2047 {
            let temp = if rs1 == T6 { T5 } else { T6 };
            addi_imm(buf, temp, rs1, imm as i64);
            emit_s_store(buf, funct3, rs2, temp, 0);
            return;
        }
        let imm_lo = (imm as u32 & 0x1F) << 7;
        let imm_hi = ((imm as u32 >> 5) & 0x7F) << 25;
        let insn = imm_hi
            | ((rs2 as u32 & 0x1F) << 20)
            | ((rs1 as u32 & 0x1F) << 15)
            | ((funct3 & 0x7) << 12)
            | imm_lo
            | OP_STORE;
        emit32(buf, insn);
    }

    #[inline]
    pub fn beq(buf: &mut Vec<u8>, rs1: u8, rs2: u8, off13: i32) { emit_b(buf, 0x0, rs1, rs2, off13); }
    #[inline]
    pub fn bne(buf: &mut Vec<u8>, rs1: u8, rs2: u8, off13: i32) { emit_b(buf, 0x1, rs1, rs2, off13); }
    #[inline]
    pub fn blt(buf: &mut Vec<u8>, rs1: u8, rs2: u8, off13: i32) { emit_b(buf, 0x4, rs1, rs2, off13); }
    #[inline]
    pub fn bge(buf: &mut Vec<u8>, rs1: u8, rs2: u8, off13: i32) { emit_b(buf, 0x5, rs1, rs2, off13); }
    #[inline]
    pub fn bltu(buf: &mut Vec<u8>, rs1: u8, rs2: u8, off13: i32) { emit_b(buf, 0x6, rs1, rs2, off13); }
    #[inline]
    pub fn bgeu(buf: &mut Vec<u8>, rs1: u8, rs2: u8, off13: i32) { emit_b(buf, 0x7, rs1, rs2, off13); }

    fn emit_b(buf: &mut Vec<u8>, funct3: u32, rs1: u8, rs2: u8, off13: i32) {
        let imm = off13 as u32;
        let b12 = (imm & 0x1000) >> 12;
        let b11 = (imm & 0x800) >> 11;
        let b10_5 = (imm & 0x7E0) >> 5;
        let b4_1 = (imm & 0x1E) >> 1;
        let insn = (b12 << 31)
            | (b10_5 << 25)
            | ((rs2 as u32 & 0x1F) << 20)
            | ((rs1 as u32 & 0x1F) << 15)
            | ((funct3 & 0x7) << 12)
            | (b4_1 << 8)
            | (b11 << 7)
            | OP_BRANCH;
        emit32(buf, insn);
    }

    #[inline]
    pub fn jal(buf: &mut Vec<u8>, rd: u8, off21: i32) {
        let imm = off21 as u32;
        let b20 = (imm & 0x100000) >> 20;
        let b10_1 = (imm & 0x7FE) >> 1;
        let b11 = (imm & 0x800) >> 11;
        let b19_12 = (imm & 0xFF000) >> 12;
        let insn = (b20 << 31)
            | (b10_1 << 21)
            | (b11 << 20)
            | (b19_12 << 12)
            | ((rd as u32 & 0x1F) << 7)
            | OP_JAL;
        emit32(buf, insn);
    }

    #[inline]
    pub fn jalr(buf: &mut Vec<u8>, rd: u8, rs1: u8, imm: i32) {
        let insn = ((imm as u32 & 0xFFF) << 20)
            | ((rs1 as u32 & 0x1F) << 15)
            | (0u32 << 12)
            | ((rd as u32 & 0x1F) << 7)
            | OP_JALR;
        emit32(buf, insn);
    }

    #[inline]
    pub fn ecall(buf: &mut Vec<u8>) { emit32(buf, 0x00000073); }
    #[inline]
    pub fn ebreak(buf: &mut Vec<u8>) { emit32(buf, 0x00100073); }
    #[inline]
    pub fn fence(buf: &mut Vec<u8>) { emit32(buf, 0x0FF0000F); }
    #[inline]
    pub fn ret(buf: &mut Vec<u8>) { jalr(buf, ZERO, RA, 0); }
    #[inline]
    pub fn mv(buf: &mut Vec<u8>, rd: u8, rs: u8) { addi(buf, rd, rs, 0); }
    #[inline]
    pub fn neg(buf: &mut Vec<u8>, rd: u8, rs: u8) { sub(buf, rd, ZERO, rs); }

    // Float/double
    #[inline]
    pub fn flw(buf: &mut Vec<u8>, rd: u8, rs1: u8, imm: i32) {
        if imm < -2048 || imm > 2047 {
            let temp = if rs1 == T6 { T5 } else { T6 };
            addi_imm(buf, temp, rs1, imm as i64);
            flw(buf, rd, temp, 0);
            return;
        }
        let insn = ((imm as u32 & 0xFFF) << 20)
            | ((rs1 as u32 & 0x1F) << 15)
            | (0x2 << 12)
            | ((rd as u32 & 0x1F) << 7)
            | OP_FLOAD;
        emit32(buf, insn);
    }

    #[inline]
    pub fn fld(buf: &mut Vec<u8>, rd: u8, rs1: u8, imm: i32) {
        if imm < -2048 || imm > 2047 {
            let temp = if rs1 == T6 { T5 } else { T6 };
            addi_imm(buf, temp, rs1, imm as i64);
            fld(buf, rd, temp, 0);
            return;
        }
        let insn = ((imm as u32 & 0xFFF) << 20)
            | ((rs1 as u32 & 0x1F) << 15)
            | (0x3 << 12)
            | ((rd as u32 & 0x1F) << 7)
            | OP_FLOAD;
        emit32(buf, insn);
    }

    #[inline]
    pub fn fsw(buf: &mut Vec<u8>, rs2: u8, rs1: u8, imm: i32) {
        if imm < -2048 || imm > 2047 {
            let temp = if rs1 == T6 { T5 } else { T6 };
            addi_imm(buf, temp, rs1, imm as i64);
            fsw(buf, rs2, temp, 0);
            return;
        }
        let imm_lo = (imm as u32 & 0x1F) << 7;
        let imm_hi = ((imm as u32 >> 5) & 0x7F) << 25;
        let insn = imm_hi
            | ((rs2 as u32 & 0x1F) << 20)
            | ((rs1 as u32 & 0x1F) << 15)
            | (0x2 << 12)
            | imm_lo
            | OP_FSTORE;
        emit32(buf, insn);
    }

    #[inline]
    pub fn fsd(buf: &mut Vec<u8>, rs2: u8, rs1: u8, imm: i32) {
        if imm < -2048 || imm > 2047 {
            let temp = if rs1 == T6 { T5 } else { T6 };
            addi_imm(buf, temp, rs1, imm as i64);
            fsd(buf, rs2, temp, 0);
            return;
        }
        let imm_lo = (imm as u32 & 0x1F) << 7;
        let imm_hi = ((imm as u32 >> 5) & 0x7F) << 25;
        let insn = imm_hi
            | ((rs2 as u32 & 0x1F) << 20)
            | ((rs1 as u32 & 0x1F) << 15)
            | (0x3 << 12)
            | imm_lo
            | OP_FSTORE;
        emit32(buf, insn);
    }
}

/// Relocation types (matching onyxo.h)
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum RelocType {
    R_ONYO_64 = 1,
    R_ONYO_32 = 2,
    R_ONYO_HI20 = 3,
    R_ONYO_LO12_I = 4,
    R_ONYO_LO12_S = 5,
    R_ONYO_PCREL_HI20 = 6,
    R_ONYO_PCREL_LO12_I = 7,
    R_ONYO_PCREL_LO12_S = 8,
    R_ONYO_JAL = 9,
    R_ONYO_BRANCH = 10,
}

/// Section types
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum SectionType {
    TEXT = 0,
    RODATA = 1,
    DATA = 2,
    BSS = 3,
}

/// Section flags
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum SectionFlag {
    R = 0x1,
    W = 0x2,
    X = 0x4,
}

/// Symbol flags
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum SymbolFlag {
    DEFINED = 0x01,
    UNDEF = 0x02,
    GLOBAL = 0x04,
    LOCAL = 0x08,
    FUNC = 0x10,
    DATA = 0x20,
    ENTRY = 0x40,
}

/// .onx v1 header
#[derive(Copy, Clone, Debug)]
pub struct OnxHeader {
    pub magic: u32,
    pub version: u32,
    pub entry: u64,
    pub nsegs: u32,
    pub flags: u32,
    pub segments: [OnxSegment; 4],
}

#[derive(Copy, Clone, Debug)]
pub struct OnxSegment {
    pub vaddr: u64,
    pub filesz: u64,
    pub memsz: u64,
    pub offset: u32,
    pub flags: u32,
    pub align: u32,
    pub reserved: u32,
}

/// Relocation entry
#[derive(Copy, Clone, Debug)]
pub struct Reloc {
    pub section_idx: u32,
    pub reloc_type: RelocType,
    pub offset: u64,
    pub sym_idx: u32,
    pub addend: i32,
}

/// Symbol entry
#[derive(Copy, Clone, Debug)]
pub struct Symbol {
    pub name: [u8; 256],
    pub flags: u32,
    pub section_idx: i32,
    pub value: u64,
    pub size: u64,
    pub obj_idx: usize,
}

/// Object file representation
#[derive(Clone, Debug)]
pub struct ObjectFile {
    pub name: String,
    pub sections: Vec<Section>,
    pub symbols: Vec<Symbol>,
    pub relocs: Vec<Reloc>,
}

#[derive(Clone, Debug)]
pub struct Section {
    pub name_off: u32,
    pub sec_type: SectionType,
    pub flags: u32,
    pub align: u32,
    pub size: u64,
    pub data_off: u64,
    pub data: Vec<u8>,
}

impl OnxHeader {
    pub fn new() -> Self {
        OnxHeader {
            magic: 0x4F4E5831,
            version: 1,
            entry: 0,
            nsegs: 0,
            flags: 0,
            segments: [OnxSegment::default(); 4],
        }
    }

    pub fn write(&self, out: &mut Vec<u8>) {
        out.extend_from_slice(&self.magic.to_le_bytes());
        out.extend_from_slice(&self.version.to_le_bytes());
        out.extend_from_slice(&self.entry.to_le_bytes());
        out.extend_from_slice(&self.nsegs.to_le_bytes());
        out.extend_from_slice(&self.flags.to_le_bytes());
        out.extend_from_slice(&[0u8; 12]); // reserved

        for seg in &self.segments {
            if seg.filesz == 0 && seg.memsz == 0 { continue; }
            out.extend_from_slice(&seg.vaddr.to_le_bytes());
            out.extend_from_slice(&seg.filesz.to_le_bytes());
            out.extend_from_slice(&seg.memsz.to_le_bytes());
            out.extend_from_slice(&seg.offset.to_le_bytes());
            out.extend_from_slice(&seg.flags.to_le_bytes());
            out.extend_from_slice(&seg.align.to_le_bytes());
            out.extend_from_slice(&seg.reserved.to_le_bytes());
        }
    }
}

impl Default for OnxSegment {
    fn default() -> Self {
        OnxSegment {
            vaddr: 0,
            filesz: 0,
            memsz: 0,
            offset: 0,
            flags: 0,
            align: 0,
            reserved: 0,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_encode_addi() {
        let mut buf = Vec::new();
        enc::addi(&mut buf, reg::A0, reg::ZERO, 42);
        assert_eq!(buf.len(), 4);
        let insn = u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]);
        assert_eq!(insn & 0x7F, 0x13);
    }

    #[test]
    fn test_encode_lui() {
        let mut buf = Vec::new();
        enc::lui(&mut buf, reg::T0, 0x10000);
        assert_eq!(buf.len(), 4);
        let insn = u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]);
        assert_eq!(insn & 0x7F, 0x37);
    }

    #[test]
    fn test_emit_jal() {
        let mut buf = Vec::new();
        enc::jal(&mut buf, reg::RA, 0x1000);
        assert_eq!(buf.len(), 4);
        let insn = u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]);
        assert_eq!(insn & 0x7F, 0x6F);
    }

    #[test]
    fn test_onx_header() {
        let mut hdr = OnxHeader::new();
        hdr.entry = 0x10000;
        hdr.nsegs = 2;
        let mut out = Vec::new();
        hdr.write(&mut out);
        assert_eq!(out.len(), 344);
    }
}