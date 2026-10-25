#![no_std]
#![no_main]

use core::ffi::{c_char, c_int};
use pyxis_qualification_runtime as runtime;
use raster_scene::{HEIGHT, WIDTH, render_checksum};

#[unsafe(no_mangle)]
pub extern "C" fn main(argc: c_int, _argv: *const *const c_char) -> c_int {
    if argc != 1 {
        runtime::print(format_args!("usage: rust-raster\n"));
        return 2;
    }
    let checksum = render_checksum();
    if !runtime::print(format_args!(
        "rust-raster: fnv1a64={checksum:016x} width={WIDTH} height={HEIGHT}\n"
    )) {
        return 1;
    }
    0
}
