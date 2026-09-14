"""
    makerez.py - Creates/Updates 3DO REZFILES for processing by 3DO SDK for BurgerDoom.
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
    ./makerez.py [SOURCE_ROOT]
    ./makerez.py --update REZFILE --replace ID=FILE
    ./makerez.py --update REZFILE --replace-map MAP02=MAP02.wad
    ./makerez.py --update REZFILE --patch MOD_DIRECTORY -o REZFILE.mod
    ./makerez.py --update REZFILE --patch MOD_DIRECTORY --in-place
    ./makerez.py --update REZFILE --list
    ./makerez.py --update REZFILE --extract ID -o FILE
    ./makerez.py --update REZFILE --extract-map MAP02 -o MAP02.wad
    ./makerez.py --update REZFILE --replace-wad-texture BIGDOOR2=BIGDOOR2.wad -o REZFILE.mod
    ./makerez.py --update REZFILE --replace-wad-sprite rSPR_HANGINGRUMP=GOR1.wad -o REZFILE.mod

Patch directory:
    mod.json
    MAP02.wad
    plasma.cel

Example mod.json:
    {
        "name": "My DOOM 3DO Mod",
        "resources": {"217": "plasma.cel"},
        "maps": {"MAP02": "MAP02.wad"}
    }

Reverse engineered from BurgerLib2 and the REZFILE format.
"""

import argparse
import json
import os
import re
import struct
from dataclasses import dataclass
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit("makerez.py WAD asset conversion requires Pillow: python3 -m pip install Pillow") from exc

MAP_LUMPS = ['THINGS', 'LINEDEFS', 'SIDEDEFS', 'VERTEXES', 'SEGS', 'SSECTORS', 'SECTORS', 'NODES', 'REJECT', 'BLOCKMAP']
SPECIAL_ALIASES = {'rTITLE': ['title'], 'rIDCREDITS': ['credits'], 'rCREDITS': ['credits1'], 'rLOGCREDITS': ['credits2'], 'rBACKGRND': ['backgrnd'], 'rCHARSET': ['charset'], 'rPAUSED': ['paused'], 'rLOADING': ['loading'], 'rBIGNUMB': ['bignumb'], 'rINTERMIS': ['interpic'], 'rSTBAR': ['stbar'], 'rSBARSHP': ['sbarshp'], 'rFACES': ['faces'], 'rSKULLS': ['skulls'], 'rMAINDOOM': ['doommenu'], 'rMAINMENU': ['mainmenu'], 'rSLIDER': ['slider']}
FRONTEND_WAD_ALIASES = {
    'rTITLE': ('TITLE', 'TITLEPIC'),
    'rINTERMIS': ('INTERPIC', 'INTERMIS'),
    'rCREDITS': ('CREDITS1', 'CREDITS'),
    'rIDCREDITS': ('CREDITS', 'CREDITS1'),
    'rLOGCREDITS': ('CREDITS2', 'LOGCREDITS'),
}
KNOWN_ASSET_ALIASES = {'rCOMTAL02': ['comptal2']}
BACKGROUND_ALIASES = {0: ['back0', 'background0', 'gameback0', 'gamebackground0'], 1: ['back1', 'background1', 'gameback1', 'gamebackground1'], 2: ['back2', 'background2', 'gameback2', 'gamebackground2'], 3: ['back3', 'background3', 'gameback3', 'gamebackground3'], 4: ['back4', 'background4', 'gameback4', 'gamebackground4'], 5: ['back5', 'background5', 'gameback5', 'gamebackground5']}

@dataclass
class Resource:
    number: int
    type: int
    name: str
    path: Path
    data: bytes
    flags: int = 0

def norm(s: str) -> str:
    return re.sub('[^a-z0-9]+', '', s.lower())

class LBMImage:

    def __init__(self, width, height, pixels, palette, x_origin=0, y_origin=0):
        self.width = width
        self.height = height
        self.pixels = pixels
        self.palette = palette
        self.x_origin = x_origin
        self.y_origin = y_origin

def _be16(b, o):
    return struct.unpack_from('>H', b, o)[0]

def _be32(b, o):
    return struct.unpack_from('>I', b, o)[0]

def _byterun1(data, expected):
    out = bytearray()
    i = 0
    while i < len(data) and len(out) < expected:
        n = struct.unpack('b', data[i:i + 1])[0]
        i += 1
        if n >= 0:
            count = n + 1
            if i + count > len(data):
                raise ValueError('ByteRun1 literal overrun')
            out.extend(data[i:i + count])
            i += count
        elif n != -128:
            count = 1 - n
            if i >= len(data):
                raise ValueError('ByteRun1 repeat overrun')
            out.extend(data[i:i + 1] * count)
            i += 1
    if len(out) != expected:
        raise ValueError(f'ByteRun1 decoded {len(out)} of {expected}')
    return bytes(out)

