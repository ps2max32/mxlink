#!/usr/bin/env python3
"""The shell's font atlas: font.bin (512 x 256, T4: 4 bits a texel, the left
texel in the low bits) and font.h (where each glyph is). Two sizes of Lato
(SIL Open Font License), ASCII, and the pad's four symbols as characters 1-4
of the small size. A texel is a pixel of the 640 x 448 screen.

  gen-font.py <Lato-Bold.ttf> <Lato-Medium.ttf>
"""
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 512, 256
BIG, SMALL = 36, 20


def glyphs(path, size):
    font = ImageFont.truetype(path, size)
    ascent = font.getmetrics()[0]
    out = {}
    for c in range(32, 127):
        ch = chr(c)
        x0, y0, x1, y1 = font.getbbox(ch)
        w, h = max(x1 - x0, 1), max(y1 - y0, 1)
        im = Image.new("L", (w, h), 0)
        ImageDraw.Draw(im).text((-x0, -y0), ch, 255, font=font)
        out[c] = (im, x0, y0 - ascent, round(font.getlength(ch)))
    return out


def icons(size):
    """The pad's symbols, drawn 4 times as large and scaled down."""
    n, s = size - 4, 4
    out = {}
    for c in range(1, 5):
        im = Image.new("L", (n * s, n * s), 0)
        d = ImageDraw.Draw(im)
        m, e, lw = 2 * s, (n - 2) * s, 2 * s + s // 2
        if c == 1:
            d.line((m, m, e, e), 255, lw)
            d.line((m, e, e, m), 255, lw)
        elif c == 2:
            d.line((n * s // 2, m, e, e, m, e, n * s // 2, m), 255, lw, joint="curve")
        elif c == 3:
            d.rectangle((m, m, e, e), None, 255, lw)
        else:
            d.ellipse((m, m, e, e), None, 255, lw)
        out[c] = (im.resize((n, n), Image.LANCZOS), 0, -n - 1, n + 3)
    return out


def main():
    sets = [("font_big", BIG, glyphs(sys.argv[1], BIG)),
            ("font_small", SMALL, {**icons(SMALL), **glyphs(sys.argv[2], SMALL)})]
    atlas = Image.new("L", (W, H), 0)
    x = y = row = 0
    lines = ["/* Made by gen-font.py. */", "#ifndef FONT_H", "#define FONT_H", "",
             "struct glyph {", "  u16 u, v;", "  u8 w, h;", "  s8 xo, yo; /* from the pen, on the baseline */",
             "  u8 adv;", "};", "", f"#define FONT_W {W}", f"#define FONT_H_ {H}", ""]
    for name, size, gl in sets:
        lines.append(f"#define {name.upper()}_SIZE {size}")
        lines.append(f"static const struct glyph {name}[127] = {{")
        for c in sorted(gl, key=lambda c: -gl[c][0].height):
            im, xo, yo, adv = gl[c]
            if x + im.width + 2 > W:
                x, y, row = 0, y + row + 2, 0
            assert y + im.height + 2 <= H, "the atlas is full"
            atlas.paste(im, (x + 1, y + 1))
            lines.append(f"    [{c}] = {{{x + 1}, {y + 1}, {im.width}, {im.height}, {xo}, {yo}, {adv}}},")
            x += im.width + 2
            row = max(row, im.height)
        lines += ["};", ""]
    lines.append("#endif")
    open("font.h", "w").write("\n".join(lines) + "\n")
    p = [v * 15 // 255 for v in atlas.tobytes()]
    open("font.bin", "wb").write(bytes(p[i] | p[i + 1] << 4 for i in range(0, len(p), 2)))
    atlas.save(sys.argv[3]) if len(sys.argv) > 3 else None
    print("atlas used up to line", y + row)


main()
