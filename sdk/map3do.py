"""
    map3do.py - Makes 3DO friendly maps for processing by 3DO SDK for BurgerDoom.
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
    ./map3do.py INPUT.WAD -o MAP01_3DO.wad
    ./map3do.py INPUT.WAD --all-maps -o PANDORA2_3DO.wad --nodebuilder ./doombsp3do
    ./map3do.py INPUT.WAD --map MAP02 -o MAP02_3DO.wad
    ./map3do.py INPUT.WAD -o MAP01_3DO.wad --nodebuilder ./doombsp3do
    ./map3do.py INPUT.WAD --report mapping.txt
    ./map3do.py INPUT.WAD --strict -o MAP01_3DO.wad

The output WAD remains in PC DOOM map format. Texture and thing numbers are
rewritten for DOOM3DO resource set. The 3DO nodebuilder
then converts into 3DO map resources and the renderer audit
refuses maps that exceed 3DO renderer limits (IT IS STRICT!)

Reverse engineered from the REZFILE, 3DO DOOM source code and from dissecting the 3DO MAPS.
Uses some code from the reverse engineered 3DO Nodebuilder.

BEWARE: THIS FILE IS MESSY, I tried to keep it clean but reverse engineering is hard enough
so this was hastily written over time to 'get it working'.  Black formatted it though and AI
was used for some comments and documentation blocks (because I hate documenting) as well as
inline guidance.
"""

import argparse
import difflib
import json
import os
import re
import random
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

# IDs of WALLS
WALL_RESOURCES = (
    (2, "BIGDOOR2"), (3, "BIGDOOR6"), (4, "BRNPOIS"), (5, "BROWNGRN"),
    (6, "BROWN1"), (7, "COMPSPAN"), (8, "COMPTALL"), (9, "CRATE1"),
    (10, "CRATELIT"), (11, "CRATINY"), (12, "DOOR1"), (13, "DOOR3"),
    (14, "DOORBLU"), (15, "DOORRED"), (16, "DOORSTOP"), (17, "DOORTRAK"),
    (18, "DOORYEL"), (19, "EXITDOOR"), (20, "EXITSIGN"), (21, "GRAY5"),
    (22, "GSTSATYR"), (23, "LITE5"), (24, "MARBFAC3"), (25, "METAL"),
    (26, "METAL1"), (27, "NUKE24"), (28, "PIPE2"), (29, "PLAT1"),
    (30, "SHAWN2"), (31, "SKINEDGE"), (32, "SKY1"), (33, "SKY2"),
    (34, "SKY3"), (35, "SLADWALL"), (36, "SP_DUDE4"), (37, "SP_HOT1"),
    (38, "STEP6"), (39, "SUPPORT2"), (40, "SUPPORT3"), (41, "SW1BRN1"),
    (42, "SW1GARG"), (43, "SW1GSTON"), (44, "SW1HOT"), (45, "SW1WOOD"),
    (46, "SW2BRN1"), (47, "SW2GARG"), (48, "SW2GSTON"), (49, "SW2HOT"),
    (50, "SW2WOOD"), (51, "BRICK01"), (52, "BRICK02"), (53, "BRICK03"),
    (54, "DFACE01"), (55, "MARBLE01"), (56, "MARBLE02"), (57, "MARBLE03"),
    (58, "MARBLE04"), (59, "WOOD01"), (60, "ASH01"), (61, "CEMENT01"),
    (62, "CBLUE01"), (63, "TECH01"), (64, "TECH02"), (65, "TECH03"),
    (66, "TECH04"), (67, "SKIN01"), (68, "SKIN02"), (69, "SKIN03"),
    (70, "SKULLS01"), (71, "STWAR01"), (72, "STWAR02"), (73, "COMTAL02"),
    (74, "SW1STAR"), (75, "SW2STAR"),
)

# IDs of FLATS
FLAT_RESOURCES = (
    (76, "FLAT14"), (77, "FLAT23"), (78, "FLAT5_2"), (79, "FLAT5_4"),
    (80, "NUKAGE1"), (81, "NUKAGE2"), (82, "NUKAGE3"), (83, "FLOOR0_1"),
    (84, "FLOOR0_3"), (85, "FLOOR0_6"), (86, "FLOOR3_3"), (87, "FLOOR4_6"),
    (88, "FLOOR4_8"), (89, "FLOOR5_4"), (90, "STEP1"), (91, "STEP2"),
    (92, "FLOOR6_1"), (93, "FLOOR6_2"), (94, "TLITE6_4"), (95, "TLITE6_6"),
    (96, "FLOOR7_1"), (97, "FLOOR7_2"), (98, "MFLR8_1"), (99, "MFLR8_4"),
    (100, "CEIL3_2"), (101, "CEIL3_4"), (102, "CEIL5_1"), (103, "CRATOP1"),
    (104, "CRATOP2"), (105, "FLAT4"), (106, "FLAT8"), (107, "GATE3"),
    (108, "GATE4"), (109, "FWATER1"), (110, "FWATER2"), (111, "FWATER3"),
    (112, "FWATER4"), (113, "LAVA1"), (114, "LAVA2"), (115, "LAVA3"),
    (116, "LAVA4"), (117, "GRASS"), (118, "ROCKS"),
)

# IDs of THINGS (PC DOOM)
THING_NAMES = {
    1:"player 1 start", 2:"player 2 start", 3:"player 3 start", 4:"player 4 start",
    5:"blue keycard", 6:"yellow keycard", 7:"spider mastermind", 8:"backpack",
    9:"shotgun guy", 10:"dead player", 11:"deathmatch start", 12:"dead zombieman",
    13:"red keycard", 14:"teleport", 15:"dead player", 16:"cyberdemon",
    17:"dead lost soul", 18:"dead zombieman", 19:"dead shotgun guy", 20:"dead imp",
    21:"dead demon", 22:"dead cacodemon", 23:"dead lost soul", 24:"pool of blood and flesh",
    25:"impaled human", 26:"twitching impaled human", 27:"skull on pole", 28:"five skulls",
    29:"skulls and candles", 30:"tall green pillar", 31:"short green pillar",
    32:"tall red pillar", 33:"short red pillar", 34:"candle", 35:"candelabra",
    36:"short green heart pillar", 37:"short red skull pillar", 38:"red skull key",
    39:"yellow skull key", 40:"blue skull key", 41:"evil eye", 42:"floating skull",
    43:"burnt tree", 44:"tall blue firestick", 45:"tall green firestick",
    46:"tall red firestick", 47:"stalagmite", 48:"techno pillar", 49:"hanging gor1",
    50:"hanging gor2", 51:"hanging gor3", 52:"hanging gor4", 53:"hanging gor5",
    54:"large brown tree", 55:"short blue firestick", 56:"short green firestick",
    57:"short red firestick", 58:"spectre", 59:"hanging gor2 ceiling",
    60:"hanging gor4 ceiling", 61:"hanging gor3 ceiling", 62:"hanging gor5 ceiling",
    63:"hanging gor1 ceiling", 64:"arch-vile", 65:"heavy weapon dude", 66:"revenant",
    67:"mancubus", 68:"arachnotron", 69:"hell knight", 70:"burning barrel",
    71:"pain elemental", 72:"commander keen", 73:"hanging hdb1", 74:"hanging hdb2",
    75:"hanging hdb3", 76:"hanging hdb4", 77:"hanging hdb5", 78:"hanging hdb6",
    79:"pool of blood", 80:"pool of blood", 81:"pool of brains", 82:"super shotgun",
    83:"megasphere", 84:"wolfenstein ss", 85:"tall techno lamp", 86:"short techno lamp",
    87:"spawn spot", 88:"boss brain", 89:"spawn shooter",
    2001:"shotgun", 2002:"chaingun", 2003:"rocket launcher", 2004:"plasma rifle",
    2005:"chainsaw", 2006:"bfg 9000", 2007:"clip", 2008:"shells", 2010:"rocket",
    2011:"stimpack", 2012:"medikit", 2013:"soul sphere", 2014:"health bonus",
    2015:"armor bonus", 2018:"green armor", 2019:"blue armor", 2022:"invulnerability",
    2023:"berserk", 2024:"invisibility", 2025:"radiation suit", 2026:"computer map",
    2028:"floor lamp", 2035:"barrel", 2045:"light amplification visor",
    2046:"box of rockets", 2047:"cell charge", 2048:"box of ammo", 2049:"box of shells",
    3001:"imp", 3002:"demon", 3003:"baron", 3004:"zombieman", 3005:"cacodemon", 3006:"lost soul",
}

SUPPORTED_CROSS_SPECIALS = {
    2, 3, 4, 5, 6, 8, 10, 12, 13, 16, 17, 19, 22, 25, 30, 35, 36,
    37, 38, 39, 40, 44, 52, 53, 54, 56, 57, 58, 59, 72, 73, 74, 75,
    76, 77, 79, 80, 81, 82, 83, 84, 86, 87, 88, 89, 90, 91, 92, 93,
    94, 95, 96, 97, 98, 104,
}

SUPPORTED_USE_SPECIALS = {
    1, 7, 9, 11, 14, 15, 18, 20, 21, 23, 26, 27, 28, 29, 31, 32, 33,
    34, 41, 42, 43, 45, 49, 50, 51, 61, 62, 63, 64, 65, 66, 67, 68, 69,
    70, 71, 99, 100, 101, 102, 103, 105, 106, 107, 108,
}

SUPPORTED_SHOOT_SPECIALS = {24, 46, 47}
SUPPORTED_LINE_SPECIALS = SUPPORTED_CROSS_SPECIALS | SUPPORTED_USE_SPECIALS | SUPPORTED_SHOOT_SPECIALS | {48}
SUPPORTED_SECTOR_SPECIALS = {1, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13, 14, 16}

LINE_SPECIAL_REPLACEMENTS = {
    78: 104,
    85: 83,
    109: 4,
    110: 3,
    111: 29,
    112: 103,
    113: 50,
    114: 63,
    115: 61,
    116: 42,
    117: 1,
    118: 31,
    119: 22,
    120: 88,
    121: 10,
    122: 62,
    123: 68,
    124: 51,
    125: 39,
    126: 97,
    127: 7,
    128: 95,
    129: 91,
    130: 5,
    131: 101,
    132: 69,
    133: 106,
    134: 100,
    135: 107,
    136: 105,
    137: 108,
    138: 81,
    139: 79,
    140: 14,
    141: 44,
}

SECTOR_SPECIAL_REPLACEMENTS = {
    6: 5,
    11: 9,
    15: 0,
}

# PC DOOM to 3DO DOOM THING REPLACEMENTS (3DO DOOM HAS LESS)
#
# Keep these mappings explicit.  A PC DoomEd number must never be silently
# passed through when it has no valid 3DO meaning.  Mappings below are either
# exact 3DO equivalents or deliberate gameplay substitutions.
THING_REPLACEMENTS = {
    7:3003, 12:18, 15:12, 16:3003, 64:3003, 65:9, 66:3006, 67:3003,
    68:3003, 69:3003, 71:3005, 79:24, 80:24, 82:2001, 83:2013, 84:3004,
}

