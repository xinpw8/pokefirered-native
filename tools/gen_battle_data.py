#!/usr/bin/env python3
"""
gen_battle_data.py — Parse pokefirered decomp sources, emit pfr_battle_tables.h

Reads species info, battle moves, type chart, wild encounters, trainer data,
learnsets, and evolutions from the decomp tree and generates a single C header
with static const tables for the pfr_native battle engine.

Usage:
    python3 tools/gen_battle_data.py \
        --decomp third_party/pokefirered \
        --output src/pfr_battle_tables.h \
        [--map-data build/pfr_native_data.c]
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

NUM_SPECIES = 412
MOVES_COUNT = 355
NUM_TYPES = 18
NUM_NATURES = 25
NUM_GROWTH_RATES = 6

# Type IDs (from include/constants/pokemon.h)
TYPE_IDS = {
    "TYPE_NORMAL": 0, "TYPE_FIGHTING": 1, "TYPE_FLYING": 2, "TYPE_POISON": 3,
    "TYPE_GROUND": 4, "TYPE_ROCK": 5, "TYPE_BUG": 6, "TYPE_GHOST": 7,
    "TYPE_STEEL": 8, "TYPE_MYSTERY": 9, "TYPE_FIRE": 10, "TYPE_WATER": 11,
    "TYPE_GRASS": 12, "TYPE_ELECTRIC": 13, "TYPE_PSYCHIC": 14, "TYPE_ICE": 15,
    "TYPE_DRAGON": 16, "TYPE_DARK": 17,
}

TYPE_NAMES_SHORT = [
    "NOR", "FIG", "FLY", "PSN", "GND", "ROK", "BUG", "GHO",
    "STL", "???", "FIR", "WAT", "GRS", "ELC", "PSY", "ICE",
    "DRG", "DRK",
]

# Growth rate IDs (from include/constants/pokemon.h)
GROWTH_IDS = {
    "GROWTH_MEDIUM_FAST": 0, "GROWTH_ERRATIC": 1, "GROWTH_FLUCTUATING": 2,
    "GROWTH_MEDIUM_SLOW": 3, "GROWTH_FAST": 4, "GROWTH_SLOW": 5,
}

# Move flag bits (from include/constants/pokemon.h)
FLAG_BITS = {
    "FLAG_MAKES_CONTACT": 0x01,
    "FLAG_PROTECT_AFFECTED": 0x02,
    "FLAG_MAGIC_COAT_AFFECTED": 0x04,
    "FLAG_SNATCH_AFFECTED": 0x08,
    "FLAG_MIRROR_MOVE_AFFECTED": 0x10,
    "FLAG_KINGS_ROCK_AFFECTED": 0x20,
}

# AI flag bits (from include/constants/battle_ai.h)
AI_FLAG_BITS = {
    "AI_SCRIPT_CHECK_BAD_MOVE": 0x01,
    "AI_SCRIPT_CHECK_VIABILITY": 0x02,
    "AI_SCRIPT_TRY_TO_FAINT": 0x04,
}

# Evolution methods (from include/constants/pokemon.h)
EVO_METHODS = {
    "EVO_FRIENDSHIP": 1, "EVO_FRIENDSHIP_DAY": 2, "EVO_FRIENDSHIP_NIGHT": 3,
    "EVO_LEVEL": 4, "EVO_TRADE": 5, "EVO_TRADE_ITEM": 6, "EVO_ITEM": 7,
    "EVO_LEVEL_ATK_GT_DEF": 8, "EVO_LEVEL_ATK_EQ_DEF": 9,
    "EVO_LEVEL_ATK_LT_DEF": 10, "EVO_LEVEL_SILCOON": 11,
    "EVO_LEVEL_CASCOON": 12, "EVO_LEVEL_NINJASK": 13,
    "EVO_LEVEL_SHEDINJA": 14,
}

# Hardcoded nature stat modifiers: (Atk, Def, Spe, SpA, SpD)
NATURE_TABLE = [
    ( 0,  0,  0,  0,  0),  # Hardy
    ( 1, -1,  0,  0,  0),  # Lonely
    ( 1,  0, -1,  0,  0),  # Brave
    ( 1,  0,  0, -1,  0),  # Adamant
    ( 1,  0,  0,  0, -1),  # Naughty
    (-1,  1,  0,  0,  0),  # Bold
    ( 0,  0,  0,  0,  0),  # Docile
    ( 0,  1, -1,  0,  0),  # Relaxed
    ( 0,  1,  0, -1,  0),  # Impish
    ( 0,  1,  0,  0, -1),  # Lax
    (-1,  0,  1,  0,  0),  # Timid
    ( 0, -1,  1,  0,  0),  # Hasty
    ( 0,  0,  0,  0,  0),  # Serious
    ( 0,  0,  1, -1,  0),  # Jolly
    ( 0,  0,  1,  0, -1),  # Naive
    (-1,  0,  0,  1,  0),  # Modest
    ( 0, -1,  0,  1,  0),  # Mild
    ( 0,  0, -1,  1,  0),  # Quiet
    ( 0,  0,  0,  0,  0),  # Bashful
    ( 0,  0,  0,  1, -1),  # Rash
    (-1,  0,  0,  0,  1),  # Calm
    ( 0, -1,  0,  0,  1),  # Gentle
    ( 0,  0, -1,  0,  1),  # Sassy
    ( 0,  0,  0, -1,  1),  # Careful
    ( 0,  0,  0,  0,  0),  # Quirky
]

NATURE_NAMES = [
    "Hardy", "Lonely", "Brave", "Adamant", "Naughty",
    "Bold", "Docile", "Relaxed", "Impish", "Lax",
    "Timid", "Hasty", "Serious", "Jolly", "Naive",
    "Modest", "Mild", "Quiet", "Bashful", "Rash",
    "Calm", "Gentle", "Sassy", "Careful", "Quirky",
]

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def eprint(*args, **kwargs):
    """Print to stderr."""
    print(*args, file=sys.stderr, **kwargs)


def strip_c_comments(text):
    """Remove both // and /* */ comments from C source text."""
    # Remove block comments (non-greedy across lines)
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    # Remove line comments
    text = re.sub(r'//[^\n]*', '', text)
    return text


def read_file(path, required=True):
    """Read a file, returning its contents. Warn and return '' if missing."""
    try:
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            return f.read()
    except FileNotFoundError:
        if required:
            eprint(f"  WARNING: file not found: {path}")
        return ''


