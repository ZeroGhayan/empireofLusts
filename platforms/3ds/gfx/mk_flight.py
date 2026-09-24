#!/usr/bin/env python3
"""Fundos placeholder + sprites das irmas.

Ordem:
  1. platforms/3ds/gfx/pilot_src/{Shirammy,Rexxi}/{idle,low,high,flight}.png
  2. platforms/3ds/gfx/b64/{Shirammy,Rexxi}_*.b64
  3. silhueta na proporcao certa (32x48 / 48x32)
"""
import base64
import os
import shutil
import struct
import zlib

DIR = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(DIR, "pilot_src")

FRAMES = ("idle", "low", "high", "flight")
WHO = ("Shirammy", "Rexxi")


def png(path, w, h, pixels):
    def chunk(tag, data):
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)
    raw = b"".join(b"\x00" + bytes(pixels[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(data)


def fill(w, h, color):
    r, g, b, a = color
    return bytearray([r, g, b, a] * (w * h))


def rect(p, w, h, x, y, rw, rh, c):
    r, g, b, a = c
    for yy in range(max(0, y), min(h, y + rh)):
        for xx in range(max(0, x), min(w, x + rw)):
            i = (yy * w + xx) * 4
            p[i:i + 4] = bytes((r, g, b, a))


def write_if_absent(path, w, h, pixels):
    if os.path.exists(path):
        return
    png(path, w, h, pixels)


def layer0():
    w, h = 512, 128
    p = fill(w, h, (16, 24, 56, 255))
    for x in range(0, w, 48):
        rect(p, w, h, x, 20, 28, 50, (28, 40, 90, 255))
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


def t3s(name, frames, fmt="rgba5551"):
    with open(os.path.join(DIR, name), "w") as f:
        f.write("--atlas -f %s -z auto\n" % fmt)
        for fr in frames:
            f.write(fr + "\n")


def silhouette(who, frame):
    red = who == "Shirammy"
    body = (220, 40, 40, 255) if red else (40, 80, 220, 255)
    skin = (240, 200, 160, 255)
    if frame == "flight":
        w, h = 48, 32
        p = fill(w, h, (0, 0, 0, 0))
        rect(p, w, h, 4, 12, 40, 8, body)
        rect(p, w, h, 18, 8, 12, 16, body)
        return w, h, p
    w, h = 32, 48
    p = fill(w, h, (0, 0, 0, 0))
    rect(p, w, h, 10, 4, 12, 10, skin)
    rect(p, w, h, 8, 14, 16, 18, body)
    rect(p, w, h, 8, 32, 6, 14, body)
    rect(p, w, h, 18, 32, 6, 14, body)
    return w, h, p


def install_pilots():
    for who in WHO:
        for fr in FRAMES:
            dest = os.path.join(DIR, "%s_%s.png" % (who.lower(), fr))
            src = os.path.join(SRC, who, fr + ".png")
            b64p = os.path.join(DIR, "b64", "%s_%s.b64" % (who, fr))
            if os.path.isfile(src):
                shutil.copyfile(src, dest)
                continue
            if os.path.isfile(b64p):
                try:
                    raw = base64.b64decode(open(b64p).read().strip())
                    if raw.startswith(b"\x89PNG"):
                        open(dest, "wb").write(raw)
                        continue
                except Exception:
                    pass
            w, h, pix = silhouette(who, fr)
            png(dest, w, h, pix)


def main():
    for name, w, h, pix in (
        ("bg0.png",) + layer0(),
        ("bg1.png",) + layer1(),
        ("bg2.png",) + layer2(),
    ):
        write_if_absent(os.path.join(DIR, name), w, h, pix)
    install_pilots()
    t3s("bg0.t3s", ["bg0.png"])
    t3s("bg1.t3s", ["bg1.png"])
    t3s("bg2.t3s", ["bg2.png"])
    t3s("shirammy.t3s", ["shirammy_%s.png" % f for f in FRAMES], "rgba8888")
    t3s("rexxi.t3s", ["rexxi_%s.png" % f for f in FRAMES], "rgba8888")


if __name__ == "__main__":
    main()
