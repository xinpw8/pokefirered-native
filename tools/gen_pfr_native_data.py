#!/usr/bin/env python3
"""
gen_pfr_native_data.py — Generate pfr_native data tables from pokefirered decomp.

Outputs:
  - pfr_native_generated.h: flag/var/script/dialog enums
  - pfr_native_data.c: all data arrays (tiles, warps, scripts, dialogs, etc.)

Parses:
  - include/constants/flags.h, vars.h → flag/var constant lookup
  - data/maps/*/scripts.inc → script actions (setflag/setvar/clearflag/trainerbattle)
  - data/maps/*/map.json → warps, objects, bg_events, coord_events
  - data/maps/*/text.inc → dialog text
"""

import argparse
import json
import re
import struct
import sys
from collections import OrderedDict
from pathlib import Path

PFR_NATIVE_MAP_INVALID_LITERAL = "((PfrNativeMapId)0xFFFF)"
MAX_FLAGS = 64
MAX_VARS = 32

PRIORITY_MAP_ORDER = [
    "PalletTown_PlayersHouse_1F",
    "PalletTown_PlayersHouse_2F",
]

SPECIAL_REPLACEMENTS = {
    "…": "...",
    "POKéMON": "POKEMON",
    "{PLAYER}": "@PLAYER@",
    "é": "e",
}

BG_EVENT_FACING_MASKS = {
    "BG_EVENT_PLAYER_FACING_ANY": 0x0F,
    "BG_EVENT_PLAYER_FACING_NORTH": 1 << 1,
    "BG_EVENT_PLAYER_FACING_SOUTH": 1 << 2,
    "BG_EVENT_PLAYER_FACING_WEST": 1 << 3,
    "BG_EVENT_PLAYER_FACING_EAST": 1 << 4,
}

CONNECTION_DIRECTION_ENUM = {
    "up": "PFR_NATIVE_CONN_NORTH",
    "down": "PFR_NATIVE_CONN_SOUTH",
    "left": "PFR_NATIVE_CONN_WEST",
    "right": "PFR_NATIVE_CONN_EAST",
}

# ============================================================
# Flag/var definitions that MUST be included (critical path).
# Badge flags MUST be indices 0-7 for pfrn_badge_count().
# ============================================================
REQUIRED_FLAGS = [
    # Badges (indices 0-7, must be contiguous and in order)
    "FLAG_BADGE01_GET",
    "FLAG_BADGE02_GET",
    "FLAG_BADGE03_GET",
    "FLAG_BADGE04_GET",
    "FLAG_BADGE05_GET",
    "FLAG_BADGE06_GET",
    "FLAG_BADGE07_GET",
    "FLAG_BADGE08_GET",
    # System
    "FLAG_SYS_POKEMON_GET",
    "FLAG_SYS_POKEDEX_GET",
    "FLAG_SYS_GAME_CLEAR",
    # Gym leader defeats
    "FLAG_DEFEATED_BROCK",
    "FLAG_DEFEATED_MISTY",
    "FLAG_DEFEATED_LT_SURGE",
    "FLAG_DEFEATED_ERIKA",
    "FLAG_DEFEATED_KOGA",
    "FLAG_DEFEATED_SABRINA",
    "FLAG_DEFEATED_BLAINE",
    "FLAG_DEFEATED_LEADER_GIOVANNI",
    # Elite Four + Champion
    "FLAG_DEFEATED_LORELEI",
    "FLAG_DEFEATED_BRUNO",
    "FLAG_DEFEATED_AGATHA",
    "FLAG_DEFEATED_LANCE",
    "FLAG_DEFEATED_CHAMP",
    # Progression keys
    "FLAG_BEAT_RIVAL_IN_OAKS_LAB",
    "FLAG_VISITED_OAKS_LAB",
    "FLAG_GOT_POKEBALLS_FROM_OAK_AFTER_22_RIVAL",
    # Post-starter hide flags
    "FLAG_HIDE_OAK_IN_PALLET_TOWN",
    "FLAG_HIDE_OAK_IN_HIS_LAB",
    "FLAG_HIDE_BULBASAUR_BALL",
    "FLAG_HIDE_SQUIRTLE_BALL",
    "FLAG_HIDE_CHARMANDER_BALL",
    "FLAG_HIDE_RIVAL_IN_LAB",
    "FLAG_HIDE_POKEDEX",
    # Critical-path progression flags
    "FLAG_GOT_SS_TICKET",
    "FLAG_HELPED_BILL_IN_SEA_COTTAGE",
    "FLAG_GOT_HM01",               # Cut
    "FLAG_GOT_HM03",               # Surf
    "FLAG_GOT_HM04",               # Strength
    "FLAG_GOT_HM05",               # Flash
    "FLAG_GOT_POKE_FLUTE",
    "FLAG_RESCUED_MR_FUJI",
    "FLAG_GOT_TEA",
    "FLAG_OPENED_ROCKET_HIDEOUT",
    "FLAG_CAN_USE_ROCKET_HIDEOUT_LIFT",
    "FLAG_HIDE_SILPH_SCOPE",   # Silph Scope obtained (hide flag used as "got" flag)
    "FLAG_GOT_FOSSIL_FROM_MT_MOON",
    "FLAG_GOT_DOME_FOSSIL",
    "FLAG_GOT_HELIX_FOSSIL",
    "FLAG_HIDE_POKEMON_MANSION_B1F_SECRET_KEY",
    "FLAG_HIDE_ROUTE_12_SNORLAX",
    "FLAG_HIDE_ROUTE_16_SNORLAX",
    "FLAG_SYS_B_DASH",             # Running shoes
    "FLAG_PALLET_LADY_NOT_BLOCKING_SIGN",
    "FLAG_CINNABAR_GYM_QUIZ_1",
    "FLAG_CINNABAR_GYM_QUIZ_2",
    "FLAG_CINNABAR_GYM_QUIZ_3",
    "FLAG_CINNABAR_GYM_QUIZ_4",
    "FLAG_CINNABAR_GYM_QUIZ_5",
    "FLAG_CINNABAR_GYM_QUIZ_6",
]

REQUIRED_VARS = [
    "VAR_MAP_SCENE_PALLET_TOWN_PLAYERS_HOUSE_2F",
    "VAR_MAP_SCENE_PALLET_TOWN_PROFESSOR_OAKS_LAB",
    "VAR_MAP_SCENE_PALLET_TOWN_OAK",
    "VAR_MAP_SCENE_VIRIDIAN_CITY_MART",
    "VAR_MAP_SCENE_VIRIDIAN_CITY_GYM_DOOR",
    "VAR_MAP_SCENE_ROUTE5_ROUTE6_ROUTE7_ROUTE8_GATES",
    "VAR_MAP_SCENE_MT_MOON_B2F",
    "VAR_MAP_SCENE_PEWTER_CITY",
    "VAR_MAP_SCENE_ROUTE22",
    "VAR_STARTER_MON",
    "VAR_MAP_SCENE_POKEMON_TOWER_6F",
    "VAR_MAP_SCENE_VICTORY_ROAD_1F",
    "VAR_MAP_SCENE_VICTORY_ROAD_2F_BOULDER1",
    "VAR_MAP_SCENE_VICTORY_ROAD_2F_BOULDER2",
    "VAR_MAP_SCENE_VICTORY_ROAD_3F",
]

# Scripts with complex branching that need hand-authored handling.
# The generator assigns them IDs but does NOT emit action tables.
LEGACY_SCRIPTS = {
    "PalletTown_PlayersHouse_1F_EventScript_Mom": "PLAYERS_HOUSE_1F_MOM",
    "PalletTown_PlayersHouse_1F_EventScript_TV": "PLAYERS_HOUSE_1F_TV",
    "PalletTown_PlayersHouse_2F_EventScript_NES": "PLAYERS_HOUSE_2F_NES",
    "PalletTown_PlayersHouse_2F_EventScript_Sign": "PLAYERS_HOUSE_2F_SIGN",
    "PalletTown_PlayersHouse_2F_EventScript_PC": "PLAYERS_HOUSE_2F_PC",
}