THING_REMOVALS = {
    17, 23, 26, 29, 30, 32, 36, 37, 41, 44, 45, 46, 49, 50, 51, 52, 54,
    59, 60, 61, 63, 72, 73, 74, 75, 76, 77, 78, 81, 85, 86, 87, 88, 89,
}

THING_3DO_EXACT = {
    1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 18, 19, 20, 21, 22, 24,
    25, 27, 28, 31, 33, 34, 35, 38, 39, 40, 42, 43, 47, 48, 53, 55, 56,
    57, 58, 62, 70, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2010, 2011,
    2012, 2013, 2014, 2015, 2018, 2019, 2022, 2023, 2024, 2025, 2026, 2028,
    2035, 2045, 2046, 2047, 2048, 2049, 3001, 3002, 3003, 3004, 3005, 3006,
}

@dataclass
class Lump:
    name: str
    data: bytes

@dataclass
class Mapping:
    kind: str
    source: str
    target: str
    resource_id: int | None
    exact: bool
    score: float

def read_wad(path):
    data = Path(path).read_bytes()
    if len(data) < 12:
        raise SystemExit("input is not a valid WAD")
    magic, count, directory_offset = struct.unpack_from("<4sII", data, 0)
    if magic not in (b"IWAD", b"PWAD"):
        raise SystemExit("input is not an IWAD or PWAD")
    lumps = []
    for index in range(count):
        offset = directory_offset + index * 16
        if offset + 16 > len(data):
            raise SystemExit("WAD directory is truncated")
        start, size = struct.unpack_from("<II", data, offset)
        name = data[offset + 8:offset + 16].rstrip(b"\0").decode("ascii", "replace").upper()
        if start + size > len(data):
            raise SystemExit(f"WAD lump {name} is outside the file")
        lumps.append(Lump(name, data[start:start + size]))
    return magic, lumps

MAP_RAW_LUMPS = ["THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SECTORS"]
MAP_DERIVED_LUMPS = ["SEGS", "SSECTORS", "NODES", "REJECT", "BLOCKMAP"]
MAP_LUMP_ORDER = MAP_RAW_LUMPS + ["SEGS", "SSECTORS", "NODES", "REJECT", "BLOCKMAP"]
MAP_MARKER_RE = re.compile(r"(?:MAP\d\d|E\dM\d)$", re.I)

def find_map_lumps(lumps, map_name):
    names = [l.name for l in lumps]
    map_name = map_name.upper()
    try:
        marker = names.index(map_name)
    except ValueError:
        raise SystemExit(f"map marker {map_name} not found")

    # A map ends at the next map marker.
    map_end = len(lumps)
    for index in range(marker + 1, len(lumps)):
        if MAP_MARKER_RE.fullmatch(names[index]):
            map_end = index
            break

    result = {}
    for name in MAP_RAW_LUMPS:
        try:
            index = names.index(name, marker + 1, map_end)
        except ValueError:
            raise SystemExit(f"{map_name}: missing {name}")
        result[name] = index

    # SEGS/SSECTORS/NODES/REJECT/BLOCKMAP are optional. When a
    # 3DO nodebuilder is supplied they are regenerated.
    for name in MAP_DERIVED_LUMPS:
        try:
            index = names.index(name, marker + 1, map_end)
        except ValueError:
            continue
        result[name] = index

    return marker, result

# Canonical Names (So we know what is replaced)
def canon_name(value):
    if isinstance(value, (bytes, bytearray)):
        raw = bytes(value)
        raw = raw.split(b"\x00", 1)[0]
        return raw.decode("ascii", "replace").strip().upper()[:8]
    return value.split("\x00", 1)[0].strip().upper()[:8]

def normalize_name(value):
    return re.sub(r"[^A-Z0-9]", "", value.upper())

def token_set(value):
    return set(re.findall(r"[A-Z]+|\d+", value.upper()))

def lev_similarity(a, b):
    return difflib.SequenceMatcher(None, a, b).ratio()

# How close to a match we can get (choose the highest percentage amongst best options)
def name_score(source, target):
    source_n = normalize_name(source)
    target_n = normalize_name(target)
    if source_n == target_n:
        return 1000.0
    score = lev_similarity(source_n, target_n) * 100.0
    if source_n[:4] == target_n[:4]:
        score += 28.0
    if source_n[:3] == target_n[:3]:
        score += 12.0
    source_tokens = token_set(source)
    target_tokens = token_set(target)
    score += 8.0 * len(source_tokens & target_tokens)
    source_digits = "".join(ch for ch in source_n if ch.isdigit())
    target_digits = "".join(ch for ch in target_n if ch.isdigit())
    if source_digits and source_digits == target_digits:
        score += 14.0
    if any(part in target_n for part in ("DOOR", "SW", "BIGDOOR")) == any(part in source_n for part in ("DOOR", "SW", "BIGDOOR")):
        score += 5.0
    return score

# Hardcoded!!! The PC Doom texture set is mapped to the known 3DO
# equivalents below..
#
# SW2BRN1 is NOT a conversion target.  It is reserved for Rebecca Heineman's
# E3M8 tribute texture replacement (PANDORA 2).
PROTECTED_WALL_TEXTURES = {
    "SW2BRN1",
}

SKULL_SWITCH_MAP = {
    # State is preserved: SW1 sources become SW1 skull switches and SW2
    # sources become SW2 skull switches.  GARG/WOOD/BRN/STAR/etc. are all
    # replaced by an actual skull switch.
    "SW1BLUE": "SW1WOOD",
    "SW1BRCOM": "SW1WOOD",
    "SW1BRN1": "SW1WOOD",
    "SW1BRN2": "SW1WOOD",
    "SW1BRNGN": "SW1WOOD",
    "SW1BROWN": "SW1WOOD",
    "SW1CMT": "SW1WOOD",
    "SW1COMM": "SW1WOOD",
    "SW1COMP": "SW1WOOD",
    "SW1DIRT": "SW1WOOD",
    "SW1EXIT": "SW1WOOD",
    "SW1GRAY": "SW1WOOD",
    "SW1GRAY1": "SW1WOOD",
    "SW1GARG": "SW1WOOD",
    "SW1LION": "SW1WOOD",
    "SW1METAL": "SW1WOOD",
    "SW1PIPE": "SW1WOOD",
    "SW1SATYR": "SW1WOOD",
    "SW1SKIN": "SW1WOOD",
    "SW1SLAD": "SW1GSTON",
    "SW1STARG": "SW1WOOD",
    "SW1STON1": "SW1GSTON",
    "SW1STON2": "SW1GSTON",
    "SW1STONE": "SW1GSTON",
    "SW1STRTN": "SW1WOOD",
    "SW1STAR": "SW1WOOD",
    "SW2BLUE": "SW2WOOD",
    "SW2BRCOM": "SW2WOOD",
    "SW2BRN2": "SW2WOOD",
    "SW2BRNGN": "SW2WOOD",
    "SW2BROWN": "SW2WOOD",
    "SW2CMT": "SW2WOOD",
    "SW2COMM": "SW2WOOD",
    "SW2COMP": "SW2WOOD",
    "SW2DIRT": "SW2WOOD",
    "SW2EXIT": "SW2WOOD",
    "SW2GARG": "SW2WOOD",
    "SW2GRAY": "SW2WOOD",
    "SW2GRAY1": "SW2WOOD",
    "SW2LION": "SW2WOOD",
    "SW2METAL": "SW2WOOD",
    "SW2PIPE": "SW2WOOD",
    "SW2SATYR": "SW2WOOD",
    "SW2SKIN": "SW2WOOD",
    "SW2SLAD": "SW2WOOD",
    "SW2STARG": "SW2WOOD",
    "SW2STON1": "SW2GSTON",
    "SW2STON2": "SW2GSTON",
    "SW2STONE": "SW2GSTON",
    "SW2STRTN": "SW2WOOD",
    "SW2STAR": "SW2WOOD",
}

