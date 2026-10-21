#!/usr/bin/env python3
"""자체 이미지 디코더 테스트용 작은 이미지들을 tests/fixtures/images/에 만든다.

필요: Python 3 + Pillow, ImageMagick 6(`convert`). 결과 파일은 저장소에 커밋되어 있으므로
테스트 실행에는 이 스크립트가 필요 없다. 형식 변형을 추가할 때만 다시 돌린다.

PNG/BMP/TGA는 이 스크립트가 직접 바이트를 써서 필터 종류, IDAT 분할, 압축 수준(저장/고정/동적 허프만),
RLE 패킷, 행 방향 같은 경우를 확실히 만든다. JPEG는 Pillow(libjpeg)와 ImageMagick 인코더를 쓴다.
"""
import io
import random
import struct
import subprocess
import sys
import zlib
from pathlib import Path

from PIL import Image

OUT = Path(__file__).resolve().parent.parent / "tests" / "fixtures" / "images"


def pattern(width, height, seed=1):
    """경계·그라디언트·잡음이 섞인 RGBA 픽셀(행 우선)."""
    rng = random.Random(seed)
    pixels = []
    for y in range(height):
        for x in range(width):
            r = (x * 255) // max(1, width - 1)
            g = (y * 255) // max(1, height - 1)
            b = 255 if (x // 4 + y // 4) % 2 else 32
            if (x * 7 + y * 3) % 11 == 0:
                r, g, b = rng.randrange(256), rng.randrange(256), rng.randrange(256)
            a = (x * 31 + y * 17) % 256
            pixels.append((r, g, b, a))
    return pixels


def photo(width, height, seed=3):
    """JPEG용 부드러운 이미지(블록 경계와 색차 업샘플링을 보이게 한다)."""
    rng = random.Random(seed)
    image = Image.new("RGB", (width, height))
    data = []
    for y in range(height):
        for x in range(width):
            r = int(128 + 120 * ((x / max(1, width - 1)) - 0.5) * 2) & 255
            g = (x * y * 3) & 255
            b = int(255 * ((y // 5) % 2)) if x > width // 2 else (y * 255) // max(1, height - 1)
            if rng.random() < 0.02:
                r, g, b = 255, 255, 255
            data.append((max(0, min(255, r)), g, b))
    image.putdata(data)
    return image


# ---------------------------------------------------------------- PNG
def png_chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def filter_rows(rows, bpp, filters):
    """rows: 필터 전 바이트 행. filters: 행마다 쓸 필터 번호를 돌려주는 함수."""
    out = bytearray()
    previous = bytes(len(rows[0])) if rows else b""
    for index, row in enumerate(rows):
        kind = filters(index)
        out.append(kind)
        for i, value in enumerate(row):
            left = row[i - bpp] if i >= bpp else 0
            up = previous[i]
            up_left = previous[i - bpp] if i >= bpp else 0
            predicted = [0, left, up, (left + up) >> 1, paeth(left, up, up_left)][kind]
            out.append((value - predicted) & 255)
        previous = row
    return bytes(out)


def pack_samples(samples, depth):
    """한 행의 샘플을 depth 비트로 묶는다(MSB 우선)."""
    if depth == 8:
        return bytes(samples)
    if depth == 16:
        return b"".join(struct.pack(">H", s) for s in samples)
    out = bytearray()
    accumulator, bits = 0, 0
    for sample in samples:
        accumulator = (accumulator << depth) | sample
        bits += depth
        if bits == 8:
            out.append(accumulator)
            accumulator, bits = 0, 0
    if bits:
        out.append(accumulator << (8 - bits))
    return bytes(out)


ADAM7 = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]


def write_png(name, width, height, color_type, depth, sample_rows, *, interlace=False, level=9, idat_split=0,
              filters=lambda i: i % 5, extra=()):
    """sample_rows[y][x]는 픽셀의 채널 샘플 튜플(팔레트는 인덱스 하나)."""
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color_type]
    bpp = max(1, channels * depth // 8)
    passes = ADAM7 if interlace else [(0, 0, 1, 1)]
    raw = b""
    for x0, y0, dx, dy in passes:
        rows = []
        for y in range(y0, height, dy):
            samples = []
            for x in range(x0, width, dx):
                samples.extend(sample_rows[y][x])
            if samples:
                rows.append(pack_samples(samples, depth))
        if rows:
            raw += filter_rows(rows, bpp, filters)
    compressed = zlib.compress(raw, level)
    data = b"\x89PNG\r\n\x1a\n"
    data += png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, color_type, 0, 0, 1 if interlace else 0))
    for kind, body in extra:
        data += png_chunk(kind, body)
    if idat_split:
        for start in range(0, len(compressed), idat_split):
            data += png_chunk(b"IDAT", compressed[start:start + idat_split])
    else:
        data += png_chunk(b"IDAT", compressed)
    data += png_chunk(b"tEXt", b"Comment\x00ancillary chunk after IDAT")
    data += png_chunk(b"IEND", b"")
    (OUT / name).write_bytes(data)


def make_pngs():
    w, h = 37, 23
    px = pattern(w, h)
    grid = [[px[y * w + x] for x in range(w)] for y in range(h)]
    gray = [[((r * 3 + g * 6 + b) // 10,) for (r, g, b, a) in row] for row in grid]

    write_png("png-rgb8.png", w, h, 2, 8, [[p[:3] for p in row] for row in grid])
    write_png("png-rgba8.png", w, h, 6, 8, grid)
    write_png("png-rgba8-interlaced.png", w, h, 6, 8, grid, interlace=True)
    write_png("png-rgb16.png", w, h, 2, 16, [[tuple(min(65535, c * 257 + (x % 7)) for c in p[:3]) for x, p in enumerate(row)]
                                            for row in grid])
    write_png("png-rgba16-interlaced.png", w, h, 6, 16, [[tuple(c * 257 for c in p) for p in row] for row in grid],
              interlace=True)
    write_png("png-gray8.png", w, h, 0, 8, gray)
    write_png("png-gray-alpha8.png", w, h, 4, 8, [[(g[0], p[3]) for g, p in zip(grow, prow)]
                                                  for grow, prow in zip(gray, grid)])
    write_png("png-gray16.png", w, h, 0, 16, [[(g[0] * 257,) for g in row] for row in gray])
    for depth in (1, 2, 4):
        write_png(f"png-gray{depth}.png", w, h, 0, depth, [[(g[0] >> (8 - depth),) for g in row] for row in gray])
        write_png(f"png-gray{depth}-interlaced.png", w, h, 0, depth,
                  [[(g[0] >> (8 - depth),) for g in row] for row in gray], interlace=True)
    # 팔레트: 깊이마다 쓸 수 있는 색 수만큼, tRNS로 일부 투명.
    for depth in (1, 2, 4, 8):
        colors = 1 << depth
        palette = bytes(v for i in range(colors) for v in ((i * 53) % 256, (i * 97) % 256, (i * 151) % 256))
        trns = bytes((i * 40) % 256 for i in range(colors // 2 + 1))
        indices = [[((x + y * 3) % colors,) for x in range(w)] for y in range(h)]
        write_png(f"png-palette{depth}.png", w, h, 3, depth, indices,
                  extra=[(b"PLTE", palette), (b"tRNS", trns)])
    write_png("png-palette8-interlaced.png", w, h, 3, 8, [[((x * y) % 200,) for x in range(w)] for y in range(h)],
              interlace=True,
              extra=[(b"PLTE", bytes(v for i in range(200) for v in (i, 255 - i, (i * 7) % 256)))])
    # 색 키 투명(tRNS): 회색과 RGB.
    write_png("png-gray8-trns.png", w, h, 0, 8, gray, extra=[(b"tRNS", struct.pack(">H", gray[0][0][0]))])
    key = grid[3][5][:3]
    write_png("png-rgb8-trns.png", w, h, 2, 8, [[p[:3] for p in row] for row in grid],
              extra=[(b"tRNS", struct.pack(">HHH", *key))])
    # inflate 경로: 저장 블록(level 0), 빠른 압축(level 1, 고정 허프만 위주), 작은 IDAT 여러 개, 필터 하나만.
    write_png("png-stored.png", w, h, 6, 8, grid, level=0)
    write_png("png-level1-split.png", w, h, 6, 8, grid, level=1, idat_split=7)
    for kind in range(5):
        write_png(f"png-filter{kind}.png", w, h, 2, 8, [[p[:3] for p in row] for row in grid],
                  filters=lambda i, k=kind: k)
    # 크기 극단: 1×1, 1행, 1열, 큰 반복(긴 거리·긴 길이 일치).
    write_png("png-1x1.png", 1, 1, 6, 8, [[(10, 20, 30, 40)]])
    write_png("png-1x9-interlaced.png", 1, 9, 2, 8, [[(y * 20, 0, 255 - y * 20)] for y in range(9)],
              interlace=True)
    write_png("png-9x1.png", 9, 1, 2, 8, [[(x * 20, x, 0) for x in range(9)]])
    big_w, big_h = 200, 120
    write_png("png-repeat.png", big_w, big_h, 2, 8,
              [[((x // 16) * 30 % 256, (y // 8) * 20 % 256, 128) for x in range(big_w)] for y in range(big_h)])


# ---------------------------------------------------------------- BMP
def write_bmp(name, width, height, bpp, rows, *, palette=None, top_down=False, compression=0, masks=None,
              header_size=40, colors_used=0):
    """rows[y]는 이미 패딩 없는 행 바이트(위에서 아래). palette는 (r,g,b) 목록."""
    stride = ((width * bpp + 31) // 32) * 4
    pixel_data = b""
    ordered = rows if top_down else list(reversed(rows))
    for row in ordered:
        pixel_data += row + bytes(stride - len(row))
    if header_size == 12:
        info = struct.pack("<IHHHH", 12, width, height, 1, bpp)
        pal = b"".join(bytes((b, g, r)) for (r, g, b) in (palette or []))
    else:
        info = struct.pack("<IiiHHIIiiII", header_size, width, -height if top_down else height, 1, bpp, compression,
                           len(pixel_data), 2835, 2835, colors_used, 0)
        if header_size >= 52:
            m = list(masks or (0, 0, 0, 0)) + [0] * 4
            info += struct.pack("<IIII", *m[:4]) if header_size >= 56 else struct.pack("<III", *m[:3])
            info += bytes(header_size - len(info))
        elif masks:
            info += b"".join(struct.pack("<I", m) for m in masks)
        pal = b"".join(bytes((b, g, r, 0)) for (r, g, b) in (palette or []))
    offset = 14 + len(info) + len(pal)
    header = b"BM" + struct.pack("<IHHI", offset + len(pixel_data), 0, 0, offset)
    (OUT / name).write_bytes(header + info + pal + pixel_data)


def pack_bits(values, bits):
    return pack_samples(values, bits)


def make_bmps():
    w, h = 37, 23
    px = pattern(w, h, seed=5)
    grid = [[px[y * w + x] for x in range(w)] for y in range(h)]
    rgb24 = [b"".join(bytes((b, g, r)) for (r, g, b, a) in row) for row in grid]
    write_bmp("bmp-rgb24.bmp", w, h, 24, rgb24)
    write_bmp("bmp-rgb24-topdown.bmp", w, h, 24, rgb24, top_down=True)
    bgra = [b"".join(bytes((b, g, r, a)) for (r, g, b, a) in row) for row in grid]
    write_bmp("bmp-bgra32.bmp", w, h, 32, bgra)
    # 알파가 전부 0인 32비트(BI_RGB): 불투명으로 읽어야 한다.
    write_bmp("bmp-bgrx32.bmp", w, h, 32, [b"".join(bytes((b, g, r, 0)) for (r, g, b, a) in row) for row in grid])
    # BI_BITFIELDS 16비트 565 / 555, 32비트 사용자 마스크(INFO 뒤 마스크)와 V5 헤더 알파 마스크.
    rgb565 = [b"".join(struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)) for (r, g, b, a) in row)
              for row in grid]
    write_bmp("bmp-rgb565.bmp", w, h, 16, rgb565, compression=3, masks=(0xF800, 0x07E0, 0x001F))
    rgb555 = [b"".join(struct.pack("<H", ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)) for (r, g, b, a) in row)
              for row in grid]
    write_bmp("bmp-rgb555.bmp", w, h, 16, rgb555)
    argb4444 = [b"".join(struct.pack("<H", ((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4))
                         for (r, g, b, a) in row) for row in grid]
    write_bmp("bmp-argb4444-v5.bmp", w, h, 16, argb4444, compression=3, masks=(0x0F00, 0x00F0, 0x000F, 0xF000),
              header_size=124)
    rgba32 = [b"".join(struct.pack("<I", (r << 24) | (g << 16) | (b << 8) | a) for (r, g, b, a) in row)
              for row in grid]
    write_bmp("bmp-rgba32-bitfields.bmp", w, h, 32, rgba32, compression=3,
              masks=(0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF), header_size=108)
    write_bmp("bmp-rgb32-bitfields-info.bmp", w, h, 32, rgba32, compression=3,
              masks=(0xFF000000, 0x00FF0000, 0x0000FF00))
    # 팔레트 1/4/8비트, 일부만 쓰는 팔레트(colorsUsed), OS/2 core 헤더.
    for bits in (1, 4, 8):
        colors = 1 << bits
        palette = [((i * 53) % 256, (i * 97) % 256, (i * 151) % 256) for i in range(colors)]
        rows = [pack_bits([(x + y * 3) % colors for x in range(w)], bits) for y in range(h)]
        write_bmp(f"bmp-palette{bits}.bmp", w, h, bits, rows, palette=palette)
    palette = [(i * 20, 255 - i * 20, 128) for i in range(12)]
    write_bmp("bmp-palette8-used12.bmp", w, h, 8, [bytes((x + y) % 12 for x in range(w)) for y in range(h)],
              palette=palette, colors_used=12)
    # stb_image는 OS/2 팔레트 수를 (offset - 38) / 3으로 세어 마지막 4색(252~255)을 놓친다.
    # 오라클 비교가 그 버그에 걸리지 않도록 인덱스를 240 미만으로 둔다.
    palette = [((i * 53) % 256, (i * 97) % 256, (i * 151) % 256) for i in range(256)]
    write_bmp("bmp-os2-palette8.bmp", w, h, 8, [bytes((x * y) % 240 for x in range(w)) for y in range(h)],
              palette=palette, header_size=12)
    write_bmp("bmp-os2-rgb24.bmp", w, h, 24, rgb24, header_size=12)
    write_bmp("bmp-1x1.bmp", 1, 1, 24, [bytes((1, 2, 3))])
    # 오라클 비교 대상이 아닌 거부 사례: RLE8.
    write_bmp("bmp-rle8-unsupported.bmp", 4, 1, 8, [bytes((4, 1, 0, 1))], palette=[(0, 0, 0), (255, 0, 0)],
              compression=1)


# ---------------------------------------------------------------- TGA
def write_tga(name, width, height, image_type, bpp, pixels, *, palette=None, palette_bits=24, top_down=False,
              right_to_left=False, rle=False, id_field=b"", alpha_bits=0):
    """pixels: 위에서 아래, 왼쪽에서 오른쪽 순서의 디스크 표현 바이트 목록(픽셀당 bytes)."""
    rows = [pixels[y * width:(y + 1) * width] for y in range(height)]
    if not top_down:
        rows.reverse()
    if right_to_left:
        rows = [list(reversed(row)) for row in rows]
    ordered = [p for row in rows for p in row]
    if rle:
        body = bytearray()
        i = 0
        while i < len(ordered):
            run = 1
            while i + run < len(ordered) and run < 128 and ordered[i + run] == ordered[i]:
                run += 1
            if run > 1:
                body.append(0x80 | (run - 1))
                body += ordered[i]
                i += run
                continue
            raw = [ordered[i]]
            i += 1
            while i < len(ordered) and len(raw) < 128 and not (i + 1 < len(ordered) and ordered[i + 1] == ordered[i]):
                raw.append(ordered[i])
                i += 1
            body.append(len(raw) - 1)
            for p in raw:
                body += p
        data = bytes(body)
    else:
        data = b"".join(ordered)
    descriptor = alpha_bits | (0x20 if top_down else 0) | (0x10 if right_to_left else 0)
    colormap = b""
    if palette is not None:
        colormap = b"".join(palette)
    header = struct.pack("<BBBHHBHHHHBB", len(id_field), 1 if palette is not None else 0,
                         image_type + (8 if rle else 0), 0, len(palette or []), palette_bits if palette else 0,
                         0, 0, width, height, bpp, descriptor)
    (OUT / name).write_bytes(header + id_field + colormap + data + b"\x00" * 26)


def make_tgas():
    w, h = 37, 23
    px = pattern(w, h, seed=9)
    # RLE가 실제로 반복 패킷을 만들도록 오른쪽 절반을 단색 띠로 만든다.
    px = [(p if x < w // 2 else ((y // 3) * 40 % 256, 90, 200, 255)) for i, p in enumerate(px)
          for x, y in [(i % w, i // w)]]
    bgr = [bytes((b, g, r)) for (r, g, b, a) in px]
    bgra = [bytes((b, g, r, a)) for (r, g, b, a) in px]
    gray = [bytes(((r * 3 + g * 6 + b) // 10,)) for (r, g, b, a) in px]
    for rle in (False, True):
        suffix = "-rle" if rle else ""
        write_tga(f"tga-rgb24{suffix}.tga", w, h, 2, 24, bgr, rle=rle)
        write_tga(f"tga-rgba32{suffix}.tga", w, h, 2, 32, bgra, rle=rle, alpha_bits=8, top_down=True)
        write_tga(f"tga-gray8{suffix}.tga", w, h, 3, 8, gray, rle=rle)
        rgb555 = [struct.pack("<H", ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)) for (r, g, b, a) in px]
        write_tga(f"tga-rgb16{suffix}.tga", w, h, 2, 16, rgb555, rle=rle)
        palette = [bytes(((i * 151) % 256, (i * 97) % 256, (i * 53) % 256)) for i in range(64)]
        indices = [bytes(((x + y * 5) % 64,)) for y in range(h) for x in range(w)]
        write_tga(f"tga-palette8{suffix}.tga", w, h, 1, 8, indices, palette=palette, rle=rle)
    write_tga("tga-gray-alpha16.tga", w, h, 3, 16, [g + bytes((a,)) for g, (r, gg, b, a) in zip(gray, px)],
              top_down=True, alpha_bits=8, id_field=b"image id field")
    palette32 = [bytes(((i * 53) % 256, (i * 97) % 256, (i * 151) % 256, (i * 29) % 256)) for i in range(256)]
    write_tga("tga-palette8-bgra32.tga", w, h, 1, 8, [bytes(((x * y) % 256,)) for y in range(h) for x in range(w)],
              palette=palette32, palette_bits=32)
    write_tga("tga-1x1.tga", 1, 1, 2, 24, [bytes((1, 2, 3))])
    # 오른쪽→왼쪽 저장(기술자 bit 4). stb는 이 비트를 무시하므로 오라클 비교 대신 기대 픽셀로 검사한다.
    write_tga("tga-right-to-left.tga", 3, 2, 2, 24, [bytes((0, 0, v)) for v in (10, 20, 30, 40, 50, 60)],
              top_down=True, right_to_left=True)


# ---------------------------------------------------------------- JPEG
def make_jpegs():
    img = photo(45, 29)
    img.save(OUT / "jpeg-444.jpg", quality=90, subsampling=0)
    img.save(OUT / "jpeg-422.jpg", quality=85, subsampling=1)
    img.save(OUT / "jpeg-420.jpg", quality=75, subsampling=2)
    img.save(OUT / "jpeg-420-optimized.jpg", quality=60, subsampling=2, optimize=True)
    img.save(OUT / "jpeg-420-restart.jpg", quality=80, subsampling=2, restart_marker_blocks=3)
    img.save(OUT / "jpeg-444-restart-rows.jpg", quality=95, subsampling=0, restart_marker_rows=1)
    img.save(OUT / "jpeg-q100.jpg", quality=100, subsampling=0)
    img.save(OUT / "jpeg-rgb.jpg", quality=90, keep_rgb=True)
    img.convert("L").save(OUT / "jpeg-gray.jpg", quality=85)
    img.resize((1, 1)).save(OUT / "jpeg-1x1.jpg", quality=90)
    photo(17, 3).save(OUT / "jpeg-17x3-420.jpg", quality=90, subsampling=2)
    photo(64, 48).save(OUT / "jpeg-64x48-420.jpg", quality=70, subsampling=2)
    # 255를 넘는 양자화 값은 16비트 DQT(정밀도 1)와 SOF1(확장 순차)로 저장된다.
    img.save(OUT / "jpeg-qtable16.jpg", qtables=[[300 + i for i in range(64)], [400] * 64])
    img.save(OUT / "jpeg-progressive-unsupported.jpg", quality=80, progressive=True)
    # ImageMagick(libjpeg): 4:1:1, 4:4:0, 16비트 양자화 표, CMYK(Adobe 변환 0).
    src = OUT / "_source.png"
    img.save(src)
    for name, args in [("jpeg-411.jpg", ["-sampling-factor", "4x1,1x1,1x1"]),
                       ("jpeg-440.jpg", ["-sampling-factor", "1x2,1x1,1x1"]),
                       ("jpeg-cmyk.jpg", ["-colorspace", "CMYK"])]:
        subprocess.run(["convert", str(src), "-quality", "85", *args, str(OUT / name)], check=True)
    src.unlink()


def make_third_party():
    """다른 인코더(ImageMagick)가 만든 사진 같은 이미지. 직접 쓴 fixture가 놓친 인코더 습관을 잡는다."""
    plasma = ["convert", "-seed", "7", "-size", "96x72", "plasma:fractal"]
    for name, args in [("im-plasma-420.jpg", ["-quality", "82", "-sampling-factor", "2x2,1x1,1x1"]),
                       ("im-plasma-444.jpg", ["-quality", "92", "-sampling-factor", "1x1,1x1,1x1"]),
                       ("im-plasma.png", ["-depth", "8"]),
                       ("im-plasma-interlaced16.png", ["-interlace", "PNG", "-depth", "16"]),
                       ("im-plasma-palette.png", ["-colors", "40", "PNG8:"]),
                       ("im-plasma.bmp", ["BMP3:"]),
                       ("im-plasma.tga", ["-compress", "RLE"])]:
        target = str(OUT / name)
        if args and args[-1].endswith(":"):
            args, target = args[:-1], args[-1] + target
        subprocess.run([*plasma, *args, target], check=True)


# ---------------------------------------------------------------- DDS
DDS_REFERENCE = "dds-reference.txt"


def fnv1a64(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def dds_header(width, height, pixel_flags, fourcc=b"\0\0\0\0", bit_count=0, masks=(0, 0, 0, 0), dxgi=None):
    flags = 0x1 | 0x2 | 0x4 | 0x1000  # CAPS | HEIGHT | WIDTH | PIXELFORMAT
    header = struct.pack("<4sIIIIIII", b"DDS ", 124, flags, height, width, 0, 0, 1)
    header += bytes(44)
    header += struct.pack("<II4sIIIII", 32, pixel_flags, fourcc, bit_count, *masks)
    header += struct.pack("<IIIII", 0x1000, 0, 0, 0, 0)
    if dxgi is not None:
        header += struct.pack("<IIIII", dxgi, 3, 0, 1, 0)
    return header


def write_dds_blocks(name, width, height, block_bytes, *, fourcc=None, dxgi=None, seed=0, block_maker=None):
    """무작위 블록. BCn은 어떤 비트 패턴도 유효한 블록이라 모든 모드·분할·끝점 순서를 고루 지난다."""
    rng = random.Random(seed)
    count = ((width + 3) // 4) * ((height + 3) // 4)
    data = b"".join((block_maker or (lambda i: bytes(rng.randrange(256) for _ in range(block_bytes))))(i)
                    for i in range(count))
    header = dds_header(width, height, 0x4, fourcc or b"DX10", dxgi=dxgi)
    (OUT / name).write_bytes(header + data)


def write_dds_masked(name, width, height, bit_count, masks, pixel_flags, pixels):
    """pixels: 위에서 아래 RGBA. masks로 패킹한 값을 리틀 엔디언으로 쓰고, 기대 RGBA도 같이 돌려준다."""
    def pack(value, mask):
        if not mask:
            return 0
        shift = (mask & -mask).bit_length() - 1
        bits = bin(mask).count("1")
        if bits > 8:
            return (((value << (bits - 8)) | (value >> (16 - bits))) << shift) & mask
        return ((value >> (8 - bits)) << shift) & mask

    def unpack(word, mask):
        if not mask:
            return None
        shift = (mask & -mask).bit_length() - 1
        bits = bin(mask).count("1")
        v = (word & mask) >> shift
        if bits >= 8:
            return (v >> (bits - 8)) & 255
        result, filled = 0, 0
        while filled < 8:
            result = (result << bits) | v
            filled += bits
        return (result >> (filled - 8)) & 255

    data = bytearray()
    expected = bytearray()
    luminance = pixel_flags & 0x20000
    alpha_only = not (pixel_flags & (0x40 | 0x20000))
    for (r, g, b, a) in pixels:
        if luminance:
            word = pack(r, masks[0]) | pack(a, masks[3])
        else:
            word = pack(r, masks[0]) | pack(g, masks[1]) | pack(b, masks[2]) | pack(a, masks[3])
        data += word.to_bytes(bit_count // 8, "little")
        if luminance:
            v = unpack(word, masks[0])
            rgb = (v, v, v)
        elif alpha_only:
            rgb = (0, 0, 0)
        else:
            rgb = tuple(unpack(word, m) for m in masks[:3])
        av = unpack(word, masks[3]) if masks[3] else 255
        expected += bytes((*rgb, av))
    (OUT / name).write_bytes(dds_header(width, height, pixel_flags, bit_count=bit_count, masks=masks) + data)
    return bytes(expected)


def make_dds():
    w, h = 37, 23
    reference = {}
    px = pattern(w, h, seed=11)
    rgba = Image.new("RGBA", (w, h))
    rgba.putdata(px)
    photo_rgb = photo(w, h, seed=5)
    # Pillow 인코더(BC1/BC2/BC3/BC5, 비압축 RGB/RGBA/L/LA).
    for name, image, fmt in [("dds-dxt1.dds", photo_rgb, "DXT1"), ("dds-dxt1-alpha.dds", rgba, "DXT1"),
                             ("dds-dxt3.dds", rgba, "DXT3"), ("dds-dxt5.dds", rgba, "DXT5"),
                             ("dds-dx10-bc2.dds", rgba, "BC2"), ("dds-dx10-bc3.dds", rgba, "BC3"),
                             ("dds-dx10-bc5.dds", photo_rgb, "BC5")]:
        image.save(OUT / name, pixel_format=fmt)
    rgba.save(OUT / "dds-rgba8.dds")
    rgba.convert("RGB").save(OUT / "dds-rgb8.dds")
    rgba.convert("L").save(OUT / "dds-l8.dds")
    rgba.convert("LA").save(OUT / "dds-la8.dds")
    # 무작위 블록(가장자리가 4의 배수가 아닌 크기).
    write_dds_blocks("dds-bc1-random.dds", w, h, 8, fourcc=b"DXT1", seed=1)
    write_dds_blocks("dds-bc2-random.dds", w, h, 16, fourcc=b"DXT3", seed=2)
    write_dds_blocks("dds-bc3-random.dds", w, h, 16, fourcc=b"DXT5", seed=3)
    write_dds_blocks("dds-bc4-ati1-random.dds", w, h, 8, fourcc=b"ATI1", seed=4)
    write_dds_blocks("dds-bc5-ati2-random.dds", w, h, 16, fourcc=b"ATI2", seed=5)
    write_dds_blocks("dds-dx10-bc4-random.dds", w, h, 8, dxgi=80, seed=6)
    rng = random.Random(7)

    def bc7_block(i):
        # 모드 0~7을 차례로: 첫 바이트의 최하위 1비트 위치가 모드다(0바이트 예약 모드는 피한다).
        mode = i % 8
        block = bytearray(rng.randrange(256) for _ in range(16))
        block[0] = ((block[0] << (mode + 1)) & 0xFF) | (1 << mode)
        return bytes(block)
    write_dds_blocks("dds-dx10-bc7-random.dds", 64, 36, 16, dxgi=98, block_maker=bc7_block)
    write_dds_blocks("dds-dx10-bc6h-unsupported.dds", 8, 8, 16, dxgi=95, seed=8)

    # 레거시 비트 마스크 비압축 형식: 기대값은 이 스크립트가 직접 계산한다.
    masked = [("dds-r5g6b5.dds", 16, (0xF800, 0x07E0, 0x001F, 0), 0x40),
              ("dds-a1r5g5b5.dds", 16, (0x7C00, 0x03E0, 0x001F, 0x8000), 0x41),
              ("dds-a4r4g4b4.dds", 16, (0x0F00, 0x00F0, 0x000F, 0xF000), 0x41),
              ("dds-r8g8b8.dds", 24, (0xFF0000, 0x00FF00, 0x0000FF, 0), 0x40),
              ("dds-x8r8g8b8.dds", 32, (0xFF0000, 0x00FF00, 0x0000FF, 0), 0x40),
              ("dds-a8b8g8r8.dds", 32, (0x0000FF, 0x00FF00, 0xFF0000, 0xFF000000), 0x41),
              ("dds-a2r10g10b10.dds", 32, (0x3FF00000, 0x000FFC00, 0x000003FF, 0xC0000000), 0x41),
              ("dds-a8.dds", 8, (0, 0, 0, 0xFF), 0x2),
              ("dds-a8l8.dds", 16, (0x00FF, 0, 0, 0xFF00), 0x20001)]
    for name, bits, masks, flags in masked:
        reference[name] = (w, h, write_dds_masked(name, w, h, bits, masks, flags, px))

    # 밉맵 체인이 붙은 파일: 첫 레벨 뒤에 작은 레벨들이 이어진다. 첫 레벨만 읽어야 하므로 기준값은 밉 없는 파일과 같다.
    base = (OUT / "dds-dxt5.dds").read_bytes()
    header = bytearray(base[:128])
    struct.pack_into("<I", header, 28, 4)                         # dwMipMapCount
    struct.pack_into("<I", header, 8, struct.unpack_from("<I", header, 8)[0] | 0x20000)  # DDSD_MIPMAPCOUNT
    struct.pack_into("<I", header, 108, 0x1000 | 0x400000 | 0x8)  # TEXTURE | MIPMAP | COMPLEX
    mips = bytes(random.Random(12).randrange(256) for _ in range(16 * (5 * 3 + 3 * 2 + 2 * 1)))
    (OUT / "dds-dxt5-mips.dds").write_bytes(bytes(header) + base[128:] + mips)

    # 압축·Pillow 출력은 Pillow 디코더를 기준값으로 쓴다(비압축은 직접 계산한 값과도 비교).
    for path in sorted(OUT.glob("dds-*.dds")):
        if "unsupported" in path.name:
            continue
        try:
            with Image.open(path) as image:
                pillow = image.convert("RGBA")
                data = pillow.tobytes()
        except (NotImplementedError, OSError):
            # Pillow가 모르는 마스크 형식(A8 등)은 직접 계산한 기대값만 쓴다.
            assert path.name in reference, path.name
            continue
        if path.name in reference:
            if reference[path.name][2] != data:
                print(f"note: Pillow differs from computed reference for {path.name}")
            continue
        reference[path.name] = (pillow.width, pillow.height, data)
    lines = ["# 이름 너비 높이 FNV-1a64(RGBA 바이트). tools/make_image_fixtures.py가 Pillow 디코더/직접 계산으로 만든다."]
    for name in sorted(reference):
        width, height, data = reference[name]
        lines.append(f"{name} {width} {height} {fnv1a64(data):016x}")
    (OUT / DDS_REFERENCE).write_text("\n".join(lines) + "\n")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    make_pngs()
    make_bmps()
    make_tgas()
    make_jpegs()
    make_third_party()
    make_dds()
    total = sum(p.stat().st_size for p in OUT.iterdir())
    print(f"{len(list(OUT.iterdir()))} files, {total} bytes in {OUT}")


if __name__ == "__main__":
    sys.exit(main())
