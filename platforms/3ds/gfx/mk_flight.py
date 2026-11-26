#!/usr/bin/env python3
"""Fundos, sprites das irmas e atlas de tiles SS4/DP1."""
import os
import shutil
import struct
import zlib

DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(DIR, "..", "..", ".."))
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
    import base64
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


def find_tiles_png(folder):
    names = (
        os.path.join(DIR, "..", "romfs", "flight", folder, "tiles.png"),
        os.path.join(DIR, "..", "romfs", folder, "tiles.png"),
        os.path.join(DIR, folder, "tiles.png"),
        os.path.join(DIR, "test", "tiles.png") if folder == "test" else "",
        os.path.join(ROOT, "Ref", "flight", folder, "tiles.png"),
        os.path.join(ROOT, "Ref", folder, "tiles.png"),
        os.path.join(ROOT, "Ref", "Sprites", folder, "tiles.png"),
        os.path.join(os.path.expanduser("~"), "empireofLust", "platforms", "3ds",
                     "romfs", "flight", folder, "tiles.png"),
    )
    for p in names:
        if os.path.isfile(p):
            print("mk_flight: encontrou", p)
            return p
    print("mk_flight: tiles.png NAO encontrado para", folder)
    return None


def placeholder_sheet(stem, step):
    cols, rows = 8, 4
    w, h = cols * step, rows * step
    p = fill(w, h, (40, 40, 50, 255))
    pal = [
        (46, 110, 58, 255), (28, 28, 32, 255), (58, 62, 74, 255),
        (210, 200, 70, 255), (90, 86, 70, 255), (150, 48, 190, 255),
        (220, 36, 36, 255), (80, 160, 90, 255),
    ]
    i = 0
    for y in range(0, h, step):
        for x in range(0, w, step):
            pad = 1 if step > 4 else 0
            rect(p, w, h, x + pad, y + pad, step - pad * 2, step - pad * 2, pal[i % 8])
            i += 1
    path = os.path.join(DIR, stem + "_sheet.png")
    png(path, w, h, p)
    return path


def slice_tileset(src_png, stem, step):
    try:
        from PIL import Image
    except ImportError:
        print("mk_flight: ERRO Pillow ausente — pip3 install --user Pillow")
        src_png = None
    if not src_png:
        src_png = placeholder_sheet(stem, step)
        print("mk_flight: placeholder", stem, "step", step)
        try:
            from PIL import Image
        except ImportError:
            t3s(stem + ".t3s", [os.path.basename(src_png)], "rgb565")
            return
    im = Image.open(src_png).convert("RGBA")
    w, h = im.size
    out = os.path.join(DIR, "_" + stem)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    frames = []
    i = 0
    if step < 1:
        step = 32
    for y in range(0, h, step):
        for x in range(0, w, step):
            tile = im.crop((x, y, min(x + step, w), min(y + step, h)))
            if tile.size != (step, step):
                canvas = Image.new("RGBA", (step, step), (0, 0, 0, 0))
                canvas.paste(tile, (0, 0))
                tile = canvas
            fn = "%03d.png" % i
            tile.save(os.path.join(out, fn))
            frames.append("_%s/%s" % (stem, fn))
            i += 1
            if i >= 512:
                break
        if i >= 512:
            break
    if frames:
        t3s(stem + ".t3s", frames, "rgb565")
        print("mk_flight: %s %d tiles %dx%d de %s (%dx%d)" % (
            stem, len(frames), step, step, src_png, w, h))
    else:
        print("mk_flight: %s ZERO tiles" % stem)


def sno_placeholder():
    cols, step = 5, 32
    w, h = cols * step, step
    p = fill(w, h, (0, 0, 0, 255))
    pal = [
        (8, 10, 16, 255),
        (136, 184, 152, 255),
        (220, 40, 180, 255),
        (220, 190, 50, 255),
        (40, 90, 110, 255),
    ]
    for i, c in enumerate(pal):
        rect(p, w, h, i * step + 1, 1, step - 2, step - 2, c)
    path = os.path.join(DIR, "sno_tiles_sheet.png")
    png(path, w, h, p)
    return path