PC_WALL_TEXTURE_MAP = {

    # Architecture / doors
    "BIGDOOR1": "BIGDOOR2", "BIGDOOR3": "BIGDOOR2",
    "BIGDOOR4": "BIGDOOR2", "BIGDOOR5": "BIGDOOR2",
    "BIGDOOR7": "BIGDOOR2", "BRICK9": "BRICK01", "BRICK10": "BRICK01",
    "BRNPOIS2": "BRNPOIS", "BROVINE": "BROWNGRN", "BROVINE2": "BROWNGRN",
    "BROWN96": "BROWNGRN", "BROWN144": "BROWNGRN", "BROWNHUG": "CEMENT01",
    "BROWNPIP": "PIPE2", "BROWNWEL": "BROWN1", "CEMENT1": "GRAY5",
    "CEMENT2": "GRAY5", "CEMENT3": "CEMENT01", "CEMENT4": "COMPTALL",
    "CEMENT5": "GRAY5", "CEMENT6": "GRAY5", "CEMPOIS": "BRNPOIS",
    "COMP2": "COMTAL02", "COMPBLUE": "COMPSPAN", "COMPOHSO": "COMPSPAN",
    "COMPSTA1": "COMTAL02", "COMPSTA2": "COMTAL02", "COMPTILE": "COMPSPAN",
    "COMPUTE1": "COMTAL02", "COMPUTE2": "COMPTALL", "COMPUTE3": "COMPTALL",
    "COMPWERD": "COMPTALL", "CRATE2": "CRATELIT", "CRATWIDE": "CRATE1",
    "DOORBLU2": "DOORBLU", "DOORHI": "METAL", "DOORRED2": "DOORRED",
    "DOORYEL2": "DOORYEL", "EXITSTON": "EXITSIGN", "ICKDOOR1": "DOOR1",

    # Fire / coloured rock
    "FIREBLU": "CBLUE01", "FIREBLU1": "CBLUE01", "FIREBLU2": "CBLUE01",
    "FIRELAV2": "SP_HOT1", "FIRELAV3": "SP_HOT1", "FIRELAVA": "SP_HOT1",
    "FIREMAG1": "SP_HOT1", "FIREMAG2": "SP_HOT1", "FIREMAG3": "SP_HOT1",
    "FIREWALA": "SP_HOT1", "FIREWALB": "SP_HOT1", "FIREWALL": "SP_HOT1",
    "REDWALL": "SP_HOT1", "REDWALL1": "SP_HOT1", "ROCKRED1": "SP_HOT1",
    "ROCKRED2": "SP_HOT1", "ROCKRED3": "SP_HOT1",

    # Grey / stone / tech
    "GRAY1": "GRAY5", "GRAY2": "GRAY5", "GRAY4": "GRAY5", "GRAY7": "GRAY5",
    "GRAYBIG": "GRAY5", "GRAYDANG": "GRAY5", "GRAYPOIS": "GRAY5", "GRAYTALL": "GRAY5",
    "GRAYVINE": "BROWNGRN", "GSTFONT1": "GSTSATYR", "GSTFONT2": "GSTSATYR",
    "GSTFONT3": "GSTSATYR", "GSTGARG": "GSTSATYR", "GSTLION": "GSTSATYR",
    "GSTONE1": "SP_DUDE4", "GSTONE2": "SP_DUDE4", "GSTVINE1": "BROWNGRN",
    "GSTVINE2": "BROWNGRN", "ICKWALL1": "GRAY5", "ICKWALL2": "GRAY5",
    "ICKWALL3": "GRAY5", "ICKWALL4": "GRAY5", "ICKWALL5": "GRAY5",
    "ICKWALL6": "GRAY5", "ICKWALL7": "GRAY5", "LITE2": "LITE5", "LITE3": "LITE5",
    "LITE4": "LITE5", "LITE96": "LITE5", "LITEBLU1": "LITE5", "LITEBLU2": "LITE5",
    "LITEBLU3": "LITE5", "LITEBLU4": "LITE5", "LITEMET": "LITE5", "LITERED": "LITE5",

    # Marble / skull / skin
    "MARBFAC2": "MARBFAC3", "MARBFACE": "DFACE01", "MARBLE1": "MARBLE01",
    "MARBLE2": "MARBLE01", "MARBLE3": "MARBLE01", "MARBLOD1": "MARBLE01",
    "NUKEDGE1": "NUKE24", "NUKEPOIS": "BRNPOIS", "NUKESLAD": "SLADWALL",
    "PIPE1": "PIPE2", "PIPE4": "PIPE2", "PIPE6": "PIPE2", "PLANET1": "SKY1",
    "RROCK04": "ROCKS", "SHAWN1": "SHAWN2", "SHAWN3": "SHAWN2", "SKIN2": "SKINEDGE",
    "SKINBORD": "SKINEDGE", "SKINCUT": "SKINEDGE", "SKINFACE": "SKINEDGE",
    "SKINLOW": "SKINEDGE", "SKINMET1": "SKIN01", "SKINSCAB": "SKINEDGE",
    "SKINSYMB": "SKIN02", "SKINTEK1": "SKIN01", "SKINTEK2": "SKIN01",
    "SKSNAKE1": "MARBLE01", "SKSNAKE2": "MARBLE01", "SKSPINE1": "MARBLE01",
    "SKSPINE2": "MARBLE01", "SKULWALL": "SKULLS01", "SKULWAL3": "SKULLS01",
    "SLADPOIS": "BRNPOIS", "SLADRIP1": "SLADWALL", "SLADRIP3": "SLADWALL",
    "SLADSKUL": "SLADWALL", "SP_DUDE1": "MARBLE04", "SP_DUDE2": "MARBLE04",
    "SP_DUDE3": "MARBLE04", "SP_DUDE5": "MARBLE03", "SP_DUDE6": "MARBLE03",
    "SP_FACE1": "SKULLS01", "SP_ROCK1": "BROWNGRN", "SP_ROCK2": "BROWNGRN",

    # Startan / steps / stone / tech / wood
    "STARBR2": "STWAR02", "STARG1": "STWAR02", "STARG2": "STWAR02",
    "STARG3": "STWAR02", "STARGR1": "STWAR01", "STARGR2": "STWAR01",
    "STARTAN1": "STWAR01", "STARTAN2": "STWAR02", "STARTAN3": "STWAR02",
    "STEP1": "STEP6", "STEP2": "STEP6", "STEP3": "STEP6", "STEP4": "STEP6",
    "STEP5": "STEP6", "STEPLAD1": "STEP6", "STEPTOP": "STEP6", "STONE": "GRAY5",
    "STONE2": "SP_DUDE4", "STONE3": "SP_DUDE4", "STONGARG": "GSTSATYR",
    "STONPOIS": "BRNPOIS", "TEKWALL1": "COMPTALL", "TEKWALL2": "TECH02",
    "TEKWALL3": "TECH03", "TEKWALL4": "PIPE2", "TEKWALL5": "PIPE2",
    "WOOD1": "WOOD01", "WOOD3": "WOOD01", "WOOD4": "WOOD01", "WOOD5": "WOOD01",
    "WOODGARG": "SW2WOOD", "WOODSKUL": "WOOD01",

    # Known non-switch wall substitutions from the Jaguar/3DO conversion chart.
    "A-MOSBRI": "BRICK01", "ADEL_G03": "GRAY5", "ADEL_G04": "GRAY5",
    "ADEL_M07": "BRICK01", "ADEL_S66": "CBLUE01", "ASHWALL": "SKINEDGE",
}

# See-through mid-textures don't exist in DOOM 3DO, so replace with solid wall.
PC_MIDDLE_TEXTURE_MAP = {
    "MIDGRATE": "BROWN1",
    "MIDBRN1": "BROWN1",
    "MIDSPACE": "BROWN1",
    "MIDVINE1": "BROWNGRN",
    "MIDVINE2": "BROWNGRN",
    "BRNBIGC": "BROWN1", "BRNBIGL": "BROWN1", "BRNBIGR": "BROWN1",
    "BRNSMAL1": "BROWN1", "BRNSMAL2": "BROWN1", "BRNSMALC": "BROWN1",
    "BRNSMALL": "BROWN1", "BRNSMALR": "BROWN1",
}

WALL_TEXTURE_ALIASES = dict(PC_WALL_TEXTURE_MAP)
WALL_TEXTURE_ALIASES.update(PC_MIDDLE_TEXTURE_MAP)
_TEXTURE_GROUPS = {
    "DOOR": ("DOOR", "BIGDOOR", "EXITDOOR"), "SWITCH": ("SW1", "SW2"),
    "FIRE": ("FIRE", "HOT", "LAVA", "SP_HOT"), "BLUE": ("BLUE", "CBLUE"),
    "RED": ("RED", "CRED"), "YELLOW": ("YEL", "YELLOW"), "GREEN": ("GREEN", "GRN"),
    "BROWN": ("BROWN", "BRN"), "GRAY": ("GRAY", "GREY"), "MARBLE": ("MARBLE", "MARB"),
    "BRICK": ("BRICK",), "METAL": ("METAL",), "WOOD": ("WOOD",), "SKIN": ("SKIN",),
    "STONE": ("STONE",), "TECH": ("TECH",), "PIPE": ("PIPE",), "CRATE": ("CRATE",),
    "CRATE": ("CRATE",), "SKULL": ("SKULL",), "STAR": ("STAR",), "STEP": ("STEP",),
    "LITE": ("LITE",),
}

def _texture_groups(name):
    name = canon_name(name)
    return {group for group, tokens in _TEXTURE_GROUPS.items()
            if any(token in name for token in tokens)}

# Kept but no longer actively used by the algorithm.  Lazy to clean :P
def texture_score(source, target):
    score = name_score(source, target)
    shared = _texture_groups(source) & _texture_groups(target)
    score += 45.0 * len(shared)
    return score

# How to choose a replacement
def choose_resource(source, resources, aliases=()):
    candidates = list(resources)
    source_upper = canon_name(source)
    candidate_by_name = {name: (rid, name) for rid, name in candidates}
    for alias in aliases:
        alias_upper = canon_name(alias)
        if alias_upper in candidate_by_name:
            rid, name = candidate_by_name[alias_upper]
            return rid, name, False, 980.0

    if source_upper in PROTECTED_WALL_TEXTURES and source_upper in candidate_by_name:
        rid, name = candidate_by_name[source_upper]
        return rid, name, True, 1000.0

    switch_target = SKULL_SWITCH_MAP.get(source_upper)
    if switch_target and switch_target in candidate_by_name:
        rid, name = candidate_by_name[switch_target]
        return rid, name, False, 970.0

    if source_upper.startswith("SW1"):
        rid, name = candidate_by_name["SW1WOOD"]
        return rid, name, False, 965.0
    if source_upper.startswith("SW2"):
        rid, name = candidate_by_name["SW2WOOD"]
        return rid, name, False, 965.0

    explicit = WALL_TEXTURE_ALIASES.get(source_upper)
    if explicit and explicit in candidate_by_name:
        rid, name = candidate_by_name[explicit]
        return rid, name, False, 975.0

    # If an exact match don't touch
    if source_upper in candidate_by_name:
        rid, name = candidate_by_name[source_upper]
        return rid, name, True, 1000.0

    # If nothing matches, fallback to best candidate
    ranked = sorted(
        ((texture_score(source_upper, name), rid, name)
         for rid, name in candidates
         if name not in PROTECTED_WALL_TEXTURES),
        key=lambda item: (-item[0], item[2]),
    )
    if not ranked:
        raise SystemExit("retail 3DO wall resource table has no usable texture")

    best_score = ranked[0][0]
    if best_score > 0.0:
        pool = [item for item in ranked if item[0] >= best_score * 0.90]
    else:
        pool = ranked[:1]
    score, rid, name = random.choice(pool)
    return rid, name, False, score

def rewrite_name_field(field, target):
    value = target.encode("ascii", "strict")[:8]
    return value.ljust(8, b"\0")

def convert_texture_fields(side_data, sector_data, strict):
    mappings = []
    side_data = bytearray(side_data)
    resource_by_name = {name: rid for rid, name in WALL_RESOURCES}
    wall_map = {name: name for _, name in WALL_RESOURCES}
    flat_map = {name: name for _, name in FLAT_RESOURCES}
    wall_names = set(wall_map)
    flat_names = set(flat_map)

    for offset in range(0, len(side_data), 30):
        for field in (4, 12, 20):
            source = canon_name(side_data[offset + field:offset + field + 8])
            if not source or source == "-":
                continue
            rid, target, exact, score = choose_resource(source, WALL_RESOURCES)
            if strict and not exact:
                raise SystemExit(f"no exact 3DO wall texture for {source}")
            side_data[offset + field:offset + field + 8] = rewrite_name_field(field, target)
            mappings.append(Mapping("wall", source, target, rid, exact, score))

    sector_data = bytearray(sector_data)
    for offset in range(0, len(sector_data), 26):
        for field in (4, 12):
            source = canon_name(sector_data[offset + field:offset + field + 8])
            if not source:
                continue
            if source == "F_SKY1":
                mappings.append(Mapping("flat", source, "F_SKY1", None, True, 1000.0))
                continue
            rid, target, exact, score = choose_resource(source, FLAT_RESOURCES)
            if strict and not exact:
                raise SystemExit(f"no exact 3DO flat texture for {source}")
            sector_data[offset + field:offset + field + 8] = rewrite_name_field(field, target)
            mappings.append(Mapping("flat", source, target, rid, exact, score))
    return bytes(side_data), bytes(sector_data), mappings