def extract_braced_block(text, start):
    """
    Given text and the index of an opening '{', return the substring from
    '{' to matching '}' (inclusive), plus the index after '}'.
    Returns (block_str, end_index) or (None, start) on failure.
    """
    if start >= len(text) or text[start] != '{':
        return None, start
    depth = 0
    i = start
    while i < len(text):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start:i + 1], i + 1
        i += 1
    return None, start


def parse_defines(text, prefix):
    """
    Parse #define lines with a given prefix, returning {name: int_value}.
    e.g. prefix='SPECIES_' matches '#define SPECIES_BULBASAUR 1'
    """
    result = {}
    for m in re.finditer(
        r'#define\s+(' + re.escape(prefix) + r'\w+)\s+(\d+)', text
    ):
        result[m.group(1)] = int(m.group(2))
    return result


def resolve_constant(name, *dicts):
    """Look up a symbolic constant name in one or more dictionaries."""
    for d in dicts:
        if name in d:
            return d[name]
    # Try parsing as a literal integer
    try:
        return int(name)
    except ValueError:
        return 0


# ---------------------------------------------------------------------------
# Parsers
# ---------------------------------------------------------------------------

def parse_species_constants(decomp):
    """Parse include/constants/species.h -> {name: id}"""
    text = read_file(os.path.join(decomp, 'include', 'constants', 'species.h'))
    return parse_defines(text, 'SPECIES_')


def parse_move_constants(decomp):
    """Parse include/constants/moves.h -> {name: id}"""
    text = read_file(os.path.join(decomp, 'include', 'constants', 'moves.h'))
    return parse_defines(text, 'MOVE_')


def parse_effect_constants(decomp):
    """Parse include/constants/battle_move_effects.h -> {name: id}"""
    text = read_file(os.path.join(
        decomp, 'include', 'constants', 'battle_move_effects.h'
    ))
    return parse_defines(text, 'EFFECT_')


def parse_trainer_constants(decomp):
    """Parse include/constants/opponents.h -> {name: id}"""
    text = read_file(os.path.join(
        decomp, 'include', 'constants', 'opponents.h'
    ))
    return parse_defines(text, 'TRAINER_')


def parse_species_info(decomp, species_map):
    """
    Parse src/data/pokemon/species_info.h -> list of dicts indexed by species id.
    Each dict: {base_hp, base_atk, base_def, base_spa, base_spd, base_spe,
                type1, type2, catch_rate, base_exp, growth_rate}
    """
    eprint("  Parsing species info...")
    path = os.path.join(decomp, 'src', 'data', 'pokemon', 'species_info.h')
    text = read_file(path)
    if not text:
        return [None] * NUM_SPECIES

    text = strip_c_comments(text)
    species = [None] * NUM_SPECIES

    # Find each [SPECIES_XXX] = { ... } block
    pattern = re.compile(r'\[(\w+)\]\s*=\s*\{')
    pos = 0
    while pos < len(text):
        m = pattern.search(text, pos)
        if not m:
            break
        name = m.group(1)
        sid = species_map.get(name)
        if sid is None or sid <= 0 or sid >= NUM_SPECIES:
            # Skip to next
            pos = m.end()
            continue

        block_start = m.end() - 1  # point at '{'
        block, end = extract_braced_block(text, block_start)
        if block is None:
            pos = m.end()
            continue
        pos = end

        # Extract fields from the block
        def get_int(field):
            fm = re.search(r'\.' + field + r'\s*=\s*(\d+)', block)
            return int(fm.group(1)) if fm else 0

        def get_types(block_text):
            tm = re.search(r'\.types\s*=\s*\{([^}]+)\}', block_text)
            if not tm:
                return 0, 0
            parts = [s.strip() for s in tm.group(1).split(',')]
            t1 = TYPE_IDS.get(parts[0], 0) if len(parts) > 0 else 0
            t2 = TYPE_IDS.get(parts[1], 0) if len(parts) > 1 else t1
            return t1, t2

        def get_growth(block_text):
            gm = re.search(r'\.growthRate\s*=\s*(\w+)', block_text)
            if not gm:
                return 0
            return GROWTH_IDS.get(gm.group(1), 0)

        t1, t2 = get_types(block)
        species[sid] = {
            'base_hp':  get_int('baseHP'),
            'base_atk': get_int('baseAttack'),
            'base_def': get_int('baseDefense'),
            'base_spa': get_int('baseSpAttack'),
            'base_spd': get_int('baseSpDefense'),
            'base_spe': get_int('baseSpeed'),
            'type1': t1,
            'type2': t2,
            'catch_rate': get_int('catchRate'),
            'base_exp': get_int('expYield'),
            'growth_rate': get_growth(block),
        }

    count = sum(1 for s in species if s is not None)
    eprint(f"    -> {count} species parsed")
    return species


