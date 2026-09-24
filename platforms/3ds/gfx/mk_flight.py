#!/usr/bin/env python3
"""Gera placeholders de voo (3 camadas de fundo + 2 poses do piloto).

Nao sobrescreve PNGs que ja existam — podes deixar recortes do Sonic CD.
"""
import os
import struct
import zlib

DIR = os.path.dirname(os.path.abspath(__file__))


def png(path, w, h, pixels):
    def chunk(tag, data):
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)
    raw = b"".join(b"\x00" + bytes(pixels[y * w * 4 : (y + 1) * w * 4]) for y in range(h))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(data)


def fill(w, h, color):
    r, g, b, a = color
    return bytearray([r, g, b, a] * (w * h))


def rect(p, w, h, x, y, rw, rh, c):
    r, g, b, a = c
    for yy in range(y, y + rh):
        for xx in range(x, x + rw):
            if 0 <= xx < w and 0 <= yy < h:
                i = (yy * w + xx) * 4
                p[i : i + 4] = bytes((r, g, b, a))


def write_if_absent(path, w, h, pixels):
    if os.path.exists(path):
        return
    png(path, w, h, pixels)


def layer0():
    w, h = 512, 128
    p = fill(w, h, (16, 24, 56, 255))
    for x in range(0, w, 48):
        rect(p, w, h, x, 20, 28, 50, (28, 40, 90, 255))
        rect(p, w, h, x + 6, 28, 16, 20, (200, 210, 230, 40))
    return w, h, p


def layer1():
    w, h = 512, 96
    p = fill(w, h, (48, 36, 88, 255))
    for x in range(0, w, 64):
        rect(p, w, h, x + 8, 40, 40, 56, (70, 50, 110, 255))
    return w, h, p


def layer2():
    w, h = 512, 64
    p = fill(w, h, (30, 80, 120, 255))
    for x in range(0, w, 32):
        rect(p, w, h, x, 40, 32, 24, (20, 60, 90, 255))
    return w, h, p


def pilot_low():
    w, h = 32, 32
    p = fill(w, h, (0, 0, 0, 0))
    rect(p, w, h, 10, 14, 12, 16, (30, 90, 160, 255))
    rect(p, w, h, 12, 16, 8, 12, (70, 180, 255, 255))
    rect(p, w, h, 12, 6, 8, 10, (240, 200, 160, 255))
    rect(p, w, h, 12, 6, 8, 3, (20, 40, 80, 255))
    return w, h, p


def pilot_high():
    w, h = 32, 32
    p = fill(w, h, (0, 0, 0, 0))
    rect(p, w, h, 6, 14, 20, 10, (180, 40, 40, 255))
    rect(p, w, h, 10, 10, 12, 8, (240, 80, 70, 255))
    rect(p, w, h, 14, 8, 4, 6, (255, 220, 80, 255))
    return w, h, p


def t3s(name, frames):
    path = os.path.join(DIR, name)
    with open(path, "w") as f:
        f.write("--atlas -f rgba5551 -z auto\n")
        for fr in frames:
            f.write(fr + "\n")


def main():
    specs = [
        ("bg0.png",) + layer0(),
        ("bg1.png",) + layer1(),
        ("bg2.png",) + layer2(),
        ("pilot_low.png",) + pilot_low(),
        ("pilot_high.png",) + pilot_high(),
    ]
    for name, w, h, pix in specs:
        write_if_absent(os.path.join(DIR, name), w, h, pix)
    t3s("bg0.t3s", ["bg0.png"])
    t3s("bg1.t3s", ["bg1.png"])
    t3s("bg2.t3s", ["bg2.png"])
    t3s("pilot.t3s", ["pilot_low.png", "pilot_high.png"])


if __name__ == "__main__":
    main()