def convert_things(data):
    data = bytearray(data)
    mappings = []
    if len(data) % 10:
        raise SystemExit("THINGS lump has invalid length")
    for offset in range(0, len(data), 10):
        thing_type = struct.unpack_from("<h", data, offset + 6)[0]

        if thing_type in THING_REMOVALS:
            mappings.append((thing_type, 0))
            continue

        if thing_type in THING_REPLACEMENTS:
            target = THING_REPLACEMENTS[thing_type]
            if target != thing_type:
                data[offset + 6:offset + 8] = struct.pack("<h", target)
                mappings.append((thing_type, target))
            continue

        if thing_type in THING_3DO_EXACT:
            continue

        if thing_type in THING_NAMES:
            raise SystemExit(f"no 3DO mapping for PC DoomEd thing {thing_type} ({THING_NAMES[thing_type]})")

        raise SystemExit(f"unknown PC DoomEd thing {thing_type}")

    if mappings:
        kept = bytearray()
        for offset in range(0, len(data), 10):
            thing_type = struct.unpack_from("<h", data, offset + 6)[0]
            if thing_type in THING_REMOVALS:
                continue
            kept.extend(data[offset:offset + 10])
        data = kept

    return bytes(data), mappings

def convert_linedefs(data, strict):
    data = bytearray(data)
    mappings = []
    if len(data) % 14:
        raise SystemExit("LINEDEFS lump has invalid length")
    for offset in range(0, len(data), 14):
        flags = struct.unpack_from("<H", data, offset + 4)[0]
        special = struct.unpack_from("<H", data, offset + 6)[0]
        back_sidedef = struct.unpack_from("<h", data, offset + 12)[0]

        # A line without a back sidedef is one-sided.
        if back_sidedef < 0 and (flags & 0x0004):
            flags &= ~0x0004
            struct.pack_into("<H", data, offset + 4, flags)

        if special == 0 or special in SUPPORTED_LINE_SPECIALS:
            continue

        target = LINE_SPECIAL_REPLACEMENTS.get(special, 0)
        if strict and special != 0:
            raise SystemExit(f"unsupported 3DO linedef special {special}")
        struct.pack_into("<H", data, offset + 6, target)
        mappings.append((special, target))
    return bytes(data), mappings

def convert_sector_specials(data, strict):
    data = bytearray(data)
    mappings = []
    if len(data) % 26:
        raise SystemExit("SECTORS lump has invalid length")
    for offset in range(0, len(data), 26):
        special = struct.unpack_from("<H", data, offset + 22)[0]

        if special == 0 or special in SUPPORTED_SECTOR_SPECIALS:
            continue

        target = SECTOR_SPECIAL_REPLACEMENTS.get(special, 0)
        if strict:
            raise SystemExit(f"unsupported 3DO sector special {special}")
        struct.pack_into("<H", data, offset + 22, target)
        mappings.append((special, target))
    return bytes(data), mappings

def write_wad(path, magic, lumps):
    directory = bytearray()
    payload = bytearray(magic + b"\0" * 8)
    offsets = []
    for lump in lumps:
        offsets.append((len(payload), len(lump.data)))
        payload.extend(lump.data)
    directory_offset = len(payload)
    for lump, (offset, size) in zip(lumps, offsets):
        directory.extend(struct.pack("<II8s", offset, size, lump.name.encode("ascii")[:8].ljust(8, b"\0")))
    payload.extend(directory)
    struct.pack_into("<II", payload, 4, len(lumps), directory_offset)
    Path(path).write_bytes(payload)

def dedupe_mappings(mappings):
    seen = set()
    out = []
    for item in mappings:
        key = (item.kind, item.source, item.target, item.resource_id, item.exact)
        if key not in seen:
            seen.add(key)
            out.append(item)
    return out

# Fancy reporting (helps with double checks)
def mapping_report(map_name, mappings, things, line_specials, sector_specials, input_path, output_path):
    lines = [
        f"DOOM 3DO MAP CONVERTER",
        f"Input:  {input_path}",
        f"Output: {output_path}",
        f"Map:    {map_name}",
        "",
        "TEXTURE MAPPING",
        "---------------",
    ]
    for item in sorted(dedupe_mappings(mappings), key=lambda x: (x.kind, x.source, x.target)):
        rid = "-" if item.resource_id is None else str(item.resource_id)
        status = "exact" if item.exact else f"fallback score={item.score:.1f}"
        lines.append(f"{item.kind:4s}  {item.source:8s} -> {item.target:8s}  resource={rid:>3s}  {status}")
    lines.extend(["", "THING MAPPING", "-------------"])
    for source, target in things:
        lines.append(f"{source:5d} {THING_NAMES.get(source, 'unknown'):24s} -> {target:5d} {THING_NAMES.get(target, 'unknown')}")
    lines.extend(["", "LINE SPECIAL MAPPING", "--------------------"])
    for source, target in sorted(set(line_specials)):
        lines.append(f"{source:5d} -> {target:5d}")
    lines.extend(["", "SECTOR SPECIAL MAPPING", "---------------------"])
    for source, target in sorted(set(sector_specials)):
        lines.append(f"{source:5d} -> {target:5d}")
    return "\n".join(lines) + "\n"

def parse_args():
    parser = argparse.ArgumentParser(description="Convert a PC DOOM map to DOOM 3DO")
    parser.add_argument("input", type=Path)
    parser.add_argument("-o", "--output", type=Path, required=True)
    parser.add_argument("--map", default=None)
    parser.add_argument("--all-maps", action="store_true",
                        help="process every MAPxx/E#M# map in the input WAD")
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--nodebuilder", type=Path)
    parser.add_argument("--nodebuilder-output", type=Path)
    parser.add_argument("--resource-wad", type=Path)
    return parser.parse_args()

# And you thought Vanilla was strict...
RENDERER_LIMITS = {
    "MAXWALLCMDS": 128,
    "MAXVISPLANES": 64,
    "MAXVISSPRITES": 128,
}

# basically copied from other SDK python files
def read_be32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def read_native_map(resources):
    def load(name):
        path = resources / name
        if not path.is_file():
            raise SystemExit(f"nodebuilder did not produce {name}")
        return path.read_bytes()

    raw_vertices = load("VERTEXES")
    if len(raw_vertices) % 8:
        raise SystemExit("VERTEXES resource has an invalid length")
    vertex_count = len(raw_vertices) // 8
    vertices = [
        (struct.unpack_from(">i", raw_vertices, offset)[0] / 65536.0,
         struct.unpack_from(">i", raw_vertices, offset + 4)[0] / 65536.0)
        for offset in range(0, len(raw_vertices), 8)
    ]

    raw_segs = load("SEGS")
    seg_count = read_be32(raw_segs, 0)
    segs = []
    for index in range(seg_count):
        offset = 4 + index * 24
        v1 = read_be32(raw_segs, offset)
        v2 = read_be32(raw_segs, offset + 4)
        linedef = read_be32(raw_segs, offset + 16)
        side = read_be32(raw_segs, offset + 20)
        segs.append((v1, v2, linedef, side))

    raw_ssectors = load("SSECTORS")
    subsector_count = read_be32(raw_ssectors, 0)
    subsectors = []
    for index in range(subsector_count):
        offset = 4 + index * 8
        count = read_be32(raw_ssectors, offset)
        first = read_be32(raw_ssectors, offset + 4)
        subsectors.append((count, first))

    raw_nodes = load("NODES")
    node_count = read_be32(raw_nodes, 0)
    nodes = []
    for index in range(node_count):
        offset = 4 + index * 56
        values = struct.unpack_from(">14I", raw_nodes, offset)
        x, y, dx, dy = (value / 65536.0 for value in values[:4])
        bboxes = []
        for base in (4, 8):
            b = values[base:base + 4]
            bboxes.append((b[0] / 65536.0, b[1] / 65536.0,
                           b[2] / 65536.0, b[3] / 65536.0))
        nodes.append((x, y, dx, dy, bboxes[0], bboxes[1], values[12], values[13]))

    return vertices, segs, subsectors, nodes


def point_on_side(x, y, node):
    nx, ny, dx, dy = node[:4]
    return 0 if dy * (x - nx) >= dx * (y - ny) else 1


def normalize_angle(angle):
    return angle % (2.0 * 3.141592653589793)


# Imports if needed
def angular_interval(x, y, p1, p2):
    a1 = normalize_angle(__import__("math").atan2(p1[1] - y, p1[0] - x))
    a2 = normalize_angle(__import__("math").atan2(p2[1] - y, p2[0] - x))
    delta = (a2 - a1) % (2.0 * 3.141592653589793)
    if delta > 3.141592653589793:
        a1, a2 = a2, a1
        delta = (a2 - a1) % (2.0 * 3.141592653589793)
    return a1, a2, delta


def project_segment(x, y, heading, p1, p2, width=280):
    import math
    a1, a2, span = angular_interval(x, y, p1, p2)
    view = normalize_angle(heading)
    rel1 = (a1 - view + math.pi) % (2.0 * math.pi) - math.pi
    rel2 = (a2 - view + math.pi) % (2.0 * math.pi) - math.pi
    if span >= math.pi:
        return None
    left = min(rel1, rel2)
    right = max(rel1, rel2)
    if left < -math.pi / 4.0 and right < -math.pi / 4.0:
        return None
    if left > math.pi / 4.0 and right > math.pi / 4.0:
        return None
    left = max(left, -math.pi / 4.0)
    right = min(right, math.pi / 4.0)
    if left >= right:
        return None
    center = width / 2.0
    scale = center
    sx1 = int(math.floor(center + math.tan(left) * scale))
    sx2 = int(math.ceil(center + math.tan(right) * scale))
    sx1 = max(0, min(width - 1, sx1))
    sx2 = max(0, min(width - 1, sx2))
    if sx1 >= sx2:
        return None
    return sx1, sx2 - 1


def bbox_visible(x, y, heading, bbox):
    import math
    xl, xh, yl, yh = bbox
    corners = ((xl, yh), (xh, yh), (xl, yl), (xh, yl))
    angles = []
    for cx, cy in corners:
        angles.append((math.atan2(cy - y, cx - x) - heading + math.pi) % (2.0 * math.pi) - math.pi)
    return any(-math.pi / 4.0 <= angle <= math.pi / 4.0 for angle in angles)


