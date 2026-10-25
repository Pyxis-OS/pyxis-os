#![no_std]
#![feature(alloc_error_handler)]

use core::alloc::{GlobalAlloc, Layout};
use core::ffi::{c_char, c_int, c_void};
use core::fmt::{self, Write};
use core::{mem, ptr};

// These declarations match the Pyxis SDK's stdlib.h, stdio.h and math.h.
// FILE remains libc-owned and opaque; libc startup initializes both streams.
#[repr(C)]
struct File {
    _private: [u8; 0],
}

unsafe extern "C" {
    fn malloc(size: usize) -> *mut c_void;
    fn free(pointer: *mut c_void);
    fn fwrite(buffer: *const c_void, size: usize, count: usize, stream: *mut File) -> usize;
    fn fflush(stream: *mut File) -> c_int;
    fn _Exit(status: c_int) -> !;
    pub fn fma(x: f64, y: f64, z: f64) -> f64;
    static mut stdout: *mut File;
    static mut stderr: *mut File;
}

struct LibcAllocator;

unsafe impl GlobalAlloc for LibcAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let alignment = layout.align().max(mem::align_of::<*mut c_void>());
        let Some(size) = layout.size().checked_add(mem::size_of::<*mut c_void>())
            .and_then(|size| size.checked_add(alignment - 1))
            .filter(|size| *size <= isize::MAX as usize)
        else {
            return ptr::null_mut();
        };
        let base = unsafe { malloc(size) }.cast::<u8>();
        if base.is_null() {
            return ptr::null_mut();
        }

        // Keep malloc's base immediately before the aligned payload. Pointer
        // arithmetic stays within the checked allocation and retains provenance.
        let payload = unsafe { base.add(mem::size_of::<*mut c_void>()) };
        let padding = payload.addr().wrapping_neg() & (alignment - 1);
        let payload = unsafe { payload.add(padding) };
        unsafe { payload.cast::<*mut c_void>().sub(1).write(base.cast()) };
        payload
    }

    unsafe fn dealloc(&self, pointer: *mut u8, _layout: Layout) {
        let base = unsafe { pointer.cast::<*mut c_void>().sub(1).read() };
        unsafe { free(base) };
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        let pointer = unsafe { self.alloc(layout) };
        if !pointer.is_null() {
            unsafe { pointer.write_bytes(0, layout.size()) };
        }
        pointer
    }

    unsafe fn realloc(&self, pointer: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        let Ok(new_layout) = Layout::from_size_align(new_size, layout.align()) else {
            return ptr::null_mut();
        };
        let replacement = unsafe { self.alloc(new_layout) };
        if !replacement.is_null() {
            unsafe { ptr::copy_nonoverlapping(pointer, replacement, layout.size().min(new_size)) };
            unsafe { self.dealloc(pointer, layout) };
        }
        replacement
    }
}

#[global_allocator]
static ALLOCATOR: LibcAllocator = LibcAllocator;

struct Output(*mut File);

impl Write for Output {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        let written = unsafe { fwrite(text.as_ptr().cast(), 1, text.len(), self.0) };
        if written == text.len() { Ok(()) } else { Err(fmt::Error) }
    }
}

pub fn print(arguments: fmt::Arguments<'_>) -> bool {
    let stream = unsafe { stdout };
    let result = Output(stream).write_fmt(arguments);
    result.is_ok() && unsafe { fflush(stream) } == 0
}

fn fail(message: &[u8], status: c_int) -> ! {
    // Fixed diagnostics avoid formatting or allocating while handling failure.
    unsafe {
        fwrite(message.as_ptr().cast(), 1, message.len(), stderr);
        fflush(stderr);
        _Exit(status)
    }
}

#[panic_handler]
fn panic(_information: &core::panic::PanicInfo<'_>) -> ! {
    fail(b"rust: panic; exiting status=101\n", 101)
}

#[alloc_error_handler]
fn allocation_failure(_layout: Layout) -> ! {
    fail(b"rust: allocation failed; exiting status=102\n", 102)
}

/// Startup's argv strings are NUL terminated and borrowed for main's lifetime.
///
/// # Safety
/// `argument` must be a nonnull pointer to such a string.
pub unsafe fn argument_is(argument: *const c_char, expected: &[u8]) -> bool {
    for (index, byte) in expected.iter().enumerate() {
        if unsafe { argument.add(index).read() } as u8 != *byte {
            return false;
        }
    }
    unsafe { argument.add(expected.len()).read() == 0 }
}
