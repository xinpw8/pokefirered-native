#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
import struct
from collections import defaultdict, deque
from pathlib import Path


MAPGRID_METATILE_ID_MASK = 0x03FF
MAPGRID_COLLISION_SHIFT = 10
MAPGRID_COLLISION_MASK = 0x0C00
MAPGRID_ELEVATION_SHIFT = 12
MAPGRID_ELEVATION_MASK = 0xF000
NUM_METATILES_IN_PRIMARY = 640

TILE_TERRAIN_GRASS = 1
TILE_TERRAIN_WATER = 2
TILE_ENCOUNTER_WATER = 2

MB_TALL_GRASS = 0x02
MB_LONG_GRASS = 0x03
MB_CAVE_DOOR = 0x60
MB_LADDER = 0x61
MB_EAST_ARROW_WARP = 0x62
MB_WEST_ARROW_WARP = 0x63
MB_NORTH_ARROW_WARP = 0x64
MB_SOUTH_ARROW_WARP = 0x65
MB_FALL_WARP = 0x66
MB_REGULAR_WARP = 0x67
MB_LAVARIDGE_1F_WARP = 0x68
MB_WARP_DOOR = 0x69
MB_UP_ESCALATOR = 0x6A
MB_DOWN_ESCALATOR = 0x6B
MB_UP_RIGHT_STAIR_WARP = 0x6C
MB_UP_LEFT_STAIR_WARP = 0x6D
MB_DOWN_RIGHT_STAIR_WARP = 0x6E
MB_DOWN_LEFT_STAIR_WARP = 0x6F
MB_UNION_ROOM_WARP = 0x71
MB_BOOKSHELF = 0x81
MB_PC = 0x83
MB_SIGNPOST = 0x84
MB_TELEVISION = 0x86
MB_KITCHEN = 0x8A
MB_DRESSER = 0x8B

MAP_DYNAMIC = "MAP_DYNAMIC"
MAP_UNDEFINED = "MAP_UNDEFINED"
WARP_ID_DYNAMIC = "WARP_ID_DYNAMIC"
WARP_ID_NONE = "WARP_ID_NONE"

ATLAS_COMPONENT_GAP = 8
ATLAS_ORPHAN_GAP = 4
ATLAS_ORPHAN_ROW_LIMIT = 256


def load_json(path: Path):
    return json.loads(path.read_text())