def renderer_wall_audit(resources, things_data, linedefs_data, sidedefs_data, sectors_data):
    vertices, segs, subsectors, nodes = read_native_map(resources)
    import math

    linedefs = []
    for offset in range(0, len(linedefs_data), 14):
        flags, special, tag, s1, s2 = struct.unpack_from("<hhhhh", linedefs_data, offset + 4)
        linedefs.append((flags, special, tag, s1, s2))

    sidedefs = []
    for offset in range(0, len(sidedefs_data), 30):
        sector = struct.unpack_from("<h", sidedefs_data, offset + 28)[0]
        top = canon_name(sidedefs_data[offset + 4:offset + 12])
        bottom = canon_name(sidedefs_data[offset + 12:offset + 20])
        mid = canon_name(sidedefs_data[offset + 20:offset + 28])
        sidedefs.append((top, bottom, mid, sector))

    sectors = []
    for offset in range(0, len(sectors_data), 26):
        floorh, ceilh, floorpic, ceilpic, light, special, tag = struct.unpack_from("<hh8s8shhh", sectors_data, offset)
        sectors.append((floorh, ceilh, canon_name(floorpic), canon_name(ceilpic), light, special, tag))

    def seg_info(seg):
        v1, v2, linedef, side = seg
        line = linedefs[linedef]
        sd = sidedefs[line[3] if side == 0 else line[4]]
        front = sd[3]
        back = None
        if line[4 if side == 0 else 3] >= 0:
            other = sidedefs[line[4 if side == 0 else 3]]
            back = other[3]
        return v1, v2, front, back, sd[2]

    leaves = []
    for count, first in subsectors:
        leaves.append(range(first, first + count))

    def traverse(viewx, viewy, heading):
        solid = [(-1, -1), (280, 280)]
        wall_count = 0
        plane_keys = set()

        def store(left, right, seg, front, back):
            nonlocal wall_count
            if left > right:
                return
            wall_count += 1
            sf = sectors[front]
            plane_keys.add((sf[0], sf[2], sf[4], "floor"))
            plane_keys.add((sf[1], sf[3], sf[4], "ceiling"))

        def clip_solid(left, right, seg, front, back):
            nonlocal solid
            new_ranges = []
            visible = [(left, right)]
            for a, b in solid:
                next_visible = []
                for l, r in visible:
                    if r < a or l > b:
                        next_visible.append((l, r))
                    else:
                        if l < a:
                            next_visible.append((l, a - 1))
                        if r > b:
                            next_visible.append((b + 1, r))
                visible = next_visible
            for l, r in visible:
                if l <= r:
                    store(l, r, seg, front, back)
            solid.extend([(left, right)])
            solid.sort()
            merged = []
            for a, b in solid:
                if not merged or a > merged[-1][1] + 1:
                    merged.append([a, b])
                else:
                    merged[-1][1] = max(merged[-1][1], b)
            solid = [tuple(v) for v in merged]

        def clip_pass(left, right, seg, front, back):
            visible = [(left, right)]
            for a, b in solid:
                next_visible = []
                for l, r in visible:
                    if r < a or l > b:
                        next_visible.append((l, r))
                    else:
                        if l < a:
                            next_visible.append((l, a - 1))
                        if r > b:
                            next_visible.append((b + 1, r))
                visible = next_visible
            for l, r in visible:
                if l <= r:
                    store(l, r, seg, front, back)

        def process_leaf(leaf_index):
            for seg_index in leaves[leaf_index]:
                seg = segs[seg_index]
                v1, v2, front, back, mid = seg_info(seg)
                projected = project_segment(viewx, viewy, heading, vertices[v1], vertices[v2])
                if projected is None or front < 0:
                    continue
                left, right = projected
                if back is None:
                    clip_solid(left, right, seg, front, back)
                    continue
                sf = sectors[front]
                sb = sectors[back]
                closed = sb[1] <= sf[0] or sb[0] >= sf[1]
                visible_window = (sb[1] != sf[1] or sb[0] != sf[0] or sb[3] != sf[3] or sb[2] != sf[2] or sb[4] != sf[4] or mid)
                if closed:
                    clip_solid(left, right, seg, front, back)
                elif visible_window:
                    clip_pass(left, right, seg, front, back)

        def recurse(child):
            if child & 0x8000:
                process_leaf(child & 0x7fff)
                return
            node = nodes[child]
            side = point_on_side(viewx, viewy, node)
            recurse(node[6 + side])
            other = 1 - side
            if bbox_visible(viewx, viewy, heading, node[4 + other]):
                recurse(node[6 + other])

        solid = [(-1, -1), (280, 280)]
        if nodes:
            recurse(len(nodes) - 1)
        else:
            process_leaf(0)
        return wall_count, len(plane_keys)

    player_positions = []
    for offset in range(0, len(things_data), 10):
        x, y, angle, thing_type, options = struct.unpack_from("<hhhhh", things_data, offset)
        if thing_type == 1:
            player_positions.append((x, y, math.radians(angle)))
    if not player_positions:
        raise SystemExit("map has no player 1 start")

    best_wall = (0, None)
    best_plane = (0, None)
    headings = [position[2] + math.radians(step) for position in player_positions for step in range(0, 360, 5)]
    positions = [(x, y) for x, y, _ in player_positions]
    samples = 0
    for x, y in positions:
        for heading in [base + math.radians(step) for _, _, base in player_positions for step in range(0, 360, 5)]:
            walls, planes = traverse(x, y, heading)
            samples += 1
            if walls > best_wall[0]:
                best_wall = (walls, (x, y, heading))
            if planes > best_plane[0]:
                best_plane = (planes, (x, y, heading))

    return {
        "samples": samples,
        "MAXWALLCMDS": best_wall[0],
        "MAXVISPLANES": best_plane[0],
        "wall_view": best_wall[1],
        "plane_view": best_plane[1],
        "seg_count": len(segs),
        "subsector_count": len(subsectors),
        "node_count": len(nodes),
    }



MAP_LIMITS = {
    "MAXSCREENWIDTH": (280, "configuration", False),
    "MAXSCREENHEIGHT": (160, "configuration", False),
    "MAXWALLCMDS": (128, "unsafe renderer array", True),
    "MAXVISPLANES": (64, "renderer array; entry 0 is reserved", True),
    "MAXVISSPRITES": (128, "renderer array; excess sprites are dropped", False),
    "MAXSEGS": (32, "unsafe solid-segment clipping array", True),
    "MAXOPENINGS": (280 * 64, "unsafe silhouette storage", True),
    "TEXTURELOADFLAGS": (100, "fixed setup table", True),
    "LIGHTTABLE": (256, "fixed lighting tables", True),
    "IDIVTABLE": (8192, "fixed reciprocal table", True),
    "DEATHMATCHSTARTS": (10, "extra starts are ignored by the original loader", False),
}


def read_pc_things(data):
    if len(data) % 10:
        raise SystemExit("THINGS lump has invalid length")
    result = []
    for offset in range(0, len(data), 10):
        x, y, angle, thing_type, options = struct.unpack_from("<hhhhh", data, offset)
        result.append((x, y, angle, thing_type, options))
    return result


def read_pc_geometry(linedefs_data, sidedefs_data, sectors_data):
    linedefs = []
    for offset in range(0, len(linedefs_data), 14):
        v1, v2, flags, special, tag, s1, s2 = struct.unpack_from("<hhhhhhh", linedefs_data, offset)
        linedefs.append((v1, v2, flags, special, tag, s1, s2))
    sidedefs = []
    for offset in range(0, len(sidedefs_data), 30):
        xoff, yoff = struct.unpack_from("<hh", sidedefs_data, offset)
        top = canon_name(sidedefs_data[offset + 4:offset + 12])
        bottom = canon_name(sidedefs_data[offset + 12:offset + 20])
        mid = canon_name(sidedefs_data[offset + 20:offset + 28])
        sector = struct.unpack_from("<h", sidedefs_data, offset + 28)[0]
        sidedefs.append((xoff, yoff, top, bottom, mid, sector))
    sectors = []
    for offset in range(0, len(sectors_data), 26):
        floorh, ceilh = struct.unpack_from("<hh", sectors_data, offset)
        floorpic = canon_name(sectors_data[offset + 4:offset + 12])
        ceilpic = canon_name(sectors_data[offset + 12:offset + 20])
        light, special, tag = struct.unpack_from("<hhh", sectors_data, offset + 20)
        sectors.append((floorh, ceilh, floorpic, ceilpic, light, special, tag))
    return linedefs, sidedefs, sectors


def static_limit_audit(things_data, linedefs_data, sidedefs_data, vertices_data, segs_data, subsectors_data, nodes_data, sectors_data, blockmap_data):
    failures = []
    warnings = []
    things = read_pc_things(things_data)
    linedefs, sidedefs, sectors = read_pc_geometry(linedefs_data, sidedefs_data, sectors_data)

    counts = {
        "vertices": len(vertices_data) // 4 if len(vertices_data) % 4 == 0 else -1,
        "linedefs": len(linedefs),
        "sidedefs": len(sidedefs),
        "sectors": len(sectors),
        "things": len(things),
        "segs": read_be32(segs_data, 0) if len(segs_data) >= 4 else -1,
        "subsectors": read_be32(subsectors_data, 0) if len(subsectors_data) >= 4 else -1,
        "nodes": read_be32(nodes_data, 0) if len(nodes_data) >= 4 else -1,
    }

    max_count_32 = 0xFFFFFFFF
    for name in ("linedefs", "sidedefs", "sectors", "subsectors"):
        value = counts[name]
        if value < 0 or value > max_count_32:
            failures.append(f"{name} count cannot be represented by the 3DO map loader")

    line_ref_fields = []
    for index, line in enumerate(linedefs):
        v1, v2, flags, special, tag, s1, s2 = line
        if v1 < 0 or v2 < 0 or v1 >= counts["vertices"] or v2 >= counts["vertices"]:
            failures.append(f"LINEDEFS[{index}] has an invalid vertex index")
        if s1 < 0 or s1 >= counts["sidedefs"]:
            failures.append(f"LINEDEFS[{index}] has an invalid front sidedef index")
        if s2 != -1 and (s2 < 0 or s2 >= counts["sidedefs"]):
            failures.append(f"LINEDEFS[{index}] has an invalid back sidedef index")
        line_ref_fields.append((s1, s2))

    for index, sidedef in enumerate(sidedefs):
        sector = sidedef[5]
        if sector < 0 or sector >= counts["sectors"]:
            failures.append(f"SIDEDEFS[{index}] has an invalid sector index")

    for index, sector in enumerate(sectors):
        light = sector[4]
        if light < 0 or light > 255:
            failures.append(f"SECTORS[{index}] light level {light} exceeds the 0..255 lighting table")

    sector_line_counts = [0] * len(sectors)
    for s1, s2 in line_ref_fields:
        sector_line_counts[sidedefs[s1][5]] += 1
        if s2 != -1:
            sector_line_counts[sidedefs[s2][5]] += 1
    grouped_line_entries = sum(sector_line_counts)
    if grouped_line_entries > 0xFFFFFFFF:
        failures.append("sector line-pointer storage exceeds the 3DO pointer count representation")

    deathmatch_starts = sum(1 for _, _, _, thing_type, _ in things if thing_type == 11)
    if deathmatch_starts > MAP_LIMITS["DEATHMATCHSTARTS"][0]:
        warnings.append(f"deathmatch starts {deathmatch_starts} exceed the 3DO stored capacity of {MAP_LIMITS['DEATHMATCHSTARTS'][0]}; extras are ignored")

    width = height = None
    if len(blockmap_data) >= 16:
        width = read_be32(blockmap_data, 8)
        height = read_be32(blockmap_data, 12)
        entries = width * height
        if width > 0xFFFFFFFF or height > 0xFFFFFFFF or entries > 0xFFFFFFFF:
            failures.append("BLOCKMAP dimensions exceed the native counter range")
        if width == 0 or height == 0:
            failures.append("BLOCKMAP has zero width or height")

    vertex_pairs = []
    for offset in range(0, len(vertices_data), 4):
        value = struct.unpack_from("<h", vertices_data, offset)[0]
        vertex_pairs.append(value)
    if vertex_pairs:
        xs = vertex_pairs[0::2]
        ys = vertex_pairs[1::2]
        xspan = max(xs) - min(xs)
        yspan = max(ys) - min(ys)
        idiv_limit = MAP_LIMITS["IDIVTABLE"][0] - 1
        if xspan > idiv_limit or yspan > idiv_limit:
            failures.append(f"map coordinate span exceeds the SlopeAngle reciprocal-table index envelope of {idiv_limit} map units")
    else:
        xspan = yspan = 0

    texture_count = len(WALL_RESOURCES)
    flat_count = len(FLAT_RESOURCES)
    texture_limit = MAP_LIMITS["TEXTURELOADFLAGS"][0]
    if texture_count > texture_limit or flat_count > texture_limit:
        failures.append(f"3DO texture/flat catalogue exceeds TextureLoadFlags capacity {texture_limit}")

    return {
        "counts": counts,
        "sector_line_entries": grouped_line_entries,
        "deathmatch_starts": deathmatch_starts,
        "blockmap_width": width,
        "blockmap_height": height,
        "xspan": xspan,
        "yspan": yspan,
        "texture_count": texture_count,
        "flat_count": flat_count,
        "failures": failures,
        "warnings": warnings,
    }