def parse_battle_moves(decomp, move_map, effect_map):
    """
    Parse src/data/battle_moves.h -> list of dicts indexed by move id.
    Each dict: {power, type, accuracy, pp, effect, priority, flags, secondary_chance}
    """
    eprint("  Parsing battle moves...")
    path = os.path.join(decomp, 'src', 'data', 'battle_moves.h')
    text = read_file(path)
    if not text:
        return [None] * MOVES_COUNT

    text = strip_c_comments(text)
    moves = [None] * MOVES_COUNT

    pattern = re.compile(r'\[(\w+)\]\s*=\s*\{')
    pos = 0
    while pos < len(text):
        m = pattern.search(text, pos)
        if not m:
            break
        name = m.group(1)
        mid = move_map.get(name)
        if mid is None or mid < 0 or mid >= MOVES_COUNT:
            pos = m.end()
            continue

        block_start = m.end() - 1
        block, end = extract_braced_block(text, block_start)
        if block is None:
            pos = m.end()
            continue
        pos = end

        def get_field(field, block_text=block):
            fm = re.search(r'\.' + field + r'\s*=\s*([^,}\s]+)', block_text)
            return fm.group(1).strip() if fm else None

        effect_str = get_field('effect') or 'EFFECT_HIT'
        effect_val = effect_map.get(effect_str, 0)
        try:
            effect_val = int(effect_str)
        except (ValueError, TypeError):
            effect_val = effect_map.get(effect_str, 0)

        power_str = get_field('power') or '0'
        try:
            power = int(power_str)
        except ValueError:
            power = 0

        type_str = get_field('type') or 'TYPE_NORMAL'
        type_val = TYPE_IDS.get(type_str, 0)

        acc_str = get_field('accuracy') or '0'
        try:
            accuracy = int(acc_str)
        except ValueError:
            accuracy = 0

        pp_str = get_field('pp') or '0'
        try:
            pp = int(pp_str)
        except ValueError:
            pp = 0

        sec_str = get_field('secondaryEffectChance') or '0'
        try:
            secondary = int(sec_str)
        except ValueError:
            secondary = 0

        pri_str = get_field('priority') or '0'
        try:
            priority = int(pri_str)
        except ValueError:
            priority = 0

        # Parse flags field (may be bitwise OR of multiple flags)
        flags_val = 0
        flags_str = get_field('flags')
        if flags_str:
            # Grab everything up to the next comma or closing brace after .flags =
            fm = re.search(r'\.flags\s*=\s*([^,}]+)', block)
            if fm:
                flags_raw = fm.group(1).strip()
                for flag_name, flag_bit in FLAG_BITS.items():
                    if flag_name in flags_raw:
                        flags_val |= flag_bit

        moves[mid] = {
            'power': power,
            'type': type_val,
            'accuracy': accuracy,
            'pp': pp,
            'effect': effect_val,
            'priority': priority,
            'flags': flags_val,
            'secondary_chance': secondary,
        }

    count = sum(1 for m in moves if m is not None)
    eprint(f"    -> {count} moves parsed")
    return moves


def parse_type_chart(decomp):
    """
    Parse gTypeEffectiveness from src/battle_main.c.
    Returns 18x18 matrix of multiplier values (0, 5, 10, 20).
    """
    eprint("  Parsing type chart...")
    path = os.path.join(decomp, 'src', 'battle_main.c')
    text = read_file(path)
    if not text:
        return [[10] * NUM_TYPES for _ in range(NUM_TYPES)]

    text = strip_c_comments(text)

    # Initialize to neutral (10)
    chart = [[10] * NUM_TYPES for _ in range(NUM_TYPES)]

    # Find the array
    m = re.search(r'gTypeEffectiveness\[\d+\]\s*=\s*\{', text)
    if not m:
        eprint("    WARNING: gTypeEffectiveness not found")
        return chart

    block_start = text.index('{', m.start())
    block, _ = extract_braced_block(text, block_start)
    if not block:
        eprint("    WARNING: could not extract type effectiveness block")
        return chart

    # Extract all symbolic values from the array body (between { and })
    inner = block[1:-1]
    tokens = [t.strip() for t in inner.split(',') if t.strip()]

    # Process in triplets: (atk_type, def_type, multiplier)
    i = 0
    entries = 0
    while i + 2 < len(tokens):
        atk_str = tokens[i]
        def_str = tokens[i + 1]
        mul_str = tokens[i + 2]
        i += 3

        # TYPE_ENDTABLE marks the true end of the table
        if atk_str == 'TYPE_ENDTABLE' or def_str == 'TYPE_ENDTABLE':
            break
        # TYPE_FORESIGHT is a separator -- skip the separator triplet itself
        # but continue processing entries after it (Normal/Fighting vs Ghost
        # immunities are stored after this separator and are the DEFAULT)
        if atk_str == 'TYPE_FORESIGHT' or def_str == 'TYPE_FORESIGHT':
            continue

        atk = TYPE_IDS.get(atk_str)
        def_ = TYPE_IDS.get(def_str)
        if atk is None or def_ is None:
            continue
        if atk >= NUM_TYPES or def_ >= NUM_TYPES:
            continue

        # Resolve multiplier
        mul_map = {
            'TYPE_MUL_NO_EFFECT': 0,
            'TYPE_MUL_NOT_EFFECTIVE': 5,
            'TYPE_MUL_NORMAL': 10,
            'TYPE_MUL_SUPER_EFFECTIVE': 20,
        }
        mul = mul_map.get(mul_str)
        if mul is None:
            try:
                mul = int(mul_str)
            except ValueError:
                continue

        chart[atk][def_] = mul
        entries += 1

    eprint(f"    -> {entries} non-neutral entries parsed")
    return chart