def read_ilbm(path: Path):
    b = path.read_bytes()
    if len(b) < 12 or b[:4] != b'FORM':
        raise ValueError(f'{path}: not FORM')
    if b[8:12] != b'ILBM':
        raise ValueError(f'{path}: not ILBM')
    pos = 12
    bmhd = cmap = body = None
    while pos + 8 <= len(b):
        cid = b[pos:pos + 4]
        n = _be32(b, pos + 4)
        a = pos + 8
        z = a + n
        if z > len(b):
            raise ValueError(f"{path}: truncated {cid.decode('latin1', 'replace')}")
        if cid == b'BMHD':
            bmhd = b[a:z]
        elif cid == b'CMAP':
            cmap = b[a:z]
        elif cid == b'BODY':
            body = b[a:z]
        pos = z + (n & 1)
    if bmhd is None or body is None or len(bmhd) < 20:
        raise ValueError(f'{path}: missing BMHD/BODY')
    w, h = (_be16(bmhd, 0), _be16(bmhd, 2))
    xo, yo = (_be16(bmhd, 4), _be16(bmhd, 6))
    if xo & 32768:
        xo -= 65536
    if yo & 32768:
        yo -= 65536
    planes = bmhd[8]
    compression = bmhd[10]
    if not 1 <= planes <= 8:
        raise ValueError(f'{path}: unsupported bitplanes {planes}')
    if compression not in (0, 1):
        raise ValueError(f'{path}: compression {compression}')
    row = (w + 15) // 16 * 2
    expected = row * h * planes
    raw = _byterun1(body, expected) if compression == 1 else body[:expected]
    if len(raw) != expected:
        raise ValueError(f'{path}: BODY size mismatch')
    pixels = bytearray(w * h)
    off = 0
    for y in range(h):
        pr = [raw[off + j * row:off + (j + 1) * row] for j in range(planes)]
        off += planes * row
        for x in range(w):
            bi = x >> 3
            bit = 7 - (x & 7)
            v = 0
            for pn in range(planes):
                if pr[pn][bi] & 1 << bit:
                    v |= 1 << pn
            pixels[y * w + x] = v
    if cmap:
        n = min(len(cmap) // 3, 256)
        pal = [tuple(cmap[i * 3:i * 3 + 3]) for i in range(n)]
    else:
        pal = []
    pal += [(0, 0, 0)] * (256 - len(pal))
    return LBMImage(w, h, bytes(pixels), pal, xo, yo)

def doom_sprite_filename(stem):
    n = stem.lower()
    if len(n) < 6:
        return None
    if len(n) >= 6 and n[:4].isalnum() and n[4].isalpha() and (n[5] in '012345678'):
        family = n[:4]
        frame = ord(n[4]) - ord('a')
        rotations = [int(n[5])]
        rest = n[6:]
        if len(rest) >= 2 and rest[0].isalpha() and (rest[1] in '012345678'):
            second_frame = ord(rest[0]) - ord('a')
            if second_frame != frame:
                return None
            rotations.append(int(rest[1]))
            rest = rest[2:]
        variant = rest
        return (family, frame, rotations, variant)
    return None

def expand_sprite_file(info):
    if info is None:
        return []
    family, frame, rotations, variant = info
    return [(family, frame, rotation, variant) for rotation in rotations]

def build_3do_plut(palette):
    out = bytearray()
    for i, (r, g, b) in enumerate(palette[:256]):
        v = r >> 3 << 10 | g >> 3 << 5 | b >> 3
        if i != 0:
            v |= 32768
        out += struct.pack('>H', v)
    return bytes(out)

def build_sprite_cel(image):
    w, h = (image.width, image.height)
    plut = build_3do_plut(image.palette)
    source = bytes(image.pixels)
    source += b'\x00' * ((4 - len(source) % 4) % 4)
    flags = 1 | 2 | 4 | 8 | 16 | 32 | 64 | 128 | 256 | 512 | 1024
    pre0 = 5
    pre1 = 1040207872 | w - 1 & 2047
    ccb = bytearray(60)
    struct.pack_into('>I', ccb, 0, flags)
    struct.pack_into('>I', ccb, 4, 0)
    struct.pack_into('>I', ccb, 8, 60 + 512)
    struct.pack_into('>I', ccb, 12, 60)
    struct.pack_into('>ii', ccb, 16, 0, 0)
    struct.pack_into('>iiii', ccb, 24, 1 << 20, 0, 0, 1 << 16)
    struct.pack_into('>ii', ccb, 40, 0, 0)
    struct.pack_into('>I', ccb, 48, 7936)
    struct.pack_into('>II', ccb, 52, pre0, pre1)
    return bytes(ccb) + plut + source

def build_patch(image):
    left = image.x_origin
    top = image.y_origin
    if left < -32768 or left > 32767 or top < -32768 or (top > 32767):
        raise ValueError('sprite origin outside patch_t range')
    return struct.pack('>hh', left, top) + build_sprite_cel(image)
WEAPON_SHAPE_SOURCES = {'rSPR_BIGFISTS': ('punch0', 'punch1', 'punch2', 'punch3'), 'rSPR_BIGPISTOL': ('pistol1', 'pistol2', 'pistol3', 'pflash'), 'rSPR_BIGSHOTGUN': ('s1', 's2', 's3', 's4', 'sflash', 'sflash2'), 'rSPR_BIGCHAINGUN': ('cattack1', 'cattack2', 'cflash', 'cflash2'), 'rSPR_BIGROCKET': ('m1', 'm2', 'mflash', 'mflash2', 'mflash3', 'mflash5'), 'rSPR_BIGPLASMA': ('plas1', 'plas2', 'plflash', 'plflash2'), 'rSPR_BIGBFG': ('bfg1', 'bfg2', 'bflash', 'bflash2'), 'rSPR_BIGCHAINSAW': ('chain1', 'chain2', 'chain1', 'chain2')}
WEAPON_SHAPE_LABELS = {symbol: ', '.join(sources) for symbol, sources in WEAPON_SHAPE_SOURCES.items()}

def load_exact_lbm(root, stem, *, resource_label):
    raw = root / 'sprites3do' / 'rawsprites'
    target = norm(stem)
    if not raw.exists():
        return None
    matches = [pth for pth in raw.rglob('*.lbm') if norm(pth.stem) == target]
    if not matches:
        return None
    if len(matches) > 1:
        raise SystemExit(f'ambiguous exact LBM match for {resource_label} frame {stem}: ' + ', '.join((str(x) for x in sorted(matches))))
    return matches[0]

def build_weapon_shape_resource(images):
    count = len(images)
    data = bytearray(4 * count)
    offsets = []
    for image in images:
        if image.x_origin != 0 or image.y_origin != 0:
            raise ValueError(f'weapon LBM has non-zero BMHD origin ({image.x_origin},{image.y_origin})')
        off = len(data)
        data.extend(struct.pack('>hh', 0, 0))
        data.extend(build_sprite_cel(image))
        offsets.append(off)
    for index, off in enumerate(offsets):
        struct.pack_into('>I', data, index * 4, off)
    return bytes(data)

def load_weapon_shape_resource(root, symbol):
    stems = WEAPON_SHAPE_SOURCES.get(symbol)
    if not stems:
        return (None, 'no verified weapon source mapping')
    images = []
    for stem in stems:
        path = load_exact_lbm(root, stem, resource_label=symbol)
        if path is None:
            return (None, f'weapon source LBM not found: {stem}.lbm')
        images.append(read_ilbm(path))
    return (build_weapon_shape_resource(images), 'ok')
SPRITE_PREFIXES = {'rSPR_ZOMBIE': 'poss', 'rSPR_ZOMBIEBODY': 'poss', 'rSPR_SHOTGUY': 'spos', 'rSPR_IMP': 'troo', 'rSPR_DEMON': 'sarg', 'rSPR_CACODEMON': 'head', 'rSPR_CACODIE': 'head', 'rSPR_CACOBDY': 'head', 'rSPR_CACOBOLT': 'bal7', 'rSPR_LOSTSOUL': 'skul', 'rSPR_BARON': 'boss', 'rSPR_BARONATK': 'boss', 'rSPR_BARONDIE': 'boss', 'rSPR_BARONBDY': 'boss', 'rSPR_BARONBLT': 'bfe1', 'rSPR_OURHERO': 'play', 'rSPR_OURHEROBDY': 'play', 'rSPR_BARREL': 'bar1', 'rSPR_IFOG': 'ifog', 'rSPR_FIRECAN': 'fcan', 'rSPR_POOLBLOOD': 'blud', 'rSPR_CANDLE': 'cand', 'rSPR_CANDLEABRA': 'cbra', 'rSPR_FLAMINGSKULLS': 'fsku', 'rSPR_MEDDEADTREE': 'tre1', 'rSPR_LARGESTAL': 'smit', 'rSPR_SMALLSTAL': 'smt2', 'rSPR_BODYPOLE': 'gor5', 'rSPR_HANGINGRUMP': 'gor1', 'rSPR_FIVESKULLPOLE': 'pol2', 'rSPR_TALLSKULLPOLE': 'pol5', 'rSPR_SHORTGREENPILLAR': 'pol4', 'rSPR_SHORTREDPILLAR': 'pol7', 'rSPR_REDTORCH': 'smrt', 'rSPR_BLUETORCH': 'smbt', 'rSPR_GREENTORCH': 'smgt', 'rSPR_TELEFOG': 'tfog', 'rSPR_SHOTGUN': 'shot', 'rSPR_CHAINGUN': 'mgun', 'rSPR_ROCKETLAUNCHER': 'rock', 'rSPR_CHAINSAW': 'csaw', 'rSPR_CLIP': 'clip', 'rSPR_SHELLS': 'shell', 'rSPR_ROCKET': 'rock', 'rSPR_STIMPACK': 'stim', 'rSPR_MEDIKIT': 'medi', 'rSPR_GREENARMOR': 'arm1', 'rSPR_BLUEARMOR': 'arm2', 'rSPR_LIGHTCOLUMN': 'colu', 'rSPR_BACKPACK': 'bpak', 'rSPR_BOXROCKETS': 'brok', 'rSPR_BOXAMMO': 'ammo', 'rSPR_BOXSHELLS': 'sbox', 'rSPR_TECHPILLAR': 'pol1', 'rSPR_BLUEKEYCARD': 'bkey', 'rSPR_YELLOWKEYCARD': 'ykey', 'rSPR_REDKEYCARD': 'rkey', 'rSPR_RADIATIONSUIT': 'suit', 'rSPR_IRGOGGLES': 'pvis', 'rSPR_COMPUTERMAP': 'pmap', 'rSPR_INVISIBILITY': 'pinv', 'rSPR_HEALTHBONUS': 'bon1', 'rSPR_SOULSPHERE': 'soul', 'rSPR_ARMORBONUS': 'bon2', 'rSPR_BLUESKULLKEY': 'bsku', 'rSPR_REDSKULLKEY': 'rsku', 'rSPR_YELLOWSKULLKEY': 'ysku', 'rSPR_PLASMARIFLE': 'plas', 'rSPR_BFG9000': 'bfug', 'rSPR_CELL': 'cell', 'rSPR_BERZERKER': 'pstr', 'rSPR_CELLPACK': 'celp', 'rSPR_INVULNERABILITY': 'pinv', 'rSPR_BFS1': 'bfs1', 'rSPR_BFE1': 'bfe1', 'rSPR_BFE2': 'bfe2', 'rSPR_PLSS': 'plss', 'rSPR_PLSE': 'plse', 'rSPR_MISL': 'misl', 'rSPR_BLUD': 'blud', 'rSPR_PUFF': 'puff'}

def parse_doom_texture_catalog(wad_lumps):
    blob = wad_lumps.get("TEXTURE1") or wad_lumps.get("TEXTURE2")
    pnames_blob = wad_lumps.get("PNAMES")

    if blob is None or pnames_blob is None or len(blob) < 4 or len(pnames_blob) < 4:
        raise ValueError("asset WAD must contain PNAMES and TEXTURE1")
    
    count = struct.unpack_from("<I", blob, 0)[0]

    if count > 4096 or 4 + count * 4 > len(blob):
        raise ValueError("invalid TEXTURE1 texture count or offset table")
    
    patch_count = struct.unpack_from("<I", pnames_blob, 0)[0]

    if patch_count > 65535 or 4 + patch_count * 8 > len(pnames_blob):
        raise ValueError("invalid PNAMES patch count")
    
    patch_names = [pnames_blob[4 + i * 8:12 + i * 8].rstrip(b"\0").decode("ascii", "replace").upper() for i in range(patch_count)]
    textures = {}
    for i in range(count):
        off = struct.unpack_from("<I", blob, 4 + i * 4)[0]
        if off + 22 > len(blob):
            raise ValueError("TEXTURE1 entry lies outside WAD lump")
        name = blob[off:off + 8].rstrip(b"\0").decode("ascii", "replace").upper()
        masked, width, height, _column_directory, patchcount = struct.unpack_from("<iHHIH", blob, off + 8)
        if patchcount < 0 or off + 22 + patchcount * 10 > len(blob):
            raise ValueError(f"{name}: invalid texture patch count")
        
        patches = []
        for j in range(patchcount):
            x, y, patch_index, stepdir, colormap = struct.unpack_from("<hhhhh", blob, off + 22 + j * 10)
            if patch_index < 0 or patch_index >= patch_count:
                raise ValueError(f"{name}: PNAMES index {patch_index} is invalid")
            patches.append((x, y, patch_names[patch_index]))
        textures[name] = (width, height, patches)
    return textures


def compose_doom_texture(wad_lumps, texture_name):
    textures = parse_doom_texture_catalog(wad_lumps)
    name = texture_name.upper()

    if name not in textures:
        raise ValueError(f"texture {name} is not defined in TEXTURE1")
    
    width, height, patches = textures[name]
    if width <= 0 or height <= 0 or width > 2048 or height > 2048:
        raise ValueError(f"{name}: invalid dimensions {width}x{height}")
    
    playpal = wad_lumps.get("PLAYPAL")
    if playpal is None or len(playpal) < 768:
        raise ValueError("asset WAD must contain PLAYPAL for 3DO texture conversion")
    
    palette = [tuple(playpal[i:i + 3]) for i in range(0, 768, 3)]
    image = Image.new("RGB", (width, height), palette[0])
    for x, y, patch_name in patches:
        patch = parse_doom_patch(wad_lumps, patch_name, palette + [(0, 0, 0)] * 256)
        if patch is None:
            raise ValueError(f"{name}: patch {patch_name} is missing or invalid")
        
        pixels = patch.pixels
        for py in range(patch.height):
            dy = y + py
            if dy < 0 or dy >= height:
                continue
            for px in range(patch.width):
                dx = x + px
                if 0 <= dx < width:
                    value = pixels[py * patch.width + px]
                    if value != 0 or patch.width == 1 and patch.height == 1:
                        image.putpixel((dx, dy), palette[value])
    return width, height, image


def quantize_3do_texture(image):
    if image.mode != "RGB":
        image = image.convert("RGB")
    indexed = image.quantize(colors=16, method=Image.Quantize.MEDIANCUT)
    raw = indexed.getpalette() or []
    palette = [tuple(raw[i:i + 3]) for i in range(0, 48, 3)]
    palette += [(0, 0, 0)] * (16 - len(palette))
    return palette[:16], bytes(indexed.get_flattened_data())


def build_3do_wall_texture(image):
    width, height = image.size
    if height != 128:
        raise ValueError(f"3DO wall texture height must be 128 pixels, got {height}")

    if width < 1 or width > 128 or width % 8:
        raise ValueError(f"3DO wall texture width must be a multiple of 8 and no more than 128, got {width}")

    palette, pixels = quantize_3do_texture(image)
    column_bytes = height // 2
    data = bytearray(32 + width * column_bytes)
    for i, (r, g, b) in enumerate(palette):
        value = (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3) | 0x8000
        struct.pack_into(">H", data, i * 2, value)
    for x in range(width):
        column_offset = 32 + x * column_bytes
        for y in range(0, height, 2):
            high = pixels[y * width + x] & 0x0F
            low = pixels[(y + 1) * width + x] & 0x0F
            data[column_offset + (y >> 1)] = (high << 4) | low
    return bytes(data)


def parse_resource_number(value, values):
    text = value.strip()
    lower = text.lower()
    lookup = text if lower.startswith("r") else "r" + text
    if lower.startswith("r"):
        lookup = "r" + text[1:]
    try:
        return int(text, 0)
    except ValueError:
        if lookup in values:
            return values[lookup]
        raise SystemExit(f"unknown resource name: {value}")


def parse_wad_asset(value):
    try:
        left, path = value.split("=", 1)
    except ValueError as exc:
        raise SystemExit(f"invalid WAD asset replacement {value!r}; use NAME=WAD") from exc
    wad_path = Path(path)
    if not wad_path.is_file():
        raise SystemExit(f"asset WAD not found: {wad_path}")
    return left.upper(), wad_path


def replace_wad_texture_resource(resources, values, replacement):
    name, wad_path = parse_wad_asset(replacement)
    resource_number = parse_resource_number(name, values) if name.startswith("R") else None
    if resource_number is not None and not (values["rT_START"] <= resource_number < values["rT_END"]):
        raise SystemExit(f"resource {name} is not a wall texture")
    
    if resource_number is None:
        target = name.lstrip("R")
        texture_map = {n: s[1:].upper() for s, n in values.items() if values["rT_START"] <= n < values["rT_END"]}
        matches = [n for n, symbol in texture_map.items() if symbol == target]
        if len(matches) != 1:
            raise SystemExit(f"unknown or ambiguous wall texture: {name}")
        
        resource_number = matches[0]
    lumps = dict(parse_wad(wad_path))
    texture_name = name.lstrip("R")
    texture_map = parse_doom_texture_catalog(lumps)

    if texture_name not in texture_map:
        if len(texture_map) == 1:
            texture_name = next(iter(texture_map))
        else:
            raise SystemExit(f"{wad_path}: TEXTURE1 does not define {texture_name}")
        
    width, height, _ = texture_map[texture_name]
    texture_resource = resources.get(values["rTEXTURE1"])
    if texture_resource is None:
        raise SystemExit("REZFILE has no rTEXTURE1 resource")
    
    texture_blob = texture_resource.data
    if len(texture_blob) >= 16:
        count = struct.unpack_from(">I", texture_blob, 0)[0]
        first = struct.unpack_from(">I", texture_blob, 4)[0]

        if 16 + count * 12 <= len(texture_blob):
            index = resource_number - first
            if 0 <= index < count:
                target_width, target_height = struct.unpack_from(">II", texture_blob, 16 + index * 12)
            else:
                raise SystemExit(f"resource {resource_number} is outside rTEXTURE1 wall texture range")
        else:
            count = struct.unpack_from(">H", texture_blob, 0)[0]
            first = struct.unpack_from(">H", texture_blob, 2)[0]
            index = resource_number - first
            target_width, target_height = struct.unpack_from(">HH", texture_blob, 16 + index * 8)
    else:
        raise SystemExit("rTEXTURE1 resource is too small")
    if (width, height) != (target_width, target_height):
        raise SystemExit(f"{texture_name}: WAD dimensions {width}x{height} do not match REZFILE texture dimensions {target_width}x{target_height}")
    native = build_3do_wall_texture(compose_doom_texture(lumps, texture_name)[2])
    if resource_number not in resources:
        raise SystemExit(f"resource ID {resource_number} is not present in the base REZFILE")
    old = resources[resource_number]
    resources[resource_number] = Resource(resource_number, old.type, old.name, wad_path, native, old.flags)


def assemble_wad_sprite_resource(wad_lumps, family):
    playpal = wad_lumps.get("PLAYPAL")
    if playpal is None or len(playpal) < 768:
        raise ValueError("asset WAD must contain PLAYPAL for sprite conversion")
    
    palette = [tuple(playpal[i:i + 3]) for i in range(0, 768, 3)] + [(0, 0, 0)] * 256
    frames = {}
    for lump_name, blob in wad_lumps.items():
        info = doom_sprite_filename(lump_name)
        if info is None or info[0].upper() != family.upper():
            continue
        image = parse_doom_patch(wad_lumps, lump_name, palette)
        if image is None:
            continue
        fam, frame, rotations, variant = info
        frames.setdefault(frame, {})
        for rotation in rotations:
            old = frames[frame].get(rotation)
            if old is None or (variant, lump_name) < (old[0], old[1]):
                frames[frame][rotation] = (variant, image)
    if not frames:
        raise ValueError(f"no sprite patches found for {family}")
    
    maxframe = max(frames)
    data = bytearray(4 * (maxframe + 1))
    offsets = [0] * (maxframe + 1)
    for frame in range(maxframe + 1):
        rs = frames.get(frame, {})
        if not rs:
            raise ValueError(f"sprite family {family}: frame {frame} is missing")
        if 0 in rs and len(rs) == 1:
            image = rs[0][1]
            patch = build_patch(image)
            off = len(data)
            data.extend(patch)
            offsets[frame] = off
        else:
            missing = [r for r in range(1, 9) if r not in rs]
            if missing:
                raise ValueError(f"sprite family {family}: frame {frame} missing rotations {missing}")
            roff = len(data)
            data.extend(b"\0" * 32)
            for rotation in range(1, 9):
                poff = len(data)
                data.extend(build_patch(rs[rotation][1]))
                struct.pack_into(">I", data, roff + (rotation - 1) * 4, poff)
            offsets[frame] = 0x40000000 | roff
    for frame, off in enumerate(offsets):
        struct.pack_into(">I", data, frame * 4, off)
    return bytes(data)



def replace_wad_frontend_resource(resources, values, replacement):
    """Replace a frontend/UI resource from a Doom patch WAD.

    Frontend resources  such as rTITLE/rINTERMIS/rCREDITS are not wall
    textures.  Their WAD is a Doom patch packaged
    with PLAYPAL/PNAMES/TEXTURE1, which is composed to an image and then
    converted to native 3DO CEL used by the REZFILE.
    """
    name, wad_path = parse_wad_asset(replacement)
    resource_number = parse_resource_number(name, values)
    frontend_numbers = {
        number for symbol, number in values.items()
        if symbol.casefold() in {key.casefold() for key in SPECIAL_ALIASES}
    }
    if resource_number not in frontend_numbers:
        raise SystemExit(f"resource {name} is not a frontend/UI resource")

    lumps = dict(parse_wad(wad_path))
    palette_blob = lumps.get("PLAYPAL")
    if palette_blob is None or len(palette_blob) < 768:
        raise SystemExit(f"{wad_path}: frontend WAD must contain PLAYPAL")

    palette = [
        tuple(palette_blob[i:i + 3])
        for i in range(0, 768, 3)
    ] + [(0, 0, 0)] * 256

    # png2wad --type screen intentionally emits a simple Doom patch WAD:
    # PLAYPAL + TITLE/INTERPIC/CREDITS* patch. It does NOT need PNAMES or
    # TEXTURE1 because fullscreen graphics are patches, not textures.
    texture_blob = lumps.get("TEXTURE1") or lumps.get("TEXTURE2")
    pnames_blob = lumps.get("PNAMES")
    if texture_blob is not None and pnames_blob is not None:
        texture_map = parse_doom_texture_catalog(lumps)
        texture_name = name.lstrip("R").upper()
        if texture_name not in texture_map:
            if len(texture_map) == 1:
                texture_name = next(iter(texture_map))
            else:
                raise SystemExit(
                    f"{wad_path}: TEXTURE1 does not define {texture_name}"
                )
        width, height, image = compose_doom_texture(lumps, texture_name)
    else:
        candidates = FRONTEND_WAD_ALIASES.get(
            next(
                (symbol for symbol, number in values.items()
                 if number == resource_number and symbol.casefold() in
                 {s.casefold() for s in SPECIAL_ALIASES}),
                name
            ),
            (name.lstrip("R").upper(),)
        )
        patch_name = next((candidate for candidate in candidates if candidate in lumps), None)
        if patch_name is None:
            raise SystemExit(
                f"{wad_path}: no frontend patch found; expected one of "
                + ", ".join(candidates)
            )
        patch = parse_doom_patch(lumps, patch_name, palette)
        if patch is None:
            raise SystemExit(f"{wad_path}: frontend patch {patch_name} is invalid")
        width, height = patch.width, patch.height
        image = Image.new("RGB", (width, height), palette[0])
        for y in range(height):
            for x in range(width):
                image.putpixel((x, y), palette[patch.pixels[y * width + x]])

    # Frontend resources are fullscreen 3DO CELs.
    if (width, height) != (320, 200):
        raise SystemExit(
            f"{name}: frontend WAD dimensions {width}x{height} "
            f"must be exactly 320x200"
        )

    # Quantize once so the pixel indices and palette describe the same image.
    indexed = image.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    raw_palette = indexed.getpalette() or []
    palette = [
        tuple(raw_palette[i:i + 3])
        for i in range(0, min(len(raw_palette), 768), 3)
    ]
    palette += [(0, 0, 0)] * (256 - len(palette))
    native = build_sprite_cel(
        LBMImage(width, height, bytes(indexed.getdata()), palette[:256])
    )

    old = resources.get(resource_number)
    if old is None:
        raise SystemExit(
            f"resource ID {resource_number} is not present in the base REZFILE"
        )
    resources[resource_number] = Resource(
        resource_number, old.type, old.name, wad_path, native, old.flags
    )


def replace_wad_sprite_resource(resources, values, replacement):
    name, wad_path = parse_wad_asset(replacement)
    resource_number = parse_resource_number(name, values)
    if not (values["rFIRSTSPRITE"] <= resource_number < values["rLASTSPRITE"]):
        raise SystemExit(f"resource {name} is not a sprite resource")
    
    symbol = next((symbol for symbol, number in values.items() if number == resource_number and symbol.startswith("rSPR_")), None)
    if symbol is None:
        raise SystemExit(f"resource {name} is not a named sprite resource")
    
    family = SPRITE_PREFIXES.get(symbol)
    if not family:
        raise SystemExit(f"no WAD sprite family mapping is known for {symbol}")
    
    lumps = dict(parse_wad(wad_path))
    native = assemble_wad_sprite_resource(lumps, family)
    old = resources[resource_number]
    resources[resource_number] = Resource(resource_number, old.type, old.name, wad_path, native, old.flags)


def parse_doom_patch(wad_lumps, name, palette):
    blob = wad_lumps.get(name.upper())
    if blob is None or len(blob) < 8:
        return None
    
    width, height, left, top = struct.unpack_from('<hhhh', blob, 0)
    if width <= 0 or height <= 0 or width > 2048 or (height > 2048):
        return None
    
    if 8 + width * 4 > len(blob):
        return None
    
    columnofs = struct.unpack_from('<' + 'I' * width, blob, 8)
    pixels = bytearray([0] * (width * height))
    for x, off in enumerate(columnofs):
        if off >= len(blob):
            raise ValueError(f'{name}: column offset outside lump')
        pos = off
        while pos < len(blob):
            y = blob[pos]
            pos += 1
            if y == 255:
                break
            if pos + 3 > len(blob):
                raise ValueError(f'{name}: truncated post')
            topdelta = y
            length = blob[pos]
            pos += 1
            pos += 1
            if pos + length > len(blob):
                raise ValueError(f'{name}: truncated post data')
            for j in range(length):
                yy = topdelta + j
                if 0 <= yy < height:
                    pixels[yy * width + x] = blob[pos + j]
            pos += length
            if pos < len(blob):
                pos += 1
    return LBMImage(width, height, bytes(pixels), palette, left, top)

def load_doom_wad_palette_and_lumps(root):
    wad = root / 'wads' / 'doom.wad'
    if not wad.exists():
        return (None, {})
    lumps = parse_wad(wad)
    ld = dict(lumps)
    pp = ld.get('PLAYPAL')
    if pp is None or len(pp) < 768:
        return (None, ld)
    palette = [tuple(pp[i:i + 3]) for i in range(0, 768, 3)]
    palette += [(0, 0, 0)] * (256 - len(palette))
    return (palette, ld)

def load_sprite_files(root, symbol):
    raw = root / 'sprites3do' / 'rawsprites'
    prefix = SPRITE_PREFIXES.get(symbol)
    if not raw.exists() or not prefix:
        return []
    target = prefix[:4].casefold()
    out = []
    for pth in sorted(raw.rglob('*.lbm')):
        info = doom_sprite_filename(pth.stem)
        if info and info[0].casefold() == target:
            out.append((info, pth))
    return out

def assemble_world_sprite_resource(files):
    parsed = []
    for info, pth in files:
        parsed.append((info, pth))
    frames = {}
    for info, pth in parsed:
        fam, frame, rotations, var = info
        frames.setdefault(frame, {})
        for rot in rotations:
            old = frames[frame].get(rot)
            if old is None or (var, str(pth)) < (old[0], str(old[1])):
                frames[frame][rot] = (var, pth)
    if not frames:
        return (None, 'no decodable frames')
    maxframe = max(frames)
    data = bytearray(4 * (maxframe + 1))
    offsets = [0] * (maxframe + 1)
    for f in range(maxframe + 1):
        rs = frames.get(f, {})
        if not rs:
            return (None, f'frame {f} missing')
        if 0 in rs and len(rs) == 1:
            patch = build_patch(read_ilbm(rs[0][1]))
            off = len(data)
            data.extend(patch)
            offsets[f] = off
        else:
            missing = [r for r in range(1, 9) if r not in rs]
            if missing:
                return (None, f'frame {f} missing rotations {missing}')
            roff = len(data)
            data.extend(b'\x00' * 32)
            for r in range(1, 9):
                poff = len(data)
                data.extend(build_patch(read_ilbm(rs[r][1])))
                struct.pack_into('>I', data, roff + (r - 1) * 4, poff)
            offsets[f] = 1073741824 | roff
    for i, off in enumerate(offsets):
        struct.pack_into('>I', data, i * 4, off)
    return (bytes(data), 'ok')

def parse_doomrez(path: Path):
    text = path.read_text(encoding='utf-8', errors='replace')
    values = {}
    for m in re.finditer('^\\s*#define\\s+(r[A-Za-z0-9_]+)\\s+([0-9]+)\\s*(?:/\\*.*)?$', text, re.M):
        values[m.group(1)] = int(m.group(2))
    in_enum = False
    value = -1
    for raw in text.splitlines():
        line = raw.split('//', 1)[0]
        if 'enum' in line and '{' in line:
            in_enum = True
            value = -1
            continue
        if in_enum and '}' in line:
            in_enum = False
            continue
        if not in_enum:
            continue
        m = re.match('\\s*(r[A-Za-z0-9_]+)\\s*(?:=\\s*([^,]+))?\\s*,?', line)
        if not m:
            continue
        name, expr = m.groups()
        if expr:
            expr = expr.strip()
            m2 = re.fullmatch('(r[A-Za-z0-9_]+)\\s*\\+\\s*(\\d+)', expr)
            if m2:
                if m2.group(1) not in values:
                    raise SystemExit(f'Cannot resolve {name} = {expr}')
                value = values[m2.group(1)] + int(m2.group(2))
            elif expr in values:
                value = values[expr]
            else:
                try:
                    value = int(expr, 0)
                except ValueError:
                    raise SystemExit(f'Cannot resolve {name} = {expr}')
        else:
            value += 1
        values[name] = value
    required = ['rTEXTURE1', 'rT_START', 'rT_END', 'rF_START', 'rF_END', 'rBACKGROUNDMASK', 'rTITLE', 'rDEMO1', 'rDEMO2', 'rMAP01', 'rFIRSTSPRITE', 'rLASTSPRITE']
    missing = [x for x in required if x not in values]
    if missing:
        raise SystemExit('DoomRez.h is missing: ' + ', '.join(missing))
    return values

def all_files(root: Path):
    skip_dirs = {'.git', '.github', 'source', 'lib', 'tools', 'build', '__pycache__'}
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in skip_dirs]
        for f in files:
            p = Path(base) / f
            if p.is_file():
                yield p

