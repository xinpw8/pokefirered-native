#!/usr/bin/env python3
"""
gen_pokemon_sprite_atlas.py -- Build pokemon front/back sprite atlas from
pokefirered decomp graphics.

Each species gets two 64x64 cells: front sprite (left) and back sprite (right).
Packed into rows of 20 pokemon (40 columns of 64px = 2560px wide).
Indexed by species ID.

Outputs:
  - pokemon_atlas.png: combined sprite sheet
  - pfr_pokemon_atlas.h: C header with atlas lookup data
"""

import argparse
import math
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow required.  Install with: pip install Pillow",
          file=sys.stderr)
    sys.exit(1)

SPRITE_SIZE = 64       # Each pokemon sprite cell is 64x64
COLS_PER_ROW = 20      # 20 pokemon per row
ATLAS_W = COLS_PER_ROW * 2 * SPRITE_SIZE  # 2560


def parse_species_constants(path):
    """Parse include/constants/species.h -> {name_lower: species_id}."""
    species = {}
    num_species = None
    for line in Path(path).read_text().splitlines():
        m = re.match(r"#define\s+SPECIES_(\w+)\s+(\d+)", line)
        if m:
            name = m.group(1).lower()
            sid = int(m.group(2))
            species[name] = sid
    # Resolve NUM_SPECIES
    if "egg" in species:
        num_species = species["egg"]
    else:
        num_species = max(species.values()) + 1
    return species, num_species


def parse_jasc_pal(path):
    """Parse JASC-PAL file -> list of (R, G, B) tuples."""
    lines = Path(path).read_text().strip().splitlines()
    if lines[0].strip() != "JASC-PAL":
        raise ValueError(f"Not a JASC-PAL file: {path}")
    count = int(lines[2].strip())
    colors = []
    for i in range(3, 3 + count):
        parts = lines[i].split()
        colors.append((int(parts[0]), int(parts[1]), int(parts[2])))
    return colors


def load_pokemon_sprite(png_path, pal_path=None):
    """Load a pokemon sprite PNG and return as RGBA image.

    The PNGs in the decomp are 4-bit indexed color with palette embedded.
    Palette index 0 is the transparent background color.
    If a .pal file is provided, it overrides the embedded palette.
    """
    if not png_path.exists():
        return None
    try:
        img = Image.open(png_path)
    except Exception as e:
        print(f"  Warning: could not open {png_path}: {e}", file=sys.stderr)
        return None

    if img.mode == "P":
        # Apply external palette if provided
        if pal_path and pal_path.exists():
            try:
                colors = parse_jasc_pal(pal_path)
                # Build flat palette: [R0, G0, B0, R1, G1, B1, ...]
                flat_pal = []
                for r, g, b in colors:
                    flat_pal.extend([r, g, b])
                # Pad to 256 entries (768 bytes) as PIL requires
                flat_pal.extend([0] * (768 - len(flat_pal)))
                img.putpalette(flat_pal)
            except Exception as e:
                print(f"  Warning: could not parse palette {pal_path}: {e}",
                      file=sys.stderr)

        # Convert to RGBA, making palette index 0 transparent
        rgba = img.convert("RGBA")
        # The PNG should already have transparency info for index 0,
        # but ensure it: any pixel that was index 0 becomes fully transparent
        pixels = img.load()
        rgba_pixels = rgba.load()
        for y in range(img.height):
            for x in range(img.width):
                if pixels[x, y] == 0:
                    rgba_pixels[x, y] = (0, 0, 0, 0)
        return rgba
    else:
        return img.convert("RGBA")


def find_sprite_paths(gfx_dir, species_name):
    """Find front.png and back.png for a species, handling special cases.

    Special cases:
    - castform: sprites in castform/normal/ subdirectory
    - unown: sprites in unown/a/ subdirectory (form A = base)
    """
    species_dir = gfx_dir / species_name

    if not species_dir.exists():
        return None, None, None

    front = species_dir / "front.png"
    back = species_dir / "back.png"
    pal = species_dir / "normal.pal"

    # If front.png not directly in species dir, check for subdirectory
    if not front.exists():
        if species_name == "castform":
            front = species_dir / "normal" / "front.png"
            back = species_dir / "normal" / "back.png"
            pal_sub = species_dir / "normal" / "normal.pal"
            if pal_sub.exists():
                pal = pal_sub
        elif species_name == "unown":
            front = species_dir / "a" / "front.png"
            back = species_dir / "a" / "back.png"
            # Unown palette is at parent level
            # pal stays as species_dir / "normal.pal"

    return front, back, pal


