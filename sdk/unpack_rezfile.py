#!/usr/bin/env python3
"""
    unpack_rezfile.py - Unpacks a 3DO BurgerLib2 REZFILE.
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

Simple BurgerLib2 REZFILE unpacker.

Usage:
    python3 unpack_rezfile_simple.py REZFILE
    python3 unpack_rezfile_simple.py REZFILE -o output_directory

The tool does one thing:
    REZFILE -> directory of individual resource payloads.

The filenames use the known DOOM 3DO resource names where available.

This was reverse engineered from examining the DOOM3DO CD Image which contained
the original REZFILE and cross checked with the original code.

Like all 3DO data files, the 3DO data is Big Endian.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import struct

MAGIC = b"BRGR"
HEADER_SIZE = 12
GROUP_SIZE = 12
ENTRY_SIZE = 12
OFFSET_FLAG = 0x80000000
OFFSET_MASK = 0x7FFFFFFF


# Exact DOOM 3DO resource naming used by the resource table.
# This covers all 473 resources.
TEXTURES = [
    "rBIGDOOR2",
    "rBIGDOOR6",
    "rBRNPOIS",
    "rBROWNGRN",
    "rBROWN1",
    "rCOMPSPAN",
    "rCOMPTALL",
    "rCRATE1",
    "rCRATELIT",
    "rCRATINY",
    "rDOOR1",
    "rDOOR3",
    "rDOORBLU",
    "rDOORRED",
    "rDOORSTOP",
    "rDOORTRAK",
    "rDOORYEL",
    "rEXITDOOR",
    "rEXITSIGN",
    "rGRAY5",
    "rGSTSATYR",
    "rLITE5",
    "rMARBFAC3",
    "rMETAL",
    "rMETAL1",
    "rNUKE24",
    "rPIPE2",
    "rPLAT1",
    "rSHAWN2",
    "rSKINEDGE",
    "rSKY1",
    "rSKY2",
    "rSKY3",
    "rSLADWALL",
    "rSP_DUDE4",
    "rSP_HOT1",
    "rSTEP6",
    "rSUPPORT2",
    "rSUPPORT3",
    "rSW1BRN1",
    "rSW1GARG",
    "rSW1GSTON",
    "rSW1HOT",
    "rSW1WOOD",
    "rSW2BRN1",
    "rSW2GARG",
    "rSW2GSTON",
    "rSW2HOT",
    "rSW2WOOD",
    "rBRICK01",
    "rBRICK02",
    "rBRICK03",
    "rDFACE01",
    "rMARBLE01",
    "rMARBLE02",
    "rMARBLE03",
    "rMARBLE04",
    "rWOOD01",
    "rASH01",
    "rCEMENT01",
    "rCBLUE01",
    "rTECH01",
    "rTECH02",
    "rTECH03",
    "rTECH04",
    "rSKIN01",
    "rSKIN02",
    "rSKIN03",
    "rSKULLS01",
    "rSTWAR01",
    "rSTWAR02",
    "rCOMTAL02",
    "rSW1STAR",
    "rSW2STAR",
]

FLATS = [
    "rFLAT14",
    "rFLAT23",
    "rFLAT5_2",
    "rFLAT5_4",
    "rNUKAGE1",
    "rNUKAGE2",
    "rNUKAGE3",
    "rFLOOR0_1",
    "rFLOOR0_3",
    "rFLOOR0_6",
    "rFLOOR3_3",
    "rFLOOR4_6",
    "rFLOOR4_8",
    "rFLOOR5_4",
    "rSTEP1",
    "rSTEP2",
    "rFLOOR6_1",
    "rFLOOR6_2",
    "rTLITE6_4",
    "rTLITE6_6",
    "rFLOOR7_1",
    "rFLOOR7_2",
    "rMFLR8_1",
    "rMFLR8_4",
    "rCEIL3_2",
    "rCEIL3_4",
    "rCEIL5_1",
    "rCRATOP1",
    "rCRATOP2",
    "rFLAT4",
    "rFLAT8",
    "rGATE3",
    "rGATE4",
    "rFWATER1",
    "rFWATER2",
    "rFWATER3",
    "rFWATER4",
    "rLAVA1",
    "rLAVA2",
    "rLAVA3",
    "rLAVA4",
    "rGRASS",
    "rROCKS",
]

FRONTEND = {
    125: "rTITLE",
    126: "rIDCREDITS",
    127: "rCREDITS",
    128: "rLOGCREDITS",
    129: "rBACKGRND",
    130: "rBACKGRNDBROWN",
    131: "rCHARSET",
    132: "rPAUSED",
    133: "rLOADING",
    134: "rBIGNUMB",
    135: "rINTERMIS",
    136: "rSTBAR",
    137: "rSBARSHP",
    138: "rFACES",
    139: "rSKULLS",
    140: "rMAINDOOM",
    141: "rMAINMENU",
    142: "rSLIDER",
    143: "rDEMO1",
    144: "rDEMO2",
}

SPRITES = [
    "rSPR_BIGFISTS",
    "rSPR_BIGPISTOL",
    "rSPR_BIGSHOTGUN",
    "rSPR_BIGCHAINGUN",
    "rSPR_BIGROCKET",
    "rSPR_BIGPLASMA",
    "rSPR_BIGBFG",
    "rSPR_BIGCHAINSAW",
    "rSPR_ZOMBIE",
    "rSPR_ZOMBIEBODY",
    "rSPR_SHOTGUY",
    "rSPR_IMP",
    "rSPR_DEMON",
    "rSPR_CACODEMON",
    "rSPR_CACODIE",
    "rSPR_CACOBDY",
    "rSPR_CACOBOLT",
    "rSPR_LOSTSOUL",
    "rSPR_BARON",
    "rSPR_BARONATK",
    "rSPR_BARONDIE",
    "rSPR_BARONBDY",
    "rSPR_BARONBLT",
    "rSPR_OURHERO",
    "rSPR_OURHEROBDY",
    "rSPR_BARREL",
    "rSPR_IFOG",
    "rSPR_FIRECAN",
    "rSPR_POOLBLOOD",
    "rSPR_CANDLE",
    "rSPR_CANDLEABRA",
    "rSPR_SHOTGUN",
    "rSPR_CHAINGUN",
    "rSPR_ROCKETLAUNCHER",
    "rSPR_CHAINSAW",
    "rSPR_CLIP",
    "rSPR_SHELLS",
    "rSPR_ROCKET",
    "rSPR_STIMPACK",
    "rSPR_MEDIKIT",
    "rSPR_GREENARMOR",
    "rSPR_BLUEARMOR",
    "rSPR_LIGHTCOLUMN",
    "rSPR_BACKPACK",
    "rSPR_BOXROCKETS",
    "rSPR_BOXAMMO",
    "rSPR_BOXSHELLS",
    "rSPR_TECHPILLAR",
    "rSPR_BLUEKEYCARD",
    "rSPR_YELLOWKEYCARD",
    "rSPR_REDKEYCARD",
    "rSPR_RADIATIONSUIT",
    "rSPR_IRGOGGLES",
    "rSPR_COMPUTERMAP",
    "rSPR_INVISIBILITY",
    "rSPR_HEALTHBONUS",
    "rSPR_SOULSPHERE",
    "rSPR_ARMORBONUS",
    "rSPR_BODYPOLE",
    "rSPR_FIVESKULLPOLE",
    "rSPR_TALLSKULLPOLE",
    "rSPR_SHORTGREENPILLAR",
    "rSPR_SHORTREDPILLAR",
    "rSPR_FLAMINGSKULLS",
    "rSPR_MEDDEADTREE",
    "rSPR_LARGESTAL",
    "rSPR_SMALLSTAL",
    "rSPR_BLUESKULLKEY",
    "rSPR_REDSKULLKEY",
    "rSPR_YELLOWSKULLKEY",
    "rSPR_PLASMARIFLE",
    "rSPR_BFG9000",
    "rSPR_CELL",
    "rSPR_BERZERKER",
    "rSPR_CELLPACK",
    "rSPR_HANGINGRUMP",
    "rSPR_REDTORCH",
    "rSPR_BLUETORCH",
    "rSPR_GREENTORCH",
    "rSPR_INVULNERABILITY",
    "rSPR_TELEFOG",
    "rSPR_BFS1",
    "rSPR_BFE1",
    "rSPR_BFE2",
    "rSPR_PLSS",
    "rSPR_PLSE",
    "rSPR_MISL",
    "rSPR_BLUD",
    "rSPR_PUFF",
]

MAP_LUMPS = [
    "THINGS",
    "LINEDEFS",
    "SIDEDEFS",
    "VERTEXES",
    "SEGS",
    "SSECTORS",
    "SECTORS",
    "NODES",
    "REJECT",
    "BLOCKMAP",
]


def brgr_resource_name(number: int) -> str:
    if number == 1:
        return "rTEXTURE1"
    if 2 <= number <= 75:
        return TEXTURES[number - 2]
    if 76 <= number <= 118:
        return FLATS[number - 76]
    if 119 <= number <= 124:
        return f"rBACKGROUNDMASK+{number - 119}"
    if 125 <= number <= 144:
        return FRONTEND[number]
    if 145 <= number <= 384:
        map_number = (number - 145) // 10 + 1
        lump = MAP_LUMPS[(number - 145) % 10]
        return f"rMAP{map_number:02d}_{lump}"
    if 385 <= number <= 473:
        return SPRITES[number - 385]
    return f"rRESOURCE_{number:04d}"


def brgr_read_u32_be(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def brgr_parse_rezfile(data: bytes):
    if len(data) < HEADER_SIZE or data[:4] != MAGIC:
        raise ValueError("Not a BurgerLib2 BRGR REZFILE")

    group_count = brgr_read_u32_be(data, 4)
    dictionary_size = brgr_read_u32_be(data, 8)

    dictionary_start = HEADER_SIZE
    dictionary_end = dictionary_start + dictionary_size

    if dictionary_end > len(data):
        raise ValueError("REZFILE dictionary extends beyond file")

    groups = []
    cursor = dictionary_start
    total_entries = 0

    for group_index in range(group_count):
        if cursor + GROUP_SIZE > dictionary_end:
            raise ValueError("Truncated group table")

        resource_type = brgr_read_u32_be(data, cursor)
        first_resource = brgr_read_u32_be(data, cursor + 4)
        count = brgr_read_u32_be(data, cursor + 8)

        if count == 0:
            raise ValueError(f"Group {group_index} has zero resources")

        groups.append((group_index, resource_type, first_resource, count))
        total_entries += count
        cursor += GROUP_SIZE

    entries = []

    for group_index, resource_type, first_resource, count in groups:
        for index_in_group in range(count):
            if cursor + ENTRY_SIZE > dictionary_end:
                raise ValueError("Truncated resource entry table")

            raw_offset = brgr_read_u32_be(data, cursor)
            size = brgr_read_u32_be(data, cursor + 4)
            reserved = brgr_read_u32_be(data, cursor + 8)

            offset = raw_offset & OFFSET_MASK
            number = first_resource + index_in_group

            if offset < dictionary_end:
                raise ValueError(
                    f"Resource {number}: payload offset 0x{offset:X} "
                    f"is inside the dictionary"
                )

            if offset + size > len(data):
                raise ValueError(f"Resource {number}: payload exceeds REZFILE")

            entries.append((number, resource_type, raw_offset, offset, size, reserved))
            cursor += ENTRY_SIZE

    if cursor != dictionary_end:
        raise ValueError(
            f"Dictionary parse mismatch: reached 0x{cursor:X}, "
            f"expected 0x{dictionary_end:X}"
        )

    if len(entries) != total_entries:
        raise ValueError("Resource count mismatch")

    return groups, entries, dictionary_end


def safe_filename(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_+.-]", "_", name)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Simply unpack a BurgerLib REZFILE into named resources."
    )
    parser.add_argument("rezfile", type=Path)
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="output directory (default: REZFILE.unpacked)",
    )
    args = parser.parse_args()

    source = args.rezfile.expanduser().resolve()
    if not source.is_file():
        raise SystemExit(f"REZFILE not found: {source}")

    output = (
        args.output.expanduser().resolve()
        if args.output
        else source.with_name(source.name + ".unpacked")
    )
    output.mkdir(parents=True, exist_ok=True)

    data = source.read_bytes()
    groups, entries, dictionary_end = brgr_parse_rezfile(data)

    # Keep one simple manifest.
    with (output / "manifest.txt").open("w", encoding="utf-8") as manifest:
        manifest.write("DOOM3DO REZFILE\n")
        manifest.write("================\n")
        manifest.write(f"Source: {source}\n")
        manifest.write(f"Resources: {len(entries)}\n")
        manifest.write(f"Dictionary end: 0x{dictionary_end:08X}\n\n")

        for number, resource_type, raw_offset, offset, size, reserved in entries:
            name = brgr_resource_name(number)
            filename = f"{number:04d}_{safe_filename(name)}"
            manifest.write(
                f"{number:04d}  {name:25s}  "
                f"type={resource_type}  size={size:8d}  "
                f"offset=0x{offset:08X}\n"
            )

            payload = data[offset : offset + size]
            (output / filename).write_bytes(payload)

    print(f"Unpacked {len(entries)} resources to:")
    print(f"  {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