# ============================================================
# Script overrides — hand-authored state transitions for
# blockers too complex for the auto-parser. Same data format
# as auto-extracted scripts: guard → actions.
# Each entry: label → {actions: [(type, flag/var_name, value, extra)],
#                       guard: (guard_type, param_name, value) or None}
# Flag/var NAMES are resolved to compact indices at emit time.
# ============================================================
SCRIPT_OVERRIDES = {
    # --- Mt. Moon: fossil selection (auto-pick Dome Fossil) ---
    "MtMoon_B2F_EventScript_DomeFossil": {
        "c_name": "MT_MOON_FOSSIL_DOME",
        "actions": [
            ("SET_FLAG", "FLAG_GOT_DOME_FOSSIL"),
            ("SET_FLAG", "FLAG_GOT_FOSSIL_FROM_MT_MOON"),
        ],
    },
    "MtMoon_B2F_EventScript_HelixFossil": {
        "c_name": "MT_MOON_FOSSIL_HELIX",
        "actions": [
            ("SET_FLAG", "FLAG_GOT_HELIX_FOSSIL"),
            ("SET_FLAG", "FLAG_GOT_FOSSIL_FROM_MT_MOON"),
        ],
    },
    # --- Bill's Sea Cottage: auto-help Bill, get S.S. Ticket ---
    "Route25_SeaCottage_EventScript_Bill": {
        "c_name": "BILL_SEA_COTTAGE",
        "actions": [
            ("SET_FLAG", "FLAG_HELPED_BILL_IN_SEA_COTTAGE"),
            ("SET_FLAG", "FLAG_GOT_SS_TICKET"),
        ],
        "guard": ("FLAG_UNSET", "FLAG_GOT_SS_TICKET"),
    },
    # --- S.S. Anne Captain: give Cut HM ---
    "SSAnne_CaptainsOffice_EventScript_Captain": {
        "c_name": "SS_ANNE_CAPTAIN",
        "actions": [
            ("SET_FLAG", "FLAG_GOT_HM01"),
        ],
        "guard": ("FLAG_UNSET", "FLAG_GOT_HM01"),
    },
    # --- Celadon Condominiums: get Tea ---
    "CeladonCity_Condominiums_1F_EventScript_TeaWoman": {
        "c_name": "CELADON_GET_TEA",
        "actions": [
            ("SET_FLAG", "FLAG_GOT_TEA"),
        ],
        "guard": ("FLAG_UNSET", "FLAG_GOT_TEA"),
    },
    # --- Saffron City guard gates: open with tea ---
    "Route5_SouthEntrance_EventScript_Guard": {
        "c_name": "SAFFRON_GUARD_ROUTE5",
        "actions": [
            ("SET_VAR", "VAR_MAP_SCENE_ROUTE5_ROUTE6_ROUTE7_ROUTE8_GATES", 1),
        ],
        "guard": ("FLAG_SET", "FLAG_GOT_TEA"),
    },
    "Route6_NorthEntrance_EventScript_Guard": {
        "c_name": "SAFFRON_GUARD_ROUTE6",
        "actions": [
            ("SET_VAR", "VAR_MAP_SCENE_ROUTE5_ROUTE6_ROUTE7_ROUTE8_GATES", 1),
        ],
        "guard": ("FLAG_SET", "FLAG_GOT_TEA"),
    },
    "Route7_EastEntrance_EventScript_Guard": {
        "c_name": "SAFFRON_GUARD_ROUTE7",
        "actions": [
            ("SET_VAR", "VAR_MAP_SCENE_ROUTE5_ROUTE6_ROUTE7_ROUTE8_GATES", 1),
        ],
        "guard": ("FLAG_SET", "FLAG_GOT_TEA"),
    },
    "Route8_WestEntrance_EventScript_Guard": {
        "c_name": "SAFFRON_GUARD_ROUTE8",
        "actions": [
            ("SET_VAR", "VAR_MAP_SCENE_ROUTE5_ROUTE6_ROUTE7_ROUTE8_GATES", 1),
        ],
        "guard": ("FLAG_SET", "FLAG_GOT_TEA"),
    },
    # --- Game Corner Rocket grunt: open Rocket Hideout ---
    "CeladonCity_GameCorner_EventScript_RocketGrunt": {
        "c_name": "GAME_CORNER_ROCKET",
        "actions": [
            ("SET_FLAG", "FLAG_OPENED_ROCKET_HIDEOUT"),
        ],
    },
    # --- Rocket Hideout B4F: get Lift Key ---
    "RocketHideout_B4F_EventScript_LiftKey": {
        "c_name": "ROCKET_HIDEOUT_LIFT_KEY",
        "actions": [
            ("SET_FLAG", "FLAG_CAN_USE_ROCKET_HIDEOUT_LIFT"),
        ],
    },
    # --- Rocket Hideout B4F: beat Giovanni, get Silph Scope ---
    "RocketHideout_B4F_EventScript_Giovanni": {
        "c_name": "ROCKET_HIDEOUT_GIOVANNI",
        "actions": [
            ("SET_FLAG", "FLAG_HIDE_SILPH_SCOPE"),
        ],
    },
    # --- Pokemon Tower 7F: rescue Mr. Fuji ---
    "PokemonTower_7F_EventScript_MrFuji": {
        "c_name": "POKEMON_TOWER_MR_FUJI",
        "actions": [
            ("SET_FLAG", "FLAG_RESCUED_MR_FUJI"),
            ("SET_FLAG", "FLAG_GOT_POKE_FLUTE"),
        ],
    },
    # --- Lavender Town: Mr. Fuji gives Poke Flute ---
    "LavenderTown_VolunteerPokemonHouse_EventScript_MrFuji": {
        "c_name": "LAVENDER_MR_FUJI_FLUTE",
        "actions": [
            ("SET_FLAG", "FLAG_GOT_POKE_FLUTE"),
        ],
        "guard": ("FLAG_SET", "FLAG_RESCUED_MR_FUJI"),
    },
    # --- Route 12 Snorlax: wake and hide ---
    "Route12_EventScript_Snorlax": {
        "c_name": "ROUTE12_SNORLAX",
        "actions": [
            ("SET_FLAG", "FLAG_HIDE_ROUTE_12_SNORLAX"),
        ],
        "guard": ("FLAG_SET", "FLAG_GOT_POKE_FLUTE"),
    },
    # --- Route 16 Snorlax: wake and hide ---
    "Route16_EventScript_Snorlax": {
        "c_name": "ROUTE16_SNORLAX",
        "actions": [
            ("SET_FLAG", "FLAG_HIDE_ROUTE_16_SNORLAX"),
        ],
        "guard": ("FLAG_SET", "FLAG_GOT_POKE_FLUTE"),
    },
    # --- Pokemon Mansion B1F: get Secret Key for Cinnabar Gym ---
    "PokemonMansion_B1F_EventScript_ItemSecretKey": {
        "c_name": "POKEMON_MANSION_SECRET_KEY",
        "actions": [
            ("SET_FLAG", "FLAG_HIDE_POKEMON_MANSION_B1F_SECRET_KEY"),
        ],
    },
    # --- Viridian Gym door: unlock with 7 badges ---
    "ViridianCity_EventScript_TryUnlockGym": {
        "c_name": "VIRIDIAN_GYM_UNLOCK",
        "actions": [
            ("SET_VAR", "VAR_MAP_SCENE_VIRIDIAN_CITY_GYM_DOOR", 1),
        ],
        "guard": ("BADGE_GE", None, 7),
    },
    # --- Cut trees: hide tree if player has HM01 ---
    "EventScript_CutTree": {
        "c_name": "CUT_TREE",
        "actions": [],  # Handled in C: deactivate the object
        "guard": ("FLAG_SET", "FLAG_GOT_HM01"),
    },
    # --- Safari Zone entrance: auto-enter ---
    "FuchsiaCity_SafariZone_Entrance_EventScript_InfoAttendant": {
        "c_name": "SAFARI_ZONE_ENTER",
        "actions": [],  # Just allow passage
    },
    # --- Warden: get HM04 Strength ---
    "FuchsiaCity_WardensHouse_EventScript_Warden": {
        "c_name": "WARDEN_HM04",
        "actions": [
            ("SET_FLAG", "FLAG_GOT_HM04"),
        ],
    },
    # --- Route 2 house: get HM05 Flash ---
    "Route2_EastBuilding_EventScript_Aide": {
        "c_name": "ROUTE2_HM05",
        "actions": [
            ("SET_FLAG", "FLAG_GOT_HM05"),
        ],
    },
    # --- Pokemon Tower 6F: ghost Marowak battle ---
    # With Silph Scope, interact → auto-win, set scene var to clear ghost.
    # Without Silph Scope → blocked (guard fails).
    "PokemonTower_6F_EventScript_MarowakGhost": {
        "c_name": "POKEMON_TOWER_GHOST",
        "actions": [
            ("SET_VAR", "VAR_MAP_SCENE_POKEMON_TOWER_6F", 1),
        ],
        "guard": ("FLAG_SET", "FLAG_HIDE_SILPH_SCOPE"),
    },
    # --- Vermilion Gym: trash can puzzle auto-solve ---
    # Any trash can interaction auto-sets all quiz flags to bypass puzzle.
    "VermilionCity_Gym_EventScript_TrashCan": {
        "c_name": "VERMILION_GYM_TRASH",
        "actions": [],  # Guard-only: deactivates blocking object
    },
    # --- Cinnabar Gym quiz: each trainer's defeat opens door ---
    "CinnabarIsland_Gym_EventScript_Quiz1": {
        "c_name": "CINNABAR_GYM_QUIZ_1",
        "actions": [
            ("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_1"),
        ],
    },
    "CinnabarIsland_Gym_EventScript_Quiz2": {
        "c_name": "CINNABAR_GYM_QUIZ_2",
        "actions": [
            ("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_2"),
        ],
    },
    "CinnabarIsland_Gym_EventScript_Quiz3": {
        "c_name": "CINNABAR_GYM_QUIZ_3",
        "actions": [
            ("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_3"),
        ],
    },
    "CinnabarIsland_Gym_EventScript_Quiz4": {
        "c_name": "CINNABAR_GYM_QUIZ_4",
        "actions": [
            ("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_4"),
        ],
    },
    "CinnabarIsland_Gym_EventScript_Quiz5": {
        "c_name": "CINNABAR_GYM_QUIZ_5",
        "actions": [
            ("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_5"),
        ],
    },
    "CinnabarIsland_Gym_EventScript_Quiz6": {
        "c_name": "CINNABAR_GYM_QUIZ_6",
        "actions": [
            ("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_6"),
        ],
    },
    # --- Cinnabar Gym quiz bg_events: auto-answer correctly ---
    # bg_events reference Quz1Left/Right (decomp typo) and Quiz2-6 Left/Right.
    # Each goto's the corresponding QuizN label, but the auto-parser can't
    # follow the branching, so we need explicit overrides.
    "CinnabarIsland_Gym_EventScript_Quz1Left": {
        "c_name": "CINNABAR_GYM_QUZ1_LEFT",
        "actions": [("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_1")],
    },
    "CinnabarIsland_Gym_EventScript_Quz1Right": {
        "c_name": "CINNABAR_GYM_QUZ1_RIGHT",
        "actions": [("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_1")],
    },
    "CinnabarIsland_Gym_EventScript_Quiz2Left": {
        "c_name": "CINNABAR_GYM_QUIZ2_LEFT",
        "actions": [("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_2")],
    },
    "CinnabarIsland_Gym_EventScript_Quiz2Right": {
        "c_name": "CINNABAR_GYM_QUIZ2_RIGHT",
        "actions": [("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_2")],
    },
    "CinnabarIsland_Gym_EventScript_Quiz3Left": {
        "c_name": "CINNABAR_GYM_QUIZ3_LEFT",
        "actions": [("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_3")],
    },
    "CinnabarIsland_Gym_EventScript_Quiz3Right": {
        "c_name": "CINNABAR_GYM_QUIZ3_RIGHT",
        "actions": [("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_3")],
    },
    "CinnabarIsland_Gym_EventScript_Quiz4Left": {
        "c_name": "CINNABAR_GYM_QUIZ4_LEFT",
        "actions": [("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_4")],
    },
    "CinnabarIsland_Gym_EventScript_Quiz4Right": {
        "c_name": "CINNABAR_GYM_QUIZ4_RIGHT",
        "actions": [("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_4")],
    },
    "CinnabarIsland_Gym_EventScript_Quiz5Left": {
        "c_name": "CINNABAR_GYM_QUIZ5_LEFT",
        "actions": [("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_5")],
    },
    "CinnabarIsland_Gym_EventScript_Quiz5Right": {
        "c_name": "CINNABAR_GYM_QUIZ5_RIGHT",
        "actions": [("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_5")],
    },
    "CinnabarIsland_Gym_EventScript_Quiz6Left": {
        "c_name": "CINNABAR_GYM_QUIZ6_LEFT",
        "actions": [("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_6")],
    },
    "CinnabarIsland_Gym_EventScript_Quiz6Right": {
        "c_name": "CINNABAR_GYM_QUIZ6_RIGHT",
        "actions": [("SET_FLAG", "FLAG_CINNABAR_GYM_QUIZ_6")],
    },
    # --- Victory Road floor switches: set scene vars to open barriers ---
    "VictoryRoad_1F_EventScript_FloorSwitch": {
        "c_name": "VICTORY_ROAD_1F_SWITCH",
        "actions": [
            ("SET_VAR", "VAR_MAP_SCENE_VICTORY_ROAD_1F", 100),
        ],
    },
    "VictoryRoad_2F_EventScript_FloorSwitch1": {
        "c_name": "VICTORY_ROAD_2F_SWITCH1",
        "actions": [
            ("SET_VAR", "VAR_MAP_SCENE_VICTORY_ROAD_2F_BOULDER1", 100),
        ],
    },
    "VictoryRoad_2F_EventScript_FloorSwitch2": {
        "c_name": "VICTORY_ROAD_2F_SWITCH2",
        "actions": [
            ("SET_VAR", "VAR_MAP_SCENE_VICTORY_ROAD_2F_BOULDER2", 100),
        ],
    },
    "VictoryRoad_3F_EventScript_FloorSwitch": {
        "c_name": "VICTORY_ROAD_3F_SWITCH",
        "actions": [
            ("SET_VAR", "VAR_MAP_SCENE_VICTORY_ROAD_3F", 100),
        ],
    },
    # --- Champion battle: MAP_SCRIPT_ON_FRAME, not an object_event ---
    # The E4 handler can't find it because it only checks object_events.
    # We register it here and inject a synthetic coord_event in build_map_data.
    "PokemonLeague_ChampionsRoom_EventScript_EnterRoom": {
        "c_name": "CHAMPION_BATTLE",
        "actions": [
            ("SET_FLAG", "FLAG_DEFEATED_CHAMP"),
            ("SET_FLAG", "FLAG_SYS_GAME_CLEAR"),
        ],
    },
}


# ============================================================
# Utility functions
# ============================================================

def load_json(path):
    return json.loads(Path(path).read_text())


def parse_int(value, default=0):
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return default
    return default


def sanitize_symbol(value):
    symbol = re.sub(r"[^A-Za-z0-9_]", "_", value)
    if not symbol:
        symbol = "MAP"
    if symbol[0].isdigit():
        symbol = f"MAP_{symbol}"
    return symbol.upper()


