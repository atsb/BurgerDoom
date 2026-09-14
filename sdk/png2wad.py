#!/usr/bin/env python3
"""
    png2wad.py - Creates standard Doom WAD textures, screen graphics, 
    or sprite assets from a PNG for processing by 3DO SDK for BurgerDoom.
    Copyright (C) 2026  Gibbon.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

Usage:
python3 png2wad.py IMAGE.png --type texture --name BIGDOOR2 --size 128x128 -o BIGDOOR2.wad
python3 png2wad.py IMAGE.png --type sprite --name TROOA0 -o TROOA0.wad

Function names have been kept short for clean code styling and 
self-describing features.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit(
        "png2wad.py requires Pillow: python3 -m pip install Pillow"
    ) from exc

MAX_TEXTURE_WIDTH = 128
MAX_TEXTURE_HEIGHT = 128
MAX_SPRITE_WIDTH = 2048
MAX_SPRITE_HEIGHT = 2048


def quantize_image(
    image: Image.Image, colors: int, transparent: bool
) -> tuple[list[tuple[int, int, int]], bytes, int | None]:
    rgba = image.convert("RGBA")
    pixels = list(rgba.get_flattened_data())
    if transparent:
        source = Image.new("RGB", rgba.size, (0, 0, 0))
        source.putdata([(r, g, b) if a else (0, 0, 0) for r, g, b, a in pixels])
        indexed = source.quantize(colors=colors - 1, method=Image.Quantize.MEDIANCUT)
        raw_palette = indexed.getpalette() or []
        palette = [
            tuple(raw_palette[i : i + 3])
            for i in range(0, min(len(raw_palette), (colors - 1) * 3), 3)
        ]
        palette += [(0, 0, 0)] * ((colors - 1) - len(palette))
        palette = [(0, 0, 0)] + palette
        mapping = list(indexed.get_flattened_data())
        output = bytearray(len(mapping))
        for i, (_, _, _, alpha) in enumerate(pixels):
            output[i] = 0 if alpha == 0 else mapping[i] + 1
        return palette[:colors], bytes(output), 0
    indexed = rgba.convert("RGB").quantize(
        colors=colors, method=Image.Quantize.MEDIANCUT
    )
    raw_palette = indexed.getpalette() or []
    palette = [
        tuple(raw_palette[i : i + 3])
        for i in range(0, min(len(raw_palette), colors * 3), 3)
    ]
    palette += [(0, 0, 0)] * (colors - len(palette))
    return palette[:colors], bytes(indexed.get_flattened_data()), None


def make_playpal(palette: list[tuple[int, int, int]]) -> bytes:
    colors = palette[:256]
    colors += [(0, 0, 0)] * (256 - len(colors))
    return b"".join(struct.pack("BBB", *rgb) for rgb in colors)


def make_patch(
    width: int,
    height: int,
    pixels: bytes,
    origin_x: int,
    origin_y: int,
    transparent_index: int | None,
) -> bytes:
    if len(pixels) != width * height:
        raise ValueError("pixel buffer size does not match image dimensions")
    if not -32768 <= origin_x <= 32767 or not -32768 <= origin_y <= 32767:
        raise ValueError("origin outside signed 16-bit range")
    column_data: list[bytes] = []
    for x in range(width):
        column = bytearray()
        y = 0
        while y < height:
            while (
                y < height
                and transparent_index is not None
                and pixels[y * width + x] == transparent_index
            ):
                y += 1
            if y >= height:
                break
            start = y
            while y < height and (
                transparent_index is None or pixels[y * width + x] != transparent_index
            ):
                y += 1
            length = y - start
            while length:
                count = min(length, 255)
                column.append(start & 0xFF)
                column.append(count & 0xFF)
                column.append(0)
                base = start * width + x
                column.extend(pixels[base + j * width] for j in range(count))
                column.append(0)
                start += count
                length -= count
        column.append(255)
        column_data.append(bytes(column))
    directory_size = width * 4
    offset = 8 + directory_size
    offsets = []
    for column in column_data:
        offsets.append(offset)
        offset += len(column)
    out = bytearray(struct.pack("<hhhh", width, height, origin_x, origin_y))
    out.extend(struct.pack("<" + "I" * width, *offsets))
    for column in column_data:
        out.extend(column)
    return bytes(out)


def wad(lumps: list[tuple[str, bytes]], magic: bytes = b"PWAD") -> bytes:
    payload = bytearray(magic + b"\0" * 8)
    entries = []
    for name, data in lumps:
        entries.append((name, len(payload), len(data)))
        payload.extend(data)
    directory_offset = len(payload)
    for name, offset, size in entries:
        encoded = name.upper().encode("ascii", "strict")[:8].ljust(8, b"\0")
        payload.extend(struct.pack("<II8s", offset, size, encoded))
    struct.pack_into("<II", payload, 4, len(entries), directory_offset)
    return bytes(payload)


def parse_size(value: str) -> tuple[int, int]:
    try:
        width_text, height_text = value.lower().split("x", 1)
        width = int(width_text)
        height = int(height_text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("size must be WIDTHxHEIGHT") from exc
    return width, height

# 3DO UI ASSETS
FRONTEND_ALIASES = {
    "RTITLE": "TITLE",
    "RINTERMIS": "INTERPIC",
    "RCREDITS": "CREDITS1",
    "RIDCREDITS": "CREDITS",
    "RLOGCREDITS": "CREDITS2",
    "RMAINMENU": "MAINMENU",
    "RMAINDOOM": "MAINDOOM",
    "RMAINMENU": "MAINMENU",
}

# MUST BE
SCREEN_SIZE = (320, 200)


def wad_name(name: str) -> str:
    """Return the actual 8-character WAD lump name for a resource symbol."""
    upper = name.upper()
    return FRONTEND_ALIASES.get(upper, upper)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Creates a standard Doom WAD texture, screen graphic, or sprite asset(s) from a PNG"
    )
    ap.add_argument("image", type=Path)
    ap.add_argument(
        "--type",
        choices=("texture", "screen", "sprite"),
        required=True,
        help="texture = 3DO wall texture, screen = 320x200 frontend graphic, sprite = patch sprite",
    )
    ap.add_argument(
        "--name",
        required=True,
        help="Doom lump/resource name. Standard WAD names are max 8 characters; "
        "rTITLE/rINTERMIS/rCREDITS are accepted as frontend aliases.",
    )
    ap.add_argument(
        "--size",
        type=parse_size,
        default=None,
        help="texture/screen size as WIDTHxHEIGHT",
    )
    ap.add_argument("--origin", default="0,0", help="sprite origin as X,Y")
    ap.add_argument("-o", "--output", type=Path, required=True)
    args = ap.parse_args()

    brgr_resource_name = args.name.upper()
    try:
        brgr_resource_name.encode("ascii")
    except UnicodeEncodeError as exc:
        raise SystemExit("name must contain only ASCII characters") from exc

    name = wad_name(brgr_resource_name)
    if not 1 <= len(name) <= 8:
        raise SystemExit(
            f"WAD lump name '{name}' must be 1 to 8 ASCII characters "
            f"(resource symbol '{brgr_resource_name}' has no known WAD alias)"
        )

    image = Image.open(args.image).convert("RGBA")

    if args.type == "texture":
        if args.size is None:
            raise SystemExit("--size is required for wall textures")
        width, height = args.size

        # A 320x200 frontend screen is a patch, not a wall texture.
        # Accept the old --type texture spelling for the three known frontend
        # resources so existing build commands do not fail unexpectedly.
        if (width, height) == SCREEN_SIZE and brgr_resource_name in FRONTEND_ALIASES:
            args.type = "screen"
        else:
            if image.size != (width, height):
                raise SystemExit(
                    f"texture image is {image.width}x{image.height}, expected {width}x{height}"
                )
            if not 1 <= width <= MAX_TEXTURE_WIDTH or width % 8:
                raise SystemExit(
                    "3DO wall texture width must be a positive multiple of 8 and no more than 128"
                )
            if height != MAX_TEXTURE_HEIGHT:
                raise SystemExit("3DO wall texture height must be exactly 128 pixels")
            if image.getextrema()[3] != (255, 255):
                raise SystemExit("wall textures cannot contain transparency")

            palette, pixels, transparent = quantize_image(image, 256, False)
            patch = make_patch(width, height, pixels, 0, 0, transparent)
            pnames = name.encode("ascii").ljust(8, b"\0")
            texture = bytearray()
            texture.extend(name.encode("ascii").ljust(8, b"\0"))
            texture.extend(struct.pack("<iHHIH", 0, width, height, 0, 1))
            texture.extend(struct.pack("<hhhhh", 0, 0, 0, 0, 0))
            texture1 = struct.pack("<I", 1) + struct.pack("<I", 8) + texture
            output = wad(
                [
                    ("PLAYPAL", make_playpal(palette)),
                    ("PNAMES", struct.pack("<I", 1) + pnames),
                    ("TEXTURE1", texture1),
                    (name, patch),
                ]
            )

    if args.type == "screen":
        if args.size is None:
            raise SystemExit("--size is required for screen graphics")
        width, height = args.size
        if image.size != (width, height):
            raise SystemExit(
                f"screen image is {image.width}x{image.height}, expected {width}x{height}"
            )
        if (width, height) != SCREEN_SIZE:
            raise SystemExit(
                "3DO frontend screen graphics must be exactly 320x200"
            )
        if image.getextrema()[3] != (255, 255):
            raise SystemExit("screen graphics cannot contain transparency")

        # Doom full-screen graphics (TITLEPIC/INTERPIC/CREDITS) are patches,
        # not wall textures. make_patch() already supports widths/heights well
        # beyond the 128x128 wall-texture limit.
        palette, pixels, transparent = quantize_image(image, 256, False)
        patch = make_patch(width, height, pixels, 0, 0, transparent)
        output = wad(
            [
                ("PLAYPAL", make_playpal(palette)),
                (name, patch),
            ]
        )

    elif args.type == "sprite":
        if args.size is not None:
            raise SystemExit("--size is not used for sprites")
        if (
            image.width < 1
            or image.height < 1
            or image.width > MAX_SPRITE_WIDTH
            or image.height > 255
        ):
            raise SystemExit("sprite dimensions are outside the supported range")
        try:
            ox_text, oy_text = args.origin.split(",", 1)
            origin_x = int(ox_text)
            origin_y = int(oy_text)
        except ValueError as exc:
            raise SystemExit("origin must be X,Y") from exc
        palette, pixels, transparent = quantize_image(image, 256, True)
        patch = make_patch(
            image.width, image.height, pixels, origin_x, origin_y, transparent
        )
        output = wad(
            [
                ("PLAYPAL", make_playpal(palette)),
                (name, patch),
            ]
        )

    args.output.write_bytes(output)
    print(f"Wrote {args.output}")
    print(f"  type      : {args.type}")
    print(f"  resource  : {brgr_resource_name}")
    print(f"  WAD lump  : {name}")
    print(f"  dimensions: {image.width}x{image.height}")
    print(f"  size      : {len(output)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