def index_files(root: Path):
    idx = {}
    for p in all_files(root):
        key = norm(p.stem)
        idx.setdefault(key, []).append(p)
    return idx

def _path_score(p: Path, expected_dirs=()):
    s = str(p).lower()
    score = 0
    if p.suffix.lower() == '.cel':
        score += 100
    if p.suffix.lower() == '.pict':
        score -= 1000
    for d in expected_dirs:
        if d.lower() in s:
            score += 40
    if 'rawsprites' in s:
        score -= 100
    if 'art' in s:
        score -= 20
    return score

def _filename_variants(name: str):
    n = norm(name)
    out = [n]
    m = re.match('^(.*?)(0+)([1-9][0-9]*)$', n)
    if m:
        out.append(m.group(1) + m.group(3))
    out.append(n.replace('sp', 'sp'))
    return list(dict.fromkeys(out))

def find_named(idx, names, extensions=None, expected_dirs=(), resource_label=None):
    ordered = []
    for name in list(names) + [x for n in names for x in KNOWN_ASSET_ALIASES.get(n, [])]:
        ordered.extend(_filename_variants(name))
    ordered = list(dict.fromkeys(ordered))
    allowed = {e.lower() for e in extensions} if extensions else None
    expected = {Path(d).name.casefold() for d in expected_dirs}
    matches = []
    for key in ordered:
        for candidate in idx.get(key, []):
            if allowed and candidate.suffix.lower() not in allowed:
                continue
            if expected and (not {part.casefold() for part in candidate.parts} & expected):
                continue
            matches.append(candidate)
    unique = {}
    for candidate in matches:
        unique[str(candidate.resolve())] = candidate
    values = list(unique.values())
    if len(values) == 1:
        return (values[0], False)
    if len(values) > 1:
        raise SystemExit(f'ambiguous exact filename match for {resource_label or names[0]}: ' + ', '.join((str(x) for x in sorted(values))))
    return (None, False)

