// OnyxOS Rust startup code — entry point for Rust binaries.
// Called after ELF loading by OnyxOS loader.

#![no_std]

use crate::ffi;

pub mod ffi;

#[no_mangle]
pub unsafe extern "C" fn _start(argc: usize, argv: *const *const u8) -> ! {
    // Initialize OnyxOS environment
    // Set up stack
    // Call user main()
    // Handle exit

    // Call user main (defined in main.rs)
    extern "C" {
        fn main() -> i32;
    }

    let ret = unsafe { main() };

    // Exit with the return code
    unsafe { ffi::exit(ret as u64) }
    loop {}
}
