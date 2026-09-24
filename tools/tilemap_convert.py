#!/usr/bin/env python3
"""Conversor de tilemap Empire of Lusts.

extract  imagem grande -> tiles.png + map.png + map.etm
pack     tiles.png + map.png (cor = indice) -> map.etm
demo     mapa de cruz 128x128 do proto

map.png: R = idx&255, G = idx>>8, B = 0x20
.etm: magic ETM1, u16 w h tile_px count, u16 flags=atlas_cols, u16 cells[w*h]
"""
from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib

MAGIC = 0x314D5445
MARK_B = 0x20


def die(msg, code=1):
    print("tilemap_convert:", msg, file=sys.stderr)
    sys.exit(code)


def pack_etm(w, h, tile_px, count, cells, cols=0):
    if len(cells) != w * h:
        die("cells %d != %d*%d" % (len(cells), w, h))
    buf = bytearray()
    buf += struct.pack("<IHHHH", MAGIC, w, h, tile_px, count)
    buf += struct.pack("<H", cols & 0xFFFF)
    for c in cells:
        buf += struct.pack("<H", c & 0xFFFF)
    return bytes(buf)


def index_rgb(idx):
    return idx & 0xFF, (idx >> 8) & 0xFF, MARK_B


def rgb_index(r, g, b):
    return r | (g << 8)


def write_png_rgba(path, w, h, rgba):
    def chunk(tag, data):
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)
    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def write_map_png(path, w, h, cells):
    pix = bytearray(w * h * 4)
    for i, idx in enumerate(cells):
        r, g, b = index_rgb(idx)
        pix[i * 4:i * 4 + 4] = bytes((r, g, b, 255))
    write_png_rgba(path, w, h, bytes(pix))


def try_pil():
    try:
        from PIL import Image
    except Exception:
        die("precisa de Pillow: python3 -m pip install --user Pillow")
    return Image


def cmd_extract(args):
    Image = try_pil()
    im = Image.open(args.image).convert("RGBA")
    tile = args.tile
    if tile <= 0:
        die("--tile tem de ser > 0")
    src_w, src_h = im.size
    cells_n = args.size
    need = cells_n * tile
    if src_w < need or src_h < need:
        print("aviso: imagem %dx%d menor que %dx%d; nearest." % (src_w, src_h, need, need), file=sys.stderr)
        im = im.resize((need, need), Image.NEAREST)
    else:
        left = (src_w - need) // 2
        top = (src_h - need) // 2
        im = im.crop((left, top, left + need, top + need))
    gw = gh = cells_n
    if gw > 128 or gh > 128:
        die("grade passa de 128")
    unique = {}
    order = []
    cells = []
    pix = im.load()
    for ty in range(gh):
        for tx in range(gw):
            raw = bytearray(tile * tile * 4)
            for py in range(tile):
                for px in range(tile):
                    r, g, b, a = pix[tx * tile + px, ty * tile + py]
                    o = (py * tile + px) * 4
                    raw[o:o + 4] = bytes((r, g, b, a))
            key = bytes(raw)
            if key not in unique:
                if len(order) >= 1024:
                    die("mais de 1024 tiles unicas")
                unique[key] = len(order)
                order.append(key)
            cells.append(unique[key])
    cols = max(1, int(len(order) ** 0.5 + 0.999))
    rows = (len(order) + cols - 1) // cols
    atlas = bytearray(cols * tile * rows * tile * 4)
    aw = cols * tile
    for i, key in enumerate(order):
        cx, cy = i % cols, i // cols
        for py in range(tile):
            for px in range(tile):
                src = (py * tile + px) * 4
                dst = ((cy * tile + py) * aw + cx * tile + px) * 4
                atlas[dst:dst + 4] = key[src:src + 4]
    out = args.out
    os.makedirs(out, exist_ok=True)
    write_png_rgba(os.path.join(out, "tiles.png"), aw, rows * tile, bytes(atlas))
    write_map_png(os.path.join(out, "map.png"), gw, gh, cells)
    etm = pack_etm(gw, gh, tile, len(order), cells, cols)
    path = os.path.join(out, "map.etm")
    with open(path, "wb") as f:
        f.write(etm)
    print("grade %dx%d  tile %d  unicas %d  cols %d" % (gw, gh, tile, len(order), cols))
    print("tiles.png map.png map.etm em", out)


def load_png_rgba(path):
    Image = try_pil()
    im = Image.open(path).convert("RGBA")
    return im.size[0], im.size[1], im.load()


def cmd_pack(args):
    tw, th, _tp = load_png_rgba(args.tiles)
    mw, mh, mpix = load_png_rgba(args.map)
    if mw > 128 or mh > 128:
        die("map.png passa de 128")
    tile = args.tile or 32
    cols = max(1, tw // tile)
    rows = max(1, th // tile)
    count = cols * rows
    cells = []
    for y in range(mh):
        for x in range(mw):
            r, g, b, a = mpix[x, y]
            idx = rgb_index(r, g, b)
            if idx >= count:
                idx = r
            cells.append(idx)
    etm = pack_etm(mw, mh, tile, count, cells, cols)
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(etm)
    print("map.etm", args.out, "%dx%d tiles=%d cols=%d" % (mw, mh, count, cols))


def cmd_demo(args):
    n = 128
    mid = n // 2
    cells = []
    for z in range(n):
        for x in range(n):
            adx, adz = abs(x - mid), abs(z - mid)
            t = 0
            if x in (0, n - 1) or z in (0, n - 1):
                t = 1
            elif adx <= 1 or adz <= 1:
                t = 3 if (adx == 0 or adz == 0) else 2
            elif adx <= 3 or adz <= 3:
                t = 4
            elif (adx == 24 or adz == 24) and adx <= 24 and adz <= 24:
                t = 2
            cells.append(t)
    out = args.out
    os.makedirs(out, exist_ok=True)
    write_map_png(os.path.join(out, "map.png"), n, n, cells)
    tile = 32
    pal = [(46, 110, 58, 255), (28, 28, 32, 255), (58, 62, 74, 255), (210, 200, 70, 255), (90, 86, 70, 255)]
    cols = 5
    atlas = bytearray(cols * tile * tile * 4)
    for i, c in enumerate(pal):
        for py in range(tile):
            for px in range(tile):
                o = (py * (cols * tile) + i * tile + px) * 4
                atlas[o:o + 4] = bytes(c)
    write_png_rgba(os.path.join(out, "tiles.png"), cols * tile, tile, bytes(atlas))
    etm = pack_etm(n, n, tile, 5, cells, cols)
    with open(os.path.join(out, "map.etm"), "wb") as f:
        f.write(etm)
    print("demo 128x128 ->", out)


def main():
    p = argparse.ArgumentParser(description="Tilemap Empire of Lusts")
    sub = p.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("extract")
    e.add_argument("image")
    e.add_argument("--tile", type=int, default=32)
    e.add_argument("--size", type=int, default=128)
    e.add_argument("--out", required=True)
    e.set_defaults(func=cmd_extract)
    k = sub.add_parser("pack")
    k.add_argument("tiles")
    k.add_argument("map")
    k.add_argument("--tile", type=int, default=32)
    k.add_argument("--out", required=True)
    k.set_defaults(func=cmd_pack)
    d = sub.add_parser("demo")
    d.add_argument("--out", required=True)
    d.set_defaults(func=cmd_demo)
    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