def read_cel_size(path: Path):
    data = path.read_bytes()
    if len(data) < 60:
        raise ValueError(f'{path}: too small to be a 3DO CEL')
    pre0 = struct.unpack_from('>I', data, 52)[0] # PRE0/PRE1 for CEL
    pre1 = struct.unpack_from('>I', data, 56)[0]
    width = (pre1 & 2047) + 1
    height = (pre0 >> 6 & 1023) + 1
    if not (1 <= width <= 2048 and 1 <= height <= 1024):
        raise ValueError(f'{path}: impossible CEL size {width}x{height}')
    return (width, height)

def load_frontend_asset(path: Path):
    suffix = path.suffix.casefold()
    if suffix == '.cel':
        return path.read_bytes()
    if suffix == '.lbm':
        return build_sprite_cel(read_ilbm(path))
    if suffix == '':
        return path.read_bytes()
    raise ValueError(f'{path}: unsupported frontend asset type')

def build_brown_background(root, idx):
    p, _ = find_named(idx, ['backgrnd'], {'.cel'}, expected_dirs=('3doart',), resource_label='rBACKGRNDBROWN source')
    if not p:
        raise SystemExit('missing art/3doart/backgrnd.cel needed to reconstruct rBACKGRNDBROWN')
    data = bytearray(p.read_bytes())
    if len(data) < 60:
        raise ValueError(f'{p}: invalid CEL')
    plut_rel = struct.unpack_from('>I', data, 12)[0]
    plut = 16 + plut_rel
    source_rel = struct.unpack_from('>I', data, 8)[0]
    source = 12 + source_rel
    if not (60 <= plut < len(data) and plut < source <= len(data)):
        raise ValueError(f'{p}: unsupported CCB-relative PLUT/source layout')
    plut_bytes = source - plut
    if plut_bytes % 2:
        raise ValueError(f'{p}: odd PLUT size')
    count = plut_bytes // 2
    if count < 2:
        raise ValueError(f'{p}: no useful PLUT')
    brown = [0, 2048, 3072, 4129, 4128, 5184, 7232, 8289, 9346, 10403, 10435, 11460, 12516, 12516, 12516, 12516]

    def rgb555(v):
        return (v >> 10 & 31, v >> 5 & 31, v & 31)

    def lum(v):
        r, g, b = rgb555(v)
        return 0.299 * r + 0.587 * g + 0.114 * b
    src_plut = [struct.unpack_from('>H', data, plut + i * 2)[0] for i in range(count)]
    vals = [lum(v) for v in src_plut]
    lo, hi = (min(vals), max(vals))
    for i, old in enumerate(src_plut):
        if i == 0:
            new = brown[0]
        elif hi <= lo:
            new = brown[min(i, len(brown) - 1)]
        else:
            t = (lum(old) - lo) / (hi - lo)
            bi = 1 + int(round(t * (len(brown) - 2)))
            bi = max(1, min(len(brown) - 1, bi))
            new = brown[bi]
        new = new & 32767 | old & 32768
        struct.pack_into('>H', data, plut + i * 2, new)
    return (bytes(data), p)

