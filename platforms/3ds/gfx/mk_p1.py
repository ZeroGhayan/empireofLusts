#!/usr/bin/env python3
import os, struct, zlib

DIR = os.path.dirname(os.path.abspath(__file__))
W, H = 48, 64
BLUE = (70, 180, 255, 255)
NAVY = (30, 90, 160, 255)
WHITE = (255, 255, 255, 255)
SKIN = (240, 200, 160, 255)
OUT = (12, 20, 40, 255)


def png(path, pixels):
    def chunk(tag, data):
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

    raw = b"".join(b"\x00" + bytes(pixels[y * W * 4:(y + 1) * W * 4]) for y in range(H))
    ihdr = struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def blank():
    return bytearray(W * H * 4)


def rect(p, x, y, rw, rh, c):
    r, g, b, a = c
    for yy in range(y, y + rh):
        for xx in range(x, x + rw):
            if 0 <= xx < W and 0 <= yy < H:
                i = (yy * W + xx) * 4
                p[i:i + 4] = bytes((r, g, b, a))


def fighter(bob):
    p = blank()
    y = 8 - bob
    rect(p, 14, y + 10, 20, 28, OUT)
    rect(p, 16, y + 12, 16, 24, BLUE)
    rect(p, 16, y + 0, 16, 14, OUT)
    rect(p, 18, y + 2, 12, 10, SKIN)
    rect(p, 18, y + 2, 12, 4, NAVY)
    rect(p, 26, y + 6, 3, 3, WHITE)
    rect(p, 27, y + 7, 2, 2, OUT)
    rect(p, 10, y + 14 + bob, 6, 16, OUT)
    rect(p, 11, y + 15 + bob, 4, 14, BLUE)
    rect(p, 32, y + 14 - bob, 6, 16, OUT)
    rect(p, 33, y + 15 - bob, 4, 14, BLUE)
    rect(p, 16, y + 36, 7, 18, OUT)
    rect(p, 25, y + 36, 7, 18, OUT)
    rect(p, 17, y + 37, 5, 16, NAVY)
    rect(p, 26, y + 37, 5, 16, NAVY)
    return p


def main():
    png(os.path.join(DIR, "p1_idle0.png"), fighter(0))
    png(os.path.join(DIR, "p1_idle1.png"), fighter(2))


if __name__ == "__main__":
    main()
