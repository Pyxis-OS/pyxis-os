#![no_std]
#![no_main]
#![feature(thread_local)]

extern crate alloc;

use alloc::alloc::{alloc_zeroed, dealloc, handle_alloc_error, realloc};
use alloc::{string::String, vec::Vec};
use core::alloc::Layout;
use core::cell::Cell;
use core::ffi::{c_char, c_int};
use pyxis_qualification_runtime as runtime;

#[thread_local]
static mut TLS_MARKER: u64 = 0x7352_7573_7454_4c53;

struct Dropped<'a>(&'a Cell<usize>);

impl Drop for Dropped<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() + 1);
    }
}

fn allocation_check() {
    let drops = Cell::new(0);
    let mut values = Vec::with_capacity(1);
    for _ in 0..37 {
        values.push(Dropped(&drops));
    }
    let mut text = String::from("Rust");
    text.push_str(" on Pyxis");
    assert_eq!(text, "Rust on Pyxis");
    drop(text);
    drop(values);
    assert_eq!(drops.get(), 37);

    // Exercise zeroing and both realloc directions above libc aligned_alloc's
    // 4 KiB limit. The old payload must survive each successful replacement.
    let first = Layout::from_size_align(73, 16384).unwrap();
    let larger = Layout::from_size_align(32777, first.align()).unwrap();
    let smaller = Layout::from_size_align(19, first.align()).unwrap();
    unsafe {
        let pointer = alloc_zeroed(first);
        if pointer.is_null() {
            handle_alloc_error(first);
        }
        assert_eq!(pointer.addr() % first.align(), 0);
        for index in 0..first.size() {
            assert_eq!(pointer.add(index).read(), 0);
            pointer.add(index).write(index as u8);
        }
        let pointer = realloc(pointer, first, larger.size());
        if pointer.is_null() {
            handle_alloc_error(larger);
        }
        assert_eq!(pointer.addr() % larger.align(), 0);
        for index in 0..first.size() {
            assert_eq!(pointer.add(index).read(), index as u8);
        }
        let pointer = realloc(pointer, larger, smaller.size());
        if pointer.is_null() {
            handle_alloc_error(smaller);
        }
        assert_eq!(pointer.addr() % smaller.align(), 0);
        for index in 0..smaller.size() {
            assert_eq!(pointer.add(index).read(), index as u8);
        }
        dealloc(pointer, smaller);
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn main(argc: c_int, argv: *const *const c_char) -> c_int {
    if argc == 2 && unsafe { runtime::argument_is(argv.add(1).read(), b"--allocation-failure") } {
        // This real request exceeds the bounded libc heap; no fault hook or
        // physical-memory pressure is needed to exercise Rust's failure path.
        let layout = Layout::from_size_align(1usize << 46, 16).unwrap();
        // A pointer escape keeps the optimizer from eliding this unused
        // allocation/free pair and assuming an infallible allocation.
        let pointer = core::hint::black_box(unsafe { alloc::alloc::alloc(layout) });
        if pointer.is_null() {
            handle_alloc_error(layout);
        }
        unsafe { dealloc(pointer, layout) };
        runtime::print(format_args!("rust-hello: oversized allocation unexpectedly succeeded\n"));
        return 3;
    }
    if argc == 2 && unsafe { runtime::argument_is(argv.add(1).read(), b"--panic") } {
        panic!("requested panic qualification");
    }
    if argc != 1 {
        runtime::print(format_args!("usage: rust-hello [--panic | --allocation-failure]\n"));
        return 2;
    }
    unsafe {
        let marker = core::ptr::addr_of_mut!(TLS_MARKER);
        assert_eq!(marker.read_volatile(), 0x7352_7573_7454_4c53);
        marker.write_volatile(0x1234_5678_9abc_def0);
        assert_eq!(marker.read_volatile(), 0x1234_5678_9abc_def0);
    }
    allocation_check();
    let x = core::hint::black_box(1.5);
    let y = core::hint::black_box(2.0);
    let z = core::hint::black_box(0.25);
    assert_eq!(unsafe { runtime::fma(x, y, z) }, 3.25);
    if !runtime::print(format_args!("rust-hello: vec/string/drop/alignment/f64/tls ok\n")) {
        return 1;
    }
    0
}