def compute_exp_tables():
    """
    Compute experience tables for all 6 growth rates, levels 0..100.
    Returns list of 6 lists, each with 101 entries.
    """
    eprint("  Computing experience tables...")
    tables = []

    for rate in range(NUM_GROWTH_RATES):
        table = [0] * 101
        for n in range(2, 101):
            n3 = n * n * n
            if rate == 0:    # MEDIUM_FAST
                table[n] = n3
            elif rate == 1:  # ERRATIC
                if n <= 50:
                    table[n] = n3 * (100 - n) // 50
                elif n <= 68:
                    table[n] = n3 * (150 - n) // 100
                elif n <= 98:
                    table[n] = n3 * ((1911 - 10 * n) // 3) // 500
                else:
                    table[n] = n3 * (160 - n) // 100
            elif rate == 2:  # FLUCTUATING
                if n <= 15:
                    table[n] = n3 * ((n + 1) // 3 + 24) // 50
                elif n <= 36:
                    table[n] = n3 * (n + 14) // 50
                else:
                    table[n] = n3 * (n // 2 + 32) // 50
            elif rate == 3:  # MEDIUM_SLOW
                val = 6 * n3 // 5 - 15 * n * n + 100 * n - 140
                table[n] = max(0, val)
            elif rate == 4:  # FAST
                table[n] = 4 * n3 // 5
            elif rate == 5:  # SLOW
                table[n] = 5 * n3 // 4
        tables.append(table)

    return tables


def parse_wild_encounters(decomp, species_map, map_name_to_id):
    """
    Parse src/data/wild_encounters.json.
    Returns (encounter_slots, encounter_tables) where:
      - encounter_slots: list of (species_id, min_lv, max_lv)
      - encounter_tables: list of (map_id, kind, rate, slot_start, slot_count)
        kind: 0=land, 1=water
    Only FireRed entries are used (base_label containing 'FireRed' preferred;
    if no FireRed-specific label exists, accept any).
    """
    eprint("  Parsing wild encounters...")
    path = os.path.join(decomp, 'src', 'data', 'wild_encounters.json')
    raw = read_file(path)
    if not raw:
        return [], []

    try:
        data = json.loads(raw)
    except json.JSONDecodeError as e:
        eprint(f"    WARNING: JSON parse error: {e}")
        return [], []

    slots = []      # (species_id, min_lv, max_lv)
    tables = []     # (map_id, kind, rate, slot_start, slot_count)

    # Group encounters by map name: prefer FireRed, skip LeafGreen if both exist
    groups = data.get('wild_encounter_groups', [])
    for group in groups:
        if not group.get('for_maps'):
            continue
        encounters = group.get('encounters', [])

        # Build per-map dict, preferring FireRed entries
        by_map = {}  # map_name -> encounter_dict
        for enc in encounters:
            map_name = enc.get('map', '')
            label = enc.get('base_label', '')
            # If LeafGreen and we already have a FireRed entry, skip
            if 'LeafGreen' in label and map_name in by_map:
                continue
            # If FireRed, always overwrite
            if 'FireRed' in label or map_name not in by_map:
                by_map[map_name] = enc

        for map_name, enc in sorted(by_map.items()):
            map_id = map_name_to_id.get(map_name)
            if map_id is None:
                continue

            # Land encounters
            land = enc.get('land_mons')
            if land:
                rate = land.get('encounter_rate', 0)
                mons = land.get('mons', [])
                if mons:
                    slot_start = len(slots)
                    for mon in mons:
                        sp_name = mon.get('species', 'SPECIES_NONE')
                        sp_id = species_map.get(sp_name, 0)
                        min_lv = mon.get('min_level', 1)
                        max_lv = mon.get('max_level', 1)
                        slots.append((sp_id, min_lv, max_lv))
                    tables.append((
                        map_id, 0, rate, slot_start, len(mons)
                    ))

            # Water encounters
            water = enc.get('water_mons')
            if water:
                rate = water.get('encounter_rate', 0)
                mons = water.get('mons', [])
                if mons:
                    slot_start = len(slots)
                    for mon in mons:
                        sp_name = mon.get('species', 'SPECIES_NONE')
                        sp_id = species_map.get(sp_name, 0)
                        min_lv = mon.get('min_level', 1)
                        max_lv = mon.get('max_level', 1)
                        slots.append((sp_id, min_lv, max_lv))
                    tables.append((
                        map_id, 1, rate, slot_start, len(mons)
                    ))

    eprint(f"    -> {len(slots)} encounter slots, {len(tables)} encounter tables")
    return slots, tables


def parse_trainer_parties(decomp, species_map, move_map):
    """
    Parse src/data/trainer_parties.h.
    Returns dict: party_name -> list of {species, level, moves: [4 ints]}
    Skips dummy trainer mons.
    """
    eprint("  Parsing trainer parties...")
    path = os.path.join(decomp, 'src', 'data', 'trainer_parties.h')
    text = read_file(path)
    if not text:
        return {}

    text = strip_c_comments(text)
    parties = {}

    # Match party array declarations:
    # static const struct TrainerMon<Type> sParty_Name[] = { ... };
    pat = re.compile(
        r'static\s+const\s+struct\s+TrainerMon(\w+)\s+(sParty_\w+)\s*\[\s*\]\s*=\s*\{'
    )
    pos = 0
    while pos < len(text):
        m = pat.search(text, pos)
        if not m:
            break
        struct_type = m.group(1)  # e.g. NoItemDefaultMoves, ItemCustomMoves
        party_name = m.group(2)
        block_start = text.index('{', m.start())
        block, end = extract_braced_block(text, block_start)
        if block is None:
            pos = m.end()
            continue
        pos = end

        # Check if party uses only DUMMY macros
        if 'DUMMY_TRAINER_MON' in block and '.species' not in block:
            continue

        has_custom_moves = 'CustomMoves' in struct_type

        # Parse individual mons in the block
        mons = []
        inner = block[1:-1]  # strip outer braces

        # Find each mon block: { ... }
        mon_pos = 0
        while mon_pos < len(inner):
            brace = inner.find('{', mon_pos)
            if brace == -1:
                break
            mon_block, mon_end = extract_braced_block(inner, brace)
            if mon_block is None:
                mon_pos = brace + 1
                continue
            mon_pos = mon_end

            # Skip DUMMY entries
            if 'DUMMY_TRAINER_MON' in inner[brace - 30:brace] if brace >= 30 else '':
                continue

            # Extract species
            sp_m = re.search(r'\.species\s*=\s*(\w+)', mon_block)
            if not sp_m:
                continue
            sp_id = species_map.get(sp_m.group(1), 0)

            # Extract level
            lv_m = re.search(r'\.lvl\s*=\s*(\d+)', mon_block)
            level = int(lv_m.group(1)) if lv_m else 1

            # Extract moves (if custom)
            moves = [0, 0, 0, 0]
            if has_custom_moves:
                mv_m = re.search(r'\.moves\s*=\s*\{([^}]+)\}', mon_block)
                if mv_m:
                    mv_parts = [s.strip() for s in mv_m.group(1).split(',')]
                    for i, mv_name in enumerate(mv_parts[:4]):
                        moves[i] = move_map.get(mv_name, 0)

            mons.append({
                'species': sp_id,
                'level': level,
                'moves': moves,
            })

        if mons:
            parties[party_name] = mons

    eprint(f"    -> {len(parties)} trainer parties parsed")
    return parties


def parse_trainers(decomp, trainer_map, parties):
    """
    Parse src/data/trainers.h.
    Returns list of (trainer_id, ai_flags, party_name) for each trainer.
    """
    eprint("  Parsing trainers...")
    path = os.path.join(decomp, 'src', 'data', 'trainers.h')
    text = read_file(path)
    if not text:
        return []

    text = strip_c_comments(text)
    trainers = []

    pattern = re.compile(r'\[(\w+)\]\s*=\s*\{')
    pos = 0
    while pos < len(text):
        m = pattern.search(text, pos)
        if not m:
            break
        trainer_name = m.group(1)
        tid = trainer_map.get(trainer_name)
        if tid is None:
            pos = m.end()
            continue

        block_start = m.end() - 1
        block, end = extract_braced_block(text, block_start)
        if block is None:
            pos = m.end()
            continue
        pos = end

        if tid == 0:  # Skip TRAINER_NONE
            continue

        # Parse AI flags
        ai_flags = 0
        ai_m = re.search(r'\.aiFlags\s*=\s*([^,}]+)', block)
        if ai_m:
            ai_raw = ai_m.group(1).strip()
            for flag_name, flag_bit in AI_FLAG_BITS.items():
                if flag_name in ai_raw:
                    ai_flags |= flag_bit

        # Parse party reference: .party = MACRO(sParty_Name),
        party_name = None
        party_m = re.search(
            r'\.party\s*=\s*\w+\(\s*(sParty_\w+)\s*\)', block
        )
        if party_m:
            party_name = party_m.group(1)

        if party_name and party_name in parties:
            trainers.append((tid, ai_flags, party_name))

    # Sort by trainer ID for deterministic output
    trainers.sort(key=lambda x: x[0])
    eprint(f"    -> {len(trainers)} trainers with valid parties")
    return trainers


def parse_learnsets(decomp, species_map, move_map):
    """
    Parse src/data/pokemon/level_up_learnsets.h and
    src/data/pokemon/level_up_learnset_pointers.h.
    Returns dict: species_id -> list of (level, move_id)
    """
    eprint("  Parsing learnsets...")
    # First parse individual learnset arrays
    path = os.path.join(
        decomp, 'src', 'data', 'pokemon', 'level_up_learnsets.h'
    )
    text = read_file(path)
    if not text:
        return {}

    text = strip_c_comments(text)

    # Parse each static array: static const u16 sXxxLevelUpLearnset[] = { ... };
    learnset_arrays = {}  # array_name -> list of (level, move_id)
    pat = re.compile(
        r'static\s+const\s+u16\s+(s\w+LevelUpLearnset)\s*\[\s*\]\s*=\s*\{'
    )
    for m in pat.finditer(text):
        arr_name = m.group(1)
        block_start = text.index('{', m.start())
        block, _ = extract_braced_block(text, block_start)
        if block is None:
            continue

        entries = []
        # Match LEVEL_UP_MOVE(level, MOVE_XXX)
        for lm in re.finditer(
            r'LEVEL_UP_MOVE\s*\(\s*(\d+)\s*,\s*(\w+)\s*\)', block
        ):
            level = int(lm.group(1))
            move_id = move_map.get(lm.group(2), 0)
            entries.append((level, move_id))

        learnset_arrays[arr_name] = entries

    # Now parse the pointer table to map species -> array name
    ptr_path = os.path.join(
        decomp, 'src', 'data', 'pokemon', 'level_up_learnset_pointers.h'
    )
    ptr_text = read_file(ptr_path)
    if not ptr_text:
        # Fallback: try to infer from array names
        eprint("    WARNING: learnset pointer file not found, using heuristic mapping")
        return {}

    ptr_text = strip_c_comments(ptr_text)
    learnsets = {}  # species_id -> list of (level, move_id)

    for pm in re.finditer(
        r'\[(\w+)\]\s*=\s*(s\w+LevelUpLearnset)', ptr_text
    ):
        sp_name = pm.group(1)
        arr_name = pm.group(2)
        sp_id = species_map.get(sp_name)
        if sp_id is not None and sp_id > 0 and sp_id < NUM_SPECIES:
            if arr_name in learnset_arrays:
                learnsets[sp_id] = learnset_arrays[arr_name]

    eprint(f"    -> {len(learnsets)} species learnsets, "
           f"{len(learnset_arrays)} unique arrays")
    return learnsets


def parse_evolutions(decomp, species_map):
    """
    Parse src/data/pokemon/evolution.h.
    Returns dict: species_id -> list of (target_species, method, param)
    For level-based evos, param is the level. For others, param=0 (simplified).
    """
    eprint("  Parsing evolutions...")
    path = os.path.join(decomp, 'src', 'data', 'pokemon', 'evolution.h')
    text = read_file(path)
    if not text:
        return {}

    text = strip_c_comments(text)
    evolutions = {}

    # Match [SPECIES_XXX] = {{ ... }, ...},
    pattern = re.compile(r'\[(\w+)\]\s*=\s*\{')
    pos = 0
    while pos < len(text):
        m = pattern.search(text, pos)
        if not m:
            break
        sp_name = m.group(1)
        sp_id = species_map.get(sp_name)

        # Skip non-species entries (e.g. [NUM_SPECIES], [EVOS_PER_MON])
        # BEFORE extracting braced block to avoid consuming the entire array
        if sp_id is None or sp_id <= 0 or sp_id >= NUM_SPECIES:
            pos = m.end()
            continue

        block_start = m.end() - 1
        block, end = extract_braced_block(text, block_start)
        if block is None:
            pos = m.end()
            continue
        pos = end

        # Parse each evolution entry: {EVO_METHOD, param, SPECIES_TARGET}
        evos = []
        for em in re.finditer(
            r'\{\s*(\w+)\s*,\s*([^,]+)\s*,\s*(\w+)\s*\}', block
        ):
            method_name = em.group(1).strip()
            param_str = em.group(2).strip()
            target_name = em.group(3).strip()

            method = EVO_METHODS.get(method_name, 0)
            target = species_map.get(target_name, 0)

            # For EVO_LEVEL and level-like methods, param is the level
            # For others (item, trade), simplify param to 0
            if method == EVO_METHODS['EVO_LEVEL'] or method in (
                EVO_METHODS.get('EVO_LEVEL_ATK_GT_DEF', 99),
                EVO_METHODS.get('EVO_LEVEL_ATK_EQ_DEF', 99),
                EVO_METHODS.get('EVO_LEVEL_ATK_LT_DEF', 99),
                EVO_METHODS.get('EVO_LEVEL_SILCOON', 99),
                EVO_METHODS.get('EVO_LEVEL_CASCOON', 99),
                EVO_METHODS.get('EVO_LEVEL_NINJASK', 99),
                EVO_METHODS.get('EVO_LEVEL_SHEDINJA', 99),
            ):
                try:
                    param = int(param_str)
                except ValueError:
                    param = 0
            else:
                param = 0

            if target > 0:
                evos.append((target, method, param))

        if evos:
            evolutions[sp_id] = evos

    eprint(f"    -> {len(evolutions)} species with evolutions")
    return evolutions


def parse_map_data(map_data_path):
    """
    Parse pfr_native_data.c to build MAP_XXX -> numeric map_id mapping.
    """
    eprint("  Parsing map data...")
    text = read_file(map_data_path, required=False)
    if not text:
        eprint("    WARNING: map data file not found, encounter mapping disabled")
        return {}

    mapping = {}
    # Match: .id_symbol = "MAP_XXX" ... .map_id = (PfrNativeMapId)N
    # These are on a single long line per map entry
    for line in text.split('\n'):
        sym_m = re.search(r'\.id_symbol\s*=\s*"(MAP_\w+)"', line)
        id_m = re.search(r'\.map_id\s*=\s*\(PfrNativeMapId\)(\d+)', line)
        if sym_m and id_m:
            mapping[sym_m.group(1)] = int(id_m.group(1))

    eprint(f"    -> {len(mapping)} map symbols resolved")
    return mapping


# ---------------------------------------------------------------------------
# Code generation
# ---------------------------------------------------------------------------

def species_name(species_map, sid):
    """Get human-readable species name from ID."""
    for name, val in species_map.items():
        if val == sid:
            # Strip SPECIES_ prefix, title-case
            return name.replace('SPECIES_', '').replace('_', ' ').title()
    return f"#{sid}"


def move_name(move_map, mid):
    """Get human-readable move name from ID."""
    for name, val in move_map.items():
        if val == mid:
            return name.replace('MOVE_', '').replace('_', ' ').title()
    return f"#{mid}"


def generate_header(
    species_data, moves_data, type_chart, exp_tables,
    encounter_slots, encounter_tables, map_name_to_id,
    trainers, parties,
    learnsets, evolutions,
    species_map, move_map,
):
    """Generate the complete pfr_battle_tables.h content."""
    lines = []

    def w(s=''):
        lines.append(s)

    # Reverse maps for comments
    id_to_species = {v: k for k, v in species_map.items()}
    id_to_move = {v: k for k, v in move_map.items()}

    # --- Header guard ---
    w("/* Auto-generated by tools/gen_battle_data.py -- DO NOT EDIT */")
    w("#ifndef PFR_BATTLE_TABLES_H")
    w("#define PFR_BATTLE_TABLES_H")
    w()
    w(f"#define PFR_NUM_SPECIES {NUM_SPECIES}")
    w(f"#define PFR_NUM_MOVES {MOVES_COUNT}")
    w(f"#define PFR_NUM_TYPES {NUM_TYPES}")
    w(f"#define PFR_NUM_NATURES {NUM_NATURES}")
    w(f"#define PFR_NUM_GROWTH_RATES {NUM_GROWTH_RATES}")
    w()

    # --- Species ---
    w("/* ---- Species ---- */")
    w("/* PfrSpeciesData: {base_hp, base_atk, base_def, base_spa, base_spd, base_spe, type1, type2, catch_rate, base_exp, growth_rate, _pad} */")
    w("static const PfrSpeciesData PFR_SPECIES[PFR_NUM_SPECIES] = {")
    for sid in range(NUM_SPECIES):
        s = species_data[sid]
        if s is None:
            continue
        name = id_to_species.get(sid, '').replace('SPECIES_', '')
        w(f"    [{sid}] = {{{s['base_hp']},{s['base_atk']},{s['base_def']},"
          f"{s['base_spa']},{s['base_spd']},{s['base_spe']},"
          f" {s['type1']},{s['type2']},"
          f" {s['catch_rate']},{s['base_exp']},{s['growth_rate']}, 0}}, /* {name} */")
    w("};")
    w()

    # --- Moves ---
    w("/* ---- Moves ---- */")
    w("/* PfrMoveData: {power, type, accuracy, pp, effect, priority, flags, secondary_chance} */")
    w("static const PfrMoveData PFR_MOVES[PFR_NUM_MOVES] = {")
    for mid in range(MOVES_COUNT):
        mv = moves_data[mid]
        if mv is None:
            continue
        name = id_to_move.get(mid, '').replace('MOVE_', '')
        pri = mv['priority']
        # priority is int8_t, handle negative
        pri_str = str(pri) if pri >= 0 else str(pri)
        w(f"    [{mid}] = {{{mv['power']},{mv['type']},{mv['accuracy']},{mv['pp']},"
          f" {mv['effect']},{pri_str},0x{mv['flags']:02X},{mv['secondary_chance']}}}, /* {name} */")
    w("};")
    w()

    # --- Species names ---
    w("/* ---- Species names ---- */")
    w("static const char *const PFR_SPECIES_NAMES[PFR_NUM_SPECIES] = {")
    for sid in range(NUM_SPECIES):
        name = species_name(species_map, sid)
        w(f'    [{sid}] = "{name}",')
    w("};")
    w()

    # --- Move names ---
    w("/* ---- Move names ---- */")
    w("static const char *const PFR_MOVE_NAMES[PFR_NUM_MOVES] = {")
    for mid in range(MOVES_COUNT):
        name = move_name(move_map, mid)
        w(f'    [{mid}] = "{name}",')
    w("};")
    w()

    # --- Type chart ---
    w("/* ---- Type chart (multiplier: 0=immune, 5=resist, 10=neutral, 20=super) ---- */")
    header_comment = "    /* " + " ".join(f"{t:>3}" for t in TYPE_NAMES_SHORT) + " */"
    w("static const uint8_t PFR_TYPE_CHART[PFR_NUM_TYPES][PFR_NUM_TYPES] = {")
    w(header_comment)
    for atk in range(NUM_TYPES):
        row = ",".join(f"{type_chart[atk][d]:2}" for d in range(NUM_TYPES))
        w(f"    {{{row}}}, /* {TYPE_NAMES_SHORT[atk]} */")
    w("};")
    w()

    # --- Natures ---
    w("/* ---- Natures (Atk, Def, Spe, SpA, SpD modifiers: -1/0/+1) ---- */")
    w("static const int8_t PFR_NATURES[PFR_NUM_NATURES][5] = {")
    for i, (nat, name) in enumerate(zip(NATURE_TABLE, NATURE_NAMES)):
        vals = ",".join(f"{v:+d}" for v in nat)
        w(f"    {{{vals}}}, /* {name} */")
    w("};")
    w()

    # --- Experience tables ---
    growth_names = [
        "MEDIUM_FAST", "ERRATIC", "FLUCTUATING",
        "MEDIUM_SLOW", "FAST", "SLOW"
    ]
    w("/* ---- Experience tables [growth_rate][level] ---- */")
    w("static const uint32_t PFR_EXP_TABLES[PFR_NUM_GROWTH_RATES][101] = {")
    for gi, table in enumerate(exp_tables):
        w(f"    /* {growth_names[gi]} */")
        w("    {")
        # Write 10 values per line
        for row_start in range(0, 101, 10):
            chunk = table[row_start:row_start + 10]
            vals = ",".join(str(v) for v in chunk)
            if row_start + 10 < 101:
                vals += ","
            w(f"        {vals}")
        w("    },")
    w("};")
    w()

    # --- Wild encounter slots ---
    w("/* ---- Wild encounter slots ---- */")
    w(f"/* PfrEncounterSlot: {{uint16_t species; uint8_t min_level, max_level}} */")
    w(f"#define PFR_ENCOUNTER_SLOT_COUNT {len(encounter_slots)}")
    if encounter_slots:
        w("static const PfrEncounterSlot PFR_ENCOUNTER_SLOTS[] = {")
        for i, (sp, mn, mx) in enumerate(encounter_slots):
            name = id_to_species.get(sp, '').replace('SPECIES_', '')
            w(f"    {{{sp}, {mn}, {mx}}}, /* [{i}] {name} */")
        w("};")
    else:
        w("static const PfrEncounterSlot PFR_ENCOUNTER_SLOTS[] = {{0,0,0}};")
    w()

    # --- Wild encounter tables ---
    w("/* ---- Wild encounter tables (map_id, kind, rate, slot_start, slot_count, _pad) ---- */")
    w(f"#define PFR_ENCOUNTER_TABLE_COUNT {len(encounter_tables)}")
    if encounter_tables:
        w("static const PfrEncounterTable PFR_ENCOUNTERS[] = {")
        kind_names = {0: "land", 1: "water"}
        for i, (mid, kind, rate, start, count) in enumerate(encounter_tables):
            kn = kind_names.get(kind, "?")
            w(f"    {{{mid}, {kind}/*{kn}*/, {rate}, {start}, {count}, 0}},")
        w("};")
    else:
        w("static const PfrEncounterTable PFR_ENCOUNTERS[] = {{0,0,0,0,0,0}};")
    w()

    # --- Encounter lookup by map ID ---
    max_map_id = 0
    for t in encounter_tables:
        if t[0] > max_map_id:
            max_map_id = t[0]
    # Also check map_name_to_id for the overall max
    if map_name_to_id:
        overall_max = max(map_name_to_id.values()) if map_name_to_id else 0
        max_map_id = max(max_map_id, overall_max)
    max_map_id += 1  # array size

    w(f"#define PFR_MAX_ENCOUNTER_MAP_ID {max_map_id}")

    # Build lookup arrays
    land_lookup = [-1] * max_map_id
    water_lookup = [-1] * max_map_id
    for idx, (mid, kind, rate, start, count) in enumerate(encounter_tables):
        if mid < max_map_id:
            if kind == 0:
                land_lookup[mid] = idx
            elif kind == 1:
                water_lookup[mid] = idx

    w("static const int16_t PFR_ENCOUNTER_LAND_BY_MAP[PFR_MAX_ENCOUNTER_MAP_ID] = {")
    # Write in chunks for readability
    for row_start in range(0, max_map_id, 16):
        chunk = land_lookup[row_start:row_start + 16]
        vals = ",".join(f"{v:4}" for v in chunk)
        if row_start + 16 < max_map_id:
            vals += ","
        w(f"    {vals}")
    w("};")
    w("static const int16_t PFR_ENCOUNTER_WATER_BY_MAP[PFR_MAX_ENCOUNTER_MAP_ID] = {")
    for row_start in range(0, max_map_id, 16):
        chunk = water_lookup[row_start:row_start + 16]
        vals = ",".join(f"{v:4}" for v in chunk)
        if row_start + 16 < max_map_id:
            vals += ","
        w(f"    {vals}")
    w("};")
    w()

    # --- Trainer mons ---
    w("/* ---- Trainer mons ---- */")
    w("/* PfrTrainerMon: {uint16_t species; uint8_t level, _pad; uint16_t moves[4]} */")
    # Flatten all trainer mons into one big array, recording offsets
    all_mons = []
    trainer_entries = []  # (trainer_id, ai_flags, mon_count, first_mon_idx)
    for tid, ai_flags, party_name in trainers:
        party = parties[party_name]
        first = len(all_mons)
        for mon in party:
            all_mons.append(mon)
        trainer_entries.append((tid, ai_flags, len(party), first))

    w(f"#define PFR_TRAINER_MON_COUNT {len(all_mons)}")
    if all_mons:
        w("static const PfrTrainerMon PFR_TRAINER_MONS[] = {")
        for i, mon in enumerate(all_mons):
            sp = mon['species']
            lv = mon['level']
            mv = mon['moves']
            name = id_to_species.get(sp, '').replace('SPECIES_', '')
            w(f"    {{{sp}, {lv}, 0, {{{mv[0]},{mv[1]},{mv[2]},{mv[3]}}}}}, "
              f"/* [{i}] {name} Lv{lv} */")
        w("};")
    else:
        w("static const PfrTrainerMon PFR_TRAINER_MONS[] = {{0,0,0,{0,0,0,0}}};")
    w()

    # --- Trainer data ---
    w("/* ---- Trainer data ---- */")
    w("/* PfrTrainerData: {uint16_t trainer_id; uint8_t ai_flags, mon_count; uint16_t first_mon, _pad} */")
    w(f"#define PFR_TRAINER_TABLE_COUNT {len(trainer_entries)}")
    if trainer_entries:
        w("static const PfrTrainerData PFR_TRAINERS[PFR_TRAINER_TABLE_COUNT] = {")
        for tid, ai, mc, first in trainer_entries:
            w(f"    {{{tid}, 0x{ai:02X}, {mc}, {first}, 0}},")
        w("};")
    else:
        w("static const PfrTrainerData PFR_TRAINERS[1] = {{0,0,0,0,0}};")
    w()

    # --- Learnset data ---
    w("/* ---- Learnset entries (packed: (level << 9) | move_id) ---- */")
    learnset_flat = []  # packed uint16_t values
    learnset_index = {}  # species_id -> (start, count)
    for sp_id in sorted(learnsets.keys()):
        entries = learnsets[sp_id]
        start = len(learnset_flat)
        for level, move_id in entries:
            packed = (level << 9) | (move_id & 0x1FF)
            learnset_flat.append(packed)
        learnset_index[sp_id] = (start, len(entries))

    w(f"#define PFR_LEARNSET_DATA_COUNT {len(learnset_flat)}")
    if learnset_flat:
        w("static const uint16_t PFR_LEARNSET_DATA[] = {")
        for row_start in range(0, len(learnset_flat), 12):
            chunk = learnset_flat[row_start:row_start + 12]
            vals = ",".join(f"0x{v:04X}" for v in chunk)
            if row_start + 12 < len(learnset_flat):
                vals += ","
            w(f"    {vals}")
        w("};")
    else:
        w("static const uint16_t PFR_LEARNSET_DATA[] = {0};")
    w()

    w("/* ---- Learnset index per species ---- */")
    w("/* PfrLearnsetIndex: {uint16_t start, count} */")
    w("static const PfrLearnsetIndex PFR_LEARNSETS[PFR_NUM_SPECIES] = {")
    for sp_id in range(NUM_SPECIES):
        if sp_id in learnset_index:
            start, count = learnset_index[sp_id]
            name = id_to_species.get(sp_id, '').replace('SPECIES_', '')
            w(f"    [{sp_id}] = {{{start}, {count}}}, /* {name} */")
    w("};")
    w()

    # --- Evolution data ---
    w("/* ---- Evolution entries ---- */")
    w("/* PfrEvolutionEntry: {uint16_t target_species; uint8_t method, param} */")
    evo_flat = []  # (target, method, param)
    evo_index = {}  # species_id -> (start, count)
    for sp_id in sorted(evolutions.keys()):
        entries = evolutions[sp_id]
        start = len(evo_flat)
        for target, method, param in entries:
            evo_flat.append((target, method, param))
        evo_index[sp_id] = (start, len(entries))

    w(f"#define PFR_EVOLUTION_DATA_COUNT {len(evo_flat)}")
    if evo_flat:
        w("static const PfrEvolutionEntry PFR_EVOLUTION_DATA[] = {")
        for i, (target, method, param) in enumerate(evo_flat):
            name = id_to_species.get(target, '').replace('SPECIES_', '')
            w(f"    {{{target}, {method}, {param}}}, /* -> {name} */")
        w("};")
    else:
        w("static const PfrEvolutionEntry PFR_EVOLUTION_DATA[] = {{0,0,0}};")
    w()

    w("/* ---- Evolution index per species ---- */")
    w("/* PfrEvolutionIndex: {uint16_t start, count} */")
    w("static const PfrEvolutionIndex PFR_EVOLUTIONS[PFR_NUM_SPECIES] = {")
    for sp_id in range(NUM_SPECIES):
        if sp_id in evo_index:
            start, count = evo_index[sp_id]
            name = id_to_species.get(sp_id, '').replace('SPECIES_', '')
            w(f"    [{sp_id}] = {{{start}, {count}}}, /* {name} */")
    w("};")
    w()

    w("#endif /* PFR_BATTLE_TABLES_H */")
    return '\n'.join(lines) + '\n'


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Generate pfr_battle_tables.h from pokefirered decomp"
    )
    parser.add_argument(
        '--decomp', required=True,
        help='Path to pokefirered decomp root (e.g. third_party/pokefirered)'
    )
    parser.add_argument(
        '--output', required=True,
        help='Output header file path (e.g. src/pfr_battle_tables.h)'
    )
    parser.add_argument(
        '--map-data', default=None,
        help='Path to pfr_native_data.c for map ID resolution '
             '(default: {repo_root}/build/pfr_native_data.c)'
    )
    args = parser.parse_args()

    decomp = os.path.abspath(args.decomp)
    output = os.path.abspath(args.output)

    # Resolve map-data default: repo root is two levels above decomp
    if args.map_data:
        map_data_path = os.path.abspath(args.map_data)
    else:
        repo_root = os.path.dirname(os.path.dirname(decomp))
        map_data_path = os.path.join(repo_root, 'build', 'pfr_native_data.c')

    eprint(f"gen_battle_data.py")
    eprint(f"  decomp:    {decomp}")
    eprint(f"  output:    {output}")
    eprint(f"  map-data:  {map_data_path}")
    eprint()

    # --- Parse constants ---
    eprint("[1/10] Parsing constants...")
    species_map = parse_species_constants(decomp)
    move_map = parse_move_constants(decomp)
    effect_map = parse_effect_constants(decomp)
    trainer_map = parse_trainer_constants(decomp)
    eprint(f"    -> {len(species_map)} species, {len(move_map)} moves, "
           f"{len(effect_map)} effects, {len(trainer_map)} trainers")

    # --- Parse data files ---
    eprint("[2/10] Parsing species info...")
    species_data = parse_species_info(decomp, species_map)

    eprint("[3/10] Parsing battle moves...")
    moves_data = parse_battle_moves(decomp, move_map, effect_map)

    eprint("[4/10] Parsing type chart...")
    type_chart = parse_type_chart(decomp)

    eprint("[5/10] Computing experience tables...")
    exp_tables = compute_exp_tables()

    eprint("[6/10] Parsing map data...")
    map_name_to_id = parse_map_data(map_data_path)

    eprint("[7/10] Parsing wild encounters...")
    encounter_slots, encounter_tables = parse_wild_encounters(
        decomp, species_map, map_name_to_id
    )

    eprint("[8/10] Parsing trainer data...")
    parties = parse_trainer_parties(decomp, species_map, move_map)
    trainers = parse_trainers(decomp, trainer_map, parties)

    eprint("[9/10] Parsing learnsets...")
    learnsets = parse_learnsets(decomp, species_map, move_map)

    eprint("[10/10] Parsing evolutions...")
    evolutions = parse_evolutions(decomp, species_map)

    # --- Generate output ---
    eprint()
    eprint("Generating header...")
    header = generate_header(
        species_data, moves_data, type_chart, exp_tables,
        encounter_slots, encounter_tables, map_name_to_id,
        trainers, parties,
        learnsets, evolutions,
        species_map, move_map,
    )

    # Ensure output directory exists
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, 'w', encoding='utf-8') as f:
        f.write(header)

    # Summary
    total_lines = header.count('\n')
    total_bytes = len(header.encode('utf-8'))
    eprint(f"  Wrote {output} ({total_lines} lines, {total_bytes:,} bytes)")
    eprint("Done.")


if __name__ == '__main__':
    main()