def read_u16_words(path: Path) -> list[int]:
    data = path.read_bytes()
    if len(data) % 2 != 0:
        raise ValueError(f"{path} has odd byte length")
    return list(struct.unpack("<" + "H" * (len(data) // 2), data))


def read_u32_words(path: Path) -> list[int]:
    data = path.read_bytes()
    if len(data) % 4 != 0:
        raise ValueError(f"{path} has invalid byte length")
    return list(struct.unpack("<" + "I" * (len(data) // 4), data))


def relpath(path: Path, start: Path) -> str:
    return str(path.resolve().relative_to(start.resolve()))


def dir_to_tileset_symbol(dir_name: str) -> str:
    pieces = []
    for part in dir_name.split("_"):
        if part == "ss":
            pieces.append("SS")
        elif part == "mt":
            pieces.append("Mt")
        elif part.isdigit():
            pieces.append(part)
        else:
            pieces.append(part.capitalize())
    return "gTileset_" + "".join(pieces)


def load_tileset_attribute_paths(pokedir: Path) -> dict[str, dict]:
    headers = (pokedir / "src/data/tilesets/headers.h").read_text()
    header_pattern = re.compile(
        r"const struct Tileset (gTileset_[A-Za-z0-9]+)\s*=\s*\{(.*?)\n\};",
        re.S,
    )
    header_meta = {}
    for symbol, body in header_pattern.findall(headers):
        match = re.search(r"\.isSecondary = (TRUE|FALSE)", body)
        if match is None:
            continue
        header_meta[symbol] = {"kind": "secondary" if match.group(1) == "TRUE" else "primary"}

    attr_paths = {}
    tileset_root = pokedir / "data/tilesets"
    for kind in ("primary", "secondary"):
        for attr_path in tileset_root.glob(f"{kind}/*/metatile_attributes.bin"):
            symbol = dir_to_tileset_symbol(attr_path.parent.name)
            attr_paths[symbol] = {
                "kind": kind,
                "attributes": attr_path,
                "metatiles": attr_path.parent / "metatiles.bin",
            }

    for symbol, meta in header_meta.items():
        if symbol not in attr_paths:
            raise ValueError(f"missing metatile_attributes.bin for {symbol}")
        if attr_paths[symbol]["kind"] != meta["kind"]:
            raise ValueError(f"tileset kind mismatch for {symbol}")

    return attr_paths


def load_layouts(pokedir: Path) -> dict[str, dict]:
    layouts = load_json(pokedir / "data/layouts/layouts.json")["layouts"]
    return {
        entry["id"]: entry
        for entry in layouts
        if isinstance(entry, dict) and entry.get("id")
    }


def load_map_groups(pokedir: Path) -> dict:
    return load_json(pokedir / "data/maps/map_groups.json")


def parse_numeric_symbol(value):
    if isinstance(value, int):
        return value
    text = str(value)
    if text.startswith("-"):
        return int(text, 10)
    if text.isdigit():
        return int(text, 10)
    return None


def decode_attribute(raw_attr: int) -> dict[str, int]:
    return {
        "raw": raw_attr,
        "behavior": raw_attr & 0x1FF,
        "terrain": (raw_attr >> 9) & 0x1F,
        "encounter_type": (raw_attr >> 24) & 0x7,
        "layer_type": (raw_attr >> 29) & 0x3,
    }


def decode_blocks(words: list[int], primary_attrs: list[int], secondary_attrs: list[int]) -> dict[str, list[int]]:
    raw_blocks = []
    metatile_ids = []
    collisions = []
    elevations = []
    attributes = []
    behaviors = []
    terrains = []
    encounter_types = []
    layer_types = []

    for word in words:
        metatile = word & MAPGRID_METATILE_ID_MASK
        collision = (word & MAPGRID_COLLISION_MASK) >> MAPGRID_COLLISION_SHIFT
        elevation = (word & MAPGRID_ELEVATION_MASK) >> MAPGRID_ELEVATION_SHIFT
        if metatile < NUM_METATILES_IN_PRIMARY:
            raw_attr = primary_attrs[metatile]
        else:
            raw_attr = secondary_attrs[metatile - NUM_METATILES_IN_PRIMARY]
        decoded_attr = decode_attribute(raw_attr)

        raw_blocks.append(word)
        metatile_ids.append(metatile)
        collisions.append(collision)
        elevations.append(elevation)
        attributes.append(decoded_attr["raw"])
        behaviors.append(decoded_attr["behavior"])
        terrains.append(decoded_attr["terrain"])
        encounter_types.append(decoded_attr["encounter_type"])
        layer_types.append(decoded_attr["layer_type"])

    return {
        "raw_blocks": raw_blocks,
        "metatile_ids": metatile_ids,
        "collisions": collisions,
        "elevations": elevations,
        "attributes": attributes,
        "behaviors": behaviors,
        "terrains": terrains,
        "encounter_types": encounter_types,
        "layer_types": layer_types,
    }


def classify_warp_behavior(behavior: int) -> tuple[str, str | None, bool]:
    if behavior == MB_EAST_ARROW_WARP:
        return "east_arrow", "east", False
    if behavior == MB_WEST_ARROW_WARP:
        return "west_arrow", "west", False
    if behavior == MB_NORTH_ARROW_WARP:
        return "north_arrow", "north", False
    if behavior == MB_SOUTH_ARROW_WARP:
        return "south_arrow", "south", False
    if behavior == MB_UP_RIGHT_STAIR_WARP:
        return "up_right_stair", "east", False
    if behavior == MB_DOWN_RIGHT_STAIR_WARP:
        return "down_right_stair", "east", False
    if behavior == MB_UP_LEFT_STAIR_WARP:
        return "up_left_stair", "west", False
    if behavior == MB_DOWN_LEFT_STAIR_WARP:
        return "down_left_stair", "west", False
    if behavior in (MB_CAVE_DOOR, MB_WARP_DOOR):
        return "door", "north", False
    if behavior == MB_LADDER:
        return "ladder", None, False
    if behavior == MB_REGULAR_WARP:
        return "regular_warp", None, False
    if behavior == MB_FALL_WARP:
        return "fall_warp", None, True
    if behavior == MB_LAVARIDGE_1F_WARP:
        return "lavaridge_1f_warp", None, True
    if behavior in (MB_UP_ESCALATOR, MB_DOWN_ESCALATOR):
        return "escalator", None, True
    if behavior == MB_UNION_ROOM_WARP:
        return "union_room_warp", None, True
    return "event_warp_untyped", None, False


def connection_target_position(base_map: dict, other_map: dict, direction: str, offset: int) -> tuple[int, int]:
    if direction == "up":
        return base_map["atlas_x"] + offset, base_map["atlas_y"] - other_map["height"]
    if direction == "down":
        return base_map["atlas_x"] + offset, base_map["atlas_y"] + base_map["height"]
    if direction == "left":
        return base_map["atlas_x"] - other_map["width"], base_map["atlas_y"] + offset
    if direction == "right":
        return base_map["atlas_x"] + base_map["width"], base_map["atlas_y"] + offset
    raise ValueError(f"unsupported connection direction {direction}")


def opposite_direction(direction: str) -> str:
    return {
        "up": "down",
        "down": "up",
        "left": "right",
        "right": "left",
    }[direction]


def is_connection_variant_map(entry: dict) -> bool:
    return entry["name"].endswith("_Connection")


def build_world_data(repo_root: Path) -> dict:
    pokedir = repo_root / "third_party/pokefirered"
    layouts = load_layouts(pokedir)
    map_groups = load_map_groups(pokedir)
    tileset_paths = load_tileset_attribute_paths(pokedir)

    maps = []
    maps_by_name = {}
    maps_by_symbol = {}

    for group_index, group_name in enumerate(map_groups["group_order"]):
        for map_num, map_name in enumerate(map_groups[group_name]):
            map_path = pokedir / "data/maps" / map_name / "map.json"
            map_json = load_json(map_path)
            layout = layouts[map_json["layout"]]
            primary_symbol = layout["primary_tileset"]
            secondary_symbol = layout["secondary_tileset"]
            primary_info = tileset_paths.get(primary_symbol)
            secondary_info = tileset_paths.get(secondary_symbol)
            if primary_symbol != "NULL" and primary_info is None:
                raise ValueError(f"missing tileset info for {primary_symbol}")
            if secondary_symbol != "NULL" and secondary_info is None:
                raise ValueError(f"missing tileset info for {secondary_symbol}")

            primary_attrs = read_u32_words(primary_info["attributes"]) if primary_info else [0] * NUM_METATILES_IN_PRIMARY
            secondary_attrs = read_u32_words(secondary_info["attributes"]) if secondary_info else [0] * NUM_METATILES_IN_PRIMARY
            block_words = read_u16_words(pokedir / layout["blockdata_filepath"])
            border_words = read_u16_words(pokedir / layout["border_filepath"])
            tiles = decode_blocks(block_words, primary_attrs, secondary_attrs)
            border = decode_blocks(border_words, primary_attrs, secondary_attrs)
            map_id = (group_index << 8) | map_num

            entry = {
                "id": map_id,
                "id_symbol": map_json["id"],
                "name": map_name,
                "map_group": group_index,
                "map_group_name": group_name,
                "map_num": map_num,
                "layout_id": map_json["layout"],
                "layout_name": layout["name"],
                "width": int(layout["width"]),
                "height": int(layout["height"]),
                "border_width": int(layout["border_width"]),
                "border_height": int(layout["border_height"]),
                "primary_tileset": primary_symbol,
                "secondary_tileset": secondary_symbol,
                "primary_tileset_attr_path": relpath(primary_info["attributes"], repo_root) if primary_info else None,
                "secondary_tileset_attr_path": relpath(secondary_info["attributes"], repo_root) if secondary_info else None,
                "music": map_json["music"],
                "region_map_section": map_json["region_map_section"],
                "requires_flash": map_json["requires_flash"],
                "weather": map_json["weather"],
                "map_type": map_json["map_type"],
                "allow_cycling": map_json["allow_cycling"],
                "allow_escaping": map_json["allow_escaping"],
                "allow_running": map_json["allow_running"],
                "show_map_name": map_json["show_map_name"],
                "floor_number": map_json["floor_number"],
                "battle_scene": map_json["battle_scene"],
                "tiles": tiles,
                "border": border,
                "connections": [],
                "warp_events": [],
                "coord_events": list(map_json["coord_events"]),
                "bg_events": list(map_json["bg_events"]),
                "object_events": list(map_json["object_events"]),
                "atlas_x": None,
                "atlas_y": None,
                "atlas_component": None,
                "atlas_kind": None,
                "atlas_anchor_map_id": None,
            }
            maps.append(entry)
            maps_by_name[map_name] = entry
            maps_by_symbol[entry["id_symbol"]] = entry

    for entry in maps:
        map_json = load_json(pokedir / "data/maps" / entry["name"] / "map.json")

        for connection in map_json["connections"] or []:
            dest = maps_by_symbol.get(connection["map"])
            entry["connections"].append({
                "direction": connection["direction"],
                "offset": int(connection["offset"]),
                "dest_map_symbol": connection["map"],
                "dest_map_id": None if dest is None else dest["id"],
            })

        for index, warp in enumerate(map_json["warp_events"]):
            dest = maps_by_symbol.get(warp["dest_map"])
            x = int(warp["x"])
            y = int(warp["y"])
            tile_index = y * entry["width"] + x
            behavior = entry["tiles"]["behaviors"][tile_index]
            warp_class, required_direction, special = classify_warp_behavior(behavior)
            dest_warp_index = parse_numeric_symbol(warp["dest_warp_id"])
            entry["warp_events"].append({
                "index": index,
                "x": x,
                "y": y,
                "elevation": int(warp["elevation"]),
                "dest_map_symbol": warp["dest_map"],
                "dest_map_id": None if dest is None else dest["id"],
                "dest_warp_id": dest_warp_index,
                "dest_warp_symbol": str(warp["dest_warp_id"]),
                "source_behavior": behavior,
                "warp_class": warp_class,
                "required_direction": required_direction,
                "special_case": special,
                "dynamic_dest": warp["dest_map"] == MAP_DYNAMIC,
            })

    maps_by_id = {entry["id"]: entry for entry in maps}
    adjacency = defaultdict(list)
    for entry in maps:
        if is_connection_variant_map(entry):
            continue
        for connection in entry["connections"]:
            if connection["dest_map_id"] is None:
                continue
            if is_connection_variant_map(maps_by_id[connection["dest_map_id"]]):
                continue
            adjacency[entry["id"]].append((connection["dest_map_id"], connection["direction"], connection["offset"]))
            adjacency[connection["dest_map_id"]].append((entry["id"], opposite_direction(connection["direction"]), -connection["offset"]))

    component_maps = []
    visited = set()
    connection_conflicts = []
    connected_ids = {map_id for map_id, edges in adjacency.items() if edges}
    for root_id in sorted(connected_ids):
        if root_id in visited:
            continue
        queue = deque([root_id])
        local = {root_id: (0, 0)}
        component_id = len(component_maps)
        component_nodes = []
        visited.add(root_id)

        while queue:
            current_id = queue.popleft()
            current = maps_by_id[current_id]
            component_nodes.append(current_id)
            current["atlas_x"], current["atlas_y"] = local[current_id]
            current["atlas_component"] = component_id
            current["atlas_kind"] = "connected_component"

            for neighbor_id, direction, offset in adjacency[current_id]:
                neighbor = maps_by_id[neighbor_id]
                target = connection_target_position(current, neighbor, direction, offset)
                if neighbor_id not in local:
                    local[neighbor_id] = target
                    if neighbor_id not in visited:
                        visited.add(neighbor_id)
                        queue.append(neighbor_id)
                elif local[neighbor_id] != target:
                    connection_conflicts.append({
                        "source_map_id": current_id,
                        "source_map_name": current["name"],
                        "dest_map_id": neighbor_id,
                        "dest_map_name": neighbor["name"],
                        "direction": direction,
                        "offset": offset,
                        "expected": [local[neighbor_id][0], local[neighbor_id][1]],
                        "saw": [target[0], target[1]],
                    })

        min_x = min(local[map_id][0] for map_id in component_nodes)
        min_y = min(local[map_id][1] for map_id in component_nodes)
        max_x = max(local[map_id][0] + maps_by_id[map_id]["width"] for map_id in component_nodes)
        max_y = max(local[map_id][1] + maps_by_id[map_id]["height"] for map_id in component_nodes)
        component_maps.append({
            "component_index": component_id,
            "map_ids": sorted(component_nodes),
            "bounds": [min_x, min_y, max_x - min_x, max_y - min_y],
        })

    atlas_cursor_x = 0
    atlas_height = 0
    for component in component_maps:
        min_x, min_y, width, height = component["bounds"]
        for map_id in component["map_ids"]:
            entry = maps_by_id[map_id]
            entry["atlas_x"] = entry["atlas_x"] - min_x + atlas_cursor_x
            entry["atlas_y"] = entry["atlas_y"] - min_y
        component["packed_bounds"] = [atlas_cursor_x, 0, width, height]
        atlas_cursor_x += width + ATLAS_COMPONENT_GAP
        atlas_height = max(atlas_height, height)

    for entry in maps:
        if entry["atlas_x"] is not None:
            continue
        for warp in entry["warp_events"]:
            if warp["dynamic_dest"] or warp["dest_map_id"] is None:
                continue
            dest = maps_by_id[warp["dest_map_id"]]
            if dest["atlas_x"] is not None:
                entry["atlas_anchor_map_id"] = dest["id"]
                break
        if entry["atlas_anchor_map_id"] is None:
            for candidate in maps:
                if candidate["atlas_x"] is None:
                    continue
                if candidate["region_map_section"] == entry["region_map_section"]:
                    entry["atlas_anchor_map_id"] = candidate["id"]
                    break

    orphan_maps = [entry for entry in maps if entry["atlas_x"] is None]
    orphan_x = atlas_cursor_x
    orphan_y = 0
    row_height = 0
    row_width = 0
    for entry in sorted(
        orphan_maps,
        key=lambda item: (
            item["atlas_anchor_map_id"] if item["atlas_anchor_map_id"] is not None else 1 << 30,
            item["region_map_section"],
            item["id"],
        ),
    ):
        if row_width and row_width + entry["width"] + ATLAS_ORPHAN_GAP > ATLAS_ORPHAN_ROW_LIMIT:
            orphan_y += row_height + ATLAS_ORPHAN_GAP
            row_width = 0
            row_height = 0
        entry["atlas_x"] = orphan_x + row_width
        entry["atlas_y"] = orphan_y
        entry["atlas_kind"] = "connection_variant" if is_connection_variant_map(entry) else "orphan_pack"
        entry["atlas_component"] = None
        row_width += entry["width"] + ATLAS_ORPHAN_GAP
        row_height = max(row_height, entry["height"])

    atlas_width = 0
    atlas_height = 0
    for entry in maps:
        atlas_width = max(atlas_width, entry["atlas_x"] + entry["width"])
        atlas_height = max(atlas_height, entry["atlas_y"] + entry["height"])

    warp_report = []
    static_warp_errors = []
    for entry in maps:
        for warp in entry["warp_events"]:
            report = {
                "source_map_id": entry["id"],
                "source_map_name": entry["name"],
                "source_warp_index": warp["index"],
                "source_x": warp["x"],
                "source_y": warp["y"],
                "source_elevation": warp["elevation"],
                "source_behavior": warp["source_behavior"],
                "warp_class": warp["warp_class"],
                "required_direction": warp["required_direction"],
                "special_case": warp["special_case"],
                "dynamic_dest": warp["dynamic_dest"],
                "dest_map_symbol": warp["dest_map_symbol"],
                "dest_map_id": warp["dest_map_id"],
                "dest_warp_id": warp["dest_warp_id"],
                "dest_warp_symbol": warp["dest_warp_symbol"],
                "valid_static_dest": False,
                "dest_x": None,
                "dest_y": None,
                "dest_behavior": None,
            }

            if not warp["dynamic_dest"] and warp["dest_map_id"] is not None and warp["dest_warp_id"] is not None:
                dest_map = maps_by_id[warp["dest_map_id"]]
                if 0 <= warp["dest_warp_id"] < len(dest_map["warp_events"]):
                    dest_warp = dest_map["warp_events"][warp["dest_warp_id"]]
                    report["valid_static_dest"] = True
                    report["dest_x"] = dest_warp["x"]
                    report["dest_y"] = dest_warp["y"]
                    report["dest_behavior"] = dest_warp["source_behavior"]
                else:
                    static_warp_errors.append({
                        "source_map_id": entry["id"],
                        "source_map_name": entry["name"],
                        "source_warp_index": warp["index"],
                        "dest_map_name": dest_map["name"],
                        "dest_warp_id": warp["dest_warp_id"],
                        "error": "dest_warp_id_out_of_range",
                    })
            elif not warp["dynamic_dest"]:
                static_warp_errors.append({
                    "source_map_id": entry["id"],
                    "source_map_name": entry["name"],
                    "source_warp_index": warp["index"],
                    "dest_map_symbol": warp["dest_map_symbol"],
                    "dest_warp_id": warp["dest_warp_symbol"],
                    "error": "unresolved_static_destination",
                })

            warp_report.append(report)

    summary = {
        "map_count": len(maps),
        "connected_component_count": len(component_maps),
        "connected_map_count": len(maps) - len(orphan_maps),
        "orphan_map_count": len(orphan_maps),
        "connection_variant_count": sum(1 for item in maps if is_connection_variant_map(item)),
        "atlas_width": atlas_width,
        "atlas_height": atlas_height,
        "warp_count": len(warp_report),
        "dynamic_warp_count": sum(1 for item in warp_report if item["dynamic_dest"]),
        "static_warp_count": sum(1 for item in warp_report if not item["dynamic_dest"]),
        "valid_static_warp_count": sum(1 for item in warp_report if item["valid_static_dest"]),
        "connection_conflict_count": len(connection_conflicts),
        "static_warp_error_count": len(static_warp_errors),
    }

    maps.sort(key=lambda item: item["id"])
    return {
        "summary": summary,
        "maps": maps,
        "atlas": {
            "width": atlas_width,
            "height": atlas_height,
            "components": component_maps,
        },
        "connection_conflicts": connection_conflicts,
        "warp_report": warp_report,
        "static_warp_errors": static_warp_errors,
    }


def tile_rgb(behavior: int, collision: int, terrain: int, encounter_type: int) -> tuple[int, int, int]:
    if collision:
        return (75, 85, 99)
    if behavior in {
        MB_CAVE_DOOR,
        MB_LADDER,
        MB_EAST_ARROW_WARP,
        MB_WEST_ARROW_WARP,
        MB_NORTH_ARROW_WARP,
        MB_SOUTH_ARROW_WARP,
        MB_REGULAR_WARP,
        MB_WARP_DOOR,
        MB_UP_RIGHT_STAIR_WARP,
        MB_UP_LEFT_STAIR_WARP,
        MB_DOWN_RIGHT_STAIR_WARP,
        MB_DOWN_LEFT_STAIR_WARP,
    }:
        return (15, 118, 110)
    if terrain == TILE_TERRAIN_WATER or encounter_type == TILE_ENCOUNTER_WATER:
        return (37, 99, 235)
    if terrain == TILE_TERRAIN_GRASS or behavior in {MB_TALL_GRASS, MB_LONG_GRASS}:
        return (34, 139, 34)
    if behavior in {MB_BOOKSHELF, MB_PC, MB_SIGNPOST, MB_TELEVISION, MB_KITCHEN, MB_DRESSER}:
        return (146, 64, 14)
    return (231, 215, 177)


def render_atlas_ppm(world: dict, path: Path) -> None:
    width = world["atlas"]["width"]
    height = world["atlas"]["height"]
    pixels = bytearray([8, 10, 16] * width * height)

    for entry in world["maps"]:
        outline = (240, 248, 255) if entry["atlas_kind"] == "connected_component" else (148, 163, 184)
        warp_tiles = {(item["x"], item["y"]) for item in entry["warp_events"]}
        for y in range(entry["height"]):
            for x in range(entry["width"]):
                atlas_x = entry["atlas_x"] + x
                atlas_y = entry["atlas_y"] + y
                tile_index = y * entry["width"] + x
                color = tile_rgb(
                    entry["tiles"]["behaviors"][tile_index],
                    entry["tiles"]["collisions"][tile_index],
                    entry["tiles"]["terrains"][tile_index],
                    entry["tiles"]["encounter_types"][tile_index],
                )
                if (x, y) in warp_tiles:
                    color = (217, 70, 239)
                elif x == 0 or y == 0 or x == entry["width"] - 1 or y == entry["height"] - 1:
                    color = outline
                pixel_index = (atlas_y * width + atlas_x) * 3
                pixels[pixel_index:pixel_index + 3] = bytes(color)

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as handle:
        handle.write(f"P6\n{width} {height}\n255\n".encode("ascii"))
        handle.write(pixels)


def write_outputs(world: dict, output_json: Path, output_warp_report: Path | None,
                  output_summary: Path | None, output_atlas: Path | None) -> None:
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(world, indent=2) + "\n")

    if output_warp_report is not None:
        output_warp_report.parent.mkdir(parents=True, exist_ok=True)
        output_warp_report.write_text(json.dumps(world["warp_report"], indent=2) + "\n")

    if output_summary is not None:
        output_summary.parent.mkdir(parents=True, exist_ok=True)
        summary = {
            "summary": world["summary"],
            "connection_conflicts": world["connection_conflicts"],
            "static_warp_errors": world["static_warp_errors"],
        }
        output_summary.write_text(json.dumps(summary, indent=2) + "\n")

    if output_atlas is not None:
        render_atlas_ppm(world, output_atlas)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--output-json", required=True)
    parser.add_argument("--output-warp-report")
    parser.add_argument("--output-summary")
    parser.add_argument("--output-atlas")
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    world = build_world_data(repo_root)
    write_outputs(
        world,
        Path(args.output_json).resolve(),
        None if args.output_warp_report is None else Path(args.output_warp_report).resolve(),
        None if args.output_summary is None else Path(args.output_summary).resolve(),
        None if args.output_atlas is None else Path(args.output_atlas).resolve(),
    )


if __name__ == "__main__":
    main()
