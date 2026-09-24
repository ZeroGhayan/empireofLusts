#!/usr/bin/env python3
"""Gera fundos placeholder e desempacota sprites das irmas (pilots.json)."""
import base64
import json
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


def t3s(name, frames, fmt="rgba5551"):
    path = os.path.join(DIR, name)
    with open(path, "w") as f:
        f.write("--atlas -f %s -z auto\n" % fmt)
        for fr in frames:
            f.write(fr + "\n")


def unpack_pilots():
    pack = os.path.join(DIR, "pilots.json")
    with open(pack, "r") as f:
        data = json.load(f)
    for rel, b64 in data.items():
        who, fn = rel.split("/")
        key = who.lower() + "_" + fn
        out = os.path.join(DIR, key)
        with open(out, "wb") as g:
            g.write(base64.b64decode(b64))


def main():
    for name, w, h, pix in (
        ("bg0.png",) + layer0(),
        ("bg1.png",) + layer1(),
        ("bg2.png",) + layer2(),
    ):
        write_if_absent(os.path.join(DIR, name), w, h, pix)
    unpack_pilots()
    t3s("bg0.t3s", ["bg0.png"])
    t3s("bg1.t3s", ["bg1.png"])
    t3s("bg2.t3s", ["bg2.png"])
    t3s("shirammy.t3s", [
        "shirammy_idle.png", "shirammy_low.png",
        "shirammy_high.png", "shirammy_flight.png",
    ], "rgba8888")
    t3s("rexxi.t3s", [
        "rexxi_idle.png", "rexxi_low.png",
        "rexxi_high.png", "rexxi_flight.png",
    ], "rgba8888")


if __name__ == "__main__":
    main()
