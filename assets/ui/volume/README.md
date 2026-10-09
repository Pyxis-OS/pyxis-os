# Volume icons

Original artwork drawn by the Pyxis owner and supplied on 2026-10-09 as
`/shared/pyxis-assets/pyxis-icons.{aseprite,png}`. The owner licensed both files
under MPL-2.0 on that date; see the repository [LICENSE](../../../LICENSE).
The Aseprite file is the editable source; PNG is the supplied export.

The 192×24 sheet has eight 24×24 slots. Only slots 0–3 (high, medium, low,
muted) are used. Opaque magenta is the transparency key; black ink is blitted
unscaled in the theme's text colour. `scripts/volume-icons.py` converts those
four slots to compiled row masks at build time with Python's standard library.

Source SHA-256:

- PNG: `9f63995b47ef08342e4fdd377ee22b791417c78696a465adf316f6bc14909b47`.
- Aseprite: `6c6d103732c0d36351e70921ac830ddfc859f3b9c2e4f7cdb95a0319ba76b0d7`.