def add_visible_range(ranges, left, right):
    merged = []
    for a, b in sorted(ranges + [(left, right)]):
        if not merged or a > merged[-1][1] + 1:
            merged.append([a, b])
        else:
            merged[-1][1] = max(merged[-1][1], b)
    return [tuple(item) for item in merged]


def clip_solid_ranges(ranges, left, right):
    start = 0
    temp = left - 1
    while start < len(ranges) and ranges[start][1] < temp:
        start += 1
    stored = 0
    if start == len(ranges) or left < ranges[start][0]:
        if start == len(ranges) or right < ranges[start][0] - 1:
            stored = 1
            ranges.insert(start, (left, right))
            return ranges, stored
        if left < ranges[start][0]:
            stored = 1
            ranges[start] = (left, ranges[start][1])
    if right <= ranges[start][1]:
        return ranges, stored
    next_index = start + 1
    if next_index < len(ranges) and right >= ranges[next_index][0] - 1:
        while next_index < len(ranges):
            stored += 1
            if right <= ranges[next_index][1]:
                ranges[start] = (ranges[start][0], ranges[next_index][1])
                del ranges[start + 1:next_index + 1]
                return ranges, stored
            next_index += 1
            if next_index >= len(ranges) or right < ranges[next_index][0] - 1:
                break
    stored += 1
    ranges[start] = (ranges[start][0], right)
    return ranges, stored


def clip_pass_ranges(ranges, left, right):
    visible = [(left, right)]
    for a, b in ranges:
        next_visible = []
        for l, r in visible:
            if r < a or l > b:
                next_visible.append((l, r))
            else:
                if l < a:
                    next_visible.append((l, a - 1))
                if r > b:
                    next_visible.append((b + 1, r))
        visible = next_visible
    return visible


def renderer_limit_audit(resources, things_data, linedefs_data, sidedefs_data, sectors_data):
    vertices, segs, subsectors, nodes = read_native_map(resources)
    linedefs, sidedefs, sectors = read_pc_geometry(linedefs_data, sidedefs_data, sectors_data)
    things = read_pc_things(things_data)
    import math

    def seg_info(seg):
        v1, v2, linedef, side = seg
        line = linedefs[linedef]
        front_sd = sidedefs[line[5] if side == 0 else line[6]]
        back_sd = sidedefs[line[6] if side == 0 else line[5]] if (line[6 if side == 0 else 5] >= 0) else None
        return v1, v2, front_sd[5], back_sd[5] if back_sd else None

    leaves = [range(first, first + count) for count, first in subsectors]

    def traverse(viewx, viewy, heading):
        solid = [(-1, -1), (280, 280)]
        wall_count = 0
        plane_keys = set()
        max_solid = len(solid)
        opening_used = 0
        max_opening = 0

        def store(left, right, seg, front, back):
            nonlocal wall_count, max_opening
            if left > right:
                return
            wall_count += 1
            sf = sectors[front]
            plane_keys.add((sf[0], sf[2], sf[4], "floor"))
            plane_keys.add((sf[1], sf[3], sf[4], "ceiling"))
            if back is not None:
                sb = sectors[back]
                viewz = sectors[front][0] + 41
                ff = sf[0] - viewz
                fc = sf[1] - viewz
                bf = sb[0] - viewz
                bc = sb[1] - viewz
                if bf > 0 and bf > ff:
                    max_opening = max(max_opening, opening_used)
                elif ff < 0 and ff > bf:
                    max_opening = max(max_opening, opening_used)
                if bc <= 0 and bc < fc or fc > 0 and bc > fc:
                    max_opening = max(max_opening, opening_used)

        def wall_openings(left, right, seg, front, back, current_opening):
            if back is None:
                return current_opening
            sf = sectors[front]
            sb = sectors[back]
            viewz = sf[0] + 41
            ff = sf[0] - viewz
            fc = sf[1] - viewz
            bf = sb[0] - viewz
            bc = sb[1] - viewz
            if bf >= fc or bc <= ff:
                return current_opening
            width = right - left + 1
            if (bf > 0 and bf > ff) or (ff < 0 and ff > bf):
                current_opening += width
            if (sf[1] != -99999 and ((bc <= 0 and bc < fc) or (fc > 0 and bc > fc))):
                current_opening += width
            return current_opening

        def solid(left, right, seg, front, back):
            nonlocal max_solid, opening_used
            visible_ranges = clip_pass_ranges(solid_ranges, left, right)
            for l, r in visible_ranges:
                store(l, r, seg, front, back)
                opening_used = wall_openings(l, r, seg, front, back, opening_used)
            solid_ranges[:], _ = clip_solid_ranges(solid_ranges, left, right)
            max_solid = max(max_solid, len(solid_ranges))

        def pass_wall(left, right, seg, front, back):
            nonlocal opening_used
            for l, r in clip_pass_ranges(solid_ranges, left, right):
                store(l, r, seg, front, back)
                opening_used = wall_openings(l, r, seg, front, back, opening_used)

        def process_leaf(leaf_index):
            for seg_index in leaves[leaf_index]:
                v1, v2, linedef, side = segs[seg_index]
                try:
                    front, back = seg_info((v1, v2, linedef, side))[2:4]
                except IndexError:
                    continue
                p = project_segment(viewx, viewy, heading, vertices[v1], vertices[v2])
                if p is None or front is None:
                    continue
                left, right = p
                if back is None:
                    solid(left, right, seg_index, front, back)
                    continue
                sf = sectors[front]
                sb = sectors[back]
                closed = sb[1] <= sf[0] or sb[0] >= sf[1]
                visible = (sb[1] != sf[1] or sb[0] != sf[0] or sb[3] != sf[3] or sb[2] != sf[2] or sidedefs[linedefs[linedef][5 if side == 0 else 6]][4])
                if closed:
                    solid(left, right, seg_index, front, back)
                elif visible:
                    pass_wall(left, right, seg_index, front, back)

        def recurse(child):
            if child & 0x8000:
                process_leaf(child & 0x7fff)
                return
            if child >= len(nodes):
                return
            node = nodes[child]
            side = point_on_side(viewx, viewy, node)
            recurse(node[6 + side])
            other = 1 - side
            if bbox_visible(viewx, viewy, heading, node[4 + other]):
                recurse(node[6 + other])

        solid_ranges = [(-1, -1), (280, 280)]
        if nodes:
            recurse(len(nodes) - 1)
        elif leaves:
            process_leaf(0)

        visible_sprites = 0
        for x, y, angle, thing_type, options in things:
            if thing_type == 1:
                continue
            dx = x - viewx
            dy = y - viewy
            distance = math.hypot(dx, dy)
            if distance < 4:
                continue
            relative = (math.atan2(dy, dx) - heading + math.pi) % (2 * math.pi) - math.pi
            if abs(relative) <= math.pi / 4:
                visible_sprites += 1
        return wall_count, len(plane_keys), visible_sprites, max_solid, opening_used, max_opening

    player_positions = [(x, y, math.radians(angle)) for x, y, angle, thing_type, _ in things if thing_type == 1]
    if not player_positions:
        raise SystemExit("map has no player 1 start")
    candidate_positions = [(x, y, base, 5) for x, y, base in player_positions]
    candidate_positions.extend((x, y, 0.0, 45) for x, y, _, thing_type, _ in things if thing_type != 1)
    samples = 0
    best = {
        "MAXWALLCMDS": 0,
        "MAXVISPLANES": 0,
        "MAXVISSPRITES": 0,
        "MAXSEGS": 0,
        "MAXOPENINGS": 0,
    }
    best_views = {}
    for x, y, base_heading, step_size in candidate_positions:
        for step in range(0, 360, step_size):
            heading = base_heading + math.radians(step)
            metrics = traverse(x, y, heading)
            samples += 1
            names = ("MAXWALLCMDS", "MAXVISPLANES", "MAXVISSPRITES", "MAXSEGS", "MAXOPENINGS")
            for name, value in zip(names, metrics):
                if value > best[name]:
                    best[name] = value
                    best_views[name] = (x, y, heading)
    best["samples"] = samples
    best["views"] = best_views
    return best