def c_string(value):
    return '"' + (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
    ) + '"'


def facing_mask_for(bg_event):
    return BG_EVENT_FACING_MASKS.get(bg_event.get("player_facing_dir"), 0x0F)


def movement_to_facing(movement_type):
    if not movement_type:
        return "PFR_NATIVE_DIR_SOUTH"
    text = movement_type.upper()
    if "DOWN" in text and "UP" not in text:
        return "PFR_NATIVE_DIR_SOUTH"
    if "UP" in text and "DOWN" not in text:
        return "PFR_NATIVE_DIR_NORTH"
    if "LEFT" in text and "RIGHT" not in text:
        return "PFR_NATIVE_DIR_WEST"
    if "RIGHT" in text and "LEFT" not in text:
        return "PFR_NATIVE_DIR_EAST"
    if "UP" in text:
        return "PFR_NATIVE_DIR_NORTH"
    if "LEFT" in text:
        return "PFR_NATIVE_DIR_WEST"
    if "RIGHT" in text:
        return "PFR_NATIVE_DIR_EAST"
    return "PFR_NATIVE_DIR_SOUTH"


# ============================================================
# Constants parser — reads flags.h and vars.h
# ============================================================

def parse_constants_file(path):
    """Parse a C header with #define NAME value → {name: resolved_int_value}."""
    raw = {}
    # Also extract values from trailing comments like "// 0x800"
    comment_vals = {}
    for line in Path(path).read_text().splitlines():
        m = re.match(r"#define\s+(\w+)\s+(.+?)(?:\s*//.*)?\s*$", line)
        if m:
            name = m.group(1)
            # Only capture flags/vars/known constants
            if (name.startswith("FLAG_") or name.startswith("VAR_") or
                name in ("SYS_FLAGS", "TRAINER_FLAGS_END", "TRAINER_FLAGS_START",
                          "MAX_TRAINERS_COUNT", "VARS_START", "VARS_END",
                          "SPECIAL_VARS_START")):
                raw[name] = m.group(2).strip()
                # Check for hex value in comment
                cm = re.search(r"//\s*(0x[0-9A-Fa-f]+)", line)
                if cm:
                    comment_vals[name] = int(cm.group(1), 16)

    # Pre-seed known base values from comments (fallback for unresolvable chains)
    known = dict(comment_vals)

    def resolve(expr):
        expr = expr.strip()
        # Direct hex/decimal
        try:
            return int(expr, 0)
        except ValueError:
            pass
        # Expression like (SYS_FLAGS + 0x20) or (A + B - C)
        m = re.match(r"\((\w+)\s*\+\s*(.+)\)", expr)
        if m:
            base_name = m.group(1)
            base = known.get(base_name) or resolve(raw.get(base_name, base_name))
            offset = resolve(m.group(2))
            if base is not None and offset is not None:
                return base + offset
        # Simple symbol reference
        if expr in known:
            return known[expr]
        if expr in raw:
            val = resolve(raw[expr])
            if val is not None:
                known[expr] = val
            return val
        return None

    resolved = {}
    for name, expr in raw.items():
        val = resolve(expr)
        if val is not None:
            resolved[name] = val
            known[name] = val
    return resolved


def load_constants(repo_root):
    """Load flag and var constant definitions from the decomp."""
    flags = parse_constants_file(
        repo_root / "third_party/pokefirered/include/constants/flags.h"
    )
    vars_ = parse_constants_file(
        repo_root / "third_party/pokefirered/include/constants/vars.h"
    )
    return flags, vars_


def load_trainer_constants(repo_root):
    """Load TRAINER_* constants from opponents.h → {name: int_id}."""
    path = repo_root / "third_party/pokefirered/include/constants/opponents.h"
    if not path.exists():
        return {}
    result = {}
    for line in path.read_text().splitlines():
        m = re.match(r"#define\s+(TRAINER_\w+)\s+(\d+)", line)
        if m:
            result[m.group(1)] = int(m.group(2))
    return result


# ============================================================
# Script parser — reads scripts.inc for state mutations
# ============================================================

def parse_scripts_inc(path):
    """Parse a scripts.inc file into label → list of script commands."""
    if not path.exists():
        return {}
    labels = {}
    current = None
    for line in path.read_text().splitlines():
        stripped = line.strip()
        if stripped.endswith("::") and not stripped.startswith("."):
            current = stripped[:-2]
            labels[current] = []
            continue
        if current is not None and stripped and not stripped.startswith("@"):
            labels[current].append(stripped)
    return labels


def extract_script_actions(label, labels, flag_constants, var_constants,
                            flag_index, var_index, dialog_collector,
                            map_dir, repo_root, depth=0,
                            trainer_constants=None):
    """Walk a script label and extract state-mutation actions.

    Returns list of (action_type, param, value, extra) tuples and a guard dict.
    """
    if depth > 5 or label not in labels:
        return [], None

    actions = []
    guard = None
    lines = labels[label]

    for line in lines:
        # setflag FLAG_*
        m = re.match(r"setflag\s+(FLAG_\w+)", line)
        if m:
            flag_name = m.group(1)
            idx = flag_index.get(flag_name)
            if idx is not None:
                actions.append(("PFRN_ACT_SET_FLAG", idx, 0, 0))
            continue

        # clearflag FLAG_*
        m = re.match(r"clearflag\s+(FLAG_\w+)", line)
        if m:
            flag_name = m.group(1)
            idx = flag_index.get(flag_name)
            if idx is not None:
                actions.append(("PFRN_ACT_CLEAR_FLAG", idx, 0, 0))
            continue

        # setvar VAR_*, value
        m = re.match(r"setvar\s+(VAR_\w+),\s*(\d+)", line)
        if m:
            var_name = m.group(1)
            idx = var_index.get(var_name)
            if idx is not None:
                actions.append(("PFRN_ACT_SET_VAR", idx, int(m.group(2)), 0))
            continue

        # trainerbattle_* TRAINER_* — emit AUTO_BATTLE action
        # Format: trainerbattle_single TRAINER_X, TEXT_INTRO, TEXT_DEFEAT, DefeatCallback
        # Encoding: value=trainer_id_lo, param=trainer_id_hi, extra=PFRN_FLAG_NONE
        # The defeat callback is followed separately; flag setting happens there.
        m = re.match(r"trainerbattle_\w+\s+(TRAINER_\w+)", line)
        if m:
            trainer_name = m.group(1)
            tid = (trainer_constants or {}).get(trainer_name)
            if tid is not None:
                tid_lo = tid & 0xFF
                tid_hi = (tid >> 8) & 0xFF
                actions.append(("PFRN_ACT_AUTO_BATTLE", tid_hi, tid_lo, 0xFF))
            # Also follow the defeat callback if present
            m_cb = re.match(
                r"trainerbattle_\w+\s+\w+,\s*\w+,\s*\w+,\s*(\w+)", line)
            if m_cb and m_cb.group(1) in labels:
                sub_actions, _ = extract_script_actions(
                    m_cb.group(1), labels, flag_constants, var_constants,
                    flag_index, var_index, dialog_collector, map_dir,
                    repo_root, depth + 1, trainer_constants)
                actions.extend(sub_actions)
            continue

        # msgbox TEXT_LABEL — extract dialog reference
        m = re.match(r"msgbox\s+(\w+)", line)
        if m:
            dialog_collector.add((map_dir, m.group(1)))
            continue

        # goto LABEL — follow inline
        m = re.match(r"goto\s+(\w+)", line)
        if m:
            sub_actions, _ = extract_script_actions(
                m.group(1), labels, flag_constants, var_constants,
                flag_index, var_index, dialog_collector, map_dir, repo_root,
                depth + 1, trainer_constants
            )
            actions.extend(sub_actions)
            break  # goto is unconditional

        # goto_if_set FLAG_*, LABEL
        m = re.match(r"goto_if_set\s+(FLAG_\w+),\s*(\w+)", line)
        if m:
            idx = flag_index.get(m.group(1))
            if idx is not None and guard is None:
                guard = {"type": "PFRN_GUARD_FLAG_SET", "param": idx, "value": 0}
            continue

        # goto_if_unset FLAG_*, LABEL
        m = re.match(r"goto_if_unset\s+(FLAG_\w+),\s*(\w+)", line)
        if m:
            idx = flag_index.get(m.group(1))
            if idx is not None and guard is None:
                guard = {"type": "PFRN_GUARD_FLAG_UNSET", "param": idx, "value": 0}
            continue

        # goto_if_eq VAR_*, value, LABEL
        m = re.match(r"goto_if_eq\s+(VAR_\w+),\s*(\d+),\s*(\w+)", line)
        if m:
            idx = var_index.get(m.group(1))
            if idx is not None and guard is None:
                guard = {"type": "PFRN_GUARD_VAR_EQ", "param": idx, "value": int(m.group(2))}
            continue

        # end/release/return — stop processing
        if line in ("end", "release", "return"):
            break

    return actions, guard


def parse_map_enter_scripts(labels, flag_index, var_index):
    """Extract MAP_SCRIPT_ON_TRANSITION actions from map scripts header."""
    enter_actions = []

    # Find the map scripts header (first label usually)
    for label_name, lines in labels.items():
        if not label_name.endswith("_MapScripts"):
            continue

        on_transition_label = None
        on_frame_entries = []

        for line in lines:
            # map_script MAP_SCRIPT_ON_TRANSITION, label
            m = re.match(r"map_script\s+MAP_SCRIPT_ON_TRANSITION,\s*(\w+)", line)
            if m:
                on_transition_label = m.group(1)
            # map_script_2 VAR_*, value, label
            m = re.match(r"map_script_2\s+(VAR_\w+),\s*(\d+),\s*(\w+)", line)
            if m:
                on_frame_entries.append((m.group(1), int(m.group(2)), m.group(3)))

        # Parse ON_TRANSITION label
        if on_transition_label and on_transition_label in labels:
            for tline in labels[on_transition_label]:
                # setflag FLAG_*
                m = re.match(r"setflag\s+(FLAG_\w+)", tline)
                if m:
                    idx = flag_index.get(m.group(1))
                    if idx is not None:
                        enter_actions.append({
                            "action_type": 0,  # set_flag
                            "cond_type": "PFRN_GUARD_NONE",
                            "cond_param": 0, "cond_value": 0,
                            "action_param": idx, "action_value": 0,
                        })
                # setvar VAR_*, value
                m = re.match(r"setvar\s+(VAR_\w+),\s*(\d+)", tline)
                if m:
                    idx = var_index.get(m.group(1))
                    if idx is not None:
                        enter_actions.append({
                            "action_type": 1,  # set_var
                            "cond_type": "PFRN_GUARD_NONE",
                            "cond_param": 0, "cond_value": 0,
                            "action_param": idx, "action_value": int(m.group(2)),
                        })
                # call_if_eq VAR_*, value, label
                m = re.match(r"call_if_eq\s+(VAR_\w+),\s*(\d+),\s*(\w+)", tline)
                if m:
                    var_idx = var_index.get(m.group(1))
                    if var_idx is not None and m.group(3) in labels:
                        for sub_line in labels[m.group(3)]:
                            sm = re.match(r"setflag\s+(FLAG_\w+)", sub_line)
                            if sm:
                                fidx = flag_index.get(sm.group(1))
                                if fidx is not None:
                                    enter_actions.append({
                                        "action_type": 0,
                                        "cond_type": "PFRN_GUARD_VAR_EQ",
                                        "cond_param": var_idx,
                                        "cond_value": int(m.group(2)),
                                        "action_param": fidx, "action_value": 0,
                                    })
                # call_if_set FLAG_*, label
                m = re.match(r"call_if_set\s+(FLAG_\w+),\s*(\w+)", tline)
                if m:
                    fidx = flag_index.get(m.group(1))
                    if fidx is not None and m.group(2) in labels:
                        for sub_line in labels[m.group(2)]:
                            sm = re.match(r"setflag\s+(FLAG_\w+)", sub_line)
                            if sm:
                                sf = flag_index.get(sm.group(1))
                                if sf is not None:
                                    enter_actions.append({
                                        "action_type": 0,
                                        "cond_type": "PFRN_GUARD_FLAG_SET",
                                        "cond_param": fidx, "cond_value": 0,
                                        "action_param": sf, "action_value": 0,
                                    })
        break

    return enter_actions


