#!/usr/bin/env python3
"""Rasterize Geneva and Monaco into Cloister CFF (UFX uf2, 16x16).

Reads the macOS system faces and writes:
  resources/geneva12.cff  src/geneva.h
  resources/monaco12.cff  src/monaco.h

Cell, baseline, and packing match resources/chicago12x12.cff so the three
faces share a 16-pixel em and DRAW::set-font can switch without retuning
scale. Cap height sits on rows 3-11 (baseline y=12), same as Chicago.

A 1:1 TrueType raster at 12px is a 1-pixel hairline. Classic Mac bitmap
faces (Chicago especially) use 2-pixel stems. After thresholding we OR
each glyph with a copy shifted one pixel right -- the same fake-bold
QuickDraw used -- so body text stays readable on a 1-bit screen.
"""
from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
GENEVA_TTF = "/System/Library/Fonts/Geneva.ttf"
MONACO_TTF = "/System/Library/Fonts/Monaco.ttf"

CELL = 16
GLYPH_BYTES = 32  # 2x2 8x8 tiles
CFF_LEN = 256 + 256 * GLYPH_BYTES  # 8448
BASELINE = 12
PT = 12
THRESHOLD = 160
LEFT = 1
MONO_ADVANCE = 10
GENEVA_SPACE = 5


def raster(font: ImageFont.FreeTypeFont, ch: str) -> list[list[int]]:
    img = Image.new("L", (CELL, CELL), 255)
    ImageDraw.Draw(img).text((LEFT, BASELINE), ch, font=font, fill=0, anchor="ls")
    pix = img.load()
    return [[1 if pix[x, y] < THRESHOLD else 0 for x in range(CELL)] for y in range(CELL)]


def ink_bbox(bits: list[list[int]]) -> tuple[int, int, int, int] | None:
    xs, ys = [], []
    for y, row in enumerate(bits):
        for x, on in enumerate(row):
            if on:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return min(xs), min(ys), max(xs), max(ys)


def shift_x(bits: list[list[int]], dx: int) -> list[list[int]]:
    out = [[0] * CELL for _ in range(CELL)]
    for y, row in enumerate(bits):
        for x, on in enumerate(row):
            if on:
                nx = x + dx
                if 0 <= nx < CELL:
                    out[y][nx] = 1
    return out


def clip_width(bits: list[list[int]], width: int) -> list[list[int]]:
    out = [row[:] for row in bits]
    for y in range(CELL):
        for x in range(width, CELL):
            out[y][x] = 0
    return out


def bold_h(bits: list[list[int]], dx: int = 1) -> list[list[int]]:
    """OR with a copy shifted right -- 1px stems become 2px, counters stay open."""
    out = [row[:] for row in bits]
    for y, row in enumerate(bits):
        for x, on in enumerate(row):
            if on:
                nx = x + dx
                if 0 <= nx < CELL:
                    out[y][nx] = 1
    return out


def pack_glyph(bits: list[list[int]]) -> bytes:
    out = bytearray()
    for tx in range(2):
        for ty in range(2):
            for row in range(8):
                b = 0
                for col in range(8):
                    x = tx * 8 + col
                    y = ty * 8 + row
                    if bits[y][x]:
                        b |= 0x80 >> col
                out.append(b)
    return bytes(out)


def build_face(ttf: str, mono: bool) -> bytes:
    font = ImageFont.truetype(ttf, PT)
    widths = bytearray(256)
    glyphs = bytearray(256 * GLYPH_BYTES)
    for ch in range(256):
        if ch < 32:
            continue
        s = chr(ch)
        try:
            bits = raster(font, s)
        except Exception:
            continue
        box = ink_bbox(bits)
        if box is None:
            if ch == 32:
                widths[ch] = MONO_ADVANCE if mono else GENEVA_SPACE
            continue
        x0, _, x1, _ = box
        bits = shift_x(bits, LEFT - x0)
        bits = bold_h(bits)
        if mono:
            bits = clip_width(bits, MONO_ADVANCE)
            widths[ch] = MONO_ADVANCE
        else:
            box2 = ink_bbox(bits)
            if box2 is None:
                continue
            adv = min(CELL, box2[2] + 2)
            if adv < 3:
                adv = 3
            widths[ch] = adv
        glyphs[ch * GLYPH_BYTES : (ch + 1) * GLYPH_BYTES] = pack_glyph(bits)
    if mono:
        widths[32] = MONO_ADVANCE
    elif widths[32] == 0:
        widths[32] = GENEVA_SPACE
    return bytes(widths) + bytes(glyphs)


def write_header(path: Path, name: str, data: bytes) -> None:
    lines = [f"static unsigned char {name}[] = {{"]
    for i in range(0, len(data), 12):
        chunk = data[i : i + 12]
        joined = ", ".join(f"0x{b:02x}" for b in chunk)
        comma = "," if i + 12 < len(data) else ""
        lines.append(f"  {joined}{comma}")
    lines.append("};")
    lines.append(f"static unsigned int {name}_len = {len(data)};")
    lines.append("")
    path.write_text("\n".join(lines))


def preview(data: bytes, ch: str) -> str:
    off = 256 + ord(ch) * GLYPH_BYTES
    raw = data[off : off + GLYPH_BYTES]
    bits = [["." for _ in range(CELL)] for _ in range(CELL)]
    idx = 0
    for tx in range(2):
        for ty in range(2):
            for row in range(8):
                b = raw[idx]
                idx += 1
                for col in range(8):
                    if b & (0x80 >> col):
                        bits[ty * 8 + row][tx * 8 + col] = "#"
    rows = [f"{y:2d} " + "".join(bits[y]) for y in range(CELL)]
    return f"{ch} width={data[ord(ch)]}\n" + "\n".join(rows)


def main() -> None:
    assert CFF_LEN == 8448
    geneva = build_face(GENEVA_TTF, mono=False)
    monaco = build_face(MONACO_TTF, mono=True)
    assert len(geneva) == CFF_LEN
    assert len(monaco) == CFF_LEN
    (ROOT / "resources" / "geneva12.cff").write_bytes(geneva)
    (ROOT / "resources" / "monaco12.cff").write_bytes(monaco)
    write_header(ROOT / "src" / "geneva.h", "geneva12_cff", geneva)
    write_header(ROOT / "src" / "monaco.h", "monaco12_cff", monaco)
    print("wrote resources/geneva12.cff", len(geneva))
    print("wrote resources/monaco12.cff", len(monaco))
    print(preview(geneva, "A"))
    print(preview(geneva, "g"))
    print(preview(monaco, "0"))
    print(preview(monaco, "M"))
    g_adv = {chr(i): geneva[i] for i in range(32, 127)}
    print("geneva A/M/i/W/space", g_adv["A"], g_adv["M"], g_adv["i"], g_adv["W"], geneva[32])
    print("monaco advance", monaco[ord("0")], monaco[ord("M")], monaco[ord("i")], monaco[32])
    letters = [monaco[i] for i in range(48, 123) if monaco[i]]
    assert all(w == MONO_ADVANCE for w in letters), set(letters)


if __name__ == "__main__":
    main()