def find_frontend_asset(idx, names, *, resource_label):
    return find_named(idx, names, extensions={'.cel', '.lbm', ''}, expected_dirs=('3doart',), resource_label=resource_label)

def parse_wad(path: Path):
    b = path.read_bytes()
    if len(b) < 12 or b[:4] not in (b'IWAD', b'PWAD'):
        raise ValueError(f'{path}: not a WAD')
    count, directory = struct.unpack_from('<II', b, 4)
    if directory + count * 16 > len(b):
        raise ValueError(f'{path}: invalid WAD directory')
    out = []
    for i in range(count):
        off, size = struct.unpack_from('<II', b, directory + i * 16)
        rawname = b[directory + i * 16 + 8:directory + i * 16 + 16]
        name = rawname.rstrip(b'\x00 ').decode('ascii', 'replace').upper()
        if off + size > len(b):
            raise ValueError(f'{path}: lump {name} outside file')
        out.append((name, b[off:off + size]))
    return out

def add_resource(resources, seen, number, name, path, data, rtype=1):
    if number in seen:
        raise SystemExit(f'duplicate resource {number}: {name}')
    seen.add(number)
    resources.append(Resource(number, rtype, name, path, data))

def runtime_inventory(root):
    categories = {'textures3do': [], 'flats3do': [], 'art3do': [], 'sprite_lbm': [], 'wads': [], 'sounds': [], 'music': [], 'movies': []}
    for base, dirs, files in os.walk(root):
        rel = Path(base).relative_to(root)
        parts = {p.casefold() for p in rel.parts}
        for name in files:
            path = Path(base) / name
            low = name.casefold()
            if 'textures 3do'.casefold() in parts and low.endswith('.cel'):
                categories['textures3do'].append(path)
            elif 'flats 3do'.casefold() in parts and low.endswith('.cel'):
                categories['flats3do'].append(path)
            elif '3doart' in parts and low.endswith('.cel'):
                categories['art3do'].append(path)
            elif 'rawsprites' in parts and low.endswith('.lbm'):
                categories['sprite_lbm'].append(path)
            elif 'wads' in parts and low.endswith('.wad'):
                categories['wads'].append(path)
            elif 'sounds' in parts:
                categories['sounds'].append(path)
            elif 'music' in parts:
                categories['music'].append(path)
            elif 'movies' in parts:
                categories['movies'].append(path)
    return categories

def write_sprite_naming_audit(root, path):
    raw = root / 'sprites3do' / 'rawsprites'
    with path.open('w', encoding='utf-8') as f:
        f.write('3DO RAW SPRITE NAMING AUDIT\n')
        f.write('===================================\n\n')
        f.write('Grammar: NNNNFx or NNNNFxFx. A paired form such as A2A8\n')
        f.write('assigns the same source image to both rotations 2 and 8.\n')
        f.write('A trailing source/development variant is retained separately.\n\n')
        if not raw.exists():
            f.write('sprites3do/rawsprites not found.\\n')
            return
        for pth in sorted(raw.rglob('*.lbm')):
            info = doom_sprite_filename(pth.stem)
            if info:
                family, frame, rots, var = info
                f.write(f"{pth}: family={family} frame={chr(65 + frame)} rotations={','.join(map(str, rots)) or '-'} variant={var or '-'}\\n")
            else:
                f.write(f'{pth}: UNPARSED\\n')

def write_resource_mapping_audit(root, values, path, resources, audit_missing):
    included = {r.number: r for r in resources if r.data}
    missing = {n: reason for n, _sym, reason in audit_missing}
    names = {n: s for s, n in values.items()}
    with path.open('w', encoding='utf-8') as f:
        f.write('DOOM 3DO RESOURCE MAPPING AUDIT\n')
        f.write('===============================\n\n')
        f.write('Exact flat namespace. No fuzzy matching. Real payloads are included\n')
        f.write('only when an actual runtime asset or exact WAD payload is identified.\n')
        f.write('Unresolved IDs remain empty; numbers are never renumbered.\n\n')
        f.write('ID    SYMBOL                      STATUS                 SOURCE / REASON\n')
        f.write('----  --------------------------  ---------------------  ------------------------------\n')
        for n in range(1, values['rLASTSPRITE']):
            sym = names.get(n, f'UNDEFINED_{n}')
            if n in included:
                f.write(f'{n:4d}  {sym:26s}  INCLUDED                {included[n].path}\n')
            else:
                f.write(f"{n:4d}  {sym:26s}  EMPTY / UNRESOLVED      {missing.get(n, 'no verified runtime payload')}\n")

