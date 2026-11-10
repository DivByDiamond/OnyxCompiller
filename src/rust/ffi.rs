// OnyxOS FFI layer for Rust — raw ecall syscall wrappers + error constants.

use core::arch::asm;

pub const SYS_write: u64 = 1;
pub const SYS_read: u64 = 2;
pub const SYS_exit: u64 = 3;
pub const SYS_yield: u64 = 4;
pub const SYS_getpid: u64 = 5;

pub const EPERM: i64 = -1;
pub const ENOENT: i64 = -2;
pub const EIO: i64 = -5;
pub const EBADF: i64 = -9;
pub const ECHILD: i64 = -10;
pub const EAGAIN: i64 = -11;
pub const ENOMEM: i64 = -12;
pub const EFAULT: i64 = -14;
pub const EBUSY: i64 = -16;
pub const EEXIST: i64 = -17;
pub const ENOTDIR: i64 = -20;
pub const EISDIR: i64 = -21;
pub const EINVAL: i64 = -22;
pub const ENOSPC: i64 = -28;
pub const EPIPE: i64 = -32;
pub const ERANGE: i64 = -34;
pub const ENOSYS: i64 = -38;
pub const ENOTEMPTY: i64 = -39;
pub const ELOOP: i64 = -40;
pub const EOVERFLOW: i64 = -75;

pub unsafe fn syscall0(n: u64) -> i64 {
    let ret: i64;
    unsafe {
        asm!("ecall",
            in("a0") n,
            lateout("a0") ret,
            options(nostack));
    }
    ret
}

pub unsafe fn syscall1(n: u64, a: u64) -> i64 {
    let ret: i64;
    unsafe {
        asm!("ecall",
            in("a0") n,
            in("a1") a,
            lateout("a0") ret,
            options(nostack));
    }
    ret
}

pub unsafe fn syscall2(n: u64, a: u64, b: u64) -> i64 {
    let ret: i64;
    unsafe {
        asm!("ecall",
            in("a0") n,
            in("a1") a,
            in("a2") b,
            lateout("a0") ret,
            options(nostack));
    }
    ret
}

pub unsafe fn syscall3(n: u64, a: u64, b: u64, c: u64) -> i64 {
    let ret: i64;
    unsafe {
        asm!("ecall",
            in("a0") n,
            in("a1") a,
            in("a2") b,
            in("a3") c,
            lateout("a0") ret,
            options(nostack));
    }
    ret
}

pub unsafe fn syscall4(n: u64, a: u64, b: u64, c: u64, d: u64) -> i64 {
    let ret: i64;
    unsafe {
        asm!("ecall",
            in("a0") n,
            in("a1") a,
            in("a2") b,
            in("a3") c,
            in("a4") d,
            lateout("a0") ret,
            options(nostack));
    }
    ret
}

pub unsafe fn syscall5(n: u64, a: u64, b: u64, c: u64, d: u64, e: u64) -> i64 {
    let ret: i64;
    unsafe {
        asm!("ecall",
            in("a0") n,
            in("a1") a,
            in("a2") b,
            in("a3") c,
            in("a4") d,
            in("a5") e,
            lateout("a0") ret,
            options(nostack));
    }
    ret
}

pub unsafe fn syscall6(n: u64, a: u64, b: u64, c: u64, d: u64, e: u64, f: u64) -> i64 {
    let ret: i64;
    unsafe {
        asm!("ecall",
            in("a0") n,
            in("a1") a,
            in("a2") b,
            in("a3") c,
            in("a4") d,
            in("a5") e,
            in("a6") f,
            lateout("a0") ret,
            options(nostack));
    }
    ret
}

pub unsafe fn write(fd: u64, buf: *const u8, len: u64) -> i64 {
    unsafe { syscall3(SYS_write, fd, buf as u64, len) }
}

pub unsafe fn read(fd: u64, buf: *mut u8, len: u64) -> i64 {
    unsafe { syscall3(SYS_read, fd, buf as u64, len) }
}

pub unsafe fn exit(code: u64) -> ! {
    unsafe { syscall1(SYS_exit, code) };
    loop {}
}