def main():
    parser = argparse.ArgumentParser(
        description="Generate pokemon sprite atlas from pokefirered decomp")
    parser.add_argument(
        "--decomp", default="/home/spark-advantage/pokefirered",
        help="Path to pokefirered decomp root")
    parser.add_argument(
        "--output-png",
        default="/home/spark-advantage/pufferlib-4.0/pufferlib/resources/"
                "pfr_native/pokemon_atlas.png",
        help="Output atlas PNG path")
    parser.add_argument(
        "--output-h",
        default="/home/spark-advantage/pokefirered-native/src/"
                "pfr_pokemon_atlas.h",
        help="Output C header path")
    args = parser.parse_args()

    decomp = Path(args.decomp)
    gfx_dir = decomp / "graphics" / "pokemon"
    species_h = decomp / "include" / "constants" / "species.h"

    if not species_h.exists():
        print(f"ERROR: species.h not found at {species_h}", file=sys.stderr)
        sys.exit(1)

    # Step 1: Parse species constants
    species_map, num_species = parse_species_constants(species_h)
    print(f"Parsed {len(species_map)} species constants, "
          f"NUM_SPECIES = {num_species}", file=sys.stderr)

    # Step 2: Build reverse map: species_id -> directory name
    # The SPECIES_FOO define maps to directory "foo" (lowercase)
    id_to_dir = {}
    for name, sid in species_map.items():
        if sid > 0 and sid < num_species:  # Skip NONE and EGG+
            id_to_dir[sid] = name

    # Step 3: Load sprites for each species
    loaded = {}  # species_id -> (front_rgba, back_rgba)
    missing_front = []
    missing_back = []

    for sid in sorted(id_to_dir.keys()):
        dir_name = id_to_dir[sid]
        front_path, back_path, pal_path = find_sprite_paths(gfx_dir, dir_name)

        if front_path is None or not front_path.exists():
            missing_front.append((sid, dir_name))
            continue

        front_img = load_pokemon_sprite(front_path, pal_path)
        if front_img is None:
            missing_front.append((sid, dir_name))
            continue

        back_img = None
        if back_path and back_path.exists():
            back_img = load_pokemon_sprite(back_path, pal_path)

        if back_img is None:
            missing_back.append((sid, dir_name))
            # Use front as fallback for missing back
            back_img = front_img.copy()

        # Ensure 64x64
        if front_img.size != (SPRITE_SIZE, SPRITE_SIZE):
            front_img = front_img.resize(
                (SPRITE_SIZE, SPRITE_SIZE), Image.NEAREST)
        if back_img.size != (SPRITE_SIZE, SPRITE_SIZE):
            back_img = back_img.resize(
                (SPRITE_SIZE, SPRITE_SIZE), Image.NEAREST)

        loaded[sid] = (front_img, back_img)

    print(f"Loaded {len(loaded)} pokemon sprites", file=sys.stderr)
    if missing_front:
        print(f"Missing front sprites ({len(missing_front)}): "
              f"{', '.join(n for _, n in missing_front[:10])}"
              f"{'...' if len(missing_front) > 10 else ''}",
              file=sys.stderr)
    if missing_back:
        print(f"Missing back sprites ({len(missing_back)}): "
              f"{', '.join(n for _, n in missing_back[:10])}"
              f"{'...' if len(missing_back) > 10 else ''}",
              file=sys.stderr)

    # Step 4: Compute atlas dimensions
    # We need slots for species IDs 0..num_species-1
    # Each row holds COLS_PER_ROW pokemon (2 cells each: front + back)
    num_rows = math.ceil(num_species / COLS_PER_ROW)
    atlas_h = num_rows * SPRITE_SIZE
    print(f"Atlas dimensions: {ATLAS_W}x{atlas_h} "
          f"({num_rows} rows, {COLS_PER_ROW} pokemon/row)", file=sys.stderr)

    # Step 5: Composite atlas
    atlas = Image.new("RGBA", (ATLAS_W, atlas_h), (0, 0, 0, 0))

    atlas_entries = {}  # species_id -> (front_x, front_y, back_x, back_y)
    for sid, (front_img, back_img) in loaded.items():
        row = sid // COLS_PER_ROW
        col = sid % COLS_PER_ROW

        front_x = col * 2 * SPRITE_SIZE
        front_y = row * SPRITE_SIZE
        back_x = front_x + SPRITE_SIZE
        back_y = front_y

        atlas.paste(front_img, (front_x, front_y), front_img)
        atlas.paste(back_img, (back_x, back_y), back_img)

        atlas_entries[sid] = (front_x, front_y, back_x, back_y)

    # Step 6: Save atlas PNG
    output_png = Path(args.output_png)
    output_png.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(str(output_png))
    print(f"Saved atlas: {output_png} ({ATLAS_W}x{atlas_h})", file=sys.stderr)

    # Step 7: Generate C header
    output_h = Path(args.output_h)
    output_h.parent.mkdir(parents=True, exist_ok=True)

    lines = [
        "/* Auto-generated by tools/gen_pokemon_sprite_atlas.py"
        " -- DO NOT EDIT */",
        "#ifndef PFR_POKEMON_ATLAS_H",
        "#define PFR_POKEMON_ATLAS_H",
        "",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    uint16_t front_x, front_y;  "
        "/* pixel coords of front sprite in atlas */",
        "    uint16_t back_x, back_y;    "
        "/* pixel coords of back sprite in atlas */",
        "} PfrPokemonAtlasEntry;",
        "",
        f"#define PFR_POKEMON_ATLAS_W {ATLAS_W}",
        f"#define PFR_POKEMON_ATLAS_H {atlas_h}",
        f"#define PFR_POKEMON_ATLAS_SPRITE_SIZE {SPRITE_SIZE}",
        f"#define PFR_POKEMON_ATLAS_COLS {COLS_PER_ROW}",
        "",
        f"static const PfrPokemonAtlasEntry"
        f" PFR_POKEMON_ATLAS[{num_species}] = {{",
    ]

    for sid in range(num_species):
        name = id_to_dir.get(sid, "")
        if sid in atlas_entries:
            fx, fy, bx, by = atlas_entries[sid]
            comment = f"  /* {name.upper()} */" if name else ""
            lines.append(
                f"    [{sid}] = {{ {fx}, {fy}, {bx}, {by} }},{comment}")
        else:
            comment = f"  /* {name.upper()} (missing) */" if name else ""
            lines.append(f"    [{sid}] = {{ 0, 0, 0, 0 }},{comment}")

    lines.append("};")
    lines.append("")
    lines.append("#endif /* PFR_POKEMON_ATLAS_H */")
    lines.append("")

    output_h.write_text("\n".join(lines))
    print(f"Saved header: {output_h} ({num_species} entries)", file=sys.stderr)


if __name__ == "__main__":
    main()
