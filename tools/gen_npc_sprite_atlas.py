#!/usr/bin/env python3
"""
gen_npc_sprite_atlas.py — Build NPC sprite atlas from pokefirered decomp graphics.

Each sprite is stored at its NATIVE size (16x16, 16x32, 32x32, etc.).
The atlas packs sprites vertically with a per-entry lookup that includes
the Y offset, width, height, and frame count.

Outputs:
  - npc_atlas.png: combined sprite sheet (3 columns: S, N, W at native sizes)
  - npc_atlas.h: C header with per-gfx_id lookup
"""

import argparse
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow required. Install with: pip install Pillow", file=sys.stderr)
    sys.exit(1)

ATLAS_COLS = 3  # South, North, West (East = flip West)


def parse_event_objects(path):
    mapping = {}
    for line in Path(path).read_text().splitlines():
        m = re.match(r"#define\s+(OBJ_EVENT_GFX_\w+)\s+(\d+)", line)
        if m:
            mapping[m.group(1)] = int(m.group(2))
    return mapping


def parse_graphics_files(path):
    mapping = {}
    for line in Path(path).read_text().splitlines():
        m = re.match(
            r'.*?(gObjectEventPic_\w+)\[\]\s*=\s*INCBIN_U\d+\("([^"]+)"', line)
        if m:
            mapping[m.group(1)] = m.group(2)
    return mapping


def parse_graphics_info(path):
    text = Path(path).read_text()
    info = {}
    for m in re.finditer(
        r"const struct ObjectEventGraphicsInfo (gObjectEventGraphicsInfo_(\w+))\s*=\s*\{(.*?)\};",
        text, re.DOTALL
    ):
        name = m.group(2)
        body = m.group(3)
        wm = re.search(r"\.width\s*=\s*(\d+)", body)
        hm = re.search(r"\.height\s*=\s*(\d+)", body)
        im = re.search(r"\.images\s*=\s*(sPicTable_\w+)", body)
        if wm and hm and im:
            info[name] = {
                "width": int(wm.group(1)),
                "height": int(hm.group(1)),
                "pic_table": im.group(1),
            }
    return info


def parse_pic_tables(path):
    text = Path(path).read_text()
    mapping = {}
    for m in re.finditer(
        r"static const struct SpriteFrameImage (sPicTable_(\w+))\[\]\s*=\s*\{(.*?)\};",
        text, re.DOTALL
    ):
        table_name = m.group(1)
        body = m.group(3)
        pm = re.search(r"gObjectEventPic_(\w+)", body)
        if pm:
            mapping[table_name] = f"gObjectEventPic_{pm.group(1)}"
    return mapping


def parse_pointers(path, event_objs):
    text = Path(path).read_text()
    mapping = {}
    for m in re.finditer(
        r"\[(OBJ_EVENT_GFX_\w+)\]\s*=\s*&gObjectEventGraphicsInfo_(\w+)", text):
        gfx_id = event_objs.get(m.group(1))
        if gfx_id is not None:
            mapping[gfx_id] = m.group(2)
    return mapping


