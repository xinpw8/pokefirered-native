#!/usr/bin/env python3
"""
Generate host-native map/layout/group data for the 64-bit native build.

The upstream `maps.s` / `map_events.s` data uses 32-bit GBA pointer layouts,
which are not directly usable by the native build. This script consumes the
upstream JSON map sources plus layout binaries and emits a C translation unit
with proper native structs and pointer arrays.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


INDENT = "    "


def load_json(path: Path):
    with path.open() as handle:
        return json.load(handle)


def c_expr(value) -> str:
    if value is True:
        return "TRUE"
    if value is False:
        return "FALSE"
    if value is None:
        return "NULL"
    return str(value)


def c_symbol_or_null(value) -> str:
    text = c_expr(value)
    if text in {"0", "0x0", "NULL"}:
        return "NULL"
    return text


def c_address_or_null(symbol: str) -> str:
    text = c_symbol_or_null(symbol)
    if text == "NULL":
        return "NULL"
    return f"&{text}"


def read_u16_words(path: Path) -> list[int]:
    data = path.read_bytes()
    if len(data) % 2 != 0:
        raise ValueError(f"{path} has odd byte length {len(data)}")
    return [int.from_bytes(data[i:i + 2], "little") for i in range(0, len(data), 2)]


def emit(lines: list[str], text: str = "") -> None:
    lines.append(text)


def emit_block(lines: list[str], header: str, entries: list[str], trailer: str = "};") -> None:
    emit(lines, header)
    for entry in entries:
        emit(lines, f"{INDENT}{entry}")
    emit(lines, trailer)


def format_u16_words(words: list[int]) -> list[str]:
    row = []
    formatted = []
    for idx, word in enumerate(words, start=1):
        row.append(f"0x{word:04X}")
        if idx % 12 == 0:
            formatted.append(", ".join(row) + ",")
            row = []
    if row:
        formatted.append(", ".join(row) + ",")
    return formatted


def collect_tilesets(layouts: list[dict]) -> set[str]:
    tilesets: set[str] = set()
    for layout in layouts:
        if not layout:
            continue
        primary = c_symbol_or_null(layout["primary_tileset"])
        secondary = c_symbol_or_null(layout["secondary_tileset"])
        if primary != "NULL":
            tilesets.add(primary)
        if secondary != "NULL":
            tilesets.add(secondary)
    return tilesets


def emit_layout_data(lines: list[str], root: Path, layouts: list[dict]) -> None:
    for layout in layouts:
        if not layout:
            continue
        name = layout["name"]
        border_name = f"{name}_Border"
        block_name = f"{name}_Blockdata"
        border_words = read_u16_words(root / layout["border_filepath"])
        block_words = read_u16_words(root / layout["blockdata_filepath"])

        emit_block(lines, f"static const u16 {border_name}[] = {{", format_u16_words(border_words))
        emit(lines)
        emit_block(lines, f"static const u16 {block_name}[] = {{", format_u16_words(block_words))
        emit(lines)
        emit(lines, f"const struct MapLayout {name} = {{")
        emit(lines, f"{INDENT}.width = {layout['width']},")
        emit(lines, f"{INDENT}.height = {layout['height']},")
        emit(lines, f"{INDENT}.border = {border_name},")
        emit(lines, f"{INDENT}.map = {block_name},")
        emit(lines, f"{INDENT}.primaryTileset = {c_address_or_null(layout['primary_tileset'])},")
        emit(lines, f"{INDENT}.secondaryTileset = {c_address_or_null(layout['secondary_tileset'])},")
        emit(lines, f"{INDENT}.borderWidth = {layout['border_width']},")
        emit(lines, f"{INDENT}.borderHeight = {layout['border_height']},")
        emit(lines, "};")
        emit(lines)

    emit(lines, "const struct MapLayout *gMapLayouts[] = {")
    for layout in layouts:
        if layout:
            emit(lines, f"{INDENT}&{layout['name']},")
        else:
            emit(lines, f"{INDENT}NULL,")
    emit(lines, "};")
    emit(lines)


def connection_direction(direction: str) -> str:
    table = {
        "up": "CONNECTION_NORTH",
        "down": "CONNECTION_SOUTH",
        "left": "CONNECTION_WEST",
        "right": "CONNECTION_EAST",
        "dive": "CONNECTION_DIVE",
        "emerge": "CONNECTION_EMERGE",
    }
    return table[direction]


def offset_expr(value) -> str:
    text = c_expr(value)
    if isinstance(value, int) and value < 0:
        return f"(u32)({text})"
    return text


def emit_object_events(lines: list[str], map_name: str, object_events: list[dict]) -> str:
    if not object_events:
        return "NULL"

    array_name = f"{map_name}_ObjectEvents"
    emit(lines, f"static const struct ObjectEventTemplate {array_name}[] = {{")
    for idx, obj in enumerate(object_events, start=1):
        emit(lines, f"{INDENT}{{")
        local_id = c_expr(obj.get("local_id", idx))
        emit(lines, f"{INDENT * 2}.localId = {local_id},")
        emit(lines, f"{INDENT * 2}.graphicsId = {obj['graphics_id']},")
        if obj["type"] == "clone":
            emit(lines, f"{INDENT * 2}.kind = OBJ_KIND_CLONE,")
            emit(lines, f"{INDENT * 2}.x = {obj['x']},")
            emit(lines, f"{INDENT * 2}.y = {obj['y']},")
            emit(lines, f"{INDENT * 2}.objUnion = {{ .clone = {{")
            emit(lines, f"{INDENT * 3}.targetLocalId = {obj['target_local_id']},")
            emit(lines, f"{INDENT * 3}.targetMapNum = ({obj['target_map']}) & 0xFF,")
            emit(lines, f"{INDENT * 3}.targetMapGroup = ({obj['target_map']}) >> 8,")
            emit(lines, INDENT * 2 + "} },")
            emit(lines, f"{INDENT * 2}.script = NULL,")
            emit(lines, f"{INDENT * 2}.flagId = 0,")
        else:
            emit(lines, f"{INDENT * 2}.kind = OBJ_KIND_NORMAL,")
            emit(lines, f"{INDENT * 2}.x = {obj['x']},")
            emit(lines, f"{INDENT * 2}.y = {obj['y']},")
            emit(lines, f"{INDENT * 2}.objUnion = {{ .normal = {{")
            emit(lines, f"{INDENT * 3}.elevation = {obj['elevation']},")
            emit(lines, f"{INDENT * 3}.movementType = {obj['movement_type']},")
            emit(lines, f"{INDENT * 3}.movementRangeX = {obj['movement_range_x']},")
            emit(lines, f"{INDENT * 3}.movementRangeY = {obj['movement_range_y']},")
            emit(lines, f"{INDENT * 3}.trainerType = {obj['trainer_type']},")
            emit(lines, f"{INDENT * 3}.trainerRange_berryTreeId = {obj['trainer_sight_or_berry_tree_id']},")
            emit(lines, INDENT * 2 + "} },")
            emit(lines, f"{INDENT * 2}.script = {c_symbol_or_null(obj.get('script', '0'))},")
            emit(lines, f"{INDENT * 2}.flagId = {obj['flag']},")
        emit(lines, f"{INDENT}}},")
    emit(lines, "};")
    emit(lines)
    return array_name


def emit_warp_events(lines: list[str], map_name: str, warp_events: list[dict]) -> str:
    if not warp_events:
        return "NULL"

    array_name = f"{map_name}_MapWarps"
    emit(lines, f"static const struct WarpEvent {array_name}[] = {{")
    for warp in warp_events:
        emit(lines, f"{INDENT}{{")
        emit(lines, f"{INDENT * 2}.x = {warp['x']},")
        emit(lines, f"{INDENT * 2}.y = {warp['y']},")
        emit(lines, f"{INDENT * 2}.elevation = {warp['elevation']},")
        emit(lines, f"{INDENT * 2}.warpId = {warp['dest_warp_id']},")
        emit(lines, f"{INDENT * 2}.mapNum = ({warp['dest_map']}) & 0xFF,")
        emit(lines, f"{INDENT * 2}.mapGroup = ({warp['dest_map']}) >> 8,")
        emit(lines, f"{INDENT}}},")
    emit(lines, "};")
    emit(lines)
    return array_name


def emit_coord_events(lines: list[str], map_name: str, coord_events: list[dict]) -> str:
    if not coord_events:
        return "NULL"

    array_name = f"{map_name}_MapCoordEvents"
    emit(lines, f"static const struct CoordEvent {array_name}[] = {{")
    for event in coord_events:
        emit(lines, f"{INDENT}{{")
        emit(lines, f"{INDENT * 2}.x = {event['x']},")
        emit(lines, f"{INDENT * 2}.y = {event['y']},")
        emit(lines, f"{INDENT * 2}.elevation = {event['elevation']},")
        emit(lines, f"{INDENT * 2}.trigger = {event['var']},")
        emit(lines, f"{INDENT * 2}.index = {event['var_value']},")
        emit(lines, f"{INDENT * 2}.script = {event['script']},")
        emit(lines, f"{INDENT}}},")
    emit(lines, "};")
    emit(lines)
    return array_name


def hidden_item_expr(event: dict) -> str:
    underfoot = "1" if event["underfoot"] else "0"
    return (
        f"(({event['item']}) << HIDDEN_ITEM_ITEM_SHIFT)"
        f" | ((({event['flag']}) - FLAG_HIDDEN_ITEMS_START) << HIDDEN_ITEM_FLAG_SHIFT)"
        f" | (({event['quantity']}) << HIDDEN_ITEM_QUANTITY_SHIFT)"
        f" | (({underfoot}) << HIDDEN_ITEM_UNDERFOOT_SHIFT)"
    )


def emit_bg_events(lines: list[str], map_name: str, bg_events: list[dict]) -> str:
    if not bg_events:
        return "NULL"

    array_name = f"{map_name}_MapBGEvents"
    emit(lines, f"static const struct BgEvent {array_name}[] = {{")
    for event in bg_events:
        emit(lines, f"{INDENT}{{")
        emit(lines, f"{INDENT * 2}.x = {event['x']},")
        emit(lines, f"{INDENT * 2}.y = {event['y']},")
        emit(lines, f"{INDENT * 2}.elevation = {event['elevation']},")
        if event["type"] == "hidden_item":
            emit(lines, f"{INDENT * 2}.kind = BG_EVENT_HIDDEN_ITEM,")
            emit(lines, f"{INDENT * 2}.bgUnion.hiddenItem = {hidden_item_expr(event)},")
        else:
            emit(lines, f"{INDENT * 2}.kind = {event['player_facing_dir']},")
            emit(lines, f"{INDENT * 2}.bgUnion.script = {event['script']},")
        emit(lines, f"{INDENT}}},")
    emit(lines, "};")
    emit(lines)
    return array_name


def emit_map_events(lines: list[str], map_name: str, map_data: dict) -> None:
    object_events_name = emit_object_events(lines, map_name, map_data["object_events"])
    warp_events_name = emit_warp_events(lines, map_name, map_data["warp_events"])
    coord_events_name = emit_coord_events(lines, map_name, map_data["coord_events"])
    bg_events_name = emit_bg_events(lines, map_name, map_data["bg_events"])

    emit(lines, f"static const struct MapEvents {map_name}_MapEvents = {{")
    emit(lines, f"{INDENT}.objectEventCount = {len(map_data['object_events'])},")
    emit(lines, f"{INDENT}.warpCount = {len(map_data['warp_events'])},")
    emit(lines, f"{INDENT}.coordEventCount = {len(map_data['coord_events'])},")
    emit(lines, f"{INDENT}.bgEventCount = {len(map_data['bg_events'])},")
    emit(lines, f"{INDENT}.objectEvents = {object_events_name},")
    emit(lines, f"{INDENT}.warps = {warp_events_name},")
    emit(lines, f"{INDENT}.coordEvents = {coord_events_name},")
    emit(lines, f"{INDENT}.bgEvents = {bg_events_name},")
    emit(lines, "};")
    emit(lines)


def emit_map_connections(lines: list[str], map_name: str, connections: list[dict] | None) -> str:
    if not connections:
        return "NULL"

    list_name = f"{map_name}_MapConnectionsList"
    struct_name = f"{map_name}_MapConnections"
    emit(lines, f"static const struct MapConnection {list_name}[] = {{")
    for connection in connections:
        emit(lines, f"{INDENT}{{")
        emit(lines, f"{INDENT * 2}.direction = {connection_direction(connection['direction'])},")
        emit(lines, f"{INDENT * 2}.offset = {offset_expr(connection['offset'])},")
        emit(lines, f"{INDENT * 2}.mapGroup = ({connection['map']}) >> 8,")
        emit(lines, f"{INDENT * 2}.mapNum = ({connection['map']}) & 0xFF,")
        emit(lines, f"{INDENT}}},")
    emit(lines, "};")
    emit(lines)
    emit(lines, f"static const struct MapConnections {struct_name} = {{")
    emit(lines, f"{INDENT}.count = {len(connections)},")
    emit(lines, f"{INDENT}.connections = {list_name},")
    emit(lines, "};")
    emit(lines)
    return f"&{struct_name}"


def emit_map_headers(lines: list[str], maps_by_name: dict[str, dict], map_groups: dict) -> None:
    for group_name in map_groups["group_order"]:
        for map_name in map_groups[group_name]:
            map_data = maps_by_name[map_name]
            emit_map_events(lines, map_name, map_data)
            connections_name = emit_map_connections(lines, map_name, map_data["connections"])
            emit(lines, f"const struct MapHeader {map_name} = {{")
            emit(lines, f"{INDENT}.mapLayout = &{map_data['layout_name']},")
            emit(lines, f"{INDENT}.events = &{map_name}_MapEvents,")
            emit(lines, f"{INDENT}.mapScripts = {map_name}_MapScripts,")
            emit(lines, f"{INDENT}.connections = {connections_name},")
            emit(lines, f"{INDENT}.music = {map_data['music']},")
            emit(lines, f"{INDENT}.mapLayoutId = {map_data['layout']},")
            emit(lines, f"{INDENT}.regionMapSectionId = {map_data['region_map_section']},")
            emit(lines, f"{INDENT}.cave = {c_expr(map_data['requires_flash'])},")
            emit(lines, f"{INDENT}.weather = {map_data['weather']},")
            emit(lines, f"{INDENT}.mapType = {map_data['map_type']},")
            emit(lines, f"{INDENT}.bikingAllowed = {c_expr(map_data['allow_cycling'])},")
            emit(lines, f"{INDENT}.allowEscaping = {c_expr(map_data['allow_escaping'])},")
            emit(lines, f"{INDENT}.allowRunning = {c_expr(map_data['allow_running'])},")
            emit(lines, f"{INDENT}.showMapName = {c_expr(map_data['show_map_name'])},")
            emit(lines, f"{INDENT}.floorNum = {map_data['floor_number']},")
            emit(lines, f"{INDENT}.battleType = {map_data['battle_scene']},")
            emit(lines, "};")
            emit(lines)

    for group_name in map_groups["group_order"]:
        emit(lines, f"static const struct MapHeader *const {group_name}[] = {{")
        for map_name in map_groups[group_name]:
            emit(lines, f"{INDENT}&{map_name},")
        emit(lines, "};")
        emit(lines)

    emit(lines, "const struct MapHeader *const *gMapGroups[] = {")
    for group_name in map_groups["group_order"]:
        emit(lines, f"{INDENT}{group_name},")
    emit(lines, "};")
    emit(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pokedir", required=True, help="Path to third_party/pokefirered")
    parser.add_argument("--output", required=True, help="Output C file")
    args = parser.parse_args()

    pokedir = Path(args.pokedir).resolve()
    output = Path(args.output).resolve()

    layouts_json = load_json(pokedir / "data/layouts/layouts.json")
    layouts = layouts_json["layouts"]
    layouts_by_id = {layout["id"]: layout for layout in layouts if layout.get("id")}
    map_groups = load_json(pokedir / "data/maps/map_groups.json")
    map_json_paths = sorted((pokedir / "data/maps").glob("*/map.json"))
    maps_by_name = {}

    for map_path in map_json_paths:
        map_data = load_json(map_path)
        map_data["layout_name"] = layouts_by_id[map_data["layout"]]["name"]
        maps_by_name[map_data["name"]] = map_data

    output.parent.mkdir(parents=True, exist_ok=True)
    lines: list[str] = []
    emit(lines, "/*")
    emit(lines, " * Auto-generated by tools/map_data_to_c.py")
    emit(lines, " * DO NOT EDIT — regenerate from upstream JSON map sources.")
    emit(lines, " */")
    emit(lines)
    emit(lines, '#include "global.h"')
    emit(lines, '#include "global.fieldmap.h"')
    emit(lines, '#include "constants/global.h"')
    emit(lines, '#include "constants/event_bg.h"')
    emit(lines, '#include "constants/event_object_movement.h"')
    emit(lines, '#include "constants/event_objects.h"')
    emit(lines, '#include "constants/flags.h"')
    emit(lines, '#include "constants/items.h"')
    emit(lines, '#include "constants/layouts.h"')
    emit(lines, '#include "constants/map_types.h"')
    emit(lines, '#include "constants/maps.h"')
    emit(lines, '#include "constants/region_map_sections.h"')
    emit(lines, '#include "constants/songs.h"')
    emit(lines, '#include "constants/trainer_types.h"')
    emit(lines, '#include "constants/vars.h"')
    emit(lines, '#include "constants/weather.h"')
    emit(lines, '#include "upstream_event_scripts.h"')
    emit(lines)

    tilesets = collect_tilesets(layouts)
    for tileset in sorted(tilesets):
        emit(lines, f"extern const struct Tileset {tileset};")
    emit(lines)
    emit_layout_data(lines, pokedir, layouts)

    emit_map_headers(lines, maps_by_name, map_groups)
    output.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