def native_structure_audit(resources, things_data, linedefs_data, sidedefs_data, sectors_data):
    failures = []
    warnings = []

    def load(name):
        path = resources / name
        if not path.is_file():
            failures.append(f"missing native resource {name}")
            return b""
        return path.read_bytes()

    raw_vertices = load("VERTEXES")
    raw_lines = load("LINEDEFS")
    raw_sides = load("SIDEDEFS")
    raw_segs = load("SEGS")
    raw_subsectors = load("SSECTORS")
    raw_sectors = load("SECTORS")
    raw_nodes = load("NODES")
    raw_blockmap = load("BLOCKMAP")

    def count_and_size(raw, record_size, name):
        if len(raw) < 4:
            failures.append(f"{name} is shorter than its count field")
            return 0
        count = read_be32(raw, 0)
        expected = 4 + count * record_size
        if expected > len(raw):
            failures.append(f"{name} declares {count} records but is truncated")
        return count

    if len(raw_vertices) % 8:
        failures.append("VERTEXES length is not a multiple of 8")
    vertex_count = len(raw_vertices) // 8
    line_count = count_and_size(raw_lines, 28, "LINEDEFS")
    side_count = count_and_size(raw_sides, 24, "SIDEDEFS")
    seg_count = count_and_size(raw_segs, 24, "SEGS")
    subsector_count = count_and_size(raw_subsectors, 8, "SSECTORS")
    sector_count = count_and_size(raw_sectors, 28, "SECTORS")
    node_count = count_and_size(raw_nodes, 56, "NODES")

    if subsector_count > 0x8000:
        failures.append(f"SSECTORS count {subsector_count} exceeds the 15-bit subsector child encoding")
    if node_count > 0x8000:
        failures.append(f"NODES count {node_count} exceeds the 15-bit node child encoding")

    for index in range(line_count):
        off = 4 + index * 28
        v1, v2 = read_be32(raw_lines, off), read_be32(raw_lines, off + 4)
        s1, s2 = read_be32(raw_lines, off + 20), read_be32(raw_lines, off + 24)
        if v1 >= vertex_count or v2 >= vertex_count:
            failures.append(f"LINEDEFS[{index}] references a vertex outside VERTEXES")
        if s1 >= side_count:
            failures.append(f"LINEDEFS[{index}] references an invalid front SIDEDEFS index")
        if s2 != 0xFFFFFFFF and s2 >= side_count:
            failures.append(f"LINEDEFS[{index}] references an invalid back SIDEDEFS index")

    for index in range(side_count):
        off = 4 + index * 24
        sector = read_be32(raw_sides, off + 20)
        if sector >= sector_count:
            failures.append(f"SIDEDEFS[{index}] references an invalid SECTORS index")

    for index in range(seg_count):
        off = 4 + index * 24
        v1, v2 = read_be32(raw_segs, off), read_be32(raw_segs, off + 4)
        line, side = read_be32(raw_segs, off + 16), read_be32(raw_segs, off + 20)
        if v1 >= vertex_count or v2 >= vertex_count:
            failures.append(f"SEGS[{index}] references an invalid vertex")
        if line >= line_count:
            failures.append(f"SEGS[{index}] references an invalid linedef")
        if side > 1:
            failures.append(f"SEGS[{index}] has an invalid side value {side}")

    for index in range(subsector_count):
        off = 4 + index * 8
        count = read_be32(raw_subsectors, off)
        first = read_be32(raw_subsectors, off + 4)
        if count == 0:
            failures.append(f"SSECTORS[{index}] contains zero segs")
        if first + count > seg_count:
            failures.append(f"SSECTORS[{index}] extends beyond SEGS")

    for index in range(node_count):
        off = 4 + index * 56
        for child_offset in (off + 48, off + 52):
            child = read_be32(raw_nodes, child_offset)
            if child & 0x8000:
                child_index = child & 0x7FFF
                if child_index >= subsector_count:
                    failures.append(f"NODES[{index}] references an invalid subsector child")
            elif child >= node_count:
                failures.append(f"NODES[{index}] references an invalid node child")

    if len(raw_blockmap) >= 16:
        width = read_be32(raw_blockmap, 8)
        height = read_be32(raw_blockmap, 12)
        entries = width * height
        if width == 0 or height == 0:
            failures.append("BLOCKMAP has zero dimensions")
        if entries > 0x3FFFFFFF:
            failures.append(f"BLOCKMAP cell count {entries} is unreasonably large")
        header_and_offsets = 16 + entries * 4
        if header_and_offsets > len(raw_blockmap):
            failures.append("BLOCKMAP cell offset table is truncated")
        else:
            list_words = (len(raw_blockmap) - header_and_offsets) // 4
            list_start = 4 + entries
            for index in range(entries):
                offset = read_be32(raw_blockmap, 16 + index * 4)
                if offset % 4:
                    failures.append(f"BLOCKMAP[{index}] has an unaligned line-list offset")
                    continue
                word_index = offset // 4
                if word_index < list_start or word_index >= 4 + entries + list_words:
                    failures.append(f"BLOCKMAP[{index}] points outside line-list storage")
                    continue
                seen = 0
                pos = word_index
                while pos < 4 + entries + list_words and seen <= line_count + 1:
                    raw_word = read_be32(raw_blockmap, pos * 4)
                    if raw_word == 0xFFFFFFFF:
                        break
                    if raw_word >= line_count:
                        failures.append(f"BLOCKMAP[{index}] references invalid linedef {raw_word}")
                        break
                    seen += 1
                    pos += 1
                if seen > line_count + 1:
                    failures.append(f"BLOCKMAP[{index}] line list is unterminated")

    if raw_vertices and len(raw_vertices) >= 4:
        coords = []
        for index in range(vertex_count):
            off = index * 8
            coords.append(struct.unpack_from(">i", raw_vertices, off)[0] >> 16)
            coords.append(struct.unpack_from(">i", raw_vertices, off + 4)[0] >> 16)
        if coords:
            xs = coords[0::2]
            ys = coords[1::2]
            xspan = max(xs) - min(xs)
            yspan = max(ys) - min(ys)
        else:
            xspan = yspan = 0
    else:
        xspan = yspan = 0

    idiv_limit = MAP_LIMITS["IDIVTABLE"][0] - 1
    if xspan > idiv_limit or yspan > idiv_limit:
        failures.append(f"map coordinate span {xspan}x{yspan} exceeds the reciprocal-table envelope of {idiv_limit} units")

    _, _, native_sectors = read_pc_geometry(linedefs_data, sidedefs_data, sectors_data)
    for index, sector in enumerate(native_sectors):
        light = sector[4]
        if not 0 <= light <= 255:
            failures.append(f"sector {index} light level {light} exceeds the 256-entry lighting tables")

    counts = {
        "vertices": vertex_count,
        "linedefs": line_count,
        "sidedefs": side_count,
        "segs": seg_count,
        "subsectors": subsector_count,
        "sectors": sector_count,
        "nodes": node_count,
        "things": len(things_data) // 10 if len(things_data) % 10 == 0 else -1,
    }
    deathmatch_count = sum(1 for item in read_pc_things(things_data) if item[3] == 11)
    if deathmatch_count > MAP_LIMITS["DEATHMATCHSTARTS"][0]:
        warnings.append(f"deathmatch starts {deathmatch_count} exceed the stored 3DO capacity of {MAP_LIMITS['DEATHMATCHSTARTS'][0]}")

    return {
        "counts": counts,
        "deathmatch_starts": deathmatch_count,
        "coordinate_span": (xspan, yspan),
        "texture_count": len(WALL_RESOURCES),
        "flat_count": len(FLAT_RESOURCES),
        "failures": failures,
        "warnings": warnings,
    }


def enforce_renderer_limits(resources, things, linedefs, sidedefs, sectors):
    static = native_structure_audit(resources, things, linedefs, sidedefs, sectors)
    runtime = renderer_limit_audit(resources, things, linedefs, sidedefs, sectors)
    print("3DO limit audit:")
    for key, value in static["counts"].items():
        print(f"  {key:12s} {value}")
    print(f"  Texture Flags {static['texture_count']:3d} / {MAP_LIMITS['TEXTURELOADFLAGS'][0]}")
    print(f"  Flat Flags    {static['flat_count']:3d} / {MAP_LIMITS['TEXTURELOADFLAGS'][0]}")
    print(f"  Light Table   256-entry / sector range checked")
    print(f"  IDiv Span     {static['coordinate_span'][0]} x {static['coordinate_span'][1]} / {MAP_LIMITS['IDIVTABLE'][0] - 1}")
    print(f"  MAXWALLCMDS   {runtime['MAXWALLCMDS']:3d} / {MAP_LIMITS['MAXWALLCMDS'][0]}")
    print(f"  MAXVISPLANES  {runtime['MAXVISPLANES']:3d} / {MAP_LIMITS['MAXVISPLANES'][0] - 1} usable")
    print(f"  MAXVISSPRITES {runtime['MAXVISSPRITES']:3d} / {MAP_LIMITS['MAXVISSPRITES'][0]} (excess dropped)")
    print(f"  MAXSEGS       {runtime['MAXSEGS']:3d} / {MAP_LIMITS['MAXSEGS'][0]}")
    print(f"  MAXOPENINGS   {runtime['MAXOPENINGS']:3d} / {MAP_LIMITS['MAXOPENINGS'][0]}")
    for warning in static["warnings"]:
        print(f"  warning: {warning}")
    failures = list(static["failures"])
    if runtime["MAXWALLCMDS"] > MAP_LIMITS["MAXWALLCMDS"][0]:
        failures.append(f"visible wall commands {runtime['MAXWALLCMDS']} exceed {MAP_LIMITS['MAXWALLCMDS'][0]}")
    if runtime["MAXVISPLANES"] > MAP_LIMITS["MAXVISPLANES"][0] - 1:
        failures.append(f"visible planes {runtime['MAXVISPLANES']} exceed the {MAP_LIMITS['MAXVISPLANES'][0] - 1} usable slots")
    if runtime["MAXSEGS"] > MAP_LIMITS["MAXSEGS"][0]:
        failures.append(f"solid clipping ranges {runtime['MAXSEGS']} exceed {MAP_LIMITS['MAXSEGS'][0]}")
    if runtime["MAXOPENINGS"] > MAP_LIMITS["MAXOPENINGS"][0]:
        failures.append(f"silhouette storage {runtime['MAXOPENINGS']} exceeds {MAP_LIMITS['MAXOPENINGS'][0]}")
    if runtime["MAXVISSPRITES"] > MAP_LIMITS["MAXVISSPRITES"][0]:
        print("  warning: visible sprite capacity exceeded; excess sprites are discarded")
    if failures:
        raise SystemExit("3DO compatibility failed: " + "; ".join(failures))

def write_resource_wad(output_path, resource_directory):
    names = ["THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS", "SSECTORS", "SECTORS", "NODES", "REJECT", "BLOCKMAP"]
    lumps = [Lump(name, (resource_directory / name).read_bytes()) for name in names]
    write_wad(output_path, b"PWAD", lumps)

