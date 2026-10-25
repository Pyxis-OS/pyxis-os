#![no_std]

use tiny_skia::{Color, FillRule, Paint, PathBuilder, Pixmap, Stroke, Transform};

pub const WIDTH: u32 = 96;
pub const HEIGHT: u32 = 64;

pub fn render_checksum() -> u64 {
    let mut pixmap = Pixmap::new(WIDTH, HEIGHT).expect("pixmap dimensions");
    pixmap.fill(Color::from_rgba8(17, 29, 43, 255));

    let mut builder = PathBuilder::new();
    builder.move_to(7.25, 8.5);
    builder.line_to(87.5, 13.25);
    builder.cubic_to(75.0, 61.5, 22.25, 59.75, 7.25, 8.5);
    builder.close();
    let path = builder.finish().expect("closed path");

    let mut paint = Paint::default();
    paint.set_color_rgba8(210, 91, 37, 193);
    paint.anti_alias = true;
    pixmap.fill_path(&path, &paint, FillRule::Winding, Transform::identity(), None);

    paint.set_color_rgba8(41, 183, 224, 227);
    let stroke = Stroke { width: 2.5, ..Stroke::default() };
    pixmap.stroke_path(&path, &paint, &stroke, Transform::from_translate(1.0, -0.5), None);

    // FNV-1a over the complete premultiplied RGBA byte buffer, including alpha.
    pixmap.data().iter().fold(0xcbf29ce484222325, |hash, byte| {
        (hash ^ u64::from(*byte)).wrapping_mul(0x100000001b3)
    })
}
