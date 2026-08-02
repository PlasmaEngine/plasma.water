"""Renders the icon and banner for plasma.water.

There is no rasteriser on this machine and no Pillow, so this writes PNG bytes directly - the
format is just zlib-compressed scanlines with a filter byte each. Everything is drawn at 3x and
box-downsampled, which is where the antialiasing comes from.

The motif and palette are lifted from EditorPluginWater/QtResources/Water.svg so the package
artwork and the in-editor iconography agree.
"""

import math
import os
import struct
import sys
import zlib

SS = 3  # supersample factor

# ---- palette, from Water.svg ------------------------------------------------
CREST = (0xD8, 0xF7, 0xFF)
LIGHT = (0x65, 0xD8, 0xFF)
MID = (0x87, 0xDD, 0xFF)
DEEP = (0x16, 0x72, 0xC4)
BG_TOP = (0x10, 0x40, 0x6E)
BG_BOT = (0x05, 0x18, 0x2C)


def write_png(path, w, h, rgba):
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)  # filter type 0 (None)
        raw += rgba[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(png)


def lerp(a, b, t):
    return a + (b - a) * t


def mix(c0, c1, t):
    t = 0.0 if t < 0 else (1.0 if t > 1 else t)
    return (lerp(c0[0], c1[0], t), lerp(c0[1], c1[1], t), lerp(c0[2], c1[2], t))


def over(dst, src, a):
    """src over dst, both straight RGB, a in 0..1."""
    return (dst[0] + (src[0] - dst[0]) * a,
            dst[1] + (src[1] - dst[1]) * a,
            dst[2] + (src[2] - dst[2]) * a)


class Wave(object):
    """A crest line: base height plus two sines, so it does not read as a pure sinusoid."""

    def __init__(self, base, amp, freq, phase):
        self.base, self.amp, self.freq, self.phase = base, amp, freq, phase

    def y(self, x):
        return (self.base
                + self.amp * math.sin(x * self.freq + self.phase)
                + self.amp * 0.35 * math.sin(x * self.freq * 2.17 + self.phase * 1.7))

    def dy(self, x):
        return (self.amp * self.freq * math.cos(x * self.freq + self.phase)
                + self.amp * 0.35 * self.freq * 2.17 * math.cos(x * self.freq * 2.17 + self.phase * 1.7))


def stroke_alpha(wave, x, y, half_width):
    """Perpendicular distance to the crest, so the stroke keeps its width on steep slopes."""
    d = abs(y - wave.y(x)) / math.sqrt(1.0 + wave.dy(x) ** 2)
    return 1.0 if d <= half_width else 0.0


def downsample(buf, w, h):
    """Box filter SS x SS back to w x h, producing the antialiasing."""
    out = bytearray(w * h * 4)
    n = SS * SS
    bw = w * SS
    for y in range(h):
        for x in range(w):
            r = g = b = a = 0
            for sy in range(SS):
                row = (y * SS + sy) * bw
                for sx in range(SS):
                    i = (row + x * SS + sx) * 4
                    r += buf[i]; g += buf[i + 1]; b += buf[i + 2]; a += buf[i + 3]
            o = (y * w + x) * 4
            out[o] = r // n; out[o + 1] = g // n; out[o + 2] = b // n; out[o + 3] = a // n
    return out


def rounded_rect_alpha(x, y, w, h, radius):
    """Inside-ness of a rounded rectangle; binary here because SS handles the edges."""
    cx = min(max(x, radius), w - radius)
    cy = min(max(y, radius), h - radius)
    dx, dy = x - cx, y - cy
    if dx == 0 and dy == 0:
        return 1.0
    return 1.0 if (dx * dx + dy * dy) <= radius * radius else 0.0


def render_icon(size):
    W = H = size * SS
    buf = bytearray(W * H * 4)
    radius = W * 0.22

    # Crest of the water body sits just under the middle, matching Water.svg's proportions.
    body = Wave(H * 0.46, H * 0.055, 2.0 * math.pi / (W * 0.62), 0.4)
    inner = Wave(H * 0.68, H * 0.038, 2.0 * math.pi / (W * 0.52), 2.3)

    for y in range(H):
        yt = y / float(H - 1)
        bg = mix(BG_TOP, BG_BOT, yt)
        for x in range(W):
            inside = rounded_rect_alpha(x + 0.5, y + 0.5, W, H, radius)
            if inside <= 0.0:
                continue

            col = bg
            by = body.y(x)
            if y >= by:
                # Vertical gradient through the water body, light at the crest.
                t = (y - by) / max(1.0, (H - by))
                col = mix(LIGHT, DEEP, t)

            a = stroke_alpha(body, x + 0.5, y + 0.5, H * 0.017)
            if a > 0.0:
                col = over(col, CREST, a)

            if y >= by:
                a = stroke_alpha(inner, x + 0.5, y + 0.5, H * 0.012)
                if a > 0.0:
                    col = over(col, MID, a * 0.85)

            i = (y * W + x) * 4
            buf[i] = int(col[0]); buf[i + 1] = int(col[1]); buf[i + 2] = int(col[2]); buf[i + 3] = 255

    return downsample(buf, size, size)


def render_banner(width, height):
    W, H = width * SS, height * SS
    buf = bytearray(W * H * 4)

    # Receding bands, each crest lower and lighter than the one behind it. The fills are opaque so
    # a nearer band hides the crest behind it where they cross - a translucent fill leaves the far
    # crest ghosting through the near band. Depth is carried by colour and by how strongly each
    # crest is drawn, not by fill alpha.
    # Each band carries its own crest colour. A distant crest drawn in white at low opacity turns
    # grey against the dark fill behind it and reads as a stray line rather than as water, so the
    # far crests are tinted toward the band colour and only the nearest one is white.
    bands = [
        (Wave(H * 0.42, H * 0.075, 2.0 * math.pi / (W * 0.48), 0.0),
         mix(DEEP, BG_TOP, 0.45), mix(MID, DEEP, 0.45), 0.85),
        (Wave(H * 0.56, H * 0.065, 2.0 * math.pi / (W * 0.37), 2.1),
         mix(DEEP, LIGHT, 0.20), mix(CREST, MID, 0.45), 0.90),
        (Wave(H * 0.72, H * 0.055, 2.0 * math.pi / (W * 0.29), 4.0),
         mix(DEEP, LIGHT, 0.45), CREST, 1.00),
    ]

    for y in range(H):
        yt = y / float(H - 1)
        bg = mix(BG_TOP, BG_BOT, yt ** 0.85)
        for x in range(W):
            col = bg
            for wave, colour, crest_colour, crest_alpha in bands:
                wy = wave.y(x)
                if y >= wy:
                    depth = (y - wy) / max(1.0, H - wy)
                    col = mix(colour, DEEP, depth * 0.65)
                a = stroke_alpha(wave, x + 0.5, y + 0.5, H * 0.011)
                if a > 0.0:
                    col = over(col, crest_colour, a * crest_alpha)

            i = (y * W + x) * 4
            buf[i] = int(col[0]); buf[i + 1] = int(col[1]); buf[i + 2] = int(col[2]); buf[i + 3] = 255

    return downsample(buf, width, height)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)

    icon = render_icon(512)
    write_png(os.path.join(out, "WaterIcon.png"), 512, 512, icon)
    print("WaterIcon.png   512x512")

    banner = render_banner(1100, 512)
    write_png(os.path.join(out, "WaterBanner.png"), 1100, 512, banner)
    print("WaterBanner.png 1100x512")


if __name__ == "__main__":
    main()