def run_nodebuilder(executable, converted_wad, output_directory, resource_wad):
    output_directory.mkdir(parents=True, exist_ok=True)
    manifest_path = output_directory / "3do_rez_manifest.txt"
    lines = ["DOOM 3DO REZFILE", "================", "Source: builtin retail resource catalog", "Resources: 473"]
    for resource_id, name in WALL_RESOURCES:
        lines.append(f"{resource_id:04d}  r{name:<24s}  type=1  size=0  offset=0")
    for resource_id, name in FLAT_RESOURCES:
        lines.append(f"{resource_id:04d}  r{name:<24s}  type=1  size=0  offset=0")
    lines.append("0145  rMAP01_THINGS              type=1  size=0  offset=0")
    for resource_id, name in (
        (119, "BACKGROUNDMASK+0"), (120, "BACKGROUNDMASK+1"), (121, "BACKGROUNDMASK+2"),
        (122, "BACKGROUNDMASK+3"), (123, "BACKGROUNDMASK+4"), (124, "BACKGROUNDMASK+5"),
        (125, "TITLE"), (126, "IDCREDITS"), (127, "CREDITS"), (128, "LOGCREDITS"),
        (129, "BACKGRND"), (130, "BACKGRNDBROWN"), (131, "CHARSET"), (132, "PAUSED"),
        (133, "LOADING"), (134, "BIGNUMB"), (135, "INTERMIS"), (136, "STBAR"),
        (137, "SBARSHP"), (138, "FACES"), (139, "SKULLS"), (140, "MAINDOOM"),
        (141, "MAINMENU"), (142, "SLIDER"), (143, "DEMO1"), (144, "DEMO2"),
    ):
        lines.append(f"{resource_id:04d}  r{name:<24s}  type=1  size=0  offset=0")
    manifest_path.write_text("\n".join(lines) + "\n", encoding="ascii")
    command = [str(executable), str(converted_wad), str(output_directory), "--manifest", str(manifest_path)]
    result = subprocess.run(command, text=True, capture_output=True)
    if result.stdout:
        print(result.stdout, end="")
    if result.returncode:
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr)
        raise SystemExit(f"nodebuilder failed with exit code {result.returncode}")
    if resource_wad:
        write_resource_wad(resource_wad, output_directory)
        print(f"3DO resource WAD: {resource_wad}")


def map_names_in_wad(lumps):
    """Return every map marker in WAD order, rejecting incomplete map blocks."""
    names = [l.name for l in lumps]
    maps = []
    incomplete = []
    for marker, name in enumerate(names):
        if not MAP_MARKER_RE.fullmatch(name):
            continue
        end = len(names)
        for i in range(marker + 1, len(names)):
            if MAP_MARKER_RE.fullmatch(names[i]):
                end = i
                break
        missing = [req for req in MAP_RAW_LUMPS if req not in names[marker + 1:end]]
        if missing:
            incomplete.append(f"{name}: missing {', '.join(missing)}")
        else:
            maps.append(name)
    if incomplete:
        raise SystemExit("incomplete map block(s): " + "; ".join(incomplete))
    return maps


def convert_map(lumps, map_name, strict):
    """Convert one source map and return its marker plus owned source lumps."""
    marker, indexes = find_map_lumps(lumps, map_name)
    input_things = lumps[indexes["THINGS"]].data
    source_types = [struct.unpack_from("<h", input_things, offset + 6)[0]
                    for offset in range(0, len(input_things), 10)]
    converted_things, thing_changes = convert_things(input_things)
    converted_linedefs, line_special_changes = convert_linedefs(
        lumps[indexes["LINEDEFS"]].data, strict
    )
    converted_sides, converted_sectors, mappings = convert_texture_fields(
        lumps[indexes["SIDEDEFS"]].data,
        lumps[indexes["SECTORS"]].data,
        strict,
    )
    converted_sectors, sector_special_changes = convert_sector_specials(
        converted_sectors, strict
    )

    selected = [Lump(map_name, b"")]
    for name in MAP_RAW_LUMPS:
        lump = lumps[indexes[name]]
        if name == "THINGS":
            lump = Lump(name, converted_things)
        elif name == "LINEDEFS":
            lump = Lump(name, converted_linedefs)
        elif name == "SIDEDEFS":
            lump = Lump(name, converted_sides)
        elif name == "SECTORS":
            lump = Lump(name, converted_sectors)
        selected.append(lump)

    return {
        "map_name": map_name,
        "lumps": selected,
        "input_things": input_things,
        "source_linedefs": lumps[indexes["LINEDEFS"]].data,
        "source_sidedefs": lumps[indexes["SIDEDEFS"]].data,
        "source_sectors": lumps[indexes["SECTORS"]].data,
        "source_types": source_types,
        "thing_changes": thing_changes,
        "line_special_changes": line_special_changes,
        "sector_special_changes": sector_special_changes,
        "mappings": mappings,
    }


def print_map_summary(info):
    map_name = info["map_name"]
    print(f"Map: {map_name}")
    print(f"Things: {len(info['source_types'])}  replacements: {len(info['thing_changes'])}")
    exact = sum(1 for item in info["mappings"] if item.exact)
    fallback = len(info["mappings"]) - exact
    print(f"Texture fields: {len(info['mappings'])}  exact: {exact}  fallback: {fallback}")
    print(f"Linedef specials: {len(info['line_special_changes'])} replacements")
    print(f"Sector specials: {len(info['sector_special_changes'])} replacements")
    if info["thing_changes"]:
        counts = {}
        for source, target in info["thing_changes"]:
            counts[(source, target)] = counts.get((source, target), 0) + 1
        for (source, target), count in sorted(counts.items()):
            print(f"Thing {source} -> {target}: {count}")


def process_all_maps(args, magic, lumps, map_names):
    """Convert every map, optionally node-build/audit each one, and assemble one resource WAD."""
    import tempfile
    from pathlib import Path

    if args.map:
        raise SystemExit("--all-maps cannot be combined with --map")

    if not map_names:
        raise SystemExit("no complete maps found in input WAD")

    all_lumps = []
    report_chunks = []
    base_tmp = Path(tempfile.mkdtemp(prefix="map3do_allmaps_"))
    resource_base = None
    if args.resource_wad:
        resource_base = Path(args.resource_wad)

    print(f"Processing {len(map_names)} maps from {args.input}...")
    for ordinal, map_name in enumerate(map_names, start=1):
        print(f"\n=== [{ordinal}/{len(map_names)}] {map_name} ===")
        info = convert_map(lumps, map_name, args.strict)
        print_map_summary(info)

        per_map = base_tmp / f"{map_name}.wad"
        write_wad(per_map, magic, info["lumps"])

        node_dir = base_tmp / f"{map_name}_resources"
        if args.nodebuilder:
            run_nodebuilder(args.nodebuilder, per_map, node_dir, None)
            try:
                enforce_renderer_limits(
                    node_dir,
                    info["input_things"],
                    info["source_linedefs"],
                    info["source_sidedefs"],
                    info["source_sectors"],
                )
            except SystemExit as exc:
                raise SystemExit(f"{map_name}: {exc}") from exc
            print(f"3DO resources: {node_dir}")
            if resource_base:
                resource_out = resource_base.with_name(
                    f"{resource_base.stem}_{map_name}{resource_base.suffix or '.wad'}"
                )
                write_resource_wad(resource_out, node_dir)
                print(f"3DO resource WAD: {resource_out}")

        all_lumps.extend(info["lumps"])
        report_chunks.append(mapping_report(
            map_name,
            info["mappings"],
            info["thing_changes"],
            info["line_special_changes"],
            info["sector_special_changes"],
            args.input,
            args.output,
        ))

    write_wad(args.output, magic, all_lumps)
    print(f"\nWrote {args.output}")
    print(f"Processed Maps: {len(map_names)}")
    if args.nodebuilder:
        print(f"Map Nodebuilder Output: {base_tmp}")

    if args.report:
        args.report.write_text("\n".join(report_chunks), encoding="utf-8")
    return 0

def main():
    args = parse_args()
    magic, lumps = read_wad(args.input)
    map_names = map_names_in_wad(lumps)
    if args.all_maps:
        return process_all_maps(args, magic, lumps, map_names)

    map_name = args.map.upper() if args.map else (map_names[0] if map_names else None)
    if not map_name:
        raise SystemExit("could not find a map marker")
    marker, indexes = find_map_lumps(lumps, map_name)

    input_things = lumps[indexes["THINGS"]].data
    source_types = [struct.unpack_from("<h", input_things, offset + 6)[0] for offset in range(0, len(input_things), 10)]
    converted_things, thing_changes = convert_things(input_things)
    converted_linedefs, line_special_changes = convert_linedefs(
        lumps[indexes["LINEDEFS"]].data, args.strict
    )
    converted_sides, converted_sectors, mappings = convert_texture_fields(
        lumps[indexes["SIDEDEFS"]].data,
        lumps[indexes["SECTORS"]].data,
        args.strict,
    )
    converted_sectors, sector_special_changes = convert_sector_specials(
        converted_sectors, args.strict
    )

    selected = [lumps[marker]]
    # Emit only map lumps this converter owns. Do not carry
    # stale SEGS/SSECTORS/NODES/BLOCKMAP data through after geometry/text changes.
    # The 3DO nodebuilder is responsible for rebuilding data.
    for name in MAP_LUMP_ORDER:
        index = indexes.get(name)
        if index is None:
            continue
        lump = lumps[index]
        if name == "THINGS":
            lump = Lump(lump.name, converted_things)
        elif name == "LINEDEFS":
            lump = Lump(lump.name, converted_linedefs)
        elif name == "SIDEDEFS":
            lump = Lump(lump.name, converted_sides)
        elif name == "SECTORS":
            lump = Lump(lump.name, converted_sectors)

        # Derived lumps are not copied.
        if name not in MAP_DERIVED_LUMPS:
            selected.append(lump)

    write_wad(args.output, magic, selected)
    print(f"Map: {map_name}")
    print(f"Things: {len(source_types)}  replacements: {len(thing_changes)}")
    exact = sum(1 for item in mappings if item.exact)
    fallback = len(mappings) - exact
    print(f"Texture Fields: {len(mappings)}  exact: {exact}  fallback: {fallback}")
    print(f"Linedef Specials: {len(line_special_changes)} replacements")
    print(f"Sector Specials: {len(sector_special_changes)} replacements")
    if thing_changes:
        counts = {}
        for source, target in thing_changes:
            counts[(source, target)] = counts.get((source, target), 0) + 1
        for (source, target), count in sorted(counts.items()):
            print(f"Thing {source} -> {target}: {count}")

    report = mapping_report(
        map_name, mappings, thing_changes, line_special_changes,
        sector_special_changes, args.input, args.output
    )
    if args.report:
        args.report.write_text(report, encoding="utf-8")
    else:
        print()
        print(report, end="")

    if args.nodebuilder:
        nodebuilder_output = args.nodebuilder_output or args.output.with_name(args.output.stem + "_resources")
        run_nodebuilder(args.nodebuilder, args.output, nodebuilder_output, args.resource_wad)
        try:
            enforce_renderer_limits(
                nodebuilder_output,
                input_things,
                lumps[indexes["LINEDEFS"]].data,
                lumps[indexes["SIDEDEFS"]].data,
                lumps[indexes["SECTORS"]].data,
            )
        except SystemExit:
            try:
                args.output.unlink()
            except FileNotFoundError:
                pass
            raise
        print(f"3DO resources: {nodebuilder_output}")

if __name__ == "__main__":
    main()
