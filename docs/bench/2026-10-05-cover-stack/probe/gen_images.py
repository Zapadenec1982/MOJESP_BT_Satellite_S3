"""Test covers for the stb_image stack probe.

Every PNG variant stb has a separate code path for (colour type, bit depth, interlace, the
three deflate block kinds) and the JPEG kinds the hardware engine refuses (progressive,
grayscale). Pillow writes the JPEGs - the IDF v6.1 venv has it, the v5.5.5 one does not; the
PNGs are written by hand so interlace, 16-bit and the deflate strategy are under control.

Writes images/*.png|jpg and images.c (byte arrays + a table), both next to this file.
"""
import io
import math
import os
import random
import struct
import zlib

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")
os.makedirs(OUT, exist_ok=True)
rnd = random.Random(1234)


def photo(w, h, seed=0):
    """Something with gradients, edges and noise: compresses like a real cover, not like flat fill."""
    r = random.Random(seed)
    px = []
    for y in range(h):
        for x in range(w):
            v = (math.sin(x * 0.07 + seed) + math.cos(y * 0.05)) * 60 + 128
            px.append((
                max(0, min(255, int(v + r.randint(-25, 25)))),
                max(0, min(255, int((x * 255) // max(1, w - 1) + r.randint(-10, 10)))),
                max(0, min(255, int((y * 255) // max(1, h - 1) ^ (x & 0x3F)))),
            ))
    return px


# ── PNG writer ────────────────────────────────────────────────────────────────────────────
def chunk(kind, data):
    c = struct.pack(">I", len(data)) + kind + data
    return c + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


ADAM7 = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]


def pack_row(samples, depth):
    if depth == 8:
        return bytes(samples)
    if depth == 16:
        return b"".join(struct.pack(">H", s) for s in samples)
    out, acc, n = bytearray(), 0, 0
    for s in samples:
        acc = (acc << depth) | s
        n += depth
        if n == 8:
            out.append(acc)
            acc, n = 0, 0
    if n:
        out.append(acc << (8 - n))
    return bytes(out)


def filter_rows(rows, bpp, filt):
    """Apply one PNG filter type (or cycle 0..4 when filt is None) to packed rows."""
    out = bytearray()
    prev = bytes(len(rows[0])) if rows else b""
    for i, row in enumerate(rows):
        f = (i % 5) if filt is None else filt
        cur = bytearray(len(row))
        for j in range(len(row)):
            a = row[j - bpp] if j >= bpp else 0
            b = prev[j]
            c = prev[j - bpp] if j >= bpp else 0
            if f == 0:
                p = 0
            elif f == 1:
                p = a
            elif f == 2:
                p = b
            elif f == 3:
                p = (a + b) // 2
            else:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                p = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
            cur[j] = (row[j] - p) & 0xFF
        out.append(f)
        out += cur
        prev = row
    return bytes(out)


def png(w, h, ctype, depth, pixel, interlace=0, level=9, strategy=zlib.Z_DEFAULT_STRATEGY,
        palette=None, trns=None, filt=None):
    """pixel(x, y) returns the sample tuple for that colour type."""
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, chans * depth // 8)

    def raw(xs, ys):
        rows = [pack_row([s for x in xs for s in pixel(x, y)], depth) for y in ys]
        return filter_rows(rows, bpp, filt) if rows and rows[0] else b""

    if interlace:
        data = b""
        for x0, y0, dx, dy in ADAM7:
            xs, ys = list(range(x0, w, dx)), list(range(y0, h, dy))
            if xs and ys:
                data += raw(xs, ys)
    else:
        data = raw(range(w), range(h))
    co = zlib.compressobj(level, zlib.DEFLATED, 15, 9, strategy)
    z = co.compress(data) + co.flush()
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, interlace))
    if palette:
        out += chunk(b"PLTE", b"".join(bytes(c) for c in palette))
    if trns is not None:
        out += chunk(b"tRNS", trns)
    # Split IDAT like real encoders do, so stb walks more than one chunk.
    for i in range(0, len(z), 8192):
        out += chunk(b"IDAT", z[i:i + 8192])
    return out + chunk(b"IEND", b"")


def save(name, data):
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(data)
    return name


images = []

# Favicon-sized PNGs: what a radio station's logo is. RGBA + dynamic Huffman is the common case.
fav = photo(120, 120, 1)
images.append(save("fav120_rgba8_dyn.png",
                   png(120, 120, 6, 8, lambda x, y: fav[y * 120 + x] + ((x * y) & 0xFF,))))
# Smooth on purpose: on noise a fixed-code block loses to a stored one and zlib stores.
images.append(save("fav120_rgb8_fixed.png",
                   png(120, 120, 2, 8, lambda x, y: (x * 2, y * 2, (x + y) & 0xFF),
                       strategy=zlib.Z_FIXED)))
images.append(save("fav120_rgb8_stored.png",
                   png(120, 120, 2, 8, lambda x, y: fav[y * 120 + x], level=0)))
images.append(save("fav120_rgb8_huffonly.png",
                   png(120, 120, 2, 8, lambda x, y: fav[y * 120 + x], strategy=zlib.Z_HUFFMAN_ONLY)))
pal = [(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)) for _ in range(256)]
images.append(save("fav120_pal8_trns.png",
                   png(120, 120, 3, 8, lambda x, y: ((x ^ y) & 0xFF,), palette=pal,
                       trns=bytes(range(0, 256, 2)))))
images.append(save("fav64_pal4.png",
                   png(64, 64, 3, 4, lambda x, y: ((x + y) & 15,), palette=pal[:16])))
images.append(save("fav64_pal1.png",
                   png(64, 64, 3, 1, lambda x, y: ((x ^ y) & 1,), palette=pal[:2])))
images.append(save("fav120_gray8.png", png(120, 120, 0, 8, lambda x, y: (fav[y * 120 + x][0],))))
images.append(save("fav64_gray2.png", png(64, 64, 0, 2, lambda x, y: ((x * y) & 3,))))
images.append(save("fav120_graya8.png",
                   png(120, 120, 4, 8, lambda x, y: (fav[y * 120 + x][1], x & 0xFF))))
images.append(save("fav120_rgb16.png",
                   png(120, 120, 2, 16, lambda x, y: tuple(c * 257 for c in fav[y * 120 + x]))))
images.append(save("fav120_rgba16_interlaced.png",
                   png(120, 120, 6, 16, lambda x, y: tuple(c * 257 for c in fav[y * 120 + x]) + (65535,),
                       interlace=1)))
images.append(save("fav120_rgba8_interlaced.png",
                   png(120, 120, 6, 8, lambda x, y: fav[y * 120 + x] + (200,), interlace=1)))
images.append(save("fav120_gray16_trns.png",
                   png(120, 120, 0, 16, lambda x, y: (fav[y * 120 + x][2] * 257,), trns=b"\x00\x10")))
images.append(save("fav120_rgb8_paeth.png",
                   png(120, 120, 2, 8, lambda x, y: fav[y * 120 + x], filt=4)))
# A cover-sized PNG: many deflate blocks, so many dynamic tables.
big = photo(600, 600, 7)
images.append(save("cover600_rgb8_dyn.png", png(600, 600, 2, 8, lambda x, y: big[y * 600 + x])))

# JPEGs the hardware engine refuses and stb is asked to read.
def jpeg(name, mode, w, h, **kw):
    im = Image.new("RGB", (w, h))
    im.putdata(photo(w, h, 3))
    if mode != "RGB":
        im = im.convert(mode)
    b = io.BytesIO()
    im.save(b, "JPEG", **kw)
    return save(name, b.getvalue())


images.append(jpeg("cover600_prog420.jpg", "RGB", 600, 600, progressive=True, quality=90, subsampling=2))
images.append(jpeg("cover600_prog444.jpg", "RGB", 600, 600, progressive=True, quality=90, subsampling=0))
images.append(jpeg("cover600_prog422.jpg", "RGB", 600, 600, progressive=True, quality=85, subsampling=1))
images.append(jpeg("cover333_prog420_odd.jpg", "RGB", 333, 251, progressive=True, quality=80, subsampling=2))
images.append(jpeg("cover600_prog_gray.jpg", "L", 600, 600, progressive=True, quality=90))
images.append(jpeg("cover600_base_gray.jpg", "L", 600, 600, quality=90))
images.append(jpeg("cover600_prog_cmyk.jpg", "CMYK", 600, 600, progressive=True, quality=90))
images.append(jpeg("cover600_prog420_rst.jpg", "RGB", 600, 600, progressive=True, quality=90,
                   subsampling=2, restart_marker_blocks=4))

with open(os.path.join(HERE, "images.c"), "w") as c:
    c.write("/* generated by gen_images.py */\n#include \"images.h\"\n\n")
    for i, n in enumerate(images):
        data = open(os.path.join(OUT, n), "rb").read()
        c.write(f"static const unsigned char img{i}[{len(data)}] = {{\n")
        for j in range(0, len(data), 24):
            c.write("  " + ",".join(str(b) for b in data[j:j + 24]) + ",\n")
        c.write("};\n")
    c.write(f"\nconst struct image IMAGES[] = {{\n")
    for i, n in enumerate(images):
        c.write(f"  {{\"{n}\", img{i}, sizeof(img{i})}},\n")
    c.write("};\nconst int IMAGE_COUNT = sizeof(IMAGES) / sizeof(IMAGES[0]);\n")
for n in images:
    print(f"{os.path.getsize(os.path.join(OUT, n)):8d}  {n}")
