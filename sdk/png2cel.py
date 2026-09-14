#!/usr/bin/env python3
"""
    png2wad.py - Creates a 3DO CEL image from a PNG for processing by 
    3DO SDK for BurgerDoom.
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
  python3 png2cel.py wall.png --type texture
  python3 png2cel.py sprite.png --type sprite
  python3 png2cel.py sprite.png --type sprite --sideways --origin 20,30

  Quantizes images as CELs are split into 
  sections and rearranged based on dimentions.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit(
        "png2cel.py requires Pillow: python3 -m pip install Pillow"
    ) from exc

# IMPORTANT: CCB/PLUT dimentions for CEL
CCB_SIZE = 60
SPRITE_PLUT_ENTRIES = 256
SPRITE_PLUT_SIZE = SPRITE_PLUT_ENTRIES * 2
TEXTURE_PLUT_ENTRIES = 16
TEXTURE_PLUT_SIZE = TEXTURE_PLUT_ENTRIES * 2
SYNTH_SPRITE_MAGIC = 0x53525054
MAX_SPRITE_WIDTH = 2048
MAX_SPRITE_HEIGHT = 1024
MAX_TEXTURE_WIDTH = 128
TEXTURE_HEIGHT = 128


def rgb555(red: int, green: int, blue: int) -> int:
    return ((red >> 3) << 10) | ((green >> 3) << 5) | (blue >> 3)


def pack_plut(
    palette: list[tuple[int, int, int]],
    transparent_index: int | None,
    count: int,
) -> bytes:
    data = bytearray()
    for index in range(count):
        red, green, blue = palette[index]
        value = rgb555(red, green, blue)
        if transparent_index is None or index != transparent_index:
            value |= 0x8000
        data += struct.pack(">H", value)
    return bytes(data)


def quantize_rgb(
    image: Image.Image, colours: int
) -> tuple[list[tuple[int, int, int]], bytes]:
    rgb = image.convert("RGB")
    indexed = rgb.quantize(colors=colours, method=Image.Quantize.MEDIANCUT)
    raw_palette = indexed.getpalette() or []
    palette = [
        tuple(raw_palette[index : index + 3])
        for index in range(0, min(len(raw_palette), colours * 3), 3)
    ]
    palette += [(0, 0, 0)] * (colours - len(palette))
    return palette[:colours], bytes(indexed.get_flattened_data())


def quantize_with_transparency(
    image: Image.Image,
    colours: int,
) -> tuple[list[tuple[int, int, int]], bytes, int]:
    rgba = image.convert("RGBA")
    pixels = list(rgba.get_flattened_data())
    transparent_index = 0
    source = Image.new("RGB", rgba.size, (0, 0, 0))
    source.putdata(
        [
            (red, green, blue) if alpha else (0, 0, 0)
            for red, green, blue, alpha in pixels
        ]
    )
    indexed = source.quantize(colors=colours - 1, method=Image.Quantize.MEDIANCUT)
    raw_palette = indexed.getpalette() or []
    colours_list = [
        tuple(raw_palette[index : index + 3])
        for index in range(0, min(len(raw_palette), (colours - 1) * 3), 3)
    ]
    colours_list += [(0, 0, 0)] * ((colours - 1) - len(colours_list))
    palette = [(0, 0, 0)] + colours_list
    mapping = list(indexed.get_flattened_data())
    output = bytearray(len(mapping))
    for index, value in enumerate(mapping):
        output[index] = 0 if pixels[index][3] == 0 else value + 1
    return palette[:colours], bytes(output), transparent_index


def make_sprite_palette(
    image: Image.Image,
    transparent_mode: str,
) -> tuple[list[tuple[int, int, int]], bytes, int | None]:
    rgba = image.convert("RGBA")
    pixels = list(rgba.get_flattened_data())
    has_alpha = any(alpha < 255 for _, _, _, alpha in pixels)

    if transparent_mode == "none":
        palette, indexed = quantize_rgb(rgba, 256)
        return palette, indexed, None

    if transparent_mode == "auto" and has_alpha:
        return quantize_with_transparency(rgba, 256)

    if transparent_mode == "auto":
        transparent_colour = pixels[0][:3]
        source_pixels = [pixel[:3] for pixel in pixels]
        source = Image.new("RGB", rgba.size, (0, 0, 0))
        source.putdata(
            [
                (0, 0, 0) if pixel == transparent_colour else pixel
                for pixel in source_pixels
            ]
        )
        indexed = source.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
        raw_palette = indexed.getpalette() or []
        colours = [
            tuple(raw_palette[index : index + 3])
            for index in range(0, min(len(raw_palette), 255 * 3), 3)
        ]
        colours += [(0, 0, 0)] * (255 - len(colours))
        palette = [transparent_colour] + colours
        mapping = list(indexed.get_flattened_data())
        output = bytearray(len(mapping))
        for index, value in enumerate(mapping):
            output[index] = (
                0 if source_pixels[index] == transparent_colour else value + 1
            )
        return palette[:256], bytes(output), 0

    transparent_index = int(transparent_mode)
    if not 0 <= transparent_index < 256:
        raise ValueError("sprite transparent index must be between 0 and 255")
    palette, indexed = quantize_rgb(rgba, 256)
    return palette, indexed, transparent_index


def make_texture_palette(
    image: Image.Image,
    transparent_mode: str,
) -> tuple[list[tuple[int, int, int]], bytes, None]:
    rgba = image.convert("RGBA")
    if transparent_mode != "none":
        raise ValueError(
            "wall textures do not support transparency in the DOOM3DO wall path; use --transparent none"
        )
    palette, pixels = quantize_rgb(rgba, TEXTURE_PLUT_ENTRIES)
    return palette, pixels, None


def pack_wall_texture(width: int, height: int, pixels: bytes) -> bytes:
    if height != TEXTURE_HEIGHT:
        raise ValueError(f"DOOM3DO wall textures must be {TEXTURE_HEIGHT} pixels high")
    if width < 1 or width > MAX_TEXTURE_WIDTH or width % 8:
        raise ValueError(
            f"DOOM3DO wall texture width must be a multiple of 8 pixels and no more than {MAX_TEXTURE_WIDTH} pixels"
        )

    if width % 8:
        raise ValueError("DOOM3DO wall texture width must be a multiple of 8 pixels")
    column_bytes = height // 2
    output = bytearray(TEXTURE_PLUT_SIZE + width * column_bytes)
    source_offset = TEXTURE_PLUT_SIZE
    for x in range(width):
        column_offset = source_offset + x * column_bytes
        for y in range(0, height, 2):
            high = pixels[y * width + x] & 0x0F
            low = pixels[(y + 1) * width + x] & 0x0F
            output[column_offset + (y >> 1)] = (high << 4) | low
    return bytes(output)


def build_sprite_ccb(
    width: int,
    height: int,
    palette: list[tuple[int, int, int]],
    pixels: bytes,
    transparent_index: int | None,
    sideways: bool,
) -> bytes:
    stored_width = height if sideways else width
    stored_height = width if sideways else height
    if not 1 <= stored_width <= MAX_SPRITE_WIDTH:
        raise ValueError(f"sprite CEL width must be 1..{MAX_SPRITE_WIDTH} pixels")
    if not 1 <= stored_height <= MAX_SPRITE_HEIGHT:
        raise ValueError(f"sprite CEL height must be 1..{MAX_SPRITE_HEIGHT} pixels")

    source = bytearray(pixels)
    source += b"\0" * ((4 - len(source) % 4) % 4)
    plut = pack_plut(palette, transparent_index, SPRITE_PLUT_ENTRIES)

    flags = 0x000007FF
    next_pointer = SYNTH_SPRITE_MAGIC if sideways else 0
    source_offset = CCB_SIZE + SPRITE_PLUT_SIZE
    plut_offset = CCB_SIZE
    pre0 = 0x00000005 | ((stored_height - 1) << 6)
    pre1 = 0x3E005000 | ((stored_width - 1) & 0x07FF)

    ccb = bytearray(CCB_SIZE)
    struct.pack_into(">I", ccb, 0, flags)
    struct.pack_into(">I", ccb, 4, next_pointer)
    struct.pack_into(">I", ccb, 8, source_offset)
    struct.pack_into(">I", ccb, 12, plut_offset)
    struct.pack_into(">ii", ccb, 16, 0, 0)
    struct.pack_into(">iiii", ccb, 24, 1 << 20, 0, 0, 1 << 16)
    struct.pack_into(">ii", ccb, 40, 0, 0)
    struct.pack_into(">I", ccb, 48, 0x1F00)
    struct.pack_into(">II", ccb, 52, pre0, pre1)
    return bytes(ccb) + plut + bytes(source)


def build_sprite_patch(
    width: int,
    height: int,
    palette: list[tuple[int, int, int]],
    pixels: bytes,
    transparent_index: int | None,
    sideways: bool,
    origin_x: int,
    origin_y: int,
) -> bytes:
    if not -32768 <= origin_x <= 32767 or not -32768 <= origin_y <= 32767:
        raise ValueError("sprite origin outside signed 16-bit patch range")
    ccb = build_sprite_ccb(width, height, palette, pixels, transparent_index, sideways)
    return struct.pack(">hh", origin_x, origin_y) + ccb


def verify_texture(data: bytes, width: int, height: int) -> None:
    expected_size = TEXTURE_PLUT_SIZE + (width // 8) * 4 * height
    if height != TEXTURE_HEIGHT:
        raise ValueError("texture verifier: invalid height")
    if len(data) != expected_size:
        raise ValueError("texture verifier: invalid payload size")
    for index in range(TEXTURE_PLUT_ENTRIES):
        struct.unpack_from(">H", data, index * 2)


def verify_sprite(data: bytes, width: int, height: int, sideways: bool) -> None:
    if len(data) < 4 + CCB_SIZE + SPRITE_PLUT_SIZE:
        raise ValueError("sprite verifier: payload too small")

    ccb_offset = 4
    flags = struct.unpack_from(">I", data, ccb_offset + 0)[0]
    next_pointer = struct.unpack_from(">I", data, ccb_offset + 4)[0]
    source_offset = struct.unpack_from(">I", data, ccb_offset + 8)[0]
    plut_offset = struct.unpack_from(">I", data, ccb_offset + 12)[0]
    pre0 = struct.unpack_from(">I", data, ccb_offset + 52)[0]
    pre1 = struct.unpack_from(">I", data, ccb_offset + 56)[0]

    if flags != 0x000007FF:
        raise ValueError("sprite verifier: unsupported synthetic CCB flags")
    if next_pointer != (SYNTH_SPRITE_MAGIC if sideways else 0):
        raise ValueError("sprite verifier: invalid orientation marker")
    if source_offset != CCB_SIZE + SPRITE_PLUT_SIZE:
        raise ValueError("sprite verifier: invalid source offset")
    if plut_offset != CCB_SIZE:
        raise ValueError("sprite verifier: invalid PLUT offset")

    stored_width = height if sideways else width
    stored_height = width if sideways else height
    decoded_width = (pre1 & 0x07FF) + 1
    decoded_height = ((pre0 >> 6) & 0x03FF) + 1
    if decoded_width != stored_width or decoded_height != stored_height:
        raise ValueError("sprite verifier: invalid CEL dimensions")

    source_size = len(data) - 4 - source_offset
    minimum_source = stored_width * stored_height
    if source_size < minimum_source:
        raise ValueError("sprite verifier: truncated source data")


def load_png(
    path: Path,
    asset_type: str,
    transparent: str,
    sideways: bool,
) -> tuple[int, int, list[tuple[int, int, int]], bytes, int | None]:
    with Image.open(path) as image:
        image.load()
        width, height = image.size
        if width < 1 or height < 1:
            raise ValueError("image has invalid dimensions")

        if asset_type == "texture":
            if (
                width < 1
                or width > MAX_TEXTURE_WIDTH
                or width % 8
                or height != TEXTURE_HEIGHT
            ):
                raise ValueError(
                    f"wall textures must be a multiple of 8 pixels wide, no more than {MAX_TEXTURE_WIDTH} pixels wide, and exactly {TEXTURE_HEIGHT} pixels high"
                )
            palette, pixels, transparent_index = make_texture_palette(
                image, transparent
            )
        else:
            if width > MAX_SPRITE_WIDTH or height > MAX_SPRITE_HEIGHT:
                raise ValueError(
                    f"sprite dimensions exceed the {MAX_SPRITE_WIDTH}x{MAX_SPRITE_HEIGHT} CEL limits"
                )
            palette, pixels, transparent_index = make_sprite_palette(image, transparent)

    if sideways:
        transposed = bytearray(width * height)
        for y in range(height):
            for x in range(width):
                transposed[x * height + y] = pixels[y * width + x]
        pixels = bytes(transposed)

    return width, height, palette, pixels, transparent_index


def parse_origin(value: str) -> tuple[int, int]:
    try:
        x_text, y_text = value.split(",", 1)
        return int(x_text), int(y_text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("origin must be X,Y") from exc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert PNG artwork into DOOM3DO wall-texture or world-sprite resources"
    )
    parser.add_argument("input", type=Path, help="input PNG")
    parser.add_argument(
        "--type",
        choices=("texture", "sprite"),
        default="sprite",
        help="resource type: wall texture or world sprite",
    )
    parser.add_argument("-o", "--output", type=Path, help="output resource")
    parser.add_argument(
        "--transparent",
        default=None,
        help="sprite transparent palette index: auto, none, or a numeric index; textures are opaque",
    )
    parser.add_argument(
        "--sideways",
        action="store_true",
        help="use the DOOM3DO sideways world-sprite representation",
    )
    parser.add_argument(
        "--origin",
        type=parse_origin,
        default=(0, 0),
        help="world-sprite patch origin as X,Y",
    )
    return parser.parse_args()


def default_output(path: Path) -> Path:
    return path.with_suffix(".cel")


def main() -> int:
    args = parse_args()
    input_path = args.input.expanduser().resolve()
    if not input_path.is_file():
        raise SystemExit(f"input file not found: {input_path}")
    if input_path.suffix.lower() != ".png":
        raise SystemExit("input must be a PNG file")
    if args.type == "texture" and args.sideways:
        raise SystemExit("--sideways is only valid for --type sprite")
    if args.type == "texture" and args.origin != (0, 0):
        raise SystemExit("--origin is only valid for --type sprite")

    output_path = (args.output or default_output(input_path)).expanduser().resolve()
    transparent_mode = (
        args.transparent.lower()
        if args.transparent is not None
        else ("none" if args.type == "texture" else "auto")
    )
    width, height, palette, pixels, transparent_index = load_png(
        input_path, args.type, transparent_mode, args.sideways
    )

    if args.type == "texture":
        plut = pack_plut(palette, None, TEXTURE_PLUT_ENTRIES)
        resource = plut + pack_wall_texture(width, height, pixels)[TEXTURE_PLUT_SIZE:]
        verify_texture(resource, width, height)
        layout = "DOOM3DO 4bpp wall texture payload"
    else:
        resource = build_sprite_patch(
            width,
            height,
            palette,
            pixels,
            transparent_index,
            args.sideways,
            args.origin[0],
            args.origin[1],
        )
        verify_sprite(resource, width, height, args.sideways)
        layout = "DOOM3DO world-sprite patch"

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(resource)

    print(f"[OK] {input_path}")
    print(f"     Resource: {output_path}")
    print(f"     Size: {width}x{height}")
    print(f"     Type: {layout}")
    if args.type == "sprite":
        print(f"     Orientation: {'sideways' if args.sideways else 'normal'}")
        print(f"     Origin: {args.origin[0]},{args.origin[1]}")
        print(f"     Transparent index: {transparent_index if transparent_index is not None else 'none'}")
    print(f"     Bytes: {len(resource)}")
    print("     Verify: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