def pack_grid_atlas(src_png, stem, step, start, count, grid=32):
    """Uma textura 1024x1024 com grid x grid tiles. tex3ds nao precisa resolver atlas."""
    try:
        from PIL import Image
    except ImportError:
        print("mk_flight: ERRO Pillow ausente")
        return
    if not src_png or not os.path.isfile(src_png):
        return
    im = Image.open(src_png).convert("RGBA")
    w, h = im.size
    atlas = Image.new("RGBA", (grid * step, grid * step), (0, 0, 0, 0))
    i = 0
    kept = 0
    for y in range(0, h, step):
        for x in range(0, w, step):
            if i < start:
                i += 1
                continue
            if kept >= count:
                break
            tile = im.crop((x, y, min(x + step, w), min(y + step, h)))
            if tile.size != (step, step):
                canvas = Image.new("RGBA", (step, step), (0, 0, 0, 0))
                canvas.paste(tile, (0, 0))
                tile = canvas
            cx = kept % grid
            cy = kept // grid
            atlas.paste(tile, (cx * step, cy * step))
            kept += 1
            i += 1
        if kept >= count:
            break
    if kept == 0:
        print("mk_flight: %s ZERO tiles" % stem)
        return
    path = os.path.join(DIR, stem + "_atlas.png")
    atlas.save(path)
    t3s(stem + ".t3s", [os.path.basename(path)], "rgb565")
    print("mk_flight: %s atlas %d tiles id %d+ -> %s" % (stem, kept, start, path))


def write_etm(path, w, h, tile_px, count, cols, cells):
    import struct
    n = w * h
    raw = bytearray(b"ETM1")
    raw += struct.pack("<HHHHH", w, h, tile_px, count, cols)
    for i in range(n):
        raw += struct.pack("<H", cells[i] if i < len(cells) else 0)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(raw)
    print("mk_flight: etm", path, w, "x", h, "cols", cols, "count", count)


def sno_tile_kind_src(tx, ty):
    if ty == 0 and tx <= 20:
        return "cloud"
    if tx == 20 and ty == 21:
        return "finish"
    if (ty == 16 and tx in (17, 18)) or (tx == 8 and ty == 18) or (tx == 12 and ty == 17):
        return "boost"
    return "track"