# ============================================================
# Gym leader script patterns — build auto-battle scripts
# ============================================================

# Maps gym defeat script label patterns to (defeat_flag, badge_flag)
GYM_DEFEAT_SCRIPTS = {
    "PewterCity_Gym": ("FLAG_DEFEATED_BROCK", "FLAG_BADGE01_GET"),
    "CeruleanCity_Gym": ("FLAG_DEFEATED_MISTY", "FLAG_BADGE02_GET"),
    "VermilionCity_Gym": ("FLAG_DEFEATED_LT_SURGE", "FLAG_BADGE03_GET"),
    "CeladonCity_Gym": ("FLAG_DEFEATED_ERIKA", "FLAG_BADGE04_GET"),
    "FuchsiaCity_Gym": ("FLAG_DEFEATED_KOGA", "FLAG_BADGE05_GET"),
    "SaffronCity_Gym": ("FLAG_DEFEATED_SABRINA", "FLAG_BADGE06_GET"),
    "CinnabarIsland_Gym": ("FLAG_DEFEATED_BLAINE", "FLAG_BADGE07_GET"),
    "ViridianCity_Gym": ("FLAG_DEFEATED_LEADER_GIOVANNI", "FLAG_BADGE08_GET"),
}

# ============================================================
# Metatile overrides — runtime tile collision changes.
# Map dir → list of (x, y, new_collision, guard_type, guard_flag_name, guard_value)
# When the guard condition is met, tile collision at (x,y) is overridden.
# Use "FLAG_SET" guard_type with the flag name for flag-gated overrides.
# These are resolved to compact flag indices at build time.
# ============================================================
METATILE_OVERRIDES = {
    # Cinnabar Gym: quiz doors become passable when quiz flag is set.
    # Coordinates from decomp's setmetatile calls in OpenDoor1-6 scripts.
    # (x, y, new_collision, guard_type, guard_flag_or_var_name, guard_value)
    "CinnabarIsland_Gym": [
        # Door 1 — FLAG_CINNABAR_GYM_QUIZ_1
        (26,  8, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_1", 0),
        (27,  8, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_1", 0),
        (26,  9, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_1", 0),
        (27,  9, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_1", 0),
        (26, 10, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_1", 0),
        (27, 10, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_1", 0),
        (28, 10, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_1", 0),
        # Door 2 — FLAG_CINNABAR_GYM_QUIZ_2
        (17,  8, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_2", 0),
        (18,  8, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_2", 0),
        (17,  9, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_2", 0),
        (18,  9, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_2", 0),
        (17, 10, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_2", 0),
        (18, 10, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_2", 0),
        (19, 10, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_2", 0),
        # Door 3 — FLAG_CINNABAR_GYM_QUIZ_3
        (17, 15, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_3", 0),
        (18, 15, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_3", 0),
        (17, 16, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_3", 0),
        (18, 16, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_3", 0),
        (17, 17, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_3", 0),
        (18, 17, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_3", 0),
        (19, 17, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_3", 0),
        # Door 4 — FLAG_CINNABAR_GYM_QUIZ_4 (retractable barrier, only 2 passable)
        (11, 22, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_4", 0),
        (11, 23, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_4", 0),
        # Door 5 — FLAG_CINNABAR_GYM_QUIZ_5
        ( 5, 16, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_5", 0),
        ( 6, 16, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_5", 0),
        ( 5, 17, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_5", 0),
        ( 6, 17, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_5", 0),
        ( 5, 18, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_5", 0),
        ( 6, 18, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_5", 0),
        ( 7, 18, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_5", 0),
        # Door 6 — FLAG_CINNABAR_GYM_QUIZ_6
        ( 5,  8, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_6", 0),
        ( 6,  8, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_6", 0),
        ( 5,  9, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_6", 0),
        ( 6,  9, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_6", 0),
        ( 5, 10, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_6", 0),
        ( 6, 10, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_6", 0),
        ( 7, 10, 0, "FLAG_SET", "FLAG_CINNABAR_GYM_QUIZ_6", 0),
    ],
    # Victory Road: rock barriers removed when floor switch vars == 100.
    "VictoryRoad_1F": [
        (12, 14, 0, "VAR_EQ", "VAR_MAP_SCENE_VICTORY_ROAD_1F", 100),
        (12, 15, 0, "VAR_EQ", "VAR_MAP_SCENE_VICTORY_ROAD_1F", 100),
    ],
    "VictoryRoad_2F": [
        (13, 10, 0, "VAR_EQ", "VAR_MAP_SCENE_VICTORY_ROAD_2F_BOULDER1", 100),
        (13, 11, 0, "VAR_EQ", "VAR_MAP_SCENE_VICTORY_ROAD_2F_BOULDER1", 100),
        (33, 16, 0, "VAR_EQ", "VAR_MAP_SCENE_VICTORY_ROAD_2F_BOULDER2", 100),
        (33, 17, 0, "VAR_EQ", "VAR_MAP_SCENE_VICTORY_ROAD_2F_BOULDER2", 100),
    ],
    "VictoryRoad_3F": [
        (12, 12, 0, "VAR_EQ", "VAR_MAP_SCENE_VICTORY_ROAD_3F", 100),
        (12, 13, 0, "VAR_EQ", "VAR_MAP_SCENE_VICTORY_ROAD_3F", 100),
    ],
    # ---- Water route gating: block land-bridge tiles at map borders ----
    # Without these, agents walk from Pallet Town onto Route 21 islands
    # and reach Cinnabar without Surf. Blocked until HM03 (Surf) obtained.
    # Map width = 24. Passable positions verified from tile data.
    "Route21_North": [
        # Top row (y=0): island tiles reachable from Pallet Town south border
        # Passable at x=7,8,9,10 — block all until Surf obtained
        ( 7, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        ( 8, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        ( 9, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        (10, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
    ],
    "Route21_South": [
        # Top row (y=0): island tiles reachable from Route21_North
        # Passable at x=2,3,6-17,20,21 — block all until Surf obtained
        ( 2, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        ( 3, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        ( 6, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        ( 7, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        ( 8, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        ( 9, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        (10, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        (11, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        (12, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        (13, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        (14, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        (15, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        (16, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        (17, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        (20, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
        (21, 0, 1, "FLAG_UNSET", "FLAG_GOT_HM03", 0),
    ],
}


ELITE_FOUR_MAPS = {
    "PokemonLeague_LoreleisRoom": "FLAG_DEFEATED_LORELEI",
    "PokemonLeague_BrunosRoom": "FLAG_DEFEATED_BRUNO",
    "PokemonLeague_AgathasRoom": "FLAG_DEFEATED_AGATHA",
    "PokemonLeague_LancesRoom": "FLAG_DEFEATED_LANCE",
    "PokemonLeague_ChampionsRoom": "FLAG_DEFEATED_CHAMP",
}

# ============================================================
# Tileset symbol → numeric ID mapping (for visual rendering)
# IDs match pfr_native_tileset_data.h / pfr_native_tileset_data.c
# ============================================================
TILESET_IDS = {
    "NULL": 0,
    "gTileset_BerryForest": 1,
    "gTileset_BikeShop": 2,
    "gTileset_Building": 3,
    "gTileset_BurgledHouse": 4,
    "gTileset_CableClub": 5,
    "gTileset_Cave": 6,
    "gTileset_CeladonCity": 7,
    "gTileset_CeladonGym": 8,
    "gTileset_CeruleanCave": 9,
    "gTileset_CeruleanCity": 10,
    "gTileset_CeruleanGym": 11,
    "gTileset_CinnabarGym": 12,
    "gTileset_CinnabarIsland": 13,
    "gTileset_Condominiums": 14,
    "gTileset_DepartmentStore": 15,
    "gTileset_DiglettsCave": 16,
    "gTileset_Dummy1": 17,
    "gTileset_FanClubDaycare": 18,
    "gTileset_FuchsiaCity": 19,
    "gTileset_FuchsiaGym": 20,
    "gTileset_GameCorner": 21,
    "gTileset_General": 22,
    "gTileset_GenericBuilding1": 23,
    "gTileset_GenericBuilding2": 24,
    "gTileset_HallOfFame": 25,
    "gTileset_HoennBuilding": 26,
    "gTileset_IndigoPlateau": 27,
    "gTileset_IslandHarbor": 28,
    "gTileset_Lab": 29,
    "gTileset_LavenderTown": 30,
    "gTileset_Mart": 31,
    "gTileset_MtEmber": 32,
    "gTileset_Museum": 33,
    "gTileset_NavelRock": 34,
    "gTileset_PalletTown": 35,
    "gTileset_PewterCity": 36,
    "gTileset_PewterGym": 37,
    "gTileset_PokemonCenter": 38,
    "gTileset_PokemonLeague": 39,
    "gTileset_PokemonMansion": 40,
    "gTileset_PokemonTower": 41,
    "gTileset_PowerPlant": 42,
    "gTileset_RestaurantHotel": 43,
    "gTileset_RockTunnel": 44,
    "gTileset_SSAnne": 45,
    "gTileset_SafariZoneBuilding": 46,
    "gTileset_SaffronCity": 47,
    "gTileset_SaffronGym": 48,
    "gTileset_School": 49,
    "gTileset_SeaCottage": 50,
    "gTileset_SeafoamIslands": 51,
    "gTileset_SeviiIslands123": 52,
    "gTileset_SeviiIslands45": 53,
    "gTileset_SeviiIslands67": 54,
    "gTileset_SilphCo": 55,
    "gTileset_TanobyRuins": 56,
    "gTileset_TrainerTower": 57,
    "gTileset_UndergroundPath": 58,
    "gTileset_VermilionCity": 59,
    "gTileset_VermilionGym": 60,
    "gTileset_ViridianCity": 61,
    "gTileset_ViridianForest": 62,
    "gTileset_ViridianGym": 63,
}



# ============================================================
# Map/tile loading (preserved from original)
# ============================================================

def load_map_groups(repo_root):
    groups = load_json(repo_root / "third_party/pokefirered/data/maps/map_groups.json")
    map_dirs = []
    map_group_nums = {}
    for group_index, group in enumerate(groups["group_order"]):
        for map_num, map_dir in enumerate(groups[group]):
            map_dirs.append(map_dir)
            map_group_nums[map_dir] = (group_index, map_num)

    ordered = [name for name in PRIORITY_MAP_ORDER if name in map_group_nums]
    ordered.extend(name for name in map_dirs if name not in ordered)

    if len(ordered) >= 1 << 16:
        raise ValueError("map count exceeds uint16_t capacity")

    native_ids = {map_dir: index for index, map_dir in enumerate(ordered)}
    return ordered, map_group_nums, native_ids


def load_layouts(repo_root):
    data = load_json(repo_root / "third_party/pokefirered/data/layouts/layouts.json")
    return {
        entry["id"]: entry
        for entry in data["layouts"]
        if isinstance(entry, dict) and "id" in entry
    }


def parse_text_file(path):
    texts = {}
    current = None
    chunks = []
    for raw_line in Path(path).read_text().splitlines():
        line = raw_line.rstrip()
        if line.endswith("::"):
            if current is not None:
                texts[current] = "".join(chunks)
            current = line[:-2]
            chunks = []
            continue
        if current is None:
            continue
        stripped = line.strip()
        if not stripped.startswith(".string "):
            continue
        literal = stripped[len(".string "):].strip()
        if not (literal.startswith('"') and literal.endswith('"')):
            raise ValueError(f"unexpected string literal in {path}: {line}")
        chunks.append(literal[1:-1].replace(r"\\", "\\").replace(r"\"", '"'))
    if current is not None:
        texts[current] = "".join(chunks)
    return texts


def sanitize_dialog_text(text):
    for old, new in SPECIAL_REPLACEMENTS.items():
        text = text.replace(old, new)
    text = text.replace("\\l", "\n")
    text = text.replace("\\n", "\n")
    text = text.replace("\\p", "\f")
    text = text.replace("$", "")
    return text


def split_pages(text):
    pages = [page.strip() for page in sanitize_dialog_text(text).split("\f")]
    return [page for page in pages if page]


def parse_tile_data(repo_root, layout):
    path = repo_root / "third_party/pokefirered" / layout["blockdata_filepath"]
    words = struct.unpack("<" + "H" * (path.stat().st_size // 2), path.read_bytes())
    if len(words) != layout["width"] * layout["height"]:
        raise ValueError(f"layout size mismatch for {path}")
    return words


def parse_attributes(path):
    data = Path(path).read_bytes()
    if len(data) % 4 != 0:
        raise ValueError(f"metatile attributes file has invalid size: {path}")
    return struct.unpack("<" + "I" * (len(data) // 4), data)


def build_tiles(repo_root, layout, attrs_primary, attrs_secondary):
    raw_words = parse_tile_data(repo_root, layout)
    tiles = []
    primary_count = len(attrs_primary)
    for word in raw_words:
        metatile_id = word & 0x03FF
        collision = (word >> 10) & 0x3
        elevation = (word >> 12) & 0xF
        if metatile_id < primary_count:
            behavior = attrs_primary[metatile_id] & 0x1FF
        else:
            secondary_index = metatile_id - primary_count
            if secondary_index < len(attrs_secondary):
                behavior = attrs_secondary[secondary_index] & 0x1FF
            else:
                behavior = 0
        tiles.append((word, metatile_id, behavior, collision, elevation))
    return tiles


def load_tileset_attribute_paths(repo_root):
    metatiles_h = (
        repo_root / "third_party/pokefirered/src/data/tilesets/metatiles.h"
    ).read_text()
    headers_h = (repo_root / "third_party/pokefirered/src/data/tilesets/headers.h").read_text()

    attr_symbol_to_relpath = {}
    for match in re.finditer(
        r'const u32 (gMetatileAttributes_[A-Za-z0-9_]+)\[\] = INCBIN_U32\("([^"]+)"\);',
        metatiles_h,
    ):
        attr_symbol_to_relpath[match.group(1)] = match.group(2)

    tileset_to_attr_symbol = {}
    for match in re.finditer(
        r"const struct Tileset (gTileset_[A-Za-z0-9_]+)\s*=\s*\{(.*?)\};",
        headers_h,
        re.DOTALL,
    ):
        tileset_symbol = match.group(1)
        body = match.group(2)
        attr_match = re.search(
            r"\.metatileAttributes\s*=\s*(gMetatileAttributes_[A-Za-z0-9_]+)\s*,",
            body,
        )
        if attr_match:
            tileset_to_attr_symbol[tileset_symbol] = attr_match.group(1)

    tileset_to_attr_path = {}
    for tileset_symbol, attr_symbol in tileset_to_attr_symbol.items():
        relpath = attr_symbol_to_relpath.get(attr_symbol)
        if relpath is None:
            continue
        tileset_to_attr_path[tileset_symbol] = repo_root / "third_party/pokefirered" / relpath

    return tileset_to_attr_path


# ============================================================
# Event object graphics IDs
# ============================================================

def parse_event_object_gfx(repo_root):
    """Parse OBJ_EVENT_GFX_* constants from event_objects.h → {name: int}."""
    path = repo_root / "third_party/pokefirered/include/constants/event_objects.h"
    mapping = {}
    for line in path.read_text().splitlines():
        m = re.match(r"#define\s+(OBJ_EVENT_GFX_\w+)\s+(\d+)", line)
        if m:
            mapping[m.group(1)] = int(m.group(2))
    return mapping


def resolve_graphics_id(value, gfx_constants):
    """Resolve a graphics_id value (string constant or int) to an integer."""
    if isinstance(value, int):
        return value & 0xFF
    if isinstance(value, str):
        # OBJ_EVENT_GFX_VAR_* are dynamic — no static sprite.
        # Use 0 (invisible/fallback) since the actual graphic changes at runtime.
        if value.startswith("OBJ_EVENT_GFX_VAR_"):
            return 0xFF  # Will fall through to magenta circle in renderer
        # Try as OBJ_EVENT_GFX_* constant
        if value in gfx_constants:
            return gfx_constants[value] & 0xFF
        # Try as plain integer
        try:
            return int(value, 0) & 0xFF
        except ValueError:
            return 0
    return 0


# ============================================================
# Flag/var index assignment
# ============================================================

def build_flag_index(flag_constants, referenced_flags):
    """Assign compact indices 0..N to flags. Badges MUST be 0-7."""
    index = OrderedDict()
    for i, name in enumerate(REQUIRED_FLAGS):
        if name in flag_constants:
            index[name] = i

    next_idx = len(index)
    # Add any script-referenced flags not already included
    for name in sorted(referenced_flags):
        if name not in index and name in flag_constants and next_idx < MAX_FLAGS:
            index[name] = next_idx
            next_idx += 1

    if next_idx > MAX_FLAGS:
        print(f"WARNING: {next_idx} flags exceeds MAX_FLAGS={MAX_FLAGS}, truncating",
              file=sys.stderr)

    return index


def build_var_index(var_constants, referenced_vars):
    """Assign compact indices 0..N to vars."""
    index = OrderedDict()
    for i, name in enumerate(REQUIRED_VARS):
        if name in var_constants:
            index[name] = i

    next_idx = len(index)
    for name in sorted(referenced_vars):
        if name not in index and name in var_constants and next_idx < MAX_VARS:
            index[name] = next_idx
            next_idx += 1

    return index


# ============================================================
# Scan all scripts to find referenced flags/vars
# ============================================================

def scan_script_references(repo_root, map_dirs):
    """Scan all scripts.inc files to find FLAG_* and VAR_* references."""
    ref_flags = set()
    ref_vars = set()
    for map_dir in map_dirs:
        scripts_path = repo_root / f"third_party/pokefirered/data/maps/{map_dir}/scripts.inc"
        if not scripts_path.exists():
            continue
        text = scripts_path.read_text()
        ref_flags.update(re.findall(r"\b(FLAG_\w+)\b", text))
        ref_vars.update(re.findall(r"\b(VAR_\w+)\b", text))
    return ref_flags, ref_vars


# ============================================================
# Item ball script detection
# ============================================================

def parse_item_ball_scripts(repo_root):
    """Parse item_ball_scripts.inc and return a set of script labels.

    Each item ball script uses `finditem` (not setflag/setvar), so the
    auto-parser ignores them.  We register them as guard-only scripts
    (BADGE_GE >= 0, i.e. always passable) so the engine deactivates the
    object on interaction.
    """
    path = repo_root / "third_party/pokefirered/data/scripts/item_ball_scripts.inc"
    labels = set()
    if not path.exists():
        return labels
    for line in path.read_text().splitlines():
        stripped = line.strip()
        if stripped.endswith("::") and "_EventScript_Item" in stripped:
            labels.add(stripped[:-2])
    return labels


# ============================================================
# Build script data
# ============================================================

def build_script_data(repo_root, map_dirs, flag_constants, var_constants,
                       flag_index, var_index, native_ids,
                       trainer_constants=None):
    """Parse all scripts, build script action tables and dialog references."""
    scripts = OrderedDict()  # label → {actions, guard, c_name}
    dialog_collector = set()  # (map_dir, text_label) pairs

    # Start with legacy scripts (will have empty action tables — handled in C)
    for label, short_name in LEGACY_SCRIPTS.items():
        scripts[label] = {
            "c_name": f"PFR_NATIVE_SCRIPT_{short_name}",
            "actions": [],
            "guard": None,
            "legacy": True,
        }

    # Process script overrides — hand-authored blocker resolutions
    for label, override in SCRIPT_OVERRIDES.items():
        actions = []
        for act in override.get("actions", []):
            act_type, name, *rest = act
            value = rest[0] if rest else 0
            extra = rest[1] if len(rest) > 1 else 0
            if act_type == "SET_FLAG":
                idx = flag_index.get(name)
                if idx is not None:
                    actions.append(("PFRN_ACT_SET_FLAG", idx, 0, 0))
            elif act_type == "CLEAR_FLAG":
                idx = flag_index.get(name)
                if idx is not None:
                    actions.append(("PFRN_ACT_CLEAR_FLAG", idx, 0, 0))
            elif act_type == "SET_VAR":
                idx = var_index.get(name)
                if idx is not None:
                    actions.append(("PFRN_ACT_SET_VAR", idx, value, 0))

        guard = None
        g = override.get("guard")
        if g:
            g_type, g_name, *g_rest = g
            g_value = g_rest[0] if g_rest else 0
            if g_type == "FLAG_SET" and g_name in flag_index:
                guard = {"type": "PFRN_GUARD_FLAG_SET",
                         "param": flag_index[g_name], "value": 0}
            elif g_type == "FLAG_UNSET" and g_name in flag_index:
                guard = {"type": "PFRN_GUARD_FLAG_UNSET",
                         "param": flag_index[g_name], "value": 0}
            elif g_type == "VAR_EQ" and g_name in var_index:
                guard = {"type": "PFRN_GUARD_VAR_EQ",
                         "param": var_index[g_name], "value": g_value}
            elif g_type == "BADGE_GE":
                guard = {"type": "PFRN_GUARD_BADGE_GE",
                         "param": 0, "value": g_value}

        c_name = f"PFR_NATIVE_SCRIPT_{override['c_name']}"
        scripts[label] = {
            "c_name": c_name,
            "actions": actions,
            "guard": guard,
            "legacy": False,
        }

    # Process gym leader scripts — create auto-battle entries
    for map_dir, (defeat_flag, badge_flag) in GYM_DEFEAT_SCRIPTS.items():
        scripts_path = repo_root / f"third_party/pokefirered/data/maps/{map_dir}/scripts.inc"
        if not scripts_path.exists():
            continue

        labels = parse_scripts_inc(scripts_path)
        # Find the gym leader's main script (first object_event script with trainerbattle)
        map_json_path = repo_root / f"third_party/pokefirered/data/maps/{map_dir}/map.json"
        if not map_json_path.exists():
            continue
        map_json = load_json(map_json_path)

        for obj in map_json.get("object_events") or []:
            script_label = obj.get("script", "")
            if not script_label or script_label not in labels:
                continue
            # Check if this script has a trainerbattle command
            script_lines = labels[script_label]
            has_battle = any(re.match(r"trainerbattle_\w+", l) for l in script_lines)
            if not has_battle:
                continue
            # Must be the actual gym leader — verify that the defeat
            # callback sets the badge flag, not just any trainer battle
            sets_badge = False
            for line in script_lines:
                m_tb = re.match(r"trainerbattle_single\s+\w+,\s*\w+,\s*\w+,\s*(\w+)", line)
                if m_tb and m_tb.group(1) in labels:
                    defeat_lines = labels[m_tb.group(1)]
                    if any(f"setflag {badge_flag}" in dl for dl in defeat_lines):
                        sets_badge = True
                        break
            if not sets_badge:
                continue

            # Build auto-battle script: battle first, then set flags on victory
            actions = []

            # Extract trainer ID from the trainerbattle line and emit AUTO_BATTLE
            for line in script_lines:
                m_tb = re.match(r"trainerbattle_\w+\s+(TRAINER_\w+)", line)
                if m_tb:
                    tid = trainer_constants.get(m_tb.group(1))
                    if tid is not None:
                        d_idx = flag_index.get(defeat_flag)
                        tid_lo = tid & 0xFF
                        tid_hi = (tid >> 8) & 0xFF
                        flag_extra = d_idx if d_idx is not None else 0xFF
                        actions.append(("PFRN_ACT_AUTO_BATTLE", tid_hi, tid_lo, flag_extra))
                    break

            # Set defeat + badge flags (applied after battle by runtime)
            d_idx = flag_index.get(defeat_flag)
            b_idx = flag_index.get(badge_flag)
            if d_idx is not None:
                actions.append(("PFRN_ACT_SET_FLAG", d_idx, 0, 0))
            if b_idx is not None:
                actions.append(("PFRN_ACT_SET_FLAG", b_idx, 0, 0))

            # Look for setvar in defeat sub-scripts
            for line in script_lines:
                # Follow goto to defeat handler
                m = re.match(r"trainerbattle_single\s+\w+,\s*\w+,\s*\w+,\s*(\w+)", line)
                if m and m.group(1) in labels:
                    sub_actions, _ = extract_script_actions(
                        m.group(1), labels, flag_constants, var_constants,
                        flag_index, var_index, dialog_collector, map_dir,
                        repo_root, trainer_constants=trainer_constants
                    )
                    # Deduplicate — only add actions not already present
                    existing = set(tuple(a) for a in actions)
                    for sa in sub_actions:
                        if tuple(sa) not in existing:
                            actions.append(sa)
                            existing.add(tuple(sa))

            short = sanitize_symbol(map_dir).replace("MAP_", "")
            c_name = f"PFR_NATIVE_SCRIPT_{short}_LEADER"
            scripts[script_label] = {
                "c_name": c_name,
                "actions": actions,
                "guard": None,
                "legacy": False,
            }
            break  # Only the first battle NPC (the leader)

    # Process Elite Four maps
    for map_dir, defeat_flag in ELITE_FOUR_MAPS.items():
        d_idx = flag_index.get(defeat_flag)
        if d_idx is None:
            continue
        scripts_path = repo_root / f"third_party/pokefirered/data/maps/{map_dir}/scripts.inc"
        if not scripts_path.exists():
            continue

        labels = parse_scripts_inc(scripts_path)
        map_json_path = repo_root / f"third_party/pokefirered/data/maps/{map_dir}/map.json"
        if not map_json_path.exists():
            continue
        map_json = load_json(map_json_path)

        for obj in map_json.get("object_events") or []:
            script_label = obj.get("script", "")
            if not script_label or script_label not in labels:
                continue
            script_lines = labels[script_label]
            # Check if this script (or any sub-script it calls) has a battle.
            # Use search (not match) since lines have leading whitespace.
            has_battle = any(re.search(r"trainerbattle_\w+", l) for l in script_lines)
            # Also check sub-scripts referenced via call/call_if_*/goto
            if not has_battle:
                for line in script_lines:
                    # Match: call LABEL, call_if_set FLAG, LABEL,
                    # call_if_unset FLAG, LABEL, goto LABEL
                    for sub_m in re.finditer(r"(?:call(?:_if_\w+)?|goto)\s+(?:\w+,\s*)?(\w+)", line):
                        sub_label = sub_m.group(1)
                        if sub_label in labels:
                            sub_lines = labels[sub_label]
                            if any(re.search(r"trainerbattle_\w+", sl) for sl in sub_lines):
                                has_battle = True
                                break
                    if has_battle:
                        break
            if not has_battle:
                continue

            actions = []
            # Extract trainer ID from battle line and emit AUTO_BATTLE
            all_lines = list(script_lines)
            for line in script_lines:
                for sub_m in re.finditer(r"(?:call(?:_if_\w+)?|goto)\s+(?:\w+,\s*)?(\w+)", line):
                    sub_label = sub_m.group(1)
                    if sub_label in labels:
                        all_lines.extend(labels[sub_label])
            for line in all_lines:
                m_tb = re.match(r"\s*trainerbattle_\w+\s+(TRAINER_\w+)", line)
                if m_tb:
                    tid = trainer_constants.get(m_tb.group(1))
                    if tid is not None:
                        tid_lo = tid & 0xFF
                        tid_hi = (tid >> 8) & 0xFF
                        actions.append(("PFRN_ACT_AUTO_BATTLE", tid_hi, tid_lo, d_idx))
                    break
            actions.append(("PFRN_ACT_SET_FLAG", d_idx, 0, 0))
            # Champion gets GAME_CLEAR
            if defeat_flag == "FLAG_DEFEATED_CHAMP":
                gc_idx = flag_index.get("FLAG_SYS_GAME_CLEAR")
                if gc_idx is not None:
                    actions.append(("PFRN_ACT_SET_FLAG", gc_idx, 0, 0))

            short = sanitize_symbol(map_dir).replace("MAP_", "")
            c_name = f"PFR_NATIVE_SCRIPT_{short}_BATTLE"
            scripts[script_label] = {
                "c_name": c_name,
                "actions": actions,
                "guard": None,
                "legacy": False,
            }
            break

    # Scan remaining scripts for any with setflag/setvar that we haven't captured
    for map_dir in map_dirs:
        scripts_path = repo_root / f"third_party/pokefirered/data/maps/{map_dir}/scripts.inc"
        if not scripts_path.exists():
            continue
        map_json_path = repo_root / f"third_party/pokefirered/data/maps/{map_dir}/map.json"
        if not map_json_path.exists():
            continue
        map_json = load_json(map_json_path)
        labels = parse_scripts_inc(scripts_path)

        # Check object event scripts
        for obj in map_json.get("object_events") or []:
            script_label = obj.get("script", "")
            if not script_label or script_label in scripts:
                continue
            if script_label not in labels:
                continue
            actions, guard = extract_script_actions(
                script_label, labels, flag_constants, var_constants,
                flag_index, var_index, dialog_collector, map_dir, repo_root,
                trainer_constants=trainer_constants
            )
            if actions:
                short = sanitize_symbol(script_label)
                c_name = f"PFR_NATIVE_SCRIPT_{short}"
                scripts[script_label] = {
                    "c_name": c_name,
                    "actions": actions,
                    "guard": guard,
                    "legacy": False,
                }

        # Check bg event scripts
        for bg in map_json.get("bg_events") or []:
            script_label = bg.get("script", "")
            if not script_label or script_label in scripts:
                continue
            if script_label not in labels:
                continue
            actions, guard = extract_script_actions(
                script_label, labels, flag_constants, var_constants,
                flag_index, var_index, dialog_collector, map_dir, repo_root,
                trainer_constants=trainer_constants
            )
            if actions:
                short = sanitize_symbol(script_label)
                c_name = f"PFR_NATIVE_SCRIPT_{short}"
                scripts[script_label] = {
                    "c_name": c_name,
                    "actions": actions,
                    "guard": guard,
                    "legacy": False,
                }

    # Register item ball scripts — guard-only (always passes), engine
    # deactivates the object on interaction.
    item_ball_labels = parse_item_ball_scripts(repo_root)
    for label in sorted(item_ball_labels):
        if label not in scripts:
            short = sanitize_symbol(label)
            scripts[label] = {
                "c_name": f"PFR_NATIVE_SCRIPT_{short}",
                "actions": [],
                "guard": {"type": "PFRN_GUARD_BADGE_GE", "param": 0, "value": 0},
                "legacy": False,
            }

    # Assign sequential IDs (0 = NONE)
    script_id_map = {"": 0}  # empty label → NONE
    for i, label in enumerate(scripts, start=1):
        script_id_map[label] = i

    return scripts, script_id_map, dialog_collector


# ============================================================
# Build map data (extended from original)
# ============================================================

def build_map_data(repo_root, map_dirs, map_group_nums, native_ids, layouts,
                    script_id_map, flag_index, var_index, flag_constants,
                    gfx_constants):
    tileset_attr_paths = load_tileset_attribute_paths(repo_root)
    attrs_cache = {}

    def attrs_for_tileset(tileset_symbol):
        path = tileset_attr_paths.get(tileset_symbol)
        if path is None:
            raise KeyError(f"missing metatile attributes path for tileset {tileset_symbol}")
        if path not in attrs_cache:
            attrs_cache[path] = parse_attributes(path)
        return attrs_cache[path]

    # Build reverse lookup: decomp flag hex value → flag name for HIDE flags
    hide_flag_values = {}
    for fname, fval in flag_constants.items():
        if fname.startswith("FLAG_HIDE_") or fname.startswith("FLAG_TRAINER_"):
            hide_flag_values[str(fval)] = fname
            hide_flag_values[hex(fval)] = fname

    map_json_by_dir = {}
    id_symbol_to_native_id = {}
    for map_dir in map_dirs:
        map_json = load_json(repo_root / f"third_party/pokefirered/data/maps/{map_dir}/map.json")
        map_json_by_dir[map_dir] = map_json
        id_symbol_to_native_id[map_json["id"]] = native_ids[map_dir]

    map_data = []
    for map_dir in map_dirs:
        map_json = map_json_by_dir[map_dir]
        layout = layouts[map_json["layout"]]
        primary_attrs = attrs_for_tileset(layout["primary_tileset"])
        secondary_attrs = attrs_for_tileset(layout["secondary_tileset"])
        tiles = build_tiles(repo_root, layout, primary_attrs, secondary_attrs)

        warps = []
        for warp in map_json.get("warp_events") or []:
            dest_symbol = warp.get("dest_map")
            dest_native_id = id_symbol_to_native_id.get(dest_symbol)
            dest_warp_id = parse_int(warp.get("dest_warp_id"), -1)
            supported = 1 if dest_native_id is not None and 0 <= dest_warp_id <= 0xFF else 0
            warps.append({
                "x": parse_int(warp.get("x"), 0),
                "y": parse_int(warp.get("y"), 0),
                "dest_map_expr": (
                    f"((PfrNativeMapId){dest_native_id})"
                    if dest_native_id is not None
                    else PFR_NATIVE_MAP_INVALID_LITERAL
                ),
                "dest_warp_id": dest_warp_id if supported else 0,
                "supported": supported,
            })

        connections = []
        for connection in map_json.get("connections") or []:
            dest_symbol = connection.get("map")
            dest_native_id = id_symbol_to_native_id.get(dest_symbol)
            connections.append({
                "direction": CONNECTION_DIRECTION_ENUM.get(
                    connection.get("direction"), "PFR_NATIVE_CONN_UNKNOWN"
                ),
                "offset": parse_int(connection.get("offset"), 0),
                "dest_map_expr": (
                    f"((PfrNativeMapId){dest_native_id})"
                    if dest_native_id is not None
                    else PFR_NATIVE_MAP_INVALID_LITERAL
                ),
            })

        bg_events = []
        for event in map_json.get("bg_events") or []:
            script_label = event.get("script", "")
            sid = script_id_map.get(script_label, 0)
            bg_events.append({
                "x": parse_int(event.get("x"), 0),
                "y": parse_int(event.get("y"), 0),
                "facing_mask": facing_mask_for(event),
                "script_id": sid,
            })

        object_events = []
        for index, event in enumerate(map_json.get("object_events") or []):
            local_id = parse_int(event.get("local_id"), index + 1)
            script_label = event.get("script", "")
            sid = script_id_map.get(script_label, 0)

            # Resolve hide flag
            hide_flag_raw = event.get("flag", "0")
            hide_flag_name = None
            if isinstance(hide_flag_raw, str) and hide_flag_raw.startswith("FLAG_"):
                hide_flag_name = hide_flag_raw
            elif str(hide_flag_raw) != "0":
                hide_flag_name = hide_flag_values.get(str(hide_flag_raw))
                if not hide_flag_name:
                    hide_flag_name = hide_flag_values.get(
                        hex(parse_int(hide_flag_raw, 0)))

            hide_flag_idx = 0xFF
            if hide_flag_name and hide_flag_name in flag_index:
                hide_flag_idx = flag_index[hide_flag_name]

            object_events.append({
                "local_id": local_id & 0xFF,
                "graphics_id": resolve_graphics_id(event.get("graphics_id"), gfx_constants),
                "x": parse_int(event.get("x"), 0),
                "y": parse_int(event.get("y"), 0),
                "facing": movement_to_facing(event.get("movement_type")),
                "movement_type": 0,
                "script_id": sid,
                "hide_flag": hide_flag_idx,
            })

        # Coord events from map.json
        coord_events = []
        for event in map_json.get("coord_events") or []:
            script_label = event.get("script", "")
            sid = script_id_map.get(script_label, 0)
            if sid == 0:
                continue  # Skip unknown scripts

            # Parse var guard from the coord event elevation field or script
            coord_events.append({
                "x": parse_int(event.get("x"), 0),
                "y": parse_int(event.get("y"), 0),
                "var_id": 0xFF,  # no guard by default
                "var_value": 0,
                "script_id": sid,
            })

        # Champion room: inject synthetic coord_event at entry (1 tile north
        # of warp) because the battle is a MAP_SCRIPT_ON_FRAME, not an object.
        if map_dir == "PokemonLeague_ChampionsRoom":
            champ_sid = script_id_map.get(
                "PokemonLeague_ChampionsRoom_EventScript_EnterRoom", 0)
            if champ_sid:
                coord_events.append({
                    "x": 6, "y": 18,
                    "var_id": 0xFF,
                    "var_value": 0,
                    "script_id": champ_sid,
                })

        # Map-enter rules from scripts.inc
        enter_actions = []
        scripts_path = repo_root / f"third_party/pokefirered/data/maps/{map_dir}/scripts.inc"
        if scripts_path.exists():
            labels = parse_scripts_inc(scripts_path)
            enter_actions = parse_map_enter_scripts(labels, flag_index, var_index)

        # Metatile overrides for this map
        metatile_overrides = []
        raw_overrides = METATILE_OVERRIDES.get(map_dir, [])
        for ov in raw_overrides:
            ox, oy, new_col, g_type, g_flag_name, g_value = ov
            g_type_c = "PFRN_GUARD_NONE"
            g_param = 0
            if g_type == "FLAG_SET" and g_flag_name in flag_index:
                g_type_c = "PFRN_GUARD_FLAG_SET"
                g_param = flag_index[g_flag_name]
            elif g_type == "FLAG_UNSET" and g_flag_name in flag_index:
                g_type_c = "PFRN_GUARD_FLAG_UNSET"
                g_param = flag_index[g_flag_name]
            elif g_type == "VAR_EQ" and g_flag_name in var_index:
                g_type_c = "PFRN_GUARD_VAR_EQ"
                g_param = var_index[g_flag_name]
            elif g_type == "BADGE_GE":
                g_type_c = "PFRN_GUARD_BADGE_GE"
                g_param = 0
                g_value = g_value
            metatile_overrides.append({
                "x": ox, "y": oy,
                "new_collision": new_col,
                "guard_type": g_type_c,
                "guard_param": g_param,
                "guard_value": g_value,
            })

        # Resolve tileset IDs for visual rendering
        primary_ts = layout.get("primary_tileset", "NULL")
        secondary_ts = layout.get("secondary_tileset", "NULL")
        primary_tileset_id = TILESET_IDS.get(primary_ts, 0)
        secondary_tileset_id = TILESET_IDS.get(secondary_ts, 0)

        group, map_num = map_group_nums[map_dir]
        map_data.append({
            "map_id": native_ids[map_dir],
            "map_dir": map_dir,
            "array_symbol": sanitize_symbol(map_json["id"]),
            "name": map_json["name"],
            "id_symbol": map_json["id"],
            "group": group,
            "num": map_num,
            "width": int(layout["width"]),
            "height": int(layout["height"]),
            "tiles": tiles,
            "warps": warps,
            "connections": connections,
            "bg_events": bg_events,
            "object_events": object_events,
            "coord_events": coord_events,
            "enter_actions": enter_actions,
            "metatile_overrides": metatile_overrides,
            "primary_tileset_id": primary_tileset_id,
            "secondary_tileset_id": secondary_tileset_id,
        })

    return map_data


# ============================================================
# Dialog text collection
# ============================================================

# Legacy text specs (preserved from original)
TEXT_SPECS = [
    ("PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_MALE",
     "PalletTown_PlayersHouse_1F",
     "PalletTown_PlayersHouse_1F_Text_AllBoysLeaveOakLookingForYou"),
    ("PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_FEMALE",
     "PalletTown_PlayersHouse_1F",
     "PalletTown_PlayersHouse_1F_Text_AllGirlsLeaveOakLookingForYou"),
    ("PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_HEAL_1",
     "PalletTown_PlayersHouse_1F",
     "PalletTown_PlayersHouse_1F_Text_YouShouldTakeQuickRest"),
    ("PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_HEAL_2",
     "PalletTown_PlayersHouse_1F",
     "PalletTown_PlayersHouse_1F_Text_LookingGreatTakeCare"),
    ("PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_TV_MALE",
     "PalletTown_PlayersHouse_1F",
     "PalletTown_PlayersHouse_1F_Text_MovieOnTVFourBoysOnRailroad"),
    ("PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_TV_FEMALE",
     "PalletTown_PlayersHouse_1F",
     "PalletTown_PlayersHouse_1F_Text_MovieOnTVGirlOnBrickRoad"),
    ("PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_TV_WRONG_SIDE",
     "PalletTown_PlayersHouse_1F",
     "PalletTown_PlayersHouse_1F_Text_OopsWrongSide"),
    ("PFR_NATIVE_DIALOG_PLAYERS_HOUSE_2F_NES",
     "PalletTown_PlayersHouse_2F",
     "PalletTown_PlayersHouse_2F_Text_PlayedWithNES"),
    ("PFR_NATIVE_DIALOG_PLAYERS_HOUSE_2F_SIGN",
     "PalletTown_PlayersHouse_2F",
     "PalletTown_PlayersHouse_2F_Text_PressLRForHelp"),
]

INLINE_DIALOGS = {
    "PFR_NATIVE_DIALOG_PLAYERS_HOUSE_2F_PC": ["@PLAYER@ booted up the PC."],
}


# ============================================================
# Code generation
# ============================================================

def emit_generated_header(flag_index, var_index, scripts, output_h):
    """Write pfr_native_generated.h with all enum definitions."""
    lines = [
        "/* Auto-generated by tools/gen_pfr_native_data.py — DO NOT EDIT MANUALLY */",
        "#ifndef PFR_NATIVE_GENERATED_H",
        "#define PFR_NATIVE_GENERATED_H",
        "",
        "/* ---- Flag index enum (compact mapping into uint64_t bitfield) ---- */",
        "typedef enum {",
        "    PFRN_FLAG_NONE              = 0xFF,  /* sentinel: no flag */",
    ]

    for name, idx in flag_index.items():
        # Convert FLAG_FOO_BAR → PFRN_FLAG_FOO_BAR
        pfrn_name = "PFRN_" + name
        lines.append(f"    {pfrn_name:<40s} = {idx},")

    lines.append(f"    PFRN_FLAG_COUNT             = {len(flag_index)},")
    lines.append("} PfrNativeFlagId;")
    lines.append("")
    lines.append("#define PFRN_BADGE_FLAG_START PFRN_FLAG_BADGE01_GET")
    lines.append("")

    # Var enum
    lines.append("/* ---- Var index enum (compact mapping into vars[32]) ---- */")
    lines.append("typedef enum {")
    lines.append("    PFRN_VAR_NONE                     = 0xFF,  /* sentinel */")

    for name, idx in var_index.items():
        pfrn_name = "PFRN_" + name
        lines.append(f"    {pfrn_name:<50s} = {idx},")

    lines.append(f"    PFRN_VAR_COUNT                    = {len(var_index)},")
    lines.append("} PfrNativeVarId;")
    lines.append("")

    # Script ID enum
    lines.append("/* ---- Script ID enum ---- */")
    lines.append("typedef enum {")
    lines.append("    PFR_NATIVE_SCRIPT_NONE = 0,")

    for i, (label, info) in enumerate(scripts.items(), start=1):
        lines.append(f"    {info['c_name']:<60s} = {i},")

    lines.append(f"    PFR_NATIVE_SCRIPT_COUNT = {len(scripts) + 1},")
    lines.append("} PfrNativeScriptId;")
    lines.append("")

    # Dialog ID enum — combine legacy and new
    dialog_names = [
        "PFR_NATIVE_DIALOG_NONE",
        "PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_MALE",
        "PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_FEMALE",
        "PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_HEAL_1",
        "PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_MOM_HEAL_2",
        "PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_TV_MALE",
        "PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_TV_FEMALE",
        "PFR_NATIVE_DIALOG_PLAYERS_HOUSE_1F_TV_WRONG_SIDE",
        "PFR_NATIVE_DIALOG_PLAYERS_HOUSE_2F_NES",
        "PFR_NATIVE_DIALOG_PLAYERS_HOUSE_2F_SIGN",
        "PFR_NATIVE_DIALOG_PLAYERS_HOUSE_2F_PC",
    ]
    lines.append("/* ---- Dialog ID enum ---- */")
    lines.append("typedef enum {")
    for i, name in enumerate(dialog_names):
        lines.append(f"    {name} = {i},")
    lines.append(f"    PFR_NATIVE_DIALOG_COUNT = {len(dialog_names)},")
    lines.append("} PfrNativeDialogId;")
    lines.append("")

    lines.append("#endif /* PFR_NATIVE_GENERATED_H */")

    output_h.write_text("\n".join(lines))
    print(f"Generated {output_h}: {len(flag_index)} flags, {len(var_index)} vars, "
          f"{len(scripts)} scripts", file=sys.stderr)


def emit_data_c(map_data, scripts, dialog_refs_all, output_c):
    """Write pfr_native_data.c with all data tables."""
    lines = []
    lines.append("/* Auto-generated by tools/gen_pfr_native_data.py */")
    lines.append('#include "pfr_native.h"')
    lines.append("")

    # Dialog text arrays
    for dialog_enum, array_name, pages in dialog_refs_all:
        lines.append(f"static const char *const {array_name}[] = {{")
        for page in pages:
            lines.append(f"    {c_string(page)},")
        lines.append("};")
        lines.append("")

    # Script action arrays
    for label, info in scripts.items():
        if info["legacy"] or not info["actions"]:
            continue
        arr_name = f"sScriptActions_{info['c_name']}"
        lines.append(f"static const PfrNativeScriptAction {arr_name}[] = {{")
        for act_type, param, value, extra in info["actions"]:
            lines.append(f"    {{ {act_type}, {param}, {value}, {extra} }},")
        lines.append("};")
        lines.append("")

    # Map tile/warp/connection/bg_event/object_event/coord_event/enter_action arrays
    for spec in map_data:
        symbol = spec["array_symbol"]
        lines.append(f"static const PfrNativeTile sTiles_{symbol}[] = {{")
        for raw_word, metatile_id, behavior, collision, elevation in spec["tiles"]:
            lines.append(
                f"    {{ 0x{raw_word:04X}, 0x{metatile_id:03X}, 0x{behavior:03X}, "
                f"{collision}, {elevation} }},"
            )
        lines.append("};")
        lines.append("")

        if spec["warps"]:
            lines.append(f"static const PfrNativeWarp sWarps_{symbol}[] = {{")
            for warp in spec["warps"]:
                lines.append(
                    "    { "
                    f"{warp['x']}, {warp['y']}, {warp['dest_map_expr']}, "
                    f"{warp['dest_warp_id']}, {warp['supported']}"
                    " },"
                )
            lines.append("};")
            lines.append("")

        if spec["connections"]:
            lines.append(f"static const PfrNativeConnection sConnections_{symbol}[] = {{")
            for connection in spec["connections"]:
                lines.append(
                    "    { "
                    f"{connection['direction']}, {connection['offset']}, "
                    f"{connection['dest_map_expr']}"
                    " },"
                )
            lines.append("};")
            lines.append("")

        if spec["bg_events"]:
            lines.append(f"static const PfrNativeBgEvent sBgEvents_{symbol}[] = {{")
            for event in spec["bg_events"]:
                lines.append(
                    f"    {{ {event['x']}, {event['y']}, "
                    f"0x{event['facing_mask']:02X}, {event['script_id']} }},"
                )
            lines.append("};")
            lines.append("")

        if spec["object_events"]:
            lines.append(f"static const PfrNativeObjectEvent sObjects_{symbol}[] = {{")
            for event in spec["object_events"]:
                lines.append(
                    f"    {{ {event['local_id']}, {event['graphics_id']}, "
                    f"{event['x']}, {event['y']}, "
                    f"{event['facing']}, {event['movement_type']}, "
                    f"{event['script_id']}, {event['hide_flag']} }},"
                )
            lines.append("};")
            lines.append("")

        if spec["coord_events"]:
            lines.append(f"static const PfrNativeCoordEvent sCoordEvents_{symbol}[] = {{")
            for evt in spec["coord_events"]:
                lines.append(
                    f"    {{ {evt['x']}, {evt['y']}, {evt['var_id']}, "
                    f"{evt['var_value']}, {evt['script_id']}, 0 }},"
                )
            lines.append("};")
            lines.append("")

        if spec["enter_actions"]:
            lines.append(
                f"static const PfrNativeMapEnterAction sEnterActions_{symbol}[] = {{"
            )
            for act in spec["enter_actions"]:
                lines.append(
                    f"    {{ {act['action_type']}, {act['cond_type']}, "
                    f"{act['cond_param']}, {act['cond_value']}, "
                    f"{act['action_param']}, {act['action_value']} }},"
                )
            lines.append("};")
            lines.append("")

        if spec.get("metatile_overrides"):
            lines.append(
                f"static const PfrNativeMetatileOverride sMetatileOverrides_{symbol}[] = {{"
            )
            for ov in spec["metatile_overrides"]:
                lines.append(
                    f"    {{ {ov['x']}, {ov['y']}, {ov['new_collision']}, "
                    f"{ov['guard_type']}, {ov['guard_param']}, {ov['guard_value']} }},"
                )
            lines.append("};")
            lines.append("")

    # gPfrNativeMapCount
    lines.append(f"const size_t gPfrNativeMapCount = {len(map_data)};")
    lines.append("")

    # gPfrNativeMaps
    lines.append("const PfrNativeMap gPfrNativeMaps[] = {")
    for spec in map_data:
        symbol = spec["array_symbol"]
        map_id = spec["map_id"]

        def arr_or_null(prefix, key):
            if spec[key]:
                arr = f"{prefix}_{symbol}"
                cnt = f"sizeof({arr}) / sizeof({arr}[0])"
                return arr, cnt
            return "NULL", "0"

        warp_arr, warp_cnt = arr_or_null("sWarps", "warps")
        conn_arr, conn_cnt = arr_or_null("sConnections", "connections")
        bg_arr, bg_cnt = arr_or_null("sBgEvents", "bg_events")
        obj_arr, obj_cnt = arr_or_null("sObjects", "object_events")
        coord_arr, coord_cnt = arr_or_null("sCoordEvents", "coord_events")
        enter_arr, enter_cnt = arr_or_null("sEnterActions", "enter_actions")
        mtov_arr, mtov_cnt = arr_or_null("sMetatileOverrides", "metatile_overrides")

        lines.append(
            f"    [{map_id}] = {{ "
            f".name = {c_string(spec['name'])}, "
            f".id_symbol = {c_string(spec['id_symbol'])}, "
            f".map_id = (PfrNativeMapId){map_id}, "
            f".map_group = {spec['group']}, "
            f".map_num = {spec['num']}, "
            f".width = {spec['width']}, "
            f".height = {spec['height']}, "
            f".tiles = sTiles_{symbol}, "
            f".tile_count = sizeof(sTiles_{symbol}) / sizeof(sTiles_{symbol}[0]), "
            f".warps = {warp_arr}, "
            f".warp_count = {warp_cnt}, "
            f".connections = {conn_arr}, "
            f".connection_count = {conn_cnt}, "
            f".bg_events = {bg_arr}, "
            f".bg_event_count = {bg_cnt}, "
            f".object_events = {obj_arr}, "
            f".object_event_count = {obj_cnt}, "
            f".coord_events = {coord_arr}, "
            f".coord_event_count = {coord_cnt}, "
            f".enter_actions = {enter_arr}, "
            f".enter_action_count = {enter_cnt}, "
            f".metatile_overrides = {mtov_arr}, "
            f".metatile_override_count = {mtov_cnt}, "
            f".primary_tileset_id = {spec['primary_tileset_id']}, "
            f".secondary_tileset_id = {spec['secondary_tileset_id']}"
            "},"
        )
    lines.append("};")
    lines.append("")

    # gPfrNativeDialogs
    lines.append("const PfrNativeDialog gPfrNativeDialogs[PFR_NATIVE_DIALOG_COUNT] = {")
    lines.append("    [PFR_NATIVE_DIALOG_NONE] = { NULL, 0 },")
    for dialog_enum, array_name, pages in sorted(dialog_refs_all):
        lines.append(f"    [{dialog_enum}] = {{ {array_name}, {len(pages)} }},")
    lines.append("};")
    lines.append("")

    # gPfrNativeScripts
    lines.append(f"const size_t gPfrNativeScriptCount = PFR_NATIVE_SCRIPT_COUNT;")
    lines.append("")
    lines.append("const PfrNativeScript gPfrNativeScripts[PFR_NATIVE_SCRIPT_COUNT] = {")
    lines.append("    [PFR_NATIVE_SCRIPT_NONE] = { NULL, 0, PFRN_GUARD_NONE, 0, 0 },")

    for label, info in scripts.items():
        c_name = info["c_name"]
        guard = info.get("guard")
        has_guard = guard and guard.get("type") != "PFRN_GUARD_NONE"
        if info["legacy"] or (not info["actions"] and not has_guard):
            lines.append(
                f"    [{c_name}] = {{ NULL, 0, PFRN_GUARD_NONE, 0, 0 }},"
            )
        elif not info["actions"] and has_guard:
            # Guard-only script (e.g. cut tree: guard check, no data actions)
            g_type = guard["type"]
            g_param = guard["param"]
            g_value = guard["value"]
            lines.append(
                f"    [{c_name}] = {{ NULL, 0, {g_type}, {g_param}, {g_value} }},"
            )
        else:
            arr = f"sScriptActions_{c_name}"
            count = len(info["actions"])
            guard = info.get("guard")
            if guard:
                g_type = guard["type"]
                g_param = guard["param"]
                g_value = guard["value"]
            else:
                g_type = "PFRN_GUARD_NONE"
                g_param = 0
                g_value = 0
            lines.append(
                f"    [{c_name}] = {{ {arr}, {count}, {g_type}, {g_param}, {g_value} }},"
            )

    lines.append("};")
    lines.append("")

    output_c.write_text("\n".join(lines))


# ============================================================
# Main
# ============================================================

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", required=True)
    parser.add_argument("--output-c", required=True)
    parser.add_argument("--output-h", default=None,
                        help="Path for generated header (default: src/pfr_native_generated.h)")
    args = parser.parse_args()

    repo_root = Path(args.repo_root)
    output_c = Path(args.output_c)
    output_c.parent.mkdir(parents=True, exist_ok=True)

    if args.output_h:
        output_h = Path(args.output_h)
    else:
        output_h = repo_root / "src/pfr_native_generated.h"

    # 1. Load map groups and layouts
    map_dirs, map_group_nums, native_ids = load_map_groups(repo_root)
    layouts = load_layouts(repo_root)

    # 2. Load constants
    flag_constants, var_constants = load_constants(repo_root)
    trainer_constants = load_trainer_constants(repo_root)
    print(f"Parsed {len(flag_constants)} flags, {len(var_constants)} vars, "
          f"{len(trainer_constants)} trainers from decomp",
          file=sys.stderr)

    # 3. Scan scripts for referenced flags/vars
    ref_flags, ref_vars = scan_script_references(repo_root, map_dirs)
    print(f"Scripts reference {len(ref_flags)} unique flags, {len(ref_vars)} unique vars",
          file=sys.stderr)

    # 4. Build compact flag/var indices
    flag_index = build_flag_index(flag_constants, ref_flags)
    var_index = build_var_index(var_constants, ref_vars)
    print(f"Compact indices: {len(flag_index)} flags (max {MAX_FLAGS}), "
          f"{len(var_index)} vars (max {MAX_VARS})", file=sys.stderr)

    # 5. Build script data
    scripts, script_id_map, dialog_collector = build_script_data(
        repo_root, map_dirs, flag_constants, var_constants,
        flag_index, var_index, native_ids,
        trainer_constants=trainer_constants
    )
    print(f"Built {len(scripts)} scripts ({sum(1 for s in scripts.values() if not s['legacy'])} "
          f"data-driven, {sum(1 for s in scripts.values() if s['legacy'])} legacy)",
          file=sys.stderr)

    # 5b. Parse event object graphics constants
    gfx_constants = parse_event_object_gfx(repo_root)
    print(f"Parsed {len(gfx_constants)} OBJ_EVENT_GFX_* constants", file=sys.stderr)

    # 6. Build map data (tiles, warps, events, etc.)
    map_data = build_map_data(
        repo_root, map_dirs, map_group_nums, native_ids, layouts,
        script_id_map, flag_index, var_index, flag_constants, gfx_constants
    )

    # 7. Collect dialog texts
    texts_cache = {}
    dialog_refs = []

    for dialog_enum, map_dir, text_label in TEXT_SPECS:
        if map_dir not in texts_cache:
            text_path = repo_root / f"third_party/pokefirered/data/maps/{map_dir}/text.inc"
            if text_path.exists():
                texts_cache[map_dir] = parse_text_file(text_path)
            else:
                texts_cache[map_dir] = {}

        if text_label in texts_cache.get(map_dir, {}):
            pages = split_pages(texts_cache[map_dir][text_label])
            array_name = f"sPages_{dialog_enum}"
            dialog_refs.append((dialog_enum, array_name, pages))

    for dialog_enum, pages in INLINE_DIALOGS.items():
        array_name = f"sPages_{dialog_enum}"
        dialog_refs.append((dialog_enum, array_name, pages))

    # 8. Generate outputs
    emit_generated_header(flag_index, var_index, scripts, output_h)
    emit_data_c(map_data, scripts, dialog_refs, output_c)
    print(f"Generated {output_c}: {len(map_data)} maps, {len(dialog_refs)} dialogs",
          file=sys.stderr)


if __name__ == "__main__":
    main()
