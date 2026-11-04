"""Muse-supplied PNG images for generated native apps.

Muse can generate pictures (logos, backgrounds, sprites). It sends them in the
same authenticated source submission as base64 PNG files; nothing is fetched
from the network. The builder decodes them here (pure Python, no image
library) and turns them into `include/muse_images.hpp`, a header with the RGBA
pixels compiled into the app, so the immutable native kit does not change.
The app uploads each one with `muse_images::upload(renderer, muse_images::logo)`
and draws it with `DrawList::image`.
"""
from __future__ import annotations

import base64
import binascii
import re
import struct
import zlib

MAX_IMAGES = 8
MAX_PNG_BYTES = 2 * 1024 * 1024        # each encoded PNG
MAX_SIDE = 1024                         # pixels, each side
MAX_TOTAL_RGBA = 3 * 1024 * 1024        # all decoded pixels together (compiled into the app)
NAME = re.compile(r"[a-z][a-z0-9_]{0,31}\.png")


class ImageError(ValueError):
    pass


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def decode_png(data: bytes) -> tuple[int, int, bytes]:
    """8-bit, non-interlaced grey/RGB/palette/grey+alpha/RGBA PNG -> (w, h, RGBA)."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ImageError("not a PNG file")
    pos, header, palette, alpha, idat = 8, None, None, None, bytearray()
    while pos + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if len(body) != length or pos + 12 + length > len(data):
            raise ImageError("truncated PNG chunk")
        if zlib.crc32(kind + body) & 0xffffffff != struct.unpack(">I", data[pos + 8 + length:pos + 12 + length])[0]:
            raise ImageError("PNG chunk checksum mismatch")
        pos += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = body
        elif kind == b"tRNS":
            alpha = body
        elif kind == b"IDAT":
            idat += body
            if len(idat) > MAX_PNG_BYTES:
                raise ImageError("PNG image data too large")
        elif kind == b"IEND":
            break
    if not header:
        raise ImageError("PNG has no IHDR")
    width, height, depth, color, compression, filtering, interlace = header
    if not (1 <= width <= MAX_SIDE and 1 <= height <= MAX_SIDE):
        raise ImageError(f"PNG must be at most {MAX_SIDE}x{MAX_SIDE} pixels")
    if depth != 8 or compression or filtering or interlace:
        raise ImageError("use an 8-bit, non-interlaced PNG")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color)
    if channels is None or (color == 3 and not palette):
        raise ImageError("unsupported PNG colour type")
    stride = width * channels
    try:
        raw = zlib.decompressobj().decompress(bytes(idat), (stride + 1) * height + 1)
    except zlib.error as exc:
        raise ImageError("corrupt PNG image data") from exc
    if len(raw) < (stride + 1) * height:
        raise ImageError("PNG image data is incomplete")
    out = bytearray(width * height * 4)
    previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        mode, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            if mode == 1:
                line[i] = (line[i] + left) & 255
            elif mode == 2:
                line[i] = (line[i] + up) & 255
            elif mode == 3:
                line[i] = (line[i] + ((left + up) >> 1)) & 255
            elif mode == 4:
                line[i] = (line[i] + _paeth(left, up, previous[i - channels] if i >= channels else 0)) & 255
            elif mode != 0:
                raise ImageError("invalid PNG filter")
        for x in range(width):
            o = (y * width + x) * 4
            if color == 6:
                out[o:o + 4] = line[x * 4:x * 4 + 4]
            elif color == 2:
                out[o:o + 3] = line[x * 3:x * 3 + 3]
                out[o + 3] = 255
            elif color == 0:
                out[o:o + 3] = bytes((line[x],)) * 3
                out[o + 3] = 255
            elif color == 4:
                out[o:o + 3] = bytes((line[x * 2],)) * 3
                out[o + 3] = line[x * 2 + 1]
            else:
                index = line[x]
                if index * 3 + 3 > len(palette):
                    raise ImageError("PNG palette index out of range")
                out[o:o + 3] = palette[index * 3:index * 3 + 3]
                out[o + 3] = alpha[index] if alpha and index < len(alpha) else 255
        previous = line
    return width, height, bytes(out)


def validate_images(images) -> list[tuple[str, int, int, bytes]]:
    """Check limits and decode every image; returns (identifier, w, h, rgba)."""
    if images is None:
        return []
    if not isinstance(images, dict) or len(images) > MAX_IMAGES:
        raise ImageError(f"images must map up to {MAX_IMAGES} names to base64 PNG data")
    decoded, total = [], 0
    for name in sorted(images):
        value = images[name]
        if not isinstance(name, str) or not NAME.fullmatch(name):
            raise ImageError("image names look like logo.png: lowercase letters, digits, underscore")
        if not isinstance(value, str) or len(value) > MAX_PNG_BYTES * 4 // 3 + 4:
            raise ImageError(f"{name}: send base64 PNG data of at most {MAX_PNG_BYTES} bytes")
        try:
            png = base64.b64decode(value, validate=True)
        except (binascii.Error, ValueError) as exc:
            raise ImageError(f"{name}: invalid base64") from exc
        try:
            width, height, rgba = decode_png(png)
        except ImageError as exc:
            raise ImageError(f"{name}: {exc}") from exc
        total += len(rgba)
        if total > MAX_TOTAL_RGBA:
            raise ImageError(f"images exceed {MAX_TOTAL_RGBA // (1024 * 1024)} MiB of pixels in total; use smaller images")
        decoded.append((name[:-4], width, height, rgba))
    return decoded


def header(decoded) -> str:
    """C++ header with each image's pixels, plus a one-line upload helper."""
    lines = ["// Generated by the MuseStation relay from Muse's PNG images. Do not edit.",
             "#pragma once", "#include <cstdint>", '#include "gfx/renderer.hpp"', "", "namespace muse_images {",
             "struct Image { int width, height; const unsigned char *rgba; };",
             "// Upload once after the renderer exists; draw with",
             "// draw_list.image(texture, {x, y, w, h}, hui::gfx::kFullUv, hui::gfx::Color::rgb(0xffffff));",
             "inline std::uint32_t upload(hui::gfx::Renderer &renderer, const Image &image) {",
             "  return renderer.batch().create_texture(image.width, image.height, image.rgba);", "}"]
    for ident, width, height, rgba in decoded:
        lines.append(f"alignas(4) inline const unsigned char {ident}_rgba[] = {{")
        for start in range(0, len(rgba), 4096):
            lines.append(",".join(map(str, rgba[start:start + 4096])) + ",")
        lines.append("};")
        lines.append(f"inline const Image {ident}{{{width}, {height}, {ident}_rgba}};")
    lines += ["}  // namespace muse_images", ""]
    return "\n".join(lines)