def load_sprite_frames(png_path, frame_w, frame_h):
    """Load up to 3 standing frames (S, N, W) at native size."""
    if not png_path.exists():
        return None
    try:
        img = Image.open(png_path).convert("RGBA")
    except Exception:
        return None

    if img.width < frame_w or img.height < frame_h:
        return None

    frames_per_row = img.width // frame_w
    frames = []
    for f in range(min(3, frames_per_row)):
        crop = img.crop((f * frame_w, 0, (f + 1) * frame_w, frame_h))
        frames.append(crop)

    while len(frames) < 3:
        frames.append(frames[0].copy() if frames else
                      Image.new("RGBA", (frame_w, frame_h)))
    return frames


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--output-png", required=True)
    parser.add_argument("--output-h", required=True)
    args = parser.parse_args()

    repo_root = Path(args.repo_root)
    pfr = repo_root / "third_party/pokefirered"

    event_objs = parse_event_objects(pfr / "include/constants/event_objects.h")
    graphics_files = parse_graphics_files(
        pfr / "src/data/object_events/object_event_graphics.h")
    graphics_info = parse_graphics_info(
        pfr / "src/data/object_events/object_event_graphics_info.h")
    pic_tables = parse_pic_tables(
        pfr / "src/data/object_events/object_event_pic_tables.h")
    pointers = parse_pointers(
        pfr / "src/data/object_events/object_event_graphics_info_pointers.h",
        event_objs)

    max_gfx_id = max(event_objs.values()) if event_objs else 0

    # Build per-gfx_id sprite info
    sprite_data = {}  # gfx_id → {frames, width, height, name}
    for gfx_id in range(max_gfx_id + 1):
        info_name = pointers.get(gfx_id)
        if not info_name or info_name not in graphics_info:
            continue
        ginfo = graphics_info[info_name]
        pic_table = ginfo["pic_table"]
        pic_var = pic_tables.get(pic_table)
        if not pic_var or pic_var not in graphics_files:
            continue
        rel_path = graphics_files[pic_var]
        png_path = pfr / rel_path.replace(".4bpp", ".png")

        frame_w = ginfo["width"]
        frame_h = ginfo["height"]
        frames = load_sprite_frames(png_path, frame_w, frame_h)
        if not frames:
            continue

        sprite_data[gfx_id] = {
            "frames": frames,
            "width": frame_w,
            "height": frame_h,
            "name": info_name,
        }

    print(f"Loaded {len(sprite_data)} sprites out of {max_gfx_id + 1} IDs",
          file=sys.stderr)

    # Pack atlas: sprites stacked vertically, each at its native height.
    # Atlas width = max(frame_w * 3) across all sprites.
    max_frame_w = max((s["width"] for s in sprite_data.values()), default=16)
    atlas_w = max_frame_w * ATLAS_COLS

    # First pass: compute Y offsets
    atlas_entries = {}  # gfx_id → (y_offset, width, height)
    y_cursor = 0
    for gfx_id in range(max_gfx_id + 1):
        if gfx_id not in sprite_data:
            continue
        sd = sprite_data[gfx_id]
        atlas_entries[gfx_id] = (y_cursor, sd["width"], sd["height"])
        y_cursor += sd["height"]

    atlas_h = max(y_cursor, 1)
    atlas = Image.new("RGBA", (atlas_w, atlas_h), (0, 0, 0, 0))

    # Second pass: paste frames
    for gfx_id, (y_off, fw, fh) in atlas_entries.items():
        frames = sprite_data[gfx_id]["frames"]
        for col, frame in enumerate(frames):
            atlas.paste(frame, (col * fw, y_off), frame)

    output_png = Path(args.output_png)
    output_png.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(str(output_png))
    print(f"Saved atlas: {output_png} ({atlas_w}x{atlas_h})", file=sys.stderr)

    # Generate C header
    output_h = Path(args.output_h)
    lines = [
        "/* Auto-generated by tools/gen_npc_sprite_atlas.py — DO NOT EDIT */",
        "#ifndef NPC_ATLAS_GENERATED_H",
        "#define NPC_ATLAS_GENERATED_H",
        "",
        f"#define NPC_ATLAS_WIDTH {atlas_w}",
        f"#define NPC_ATLAS_HEIGHT {atlas_h}",
        f"#define NPC_ATLAS_COLS {ATLAS_COLS}  /* S, N, W (E = flip W) */",
        f"#define NPC_MAX_GFX_ID {max_gfx_id}",
        "",
        "typedef struct {",
        "    int16_t y;       /* atlas Y offset (-1 = no sprite) */",
        "    uint8_t w;       /* frame width in pixels */",
        "    uint8_t h;       /* frame height in pixels */",
        "} NpcAtlasEntry;",
        "",
        f"static const NpcAtlasEntry npc_atlas[{max_gfx_id + 1}] = {{",
    ]

    for gfx_id in range(max_gfx_id + 1):
        if gfx_id in atlas_entries:
            y_off, fw, fh = atlas_entries[gfx_id]
            name = sprite_data[gfx_id]["name"]
            lines.append(f"    [{gfx_id}] = {{ {y_off}, {fw}, {fh} }},  /* {name} */")
        else:
            lines.append(f"    [{gfx_id}] = {{ -1, 0, 0 }},")

    lines.append("};")
    lines.append("")
    lines.append("#endif /* NPC_ATLAS_GENERATED_H */")

    output_h.write_text("\n".join(lines))
    print(f"Saved header: {output_h}", file=sys.stderr)


if __name__ == "__main__":
    main()
