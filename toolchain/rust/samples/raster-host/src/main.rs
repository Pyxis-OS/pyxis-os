use raster_scene::{HEIGHT, WIDTH, render_checksum};

fn main() {
    let checksum = render_checksum();
    println!("rust-raster: fnv1a64={checksum:016x} width={WIDTH} height={HEIGHT}");
}