def build_resources(root: Path, v, idx):
    resources = []
    seen = set()
    audit_missing = []
    t_start, t_end = (v['rT_START'], v['rT_END'])
    f_start, f_end = (v['rF_START'], v['rF_END'])
    bg = v['rBACKGROUNDMASK']
    title = v['rTITLE']
    texture_entries = []
    for n in range(t_start, t_end):
        symbol = next((k for k, val in v.items() if val == n and k not in ('rT_START', 'rT_END', 'rF_START', 'rF_END')))
        base = symbol[1:]
        p, fuzzy = find_named(idx, [symbol, base], {'.cel'}, expected_dirs=('textures 3do',), resource_label=symbol)
        if not p:
            audit_missing.append((n, symbol, 'wall texture'))
            continue
        data = p.read_bytes()
        add_resource(resources, seen, n, symbol, p, data)
        if fuzzy:
            print(f'alias: {symbol} -> {p.name}')
        texture_entries.append((symbol, p))
    flat_entries = []
    for n in range(f_start, f_end):
        symbol = next((k for k, val in v.items() if val == n and k not in ('rT_START', 'rT_END', 'rF_START', 'rF_END')))
        base = symbol[1:]
        p, fuzzy = find_named(idx, [symbol, base], {'.cel'}, expected_dirs=('flats 3do',), resource_label=symbol)
        if not p:
            audit_missing.append((n, symbol, 'flat'))
            continue
        data = p.read_bytes()
        add_resource(resources, seen, n, symbol, p, data)
        if fuzzy:
            print(f'alias: {symbol} -> {p.name}')
        flat_entries.append((symbol, p))
    tex_blob = bytearray()
    tex_blob += struct.pack('>HHHH', len(texture_entries), t_start, len(flat_entries), f_start)
    for _, p in texture_entries:
        w, h = read_cel_size(p)
        tex_blob += struct.pack('>HHI', w, h, 0)
    add_resource(resources, seen, v['rTEXTURE1'], 'rTEXTURE1', root / 'generated:TEXTURE1', bytes(tex_blob))
    for i in range(6):
        n = bg + i
        p, fuzzy = find_named(idx, BACKGROUND_ALIASES[i], {'.cel'}, expected_dirs=('art/3doart',), resource_label=f'rBACKGROUNDMASK+{i}')
        if not p:
            audit_missing.append((n, f'rBACKGROUNDMASK+{i}', 'gameplay background'))
            continue
        add_resource(resources, seen, n, f'rBACKGROUNDMASK+{i}', p, p.read_bytes())
        if fuzzy:
            print(f'alias: rBACKGROUNDMASK+{i} -> {p.name}')
    brown_num = title + 5
    try:
        brown_data, brown_source = build_brown_background(root, idx)
        add_resource(resources, seen, brown_num, 'rBACKGRNDBROWN', brown_source, brown_data)
    except Exception as exc:
        audit_missing.append((brown_num, 'rBACKGRNDBROWN', 'brown background reconstruction failed: ' + str(exc)))
    for symbol, aliases in SPECIAL_ALIASES.items():
        n = v[symbol]
        candidates = aliases + KNOWN_ASSET_ALIASES.get(symbol, [])
        p, fuzzy = find_frontend_asset(idx, candidates, resource_label=symbol)
        if not p:
            audit_missing.append((n, symbol, 'frontend/UI source asset not found'))
            continue
        try:
            data = load_frontend_asset(p)
        except Exception as exc:
            audit_missing.append((n, symbol, f'frontend/UI decode error: {exc}'))
            continue
        add_resource(resources, seen, n, symbol, p, data)
        if fuzzy:
            print(f'alias: {symbol} -> {p.name}')
    doomwad = root / 'wads' / 'doom.wad'
    if not doomwad.exists():
        p, _ = find_named(idx, ['doom'], {'.wad'}, expected_dirs=('wads',), resource_label='doom.wad')
        doomwad = p
    if not doomwad or not doomwad.exists():
        raise SystemExit('missing wads/doom.wad for DEMO1/DEMO2')
    lumps = dict(parse_wad(doomwad))
    for i, name in enumerate(('DEMO1', 'DEMO2'), start=1):
        if name not in lumps:
            audit_missing.append((v[f'rDEMO{i}'], f'rDEMO{i}', 'demo lump'))
        else:
            add_resource(resources, seen, v[f'rDEMO{i}'], f'rDEMO{i}', doomwad, lumps[name])
    for mapno in range(1, 25):
        p = root / 'wads' / f'map{mapno:02d}.wad'
        if not p.exists():
            p, _ = find_named(idx, [f'map{mapno:02d}'], {'.wad'}, expected_dirs=('wads',), resource_label=f'map{mapno:02d}.wad')
        if not p or not p.exists():
            for offset, lname in enumerate(MAP_LUMPS):
                audit_missing.append((v['rMAP01'] + (mapno - 1) * 10 + offset, f'MAP{mapno:02d}:{lname}', 'map WAD'))
            continue
        lumps_list = parse_wad(p)
        lumpdict = {n: d for n, d in lumps_list}
        base = v['rMAP01'] + (mapno - 1) * 10
        for offset, lname in enumerate(MAP_LUMPS):
            if lname not in lumpdict:
                audit_missing.append((base + offset, f'MAP{mapno:02d}:{lname}', 'map lump'))
                continue
            add_resource(resources, seen, base + offset, f'MAP{mapno:02d}:{lname}', p, lumpdict[lname])
    sprite_audit_rows = []
    for symbol, n in sorted(v.items(), key=lambda kv: kv[1]):
        if not symbol.startswith('rSPR_'):
            continue
        rawroot = root / 'sprites3do' / 'rawsprites'
        if symbol in WEAPON_SHAPE_SOURCES:
            label = WEAPON_SHAPE_LABELS[symbol]
            try:
                blob, status = load_weapon_shape_resource(root, symbol)
            except Exception as exc:
                blob, status = (None, 'DECODE ERROR: ' + str(exc))
            if blob is None:
                audit_missing.append((n, symbol, status))
                sprite_audit_rows.append((n, symbol, label, len(set(WEAPON_SHAPE_SOURCES[symbol])), 'EMPTY: ' + status))
                continue
            add_resource(resources, seen, n, symbol, root / f'<generated:{symbol}>', blob)
            sprite_audit_rows.append((n, symbol, label, len(set(WEAPON_SHAPE_SOURCES[symbol])), f'INCLUDED: {len(blob)} bytes'))
            continue
        prefix = SPRITE_PREFIXES.get(symbol)
        source_files = []
        if prefix and rawroot.exists():
            target = prefix[:4].casefold()
            for pth in rawroot.rglob('*.lbm'):
                info = doom_sprite_filename(pth.stem)
                if info and info[0].casefold() == target:
                    source_files.append(pth)
        if not source_files and symbol == 'rSPR_HANGINGRUMP' and (prefix == 'gor1'):
            try:
                palette, wad_lumps = load_doom_wad_palette_and_lumps(root)
                image = parse_doom_patch(wad_lumps, 'GOR1A0', palette) if palette else None
                if image is None:
                    raise ValueError('GOR1A0 or PLAYPAL not present in wads/doom.wad')
                blob = build_patch(image)
                add_resource(resources, seen, n, symbol, root / '<generated:rSPR_HANGINGRUMP from doom.wad:GOR1A0>', blob)
                sprite_audit_rows.append((n, symbol, 'gor1 / GOR1A0 from doom.wad', 1, f'INCLUDED: {len(blob)} bytes'))
                continue
            except Exception as exc:
                reason = 'DOOM WAD fallback failed: ' + str(exc)
        elif not source_files:
            reason = 'sprite source LBM not found'
        if not source_files:
            audit_missing.append((n, symbol, reason))
            sprite_audit_rows.append((n, symbol, prefix or 'UNMAPPED', 0, 'EMPTY: ' + reason))
            continue
        try:
            blob, status = assemble_world_sprite_resource([(doom_sprite_filename(p.stem), p) for p in source_files])
        except Exception as exc:
            blob, status = (None, 'DECODE ERROR: ' + str(exc))
        if blob is None:
            audit_missing.append((n, symbol, status))
            sprite_audit_rows.append((n, symbol, prefix, len(source_files), 'EMPTY: ' + status))
            continue
        add_resource(resources, seen, n, symbol, root / f'<generated:{symbol}>', blob)
        sprite_audit_rows.append((n, symbol, prefix, len(source_files), f'INCLUDED: {len(blob)} bytes'))
    resources.sort(key=lambda r: r.number)
    return (resources, audit_missing)

