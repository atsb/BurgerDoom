"""
    makeprezfile.py - Creates 3DO PREZFILES for BurgerDoom.
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
    ./makeprezfile.py REZFILE --replace ID=FILE -o PREZFILE
    ./makeprezfile.py REZFILE --replace-wad-texture rNUKAGE=NUKAGE.wad -o PREZFILE
    ./makeprezfile.py REZFILE --replace-wad-frontend rINTERMIS=INTER.wad -o PREZFILE
    ./makeprezfile.py REZFILE --replace-wad-sprite rSPR_HANGINGRUMP=GOR1.wad -o PREZFILE
    ./makeprezfile.py REZFILE --replace-map MAP02=MAP02.wad -o PREZFILE
    ./makeprezfile.py REZFILE --patch mod_directory -o PREZFILE
"""

import argparse
from pathlib import Path

import makerez


def main():
    ap = argparse.ArgumentParser(
        description="Creates a DOOM3DO PREZFILE containing replacement resources"
    )
    ap.add_argument("base_rezfile", type=Path)
    ap.add_argument("--root", type=Path, default=Path("."))
    ap.add_argument("-o", "--output", type=Path, default=Path("PREZFILE"))
    ap.add_argument("--replace", action="append", default=[])
    ap.add_argument("--replace-map", action="append", default=[])
    ap.add_argument("--replace-wad-texture", action="append", default=[])
    ap.add_argument("--replace-wad-frontend", action="append", default=[])
    ap.add_argument("--replace-wad-sprite", action="append", default=[])
    ap.add_argument("--patch", type=Path)
    args = ap.parse_args()

    source_root = args.root.resolve()
    values = makerez.parse_doomrez(source_root / "source" / "doomrez.h")
    resources = makerez.read_rezfile(args.base_rezfile)
    replacements = {}

    for value in args.replace:
        number, path = makerez.parse_replacement(value)
        replacements[number] = path.read_bytes()

    for value in args.replace_map:
        map_number, path = makerez.parse_map_replacement(value)
        replacements.update(makerez.load_map_replacements(path, values, map_number))

    for value in args.replace_wad_texture:
        name, _ = makerez.parse_wad_asset(value)
        number = makerez.parse_resource_number(name, values)
        if not (values["rT_START"] <= number < values["rT_END"]):
            raise SystemExit(f"resource {name} is not a wall texture")
        makerez.replace_wad_texture_resource(resources, values, value)
        replacements[number] = resources[number].data

    for value in args.replace_wad_frontend:
        name, _ = makerez.parse_wad_asset(value)
        number = makerez.parse_resource_number(name, values)
        if name.casefold() not in {
            symbol.casefold() for symbol in makerez.SPECIAL_ALIASES
        }:
            raise SystemExit(f"resource {name} is not a frontend/UI resource")
        makerez.replace_wad_frontend_resource(resources, values, value)
        replacements[number] = resources[number].data

    for value in args.replace_wad_sprite:
        name, _ = makerez.parse_wad_asset(value)
        number = makerez.parse_resource_number(name, values)
        makerez.replace_wad_sprite_resource(resources, values, value)
        replacements[number] = resources[number].data

    if args.patch:
        patch_replacements, patch_maps = makerez.load_patch_replacements(
            args.patch.resolve(), source_root
        )
        overlap = set(replacements).intersection(patch_replacements)
        if overlap:
            ids = ", ".join(str(number) for number in sorted(overlap))
            raise SystemExit(f"resource replacement overlaps patch: {ids}")
        replacements.update(patch_replacements)
        for map_number, map_path in patch_maps:
            map_data = makerez.load_map_replacements(map_path, values, map_number)
            overlap = set(replacements).intersection(map_data)
            if overlap:
                ids = ", ".join(str(number) for number in sorted(overlap))
                raise SystemExit(f"resource replacement overlaps patch map: {ids}")
            replacements.update(map_data)

    if not replacements:
        raise SystemExit("no replacements specified")

    selected = []
    for number in sorted(replacements):
        base = resources.get(number)
        if base is None:
            raise SystemExit(f"resource ID {number} is not present in the base REZFILE")
        selected.append(
            makerez.Resource(
                base.number,
                base.type,
                base.name,
                Path(f"<PREZFILE:{number}>"),
                replacements[number],
                base.flags,
            )
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    groups, dictionary_size, total_size = makerez.write_rez(selected, args.output)
    print(f"Created {args.output}")
    print(f"  resources   : {len(selected)}")
    print(f"  groups      : {len(groups)}")
    print(f"  dictionary  : {dictionary_size} bytes")
    print(f"  total size  : {total_size} bytes")


if __name__ == "__main__":
    raise SystemExit(main())
