/*
 * src/back/codegen.rs — Common codegen utilities for RISC-V64.
 *
 * Shared across C/C++/Rust frontends. Provides:
 *   - Section buffer management (text, rodata, data, bss)
 *   - Codegen state machine (frame, locals, fixups)
 *   - Basic .onx emission interface
 */

#![allow(dead_code)]

/// Value kinds used by expression evaluation.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum ValKind {
    REG,
    IMM,
    SYM,
    LVAL,
    SPILL_I,
    SPILL_F,
    SPILL,
}

/// Codegen state for the current function.
#[derive(Debug, Default)]
pub struct CodegenState {
    pub return_slot: usize,
    pub frame_size: usize,
    pub cur_offset: usize,
    pub max_call_args: usize,
    pub named_gp: usize,
    pub named_fp: usize,
    pub named_stack: usize,
    pub nparams: usize,
    pub is_variadic: bool,
    pub va_save_off: usize,
    pub va_fsave_off: usize,
    pub va_local_slots: [8; usize],
    pub n_va_locals: usize,
    pub struct_local_slots: [64; usize],
    pub n_struct_locals: usize,
    pub break_label: usize,
    pub continue_label: usize,
    pub loop_depth: usize,
    pub switch_depth: usize,
    pub break_fixups: [256; usize],
    pub n_break_fixups: usize,
    pub continue_fixups: [256; usize],
    pub n_continue_fixups: usize,
    pub in_function: bool,
}

impl CodegenState {
    pub fn new() -> Self {
        CodegenState::default()
    }

    /// Reset per-file state (keeps globals, segments, fixups).
    pub fn reset_for_file(&mut self) {
        self.cur_offset = 0;
        self.n_va_locals = 0;
        self.n_struct_locals = 0;
        self.in_function = false;
        self.break_fixups = [0; 256];
        self.n_break_fixups = 0;
        self.continue_fixups = [0; 256];
        self.n_continue_fixups = 0;
    }

    /// Initialize codegen for a new function.
    pub fn gen_init(&mut self) {
        self.max_call_args = 0;
        self.named_gp = 0;
        self.named_fp = 0;
        self.named_stack = 0;
        self.nparams = 0;
        self.is_variadic = false;
        self.va_save_off = 0;
        self.va_fsave_off = 0;
        self.va_local_slots = [0; 8];
        self.struct_local_slots = [0; 64];
        self.n_va_locals = 0;
        self.n_struct_locals = 0;
    }

    /// Record a fixup for a pending branch/jump (patched later).
    pub fn record_fixup(&mut self, off: usize, label_off: usize) {
        let delta = (label_off as i32) - (off as i32);
        // Store delta; patcher applies it.
        let _ = delta;
    }

    /// Allocate stack frame offsets: returns next free slot depth.
    pub fn alloc_slot(&mut self) -> usize {
        self.cur_offset += 8;
        self.cur_offset = (self.cur_offset + 15) & !15;
        let slot = -(self.cur_offset as i64);
        if self.cur_offset > self.frame_size {
            self.frame_size = self.cur_offset;
        }
        slot as usize
    }

    /// Apply an inlining/patching callback over pending fixes (placeholder).
    pub fn resolve_pending_fixups(&mut self) {
        self.break_fixups = [0; 256];
        self.n_break_fixups = 0;
        self.continue_fixups = [0; 256];
        self.n_continue_fixups = 0;
        let _ = (self.break_label, self.continue_label);
    }
}

/// Growable byte buffer for emitted code/data.
#[derive(Debug, Default)]
pub struct Buf {
    data: Vec<u8>,
    pub capacity: usize,
}

impl Buf {
    pub fn new() -> Self { Buf { data: Vec::new(), capacity: 1024 } }

    /// Grow to fit n bytes, reallocating if needed.
    pub fn reserve(&mut self, n: usize) {
        if self.data.len() + n > self.capacity {
            self.capacity = (self.data.len() + n) * 2;
            self.data.reserve(self.capacity - self.data.len());
        }
    }

    pub fn push8(&mut self, v: u8) {
        self.reserve(1);
        self.data.push(v);
    }

    pub fn push64(&mut self, v: u64) {
        self.reserve(8);
        self.data.extend_from_slice(&v.to_le_bytes());
    }

    pub fn push32(&mut self, v: u32) {
        self.reserve(4);
        self.data.extend_from_slice(&v.to_le_bytes());
    }

    pub fn len(&self) -> usize { self.data.len() }

    pub fn as_bytes(&self) -> &[u8] { &self.data }

    pub fn clear(&mut self) { self.data.clear(); }

    pub fn is_empty(&self) -> bool { self.data.is_empty() }
}

/// All emitted sections for one translation unit.
#[derive(Debug, Default)]
pub struct Segments {
    pub text: Buf,
    pub rodata: Buf,
    pub data: Buf,
    pub bss_size: u64,
}

impl Segments {
    pub fn new() -> Self {
        Segments {
            text: Buf::new(),
            rodata: Buf::new(),
            data: Buf::new(),
            bss_size: 0,
        }
    }

    pub fn alloc_vaddr(&self, text_vaddr: u64) -> (u64, u64, u64, u64) {
        let text_size = self.text.len() as u64;
        let rodata_vaddr = (text_vaddr + text_size + 7) & !7;
        let rodata_size = self.rodata.len() as u64;
        let data_vaddr = (rodata_vaddr + rodata_size + 7) & !7;
        let data_size = self.data.len() as u64;
        let bss_vaddr = (data_vaddr + data_size + 7) & !7;
        (text_vaddr, rodata_vaddr, data_vaddr, bss_vaddr)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_codegen_state_default() {
        let s = CodegenState::new();
        assert_eq!(s.frame_size, 0);
        assert!(!s.in_function);
    }

    #[test]
    fn test_buf_push64_len() {
        let mut b = Buf::new();
        b.push64(0x1234567890ABCDEF);
        assert_eq!(b.len(), 8);
    }

    #[test]
    fn test_segments_alloc_vaddr() {
        let mut s = Segments::new();
        let (t, r, d, b) = s.alloc_vaddr(0x10000);
        assert_eq!(t, 0x10000);
        assert_eq!(r, 0x10000);
        assert_eq!(d, 0x10000);
        assert_eq!(b, 0x10000);
    }
}