def write_rez(resources, output: Path):
    groups = []
    i = 0
    while i < len(resources):
        first = i
        rtype = resources[i].type
        i += 1
        while i < len(resources) and resources[i].type == rtype and (resources[i].number == resources[i - 1].number + 1):
            i += 1
        groups.append((rtype, resources[first].number, first, i - first))
    memsize = sum((12 + 12 * count for _, _, _, count in groups))
    data_start = 12 + memsize
    offsets = []
    off = data_start
    for r in resources:
        offsets.append(off)
        off += len(r.data)
    if off > 4294967295:
        raise SystemExit('REZFILE exceeds 4 GiB')
    if memsize > 4294967295:
        raise SystemExit('REZFILE dictionary exceeds 4 GiB')
    with output.open('wb') as f:
        f.write(b'BRGR')
        f.write(struct.pack('>II', len(groups), memsize))
        for rtype, first_num, first, count in groups:
            f.write(struct.pack('>III', rtype, first_num, count))
            for j in range(count):
                r = resources[first + j]
                f.write(struct.pack('>III', offsets[first + j] | r.flags, len(r.data), 0))
        for r in resources:
            f.write(r.data)
    return (groups, memsize, off)

def read_rezfile(path):
    data = path.read_bytes()
    if len(data) < 12 or data[:4] != b"BRGR":
        raise ValueError(f"{path}: not a BurgerLib REZFILE")
    group_count, dictionary_size = struct.unpack_from(">II", data, 4)
    dictionary_end = 12 + dictionary_size
    if dictionary_end > len(data):
        raise ValueError(f"{path}: dictionary extends beyond file")
    resources = {}
    position = 12
    for _ in range(group_count):
        if position + 12 > dictionary_end:
            raise ValueError(f"{path}: truncated resource group")
        resource_type, first_number, count = struct.unpack_from(">III", data, position)
        position += 12
        entries_end = position + count * 12
        if entries_end > dictionary_end:
            raise ValueError(f"{path}: truncated resource entries")
        for index in range(count):
            offset, size, _reserved = struct.unpack_from(">III", data, position)
            position += 12
            real_offset = offset & 0x1fffffff
            offset_flags = offset & 0xe0000000
            if real_offset + size > len(data):
                raise ValueError(f"{path}: resource extends beyond file")
            number = first_number + index
            resources[number] = Resource(
                number, resource_type, f"resource_{number:03d}", path,
                data[real_offset:real_offset + size], offset_flags,
            )
    if position != dictionary_end:
        raise ValueError(f"{path}: dictionary size does not match groups")
    return resources

def parse_replacement(value):
    try:
        number_text, filename = value.split("=", 1)
        number = int(number_text, 0)
    except ValueError as exc:
        raise SystemExit(f"invalid replacement {value!r}; use ID=PATH") from exc
    if number < 0 or number > 0xffffffff:
        raise SystemExit(f"resource ID out of range: {number}")
    path = Path(filename)
    if not path.is_file():
        raise SystemExit(f"replacement file not found: {path}")
    return number, path


def parse_map_replacement(value):
    try:
        map_name, filename = value.split("=", 1)
        map_number = int(map_name.lower().removeprefix("map"), 10)
    except ValueError as exc:
        raise SystemExit(f"invalid map replacement {value!r}; use MAPnn=PATH") from exc
    if not 1 <= map_number <= 24:
        raise SystemExit(f"map number out of range: {map_number}")
    path = Path(filename)
    if not path.is_file():
        raise SystemExit(f"replacement file not found: {path}")
    return map_number, path


def map_resource_numbers(values, map_number):
    if not 1 <= map_number <= 24:
        raise SystemExit(f"map number out of range: {map_number}")
    first = values["rMAP01"] + (map_number - 1) * len(MAP_LUMPS)
    return {first + index: lump for index, lump in enumerate(MAP_LUMPS)}


def load_map_replacements(path, values, map_number):
    lumps = dict(parse_wad(path))
    numbers = map_resource_numbers(values, map_number)
    missing = [name for name in MAP_LUMPS if name not in lumps]
    if missing:
        raise SystemExit(f"{path}: missing map lumps: {', '.join(missing)}")
    return {number: lumps[lump] for number, lump in numbers.items()}


def replace_resources(resources, replacements):
    for number, data in replacements.items():
        if number not in resources:
            raise SystemExit(f"resource ID {number} is not present in the base REZFILE")
        resource = resources[number]
        resources[number] = Resource(
            resource.number,
            resource.type,
            resource.name,
            resource.path,
            data,
            resource.flags,
        )


def write_updated_rezfile(resources, output):
    ordered = [resources[number] for number in sorted(resources)]
    return write_rez(ordered, output)


def extract_map(resources, values, map_number, output):
    numbers = map_resource_numbers(values, map_number)
    lumps = [(name, resources[number].data) for number, name in numbers.items()]
    wad = bytearray(b"PWAD")
    wad.extend(struct.pack("<II", len(lumps), 0))
    offsets = []
    for _, data in lumps:
        offsets.append(len(wad))
        wad.extend(data)
    directory_offset = len(wad)
    for (name, data), offset in zip(lumps, offsets):
        wad.extend(struct.pack("<II8s", offset, len(data), name.encode("ascii")[:8].ljust(8, b"\0")))
    struct.pack_into("<II", wad, 4, len(lumps), directory_offset)
    output.write_bytes(wad)


def print_rezfile(resources):
    for number in sorted(resources):
        resource = resources[number]
        print(f"{number:3d}  type={resource.type:2d}  size={len(resource.data):8d}")


def resolve_patch_path(patch_directory, value):
    if not isinstance(value, str) or not value.strip():
        raise SystemExit("patch paths must be non-empty strings")
    path = (patch_directory / value).resolve()
    try:
        path.relative_to(patch_directory.resolve())
    except ValueError as exc:
        raise SystemExit(f"patch path escapes patch directory: {value}") from exc
    if not path.is_file():
        raise SystemExit(f"patch file not found: {value}")
    return path


def parse_patch_directory(patch_directory, source_root):
    manifest_path = patch_directory / "mod.json"
    if not manifest_path.is_file():
        raise SystemExit(f"missing patch manifest: {manifest_path}")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SystemExit(f"invalid patch manifest {manifest_path}: {exc}") from exc
    if not isinstance(manifest, dict):
        raise SystemExit("patch manifest must contain a JSON object")

    resources = manifest.get("resources", {})
    maps = manifest.get("maps", {})
    if not isinstance(resources, dict):
        raise SystemExit("patch manifest 'resources' must be an object")
    if not isinstance(maps, dict):
        raise SystemExit("patch manifest 'maps' must be an object")

    replacements = {}
    for resource_id, filename in resources.items():
        try:
            number = int(resource_id, 0)
        except (TypeError, ValueError) as exc:
            raise SystemExit(f"invalid resource ID in patch manifest: {resource_id!r}") from exc
        if not 0 <= number <= 0xffffffff:
            raise SystemExit(f"resource ID out of range: {number}")
        if number in replacements:
            raise SystemExit(f"duplicate resource ID in patch manifest: {number}")
        replacements[number] = resolve_patch_path(patch_directory, filename)

    map_replacements = []
    for map_name, filename in maps.items():
        try:
            map_number = int(str(map_name).lower().removeprefix("map"), 10)
        except ValueError as exc:
            raise SystemExit(f"invalid map name in patch manifest: {map_name!r}") from exc
        if not 1 <= map_number <= 24:
            raise SystemExit(f"map number out of range: {map_number}")
        map_path = resolve_patch_path(patch_directory, filename)
        map_replacements.append((map_number, map_path))

    allowed_keys = {"name", "version", "description", "author", "resources", "maps"}
    unknown = set(manifest) - allowed_keys
    if unknown:
        names = ", ".join(sorted(unknown))
        raise SystemExit(f"unknown patch manifest field(s): {names}")

    header = source_root / "source" / "doomrez.h"
    if map_replacements and not header.exists():
        raise SystemExit(f"missing {header}; map replacement requires doomrez.h")

    return replacements, map_replacements


def load_patch_replacements(patch_directory, source_root):
    replacements, map_replacements = parse_patch_directory(patch_directory, source_root)
    return (
        {number: path.read_bytes() for number, path in replacements.items()},
        map_replacements,
    )


def update_rezfile(base_path, output, replacements, map_replacements, source_root):
    resources = read_rezfile(base_path)
    all_replacements = dict(replacements)
    if map_replacements:
        header = source_root / "source" / "doomrez.h"
        if not header.exists():
            raise SystemExit(f"missing {header}; map replacement requires doomrez.h")
        values = parse_doomrez(header)
        for map_number, map_path in map_replacements:
            map_data = load_map_replacements(map_path, values, map_number)
            overlap = set(all_replacements).intersection(map_data)
            if overlap:
                ids = ", ".join(str(number) for number in sorted(overlap))
                raise SystemExit(f"resource replacement overlaps map replacement: {ids}")
            all_replacements.update(map_data)
    replace_resources(resources, all_replacements)
    groups, dictionary_size, total_size = write_updated_rezfile(resources, output)
    return len(resources), len(groups), dictionary_size, total_size