def build_sno_etm_and_atlas(src_png):
    """tiles 8x8 + map 512x512 -> etm 128x128 (4x4) e atlas 32px."""
    try:
        from PIL import Image
    except ImportError:
        print("mk_flight: Pillow ausente")
        pack_grid_atlas(src_png, "sno_tiles", 32, 0, 1024, 32)
        return

    im = Image.open(src_png).convert("RGBA")
    tw, th = im.size
    step = 8
    if tw >= 256 and th >= 256 and tw % 32 == 0:
        # folha antiga 32px
        step = 32
    cols = max(1, tw // step)
    rows = max(1, th // step)
    print("mk_flight: SNO tiles", tw, "x", th, "step", step, "grid", cols, "x", rows)

    map_paths = (
        os.path.join(os.path.dirname(src_png), "map.png"),
        os.path.join(DIR, "..", "romfs", "flight", "sno", "map.png"),
        os.path.join(os.path.expanduser("~"), "empireofLust", "platforms", "3ds",
                     "romfs", "flight", "sno", "map.png"),
    )
    map_im = None
    for p in map_paths:
        if os.path.isfile(p):
            map_im = Image.open(p).convert("RGBA")
            print("mk_flight: SNO map", p, map_im.size)
            break

    cells = [0] * (128 * 128)
    if map_im is not None:
        mw, mh = map_im.size
        # index RGB (r | g<<8) ou match de cor media da tile
        avgs = []
        for ty in range(rows):
            for tx in range(cols):
                tile = im.crop((tx * step, ty * step, (tx + 1) * step, (ty + 1) * step))
                px = list(tile.getdata())
                n = max(1, len(px))
                r = sum(p[0] for p in px) // n
                g = sum(p[1] for p in px) // n
                b = sum(p[2] for p in px) // n
                avgs.append((r, g, b))

        src_w, src_h = mw, mh
        src_ids = [0] * (src_w * src_h)
        pix = map_im.load()
        indexish = 0
        for s in range(min(64, src_w)):
            r, g, b, a = pix[s, 0]
            if b in (0x20, 32, 0) and a > 0:
                indexish += 1
        use_index = indexish >= 32

        for y in range(src_h):
            for x in range(src_w):
                r, g, b, a = pix[x, y]
                if a < 16 or (r + g + b) < 12:
                    src_ids[y * src_w + x] = 0
                    continue
                if use_index:
                    src_ids[y * src_w + x] = (r | (g << 8)) & 0xFFFF
                else:
                    best, bd = 0, 1 << 30
                    for i, (ar, ag, ab) in enumerate(avgs):
                        d = (ar - r) * (ar - r) + (ag - g) * (ag - g) + (ab - b) * (ab - b)
                        if d < bd:
                            bd = d
                            best = i
                    src_ids[y * src_w + x] = best

        # 512 -> 128 (4x4) se o mapa for o mundo 8px
        scale = 4 if src_w >= 256 and src_h >= 256 else max(1, src_w // 128)
        if src_w == 128 and src_h == 128:
            scale = 1
        for cz in range(128):
            for cx in range(128):
                pick = 0
                prio = -1
                for oy in range(scale):
                    for ox in range(scale):
                        sx = min(src_w - 1, cx * scale + ox)
                        sz = min(src_h - 1, cz * scale + oy)
                        tid = src_ids[sz * src_w + sx]
                        tx = tid % cols
                        ty = tid // cols
                        kind = sno_tile_kind_src(tx, ty)
                        pr = 3 if kind == "finish" else 2 if kind == "boost" else (
                            0 if kind == "cloud" or tid == 0 else 1)
                        if pr > prio:
                            prio = pr
                            pick = tid
                cells[cz * 128 + cx] = pick if prio > 0 else 0

    etm_dir = os.path.join(DIR, "..", "romfs", "flight", "sno")
    if not os.path.isdir(etm_dir):
        etm_dir = os.path.dirname(src_png)
    write_etm(os.path.join(etm_dir, "map.etm"), 128, 128, 32, cols * rows, cols, cells)

    # atlas: sobe 8px -> 32px (nearest) para o texfloor 32u
    if step == 8:
        big = Image.new("RGBA", (tw * 4, th * 4), (0, 0, 0, 0))
        for ty in range(rows):
            for tx in range(cols):
                tile = im.crop((tx * step, ty * step, (tx + 1) * step, (ty + 1) * step))
                tile = tile.resize((32, 32), Image.NEAREST)
                big.paste(tile, (tx * 32, ty * 32))
        tmp = os.path.join(DIR, "_sno_up32.png")
        big.save(tmp)
        ntiles = cols * rows
        pack_grid_atlas(tmp, "sno_tiles", 32, 0, min(1024, ntiles), 32)
        if ntiles > 1024:
            pack_grid_atlas(tmp, "sno_tiles2", 32, 1024, ntiles - 1024, 32)
    else:
        pack_grid_atlas(src_png, "sno_tiles", 32, 0, 1024, 32)
        pack_grid_atlas(src_png, "sno_tiles2", 32, 1024, 1024, 32)


def install_test_course():
    src = find_tiles_png("test")
    etm_candidates = []
    if src:
        etm_candidates.append(os.path.join(os.path.dirname(src), "map.etm"))
    etm_candidates.extend((
        os.path.join(DIR, "test", "map.etm"),
        os.path.join(DIR, "..", "romfs", "flight", "test", "map.etm"),
        os.path.join(os.path.expanduser("~"), "empireofLust", "platforms", "3ds",
                     "romfs", "flight", "test", "map.etm"),
    ))
    dest_dir = os.path.join(DIR, "..", "romfs", "flight", "test")
    os.makedirs(dest_dir, exist_ok=True)
    if src:
        slice_tileset(src, "test_tiles", 32)
        dest_png = os.path.join(dest_dir, "tiles.png")
        if os.path.abspath(src) != os.path.abspath(dest_png):
            shutil.copyfile(src, dest_png)
        print("mk_flight: TEST tiles", src)
    else:
        print("mk_flight: TEST tiles.png AUSENTE — poe em romfs/flight/test/")
    for e in etm_candidates:
        if e and os.path.isfile(e):
            dest = os.path.join(dest_dir, "map.etm")
            if os.path.abspath(e) != os.path.abspath(dest):
                shutil.copyfile(e, dest)
            print("mk_flight: TEST etm", e)
            break
    else:
        print("mk_flight: TEST map.etm AUSENTE")


def install_course_tiles():
    install_test_course()
    slice_tileset(find_tiles_png("ss4"), "ss4_tiles", 32)
    slice_tileset(find_tiles_png("dp1"), "dp1_tiles", 8)
    src = find_tiles_png("sno")
    if not src:
        src = sno_placeholder()
        print("mk_flight: SNO placeholder 5 classes")
        slice_tileset(src, "sno_tiles", 32)
    else:
        build_sno_etm_and_atlas(src)


def main():
    print("mk_flight: DIR", DIR)
    for name, w, h, pix in (
        ("bg0.png",) + layer0(),
        ("bg1.png",) + layer1(),
        ("bg2.png",) + layer2(),
    ):
        write_if_absent(os.path.join(DIR, name), w, h, pix)
    install_pilots()
    install_course_tiles()
    t3s("bg0.t3s", ["bg0.png"])
    t3s("bg1.t3s", ["bg1.png"])
    t3s("bg2.t3s", ["bg2.png"])
    t3s("shirammy.t3s", ["shirammy_%s.png" % f for f in FRAMES], "rgba8888")
    t3s("rexxi.t3s", ["rexxi_%s.png" % f for f in FRAMES], "rgba8888")
    cloud = os.path.join(DIR, "clouds.png")
    if os.path.isfile(cloud):
        t3s("clouds.t3s", ["clouds.png"], "rgba8888")
        print("mk_flight: clouds.png")


if __name__ == "__main__":
    main()