def main():
    ap = argparse.ArgumentParser(description="Build and update the BurgerLib 2 DOOM 3DO REZFILE")
    ap.add_argument("root", type=Path, nargs="?", default=Path("."))
    ap.add_argument("-o", "--output", type=Path)
    ap.add_argument("--list-only", action="store_true")
    ap.add_argument("--update", type=Path)
    ap.add_argument("--replace", action="append", default=[])
    ap.add_argument("--replace-map", action="append", default=[])
    ap.add_argument("--replace-wad-texture", action="append", default=[])
    ap.add_argument("--replace-wad-sprite", action="append", default=[])
    ap.add_argument("--patch", type=Path)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--extract", type=int)
    ap.add_argument("--extract-map", type=str)
    ap.add_argument("--in-place", action="store_true")
    args = ap.parse_args()

    if args.update:
        source_root = args.root.resolve()
        replacements = {number: path.read_bytes() for number, path in (parse_replacement(value) for value in args.replace)}
        map_replacements = [parse_map_replacement(value) for value in args.replace_map]
        wad_texture_replacements = list(args.replace_wad_texture)
        wad_sprite_replacements = list(args.replace_wad_sprite)

        if args.patch:
            patch_directory = args.patch.resolve()
            patch_replacements, patch_maps = load_patch_replacements(patch_directory, source_root)
            overlap = set(replacements).intersection(patch_replacements)
            if overlap:
                ids = ", ".join(str(number) for number in sorted(overlap))
                raise SystemExit(f"resource replacement overlaps patch: {ids}")
            
            replacements.update(patch_replacements)
            existing_maps = {number for number, _ in map_replacements}
            duplicate_maps = existing_maps.intersection(number for number, _ in patch_maps)
            if duplicate_maps:
                names = ", ".join(f"MAP{number:02d}" for number in sorted(duplicate_maps))
                raise SystemExit(f"map replacement overlaps patch: {names}")
            
            map_replacements.extend(patch_maps)
        resources = read_rezfile(args.update)
        values = parse_doomrez(source_root / "source" / "doomrez.h")
        for replacement in wad_texture_replacements:
            replace_wad_texture_resource(resources, values, replacement)
        for replacement in wad_sprite_replacements:
            replace_wad_sprite_resource(resources, values, replacement)
        wad_source_paths = {Path(value.split("=", 1)[1]).resolve() for value in wad_texture_replacements + wad_sprite_replacements if "=" in value}
        for number, resource in resources.items():
            try:
                resource_path = resource.path.resolve()
            except OSError:
                resource_path = resource.path
            if resource_path in wad_source_paths:
                replacements[number] = resource.data

        if args.list:
            print_rezfile(resources)
            return 0

        if args.extract is not None:
            resource = resources.get(args.extract)
            if resource is None:
                raise SystemExit(f"resource ID {args.extract} is not present")
            output = args.output or Path(f"{args.extract:04d}.bin")
            output.write_bytes(resource.data)
            print(f"Extracted resource {args.extract} to {output}")
            return 0

        if args.extract_map is not None:
            try:
                map_number = int(args.extract_map.lower().removeprefix("map"), 10)
            except ValueError as exc:
                raise SystemExit(f"invalid map: {args.extract_map!r}") from exc
            header = source_root / "source" / "doomrez.h"
            if not header.exists():
                raise SystemExit(f"missing {header}; map extraction requires doomrez.h")
            values = parse_doomrez(header)
            numbers = map_resource_numbers(values, map_number)
            missing = [number for number in numbers if number not in resources]
            if missing:
                raise SystemExit("base REZFILE is missing map resources: " + ", ".join(map(str, missing)))
            output = args.output or Path(f"MAP{map_number:02d}.wad")
            extract_map(resources, values, map_number, output)
            print(f"Extracted MAP{map_number:02d} to {output}")
            return 0

        if not replacements and not map_replacements and not wad_texture_replacements and not wad_sprite_replacements:
            raise SystemExit("--update requires --replace, --replace-map, or --patch")
        
        if args.in_place and args.output:
            raise SystemExit("--in-place cannot be combined with --output")
        
        output = args.update if args.in_place else (args.output or args.update.with_name(args.update.name + ".new"))
        count, groups, dictionary_size, total_size = update_rezfile(
            args.update, output, replacements, map_replacements, source_root
        )
        print(f"Updated {output}")
        print(f"  Resources   : {count}")
        print(f"  Groups      : {groups}")
        print(f"  Dictionary  : {dictionary_size} bytes")
        print(f"  Total Size  : {total_size} bytes")
        return 0

    root = args.root.resolve()
    header = root / "source" / "doomrez.h"
    if not header.exists():
        raise SystemExit(f"missing {header}")
    if args.replace or args.replace_map or args.replace_wad_texture or args.replace_wad_sprite or args.patch or args.list or args.extract is not None or args.extract_map is not None or args.in_place:
        raise SystemExit("update options require --update")

    v = parse_doomrez(header)
    idx = index_files(root)
    inv = runtime_inventory(root)
    print("Data inventory:")
    for key, paths in inv.items():
        print(f"  {key}: {len(paths)}")
    resources, audit_missing = build_resources(root, v, idx)
    mapping_audit_path = (args.output or (root / "REZFILE")).parent / "RESOURCE_MAPPING_AUDIT.txt"
    write_resource_mapping_audit(root, v, mapping_audit_path, resources, audit_missing)
    sprite_naming_audit = (args.output or (root / "REZFILE")).parent / "RAW_SPRITE_NAMING.txt"
    write_sprite_naming_audit(root, sprite_naming_audit)
    sprite_audit_path = (args.output or (root / "REZFILE")).parent / "SPRITE_RECONSTRUCTION_AUDIT.txt"
    rawroot = root / "sprites3do" / "rawsprites"
    empty_map = {n: reason for n, _symbol, reason in audit_missing}
    with sprite_audit_path.open("w", encoding="utf-8") as sf:
        sf.write("SPRITE RECONSTRUCTION AUDIT\n")
        sf.write("===========================\n\n")
        sf.write("Exact source mapping. No fuzzy matching is used.\\n")
        sf.write("World sprites use the exact DOOM NNNNFx / NNNNFxFx grammar;\\n")
        sf.write("first-person rSPR_BIG* resources use their verified weapon frame LBMs.\\n")
        sf.write("rSPR_HANGINGRUMP is gor1; it remains empty\\n")
        sf.write("when no gor1*.lbm exists in the supplied 3DO extraction.\\n\\n")
        sf.write("resource  symbol                      source family / frame LBMs                  source LBMs  result\n")
        sf.write("--------  --------------------------  --------------------------------------------  -----------  -------------------------\n")
        included_numbers = {r.number for r in resources if r.number >= v["rFIRSTSPRITE"] and r.number < v["rLASTSPRITE"] and r.data}
        for symbol, number in sorted(v.items(), key=lambda kv: kv[1]):
            if not symbol.startswith("rSPR_"):
                continue
            if symbol in WEAPON_SHAPE_SOURCES:
                prefix = WEAPON_SHAPE_LABELS[symbol]
                count = len(set(WEAPON_SHAPE_SOURCES[symbol]))
            else:
                prefix = SPRITE_PREFIXES.get(symbol) or "UNMAPPED"
                count = 0
                if rawroot.exists() and prefix != "UNMAPPED":
                    target = prefix[:4].casefold()
                    for path in rawroot.rglob("*.lbm"):
                        info = doom_sprite_filename(path.stem)
                        if info and info[0].casefold() == target:
                            count += 1
            result = "INCLUDED" if number in included_numbers else "EMPTY: " + empty_map.get(number, "not reconstructed")
            sf.write(f"{number:8d}  {symbol:26s}  {prefix:44s}  {count:11d}  {result}\n")
    print(f"Built {len(resources)} resource dictionary entries.")
    print(f"Resource mapping audit: {mapping_audit_path}")
    print(f"Raw sprite naming audit: {sprite_naming_audit}")
    print(f"Sprite reconstruction audit: {sprite_audit_path}")
    print(f"Excluded {len(audit_missing)} undefined/unavailable resources.")
    for resource in resources:
        print(f"{resource.number:3d}  type={resource.type:2d}  {resource.name:24s}  {resource.path}")
    audit_path = (args.output or (root / "REZFILE")).parent / "REZFILE_AUDIT.txt"
    with audit_path.open("w", encoding="utf-8") as af:
        af.write("DOOM 3DO REZFILE AUDIT\n")
        af.write("====================\n\n")
        af.write(f"Included resources: {len(resources)}\n")
        af.write(f"Empty/unresolved resources: {len(audit_missing)}\n\n")
        af.write("INCLUDED\n--------\n")
        for resource in resources:
            af.write(f"{resource.number:3d}  type={resource.type:2d}  {resource.name:28s}  {resource.path}\n")
        af.write("\nEXCLUDED / NOT IN REZFILE\n--------------------------\n")
        for number, name, kind in sorted(audit_missing):
            af.write(f"{number:3d}  {name:28s}  [{kind}]\n")
    if args.list_only:
        print(f"Audit written to {audit_path}")
        return 0
    output = args.output or (root / "REZFILE")
    groups, dictionary_size, total_size = write_rez(resources, output)
    print()
    print(f"Wrote {output}")
    print(f"  Groups      : {len(groups)}")
    print(f"  Dictionary  : {dictionary_size} bytes")
    print(f"  Total Size  : {total_size} bytes")
    print("  Payloads    : Copied as is")
    print("  Audio/Video : Filesystem Data, not in REZFILE")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
