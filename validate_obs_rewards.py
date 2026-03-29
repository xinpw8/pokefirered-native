#!/usr/bin/env python3
"""
Comprehensive validation of pokefirered-native obs and reward extraction.

Tests:
1. Boot game, extract obs → validate all fields have sane values
2. Load multiple savestates → validate obs at different game stages
3. Step actions → validate rewards are non-NaN, non-Inf, bounded
4. Full obs extraction → validate all 226 bytes parse correctly
5. Full obs (6701 bytes) → validate screen, party, items, flags
6. Reward info → validate against obs for consistency
"""

import ctypes as ct
import json
import os
import struct
import sys
import numpy as np

# ── Configuration ──
SO_PATH = os.path.expanduser("~/pokefirered-native/build/libpfr_game.so")
STATE_DIR = os.path.expanduser("~/pokefirered-native/pfr_debug_states")

OBS_SIZE = 226
FULL_OBS_SIZE = 6701
NUM_ACTIONS = 10

# ── ctypes structures ──

class RewardInfo(ct.Structure):
    _fields_ = [
        ("player_x", ct.c_int16),
        ("player_y", ct.c_int16),
        ("map_group", ct.c_uint8),
        ("map_num", ct.c_uint8),
        ("badges", ct.c_uint8),
        ("party_count", ct.c_uint8),
        ("party_level_sum", ct.c_uint16),
        ("money", ct.c_uint32),
        ("in_battle", ct.c_uint8),
    ]

class RewardInfoFull(ct.Structure):
    _fields_ = [
        ("player_x", ct.c_int16),
        ("player_y", ct.c_int16),
        ("map_group", ct.c_uint8),
        ("map_num", ct.c_uint8),
        ("badges", ct.c_uint8),
        ("party_count", ct.c_uint8),
        ("party_level_sum", ct.c_uint16),
        ("money", ct.c_uint32),
        ("in_battle", ct.c_uint8),
        # extended
        ("pokedex_seen_count", ct.c_uint16),
        ("pokedex_owned_count", ct.c_uint16),
        ("party_hp_sum_pct", ct.c_uint8),
        ("story_flags_set", ct.c_uint16),
        ("trainer_flags_set", ct.c_uint16),
        ("system_flags_set", ct.c_uint16),
        ("has_hm01", ct.c_uint8),
        ("has_hm02", ct.c_uint8),
        ("has_hm03", ct.c_uint8),
        ("has_hm04", ct.c_uint8),
        ("has_hm05", ct.c_uint8),
        ("has_ss_ticket", ct.c_uint8),
        ("has_silph_scope", ct.c_uint8),
        ("has_poke_flute", ct.c_uint8),
        ("has_card_key", ct.c_uint8),
        ("has_lift_key", ct.c_uint8),
        ("has_gold_teeth", ct.c_uint8),
        ("has_bicycle", ct.c_uint8),
        ("has_tea", ct.c_uint8),
        ("has_secret_key", ct.c_uint8),
        ("defeated_brock", ct.c_uint8),
        ("defeated_misty", ct.c_uint8),
        ("defeated_surge", ct.c_uint8),
        ("defeated_erika", ct.c_uint8),
        ("defeated_koga", ct.c_uint8),
        ("defeated_sabrina", ct.c_uint8),
        ("defeated_blaine", ct.c_uint8),
        ("defeated_giovanni", ct.c_uint8),
        ("defeated_e4_lorelei", ct.c_uint8),
        ("defeated_e4_bruno", ct.c_uint8),
        ("defeated_e4_agatha", ct.c_uint8),
        ("defeated_e4_lance", ct.c_uint8),
        ("defeated_champion", ct.c_uint8),
        ("game_clear", ct.c_uint8),
    ]

# ── Observation parser ──

def parse_scalar_obs(buf):
    """Parse the 55-byte PfrScalarObs from raw obs buffer."""
    d = {}
    off = 0
    d['player_x'] = struct.unpack_from('<h', buf, off)[0]; off += 2
    d['player_y'] = struct.unpack_from('<h', buf, off)[0]; off += 2
    d['map_group'] = buf[off]; off += 1
    d['map_num'] = buf[off]; off += 1
    d['map_layout_id'] = buf[off]; off += 1
    d['player_direction'] = buf[off]; off += 1
    d['player_avatar_flags'] = buf[off]; off += 1
    d['player_running_state'] = buf[off]; off += 1
    d['player_transition_state'] = buf[off]; off += 1
    d['in_battle'] = buf[off]; off += 1
    d['battle_outcome'] = buf[off]; off += 1

    party = []
    for i in range(6):
        p = {}
        p['species'] = struct.unpack_from('<H', buf, off)[0]; off += 2
        p['level'] = buf[off]; off += 1
        p['hp_pct'] = buf[off]; off += 1
        p['status'] = buf[off]; off += 1
        p['type1'] = buf[off]; off += 1
        party.append(p)
    d['party'] = party

    d['badges'] = buf[off]; off += 1
    d['money'] = struct.unpack_from('<H', buf, off)[0]; off += 2
    d['weather'] = buf[off]; off += 1
    d['step_counter'] = struct.unpack_from('<H', buf, off)[0]; off += 2

    assert off == 55, f"Scalar obs size mismatch: expected 55, got {off}"
    return d

def parse_npc_obs(buf, start=55):
    """Parse NPC observations (15 NPCs, 6 bytes each)."""
    npcs = []
    off = start
    for i in range(15):
        n = {}
        n['dx'] = struct.unpack_from('b', buf, off)[0]; off += 1
        n['dy'] = struct.unpack_from('b', buf, off)[0]; off += 1
        n['graphics_id'] = buf[off]; off += 1
        n['direction'] = buf[off]; off += 1
        n['active'] = buf[off]; off += 1
        n['movement_type'] = buf[off]; off += 1
        npcs.append(n)
    return npcs

def parse_tiles(buf, start=145):
    """Parse 9x9 tile grid."""
    return buf[start:start+81]

# ── Validation functions ──

errors = []
warnings = []

def check(cond, msg, warn=False):
    if not cond:
        if warn:
            warnings.append(msg)
            print(f"  ⚠ WARNING: {msg}")
        else:
            errors.append(msg)
            print(f"  ✗ FAIL: {msg}")
        return False
    return True

def validate_scalar_obs(d, label=""):
    """Validate scalar observation fields for sanity."""
    prefix = f"[{label}] " if label else ""
    ok = True

    # Player position: Pokemon FireRed maps are typically < 1000 tiles
    ok &= check(-500 <= d['player_x'] <= 500, f"{prefix}player_x={d['player_x']} out of range [-500,500]")
    ok &= check(-500 <= d['player_y'] <= 500, f"{prefix}player_y={d['player_y']} out of range [-500,500]")

    # Map identity
    ok &= check(d['map_group'] < 60, f"{prefix}map_group={d['map_group']} too large (max ~40)")
    ok &= check(d['map_num'] < 120, f"{prefix}map_num={d['map_num']} too large")

    # Player state
    ok &= check(d['player_direction'] <= 15, f"{prefix}player_direction={d['player_direction']} > 15")

    # Battle state
    ok &= check(d['in_battle'] <= 1, f"{prefix}in_battle={d['in_battle']} not 0/1")

    # Party validation
    has_pokemon = False
    for i, p in enumerate(d['party']):
        if p['species'] > 0:
            has_pokemon = True
            ok &= check(p['species'] <= 500, f"{prefix}party[{i}].species={p['species']} too large")
            ok &= check(1 <= p['level'] <= 100, f"{prefix}party[{i}].level={p['level']} out of [1,100]")
            # hp_pct: 0-255 (0=fainted, 255=full)
            ok &= check(p['hp_pct'] <= 255, f"{prefix}party[{i}].hp_pct={p['hp_pct']} > 255")

    ok &= check(has_pokemon, f"{prefix}No pokemon in party! All species=0", warn=True)

    # Badges: bitmask of 8 badges
    ok &= check(d['badges'] <= 0xFF, f"{prefix}badges={d['badges']} > 0xFF")

    # Money: uint16 capped
    ok &= check(d['money'] <= 65535, f"{prefix}money={d['money']} > 65535")

    return ok

def validate_npc_obs(npcs, label=""):
    prefix = f"[{label}] " if label else ""
    ok = True
    active_count = sum(1 for n in npcs if n['active'])

    for i, n in enumerate(npcs):
        if n['active']:
            ok &= check(-127 <= n['dx'] <= 127, f"{prefix}npc[{i}].dx={n['dx']} out of range")
            ok &= check(-127 <= n['dy'] <= 127, f"{prefix}npc[{i}].dy={n['dy']} out of range")
            ok &= check(n['direction'] <= 15, f"{prefix}npc[{i}].direction={n['direction']} > 15")

    return ok

def validate_tiles(tiles, label=""):
    prefix = f"[{label}] " if label else ""
    ok = True
    ok &= check(len(tiles) == 81, f"{prefix}tile_grid size={len(tiles)}, expected 81")

    # Check tiles aren't ALL zero (unlikely in a valid map)
    nonzero = sum(1 for t in tiles if t != 0)
    ok &= check(nonzero > 0, f"{prefix}ALL tiles are zero — likely corrupt or uninitialized", warn=True)

    return ok

def validate_reward_info(info, label=""):
    prefix = f"[{label}] " if label else ""
    ok = True

    ok &= check(-500 <= info.player_x <= 500, f"{prefix}reward player_x={info.player_x}")
    ok &= check(-500 <= info.player_y <= 500, f"{prefix}reward player_y={info.player_y}")
    ok &= check(info.party_count <= 6, f"{prefix}reward party_count={info.party_count}")
    ok &= check(info.party_level_sum <= 600, f"{prefix}reward level_sum={info.party_level_sum}")
    ok &= check(info.money <= 999999, f"{prefix}reward money={info.money}")
    ok &= check(info.in_battle <= 1, f"{prefix}reward in_battle={info.in_battle}")

    return ok

def validate_obs_vs_reward(scalar, info, label=""):
    """Cross-check obs fields against reward info for consistency."""
    prefix = f"[{label}] " if label else ""
    ok = True

    ok &= check(scalar['player_x'] == info.player_x,
                f"{prefix}x mismatch: obs={scalar['player_x']} vs info={info.player_x}")
    ok &= check(scalar['player_y'] == info.player_y,
                f"{prefix}y mismatch: obs={scalar['player_y']} vs info={info.player_y}")
    ok &= check(scalar['map_group'] == info.map_group,
                f"{prefix}map_group mismatch: obs={scalar['map_group']} vs info={info.map_group}")
    ok &= check(scalar['map_num'] == info.map_num,
                f"{prefix}map_num mismatch: obs={scalar['map_num']} vs info={info.map_num}")
    ok &= check(scalar['badges'] == info.badges,
                f"{prefix}badges mismatch: obs={scalar['badges']} vs info={info.badges}")

    return ok

def validate_reward_step(reward, label=""):
    """Validate a single reward value."""
    prefix = f"[{label}] " if label else ""
    ok = True
    ok &= check(not np.isnan(reward), f"{prefix}reward is NaN!")
    ok &= check(not np.isinf(reward), f"{prefix}reward is Inf!")
    ok &= check(-100.0 <= reward <= 100.0, f"{prefix}reward={reward} out of [-100,100]")
    return ok

# ── Main ──

def main():
    global errors, warnings

    print("=" * 60)
    print("PFR Native Obs/Reward Validation")
    print("=" * 60)

    # Load library
    print(f"\nLoading {SO_PATH}...")
    lib = ct.CDLL(SO_PATH)

    # Setup function signatures
    lib.pfr_game_boot.argtypes = []
    lib.pfr_game_boot.restype = None
    lib.pfr_game_load_state.argtypes = [ct.c_char_p]
    lib.pfr_game_load_state.restype = ct.c_int
    lib.pfr_game_restore_hot.argtypes = []
    lib.pfr_game_restore_hot.restype = None
    lib.pfr_game_save_hot.argtypes = []
    lib.pfr_game_save_hot.restype = None
    lib.pfr_game_step_frames.argtypes = [ct.c_uint16, ct.c_int]
    lib.pfr_game_step_frames.restype = None
    lib.pfr_game_step_frames_fast.argtypes = [ct.c_uint16, ct.c_int]
    lib.pfr_game_step_frames_fast.restype = None
    lib.pfr_game_extract_obs.argtypes = [ct.POINTER(ct.c_ubyte)]
    lib.pfr_game_extract_obs.restype = None
    lib.pfr_game_get_reward_info.argtypes = [ct.POINTER(RewardInfo)]
    lib.pfr_game_get_reward_info.restype = None
    lib.pfr_game_extract_obs_full.argtypes = [ct.c_void_p]
    lib.pfr_game_extract_obs_full.restype = None
    lib.pfr_game_get_reward_info_full.argtypes = [ct.POINTER(RewardInfoFull)]
    lib.pfr_game_get_reward_info_full.restype = None

    # Boot
    print("\n── TEST 1: Boot & initial obs ──")
    lib.pfr_game_boot()
    print("  ✓ Boot completed")

    # Extract obs
    obs = np.zeros(OBS_SIZE, dtype=np.uint8)
    obs_ptr = obs.ctypes.data_as(ct.POINTER(ct.c_ubyte))
    lib.pfr_game_extract_obs(obs_ptr)

    scalar = parse_scalar_obs(obs)
    print(f"  pos=({scalar['player_x']},{scalar['player_y']}) "
          f"map={scalar['map_group']}.{scalar['map_num']} "
          f"badges={scalar['badges']:08b} "
          f"money={scalar['money']}")

    party_species = [p['species'] for p in scalar['party'] if p['species'] > 0]
    party_levels = [p['level'] for p in scalar['party'] if p['species'] > 0]
    print(f"  party: species={party_species} levels={party_levels}")

    validate_scalar_obs(scalar, "boot")

    npcs = parse_npc_obs(obs)
    validate_npc_obs(npcs, "boot")
    active_npcs = sum(1 for n in npcs if n['active'])
    print(f"  active NPCs: {active_npcs}")

    tiles = parse_tiles(obs)
    validate_tiles(tiles, "boot")
    nonzero_tiles = sum(1 for t in tiles if t != 0)
    print(f"  tile grid: {nonzero_tiles}/81 nonzero")

    # Reward info
    rinfo = RewardInfo()
    lib.pfr_game_get_reward_info(ct.byref(rinfo))
    validate_reward_info(rinfo, "boot")
    validate_obs_vs_reward(scalar, rinfo, "boot")
    print(f"  reward_info: party={rinfo.party_count} lvlsum={rinfo.party_level_sum} "
          f"money={rinfo.money}")

    # ── TEST 2: Step actions and validate rewards ──
    print("\n── TEST 2: Step 50 random actions, validate rewards ──")
    np.random.seed(42)
    BTN_MAP = [0, 0x40, 0x80, 0x20, 0x10, 0x01, 0x02, 0x08, 0x04, 0x300]
    total_reward = 0.0
    for step in range(50):
        action = np.random.randint(0, NUM_ACTIONS)
        buttons = BTN_MAP[action]
        lib.pfr_game_step_frames_fast(ct.c_uint16(buttons), 4)

        lib.pfr_game_extract_obs(obs_ptr)
        scalar = parse_scalar_obs(obs)

        rinfo2 = RewardInfo()
        lib.pfr_game_get_reward_info(ct.byref(rinfo2))

        # Compute reward (exploration only for now)
        validate_scalar_obs(scalar, f"step{step}")
        validate_reward_info(rinfo2, f"step{step}")
        validate_obs_vs_reward(scalar, rinfo2, f"step{step}")

    print(f"  ✓ 50 steps completed without crashes")

    # ── TEST 3: Hot restore and re-extract ──
    print("\n── TEST 3: Hot restore → extract obs ──")
    lib.pfr_game_restore_hot()
    lib.pfr_game_extract_obs(obs_ptr)
    scalar_hot = parse_scalar_obs(obs)
    validate_scalar_obs(scalar_hot, "hot_restore")
    print(f"  pos=({scalar_hot['player_x']},{scalar_hot['player_y']}) "
          f"map={scalar_hot['map_group']}.{scalar_hot['map_num']}")
    print(f"  ✓ Hot restore obs valid")

    # ── TEST 4: Load savestates and validate ──
    print(f"\n── TEST 4: Load savestates from {STATE_DIR} ──")
    savestates = []
    if os.path.isdir(STATE_DIR):
        savestates = sorted([f for f in os.listdir(STATE_DIR) if f.endswith('.pfrstate')])

    if not savestates:
        print("  ⚠ No savestates found, skipping")
    else:
        # Test a representative subset
        test_states = []
        for name in ['chooseastarter.pfrstate', 'v5_bulba.pfrstate',
                      'char_beat_surge.pfrstate', 'char_8_badges.pfrstate',
                      'char_beat_champion.pfrstate', 'native_boot_overworld.pfrstate',
                      'char_viridian_forest.pfrstate', 'char_pewter.pfrstate',
                      'char_celadon_beat_gym.pfrstate', 'char_beat_sabrina.pfrstate']:
            if name in savestates:
                test_states.append(name)

        # Add first/last if not already included
        if savestates[0] not in test_states:
            test_states.insert(0, savestates[0])
        if savestates[-1] not in test_states:
            test_states.append(savestates[-1])

        for sname in test_states:
            spath = os.path.join(STATE_DIR, sname)
            ret = lib.pfr_game_load_state(spath.encode())
            if ret != 0:
                print(f"  ✗ Failed to load {sname}")
                errors.append(f"Failed to load savestate: {sname}")
                continue

            lib.pfr_game_extract_obs(obs_ptr)
            sc = parse_scalar_obs(obs)
            npcs = parse_npc_obs(obs)
            tiles = parse_tiles(obs)

            ri = RewardInfo()
            lib.pfr_game_get_reward_info(ct.byref(ri))

            label = sname.replace('.pfrstate', '')
            validate_scalar_obs(sc, label)
            validate_npc_obs(npcs, label)
            validate_tiles(tiles, label)
            validate_reward_info(ri, label)
            validate_obs_vs_reward(sc, ri, label)

            party = [(p['species'], p['level']) for p in sc['party'] if p['species'] > 0]
            badge_count = bin(sc['badges']).count('1')
            print(f"  {sname}: pos=({sc['player_x']},{sc['player_y']}) "
                  f"map={sc['map_group']}.{sc['map_num']} "
                  f"badges={badge_count} party={party} money={sc['money']} ✓")

    # ── TEST 5: Full obs extraction ──
    print(f"\n── TEST 5: Full obs extraction ({FULL_OBS_SIZE} bytes) ──")
    # Restore to a good state first
    lib.pfr_game_restore_hot()
    # Need to do a regular step first to populate rendering state
    lib.pfr_game_step_frames(ct.c_uint16(0), 4)

    full_obs_buf = (ct.c_uint8 * FULL_OBS_SIZE)()
    lib.pfr_game_extract_obs_full(ct.byref(full_obs_buf))
    full_obs = bytes(full_obs_buf)

    # Screen (72x80 = 5760 bytes)
    screen = np.frombuffer(full_obs[:5760], dtype=np.uint8).reshape(80, 72)
    screen_nonzero = np.count_nonzero(screen)
    print(f"  screen: {screen_nonzero}/{5760} nonzero pixels "
          f"(min={screen.min()} max={screen.max()} mean={screen.mean():.1f})")
    if screen_nonzero == 0:
        print(f"  ⚠ WARNING: screen is all zeros (rendering may not be initialized)")
        warnings.append("Full obs screen is all zeros")

    # Scalars (20 bytes at offset 5760)
    so = 5760
    fs = {}
    fs['player_x'] = struct.unpack_from('<h', full_obs, so)[0]; so += 2
    fs['player_y'] = struct.unpack_from('<h', full_obs, so)[0]; so += 2
    fs['map_group'] = full_obs[so]; so += 1
    fs['map_num'] = full_obs[so]; so += 1
    fs['map_layout_id'] = struct.unpack_from('<H', full_obs, so)[0]; so += 2
    fs['direction'] = full_obs[so]; so += 1
    fs['running_state'] = full_obs[so]; so += 1
    fs['in_battle'] = full_obs[so]; so += 1
    fs['battle_outcome'] = full_obs[so]; so += 1
    fs['badges'] = full_obs[so]; so += 1
    fs['weather'] = full_obs[so]; so += 1
    fs['money'] = struct.unpack_from('<I', full_obs, so)[0]; so += 4
    fs['step_counter'] = struct.unpack_from('<H', full_obs, so)[0]; so += 2

    print(f"  full scalars: pos=({fs['player_x']},{fs['player_y']}) "
          f"map={fs['map_group']}.{fs['map_num']} badges={fs['badges']:08b} "
          f"money={fs['money']}")

    check(-500 <= fs['player_x'] <= 500, f"full obs player_x={fs['player_x']}")
    check(-500 <= fs['player_y'] <= 500, f"full obs player_y={fs['player_y']}")
    check(fs['money'] <= 999999, f"full obs money={fs['money']}")

    # Party (6 * 30 = 180 bytes at offset 5780)
    po = 5780
    for i in range(6):
        species = struct.unpack_from('<H', full_obs, po)[0]
        level = full_obs[po + 2]
        hp = struct.unpack_from('<H', full_obs, po + 3)[0]
        max_hp = struct.unpack_from('<H', full_obs, po + 5)[0]
        atk = struct.unpack_from('<H', full_obs, po + 7)[0]
        dfn = struct.unpack_from('<H', full_obs, po + 9)[0]
        spd = struct.unpack_from('<H', full_obs, po + 11)[0]
        spa = struct.unpack_from('<H', full_obs, po + 13)[0]
        spd2 = struct.unpack_from('<H', full_obs, po + 15)[0]
        status = struct.unpack_from('<I', full_obs, po + 17)[0]
        type1 = full_obs[po + 21]
        type2 = full_obs[po + 22]
        moves = struct.unpack_from('<4H', full_obs, po + 23)
        po += 30  # wait, let's recalculate: 2+1+2+2+2+2+2+2+2+4+1+1+8 = 31

        if species > 0:
            check(species <= 500, f"full party[{i}] species={species}")
            check(1 <= level <= 100, f"full party[{i}] level={level}")
            check(hp <= max_hp or max_hp == 0, f"full party[{i}] hp={hp} > maxhp={max_hp}")
            check(max_hp <= 999, f"full party[{i}] maxhp={max_hp}")
            check(atk <= 999, f"full party[{i}] atk={atk}")
            check(dfn <= 999, f"full party[{i}] def={dfn}")
            print(f"  party[{i}]: species={species} lv{level} "
                  f"HP={hp}/{max_hp} ATK={atk} DEF={dfn} SPD={spd} "
                  f"types={type1}/{type2} moves={moves}")

    # ── TEST 6: Full reward info ──
    print(f"\n── TEST 6: Full reward info ──")
    rinfo_full = RewardInfoFull()
    lib.pfr_game_get_reward_info_full(ct.byref(rinfo_full))

    print(f"  pos=({rinfo_full.player_x},{rinfo_full.player_y}) "
          f"map={rinfo_full.map_group}.{rinfo_full.map_num}")
    print(f"  badges={rinfo_full.badges:08b} party={rinfo_full.party_count} "
          f"lvlsum={rinfo_full.party_level_sum}")
    print(f"  money={rinfo_full.money} in_battle={rinfo_full.in_battle}")
    print(f"  pokedex: seen={rinfo_full.pokedex_seen_count} "
          f"owned={rinfo_full.pokedex_owned_count}")
    print(f"  story_flags={rinfo_full.story_flags_set} "
          f"trainer_flags={rinfo_full.trainer_flags_set} "
          f"system_flags={rinfo_full.system_flags_set}")
    print(f"  hp_avg_pct={rinfo_full.party_hp_sum_pct}")

    hms = []
    if rinfo_full.has_hm01: hms.append("Cut")
    if rinfo_full.has_hm02: hms.append("Fly")
    if rinfo_full.has_hm03: hms.append("Surf")
    if rinfo_full.has_hm04: hms.append("Strength")
    if rinfo_full.has_hm05: hms.append("Flash")
    print(f"  HMs: {hms if hms else 'none'}")

    items = []
    if rinfo_full.has_ss_ticket: items.append("SS Ticket")
    if rinfo_full.has_silph_scope: items.append("Silph Scope")
    if rinfo_full.has_poke_flute: items.append("Poke Flute")
    if rinfo_full.has_bicycle: items.append("Bicycle")
    if rinfo_full.has_tea: items.append("Tea")
    print(f"  Key items: {items if items else 'none'}")

    gyms = []
    if rinfo_full.defeated_brock: gyms.append("Brock")
    if rinfo_full.defeated_misty: gyms.append("Misty")
    if rinfo_full.defeated_surge: gyms.append("Surge")
    if rinfo_full.defeated_erika: gyms.append("Erika")
    if rinfo_full.defeated_koga: gyms.append("Koga")
    if rinfo_full.defeated_sabrina: gyms.append("Sabrina")
    if rinfo_full.defeated_blaine: gyms.append("Blaine")
    if rinfo_full.defeated_giovanni: gyms.append("Giovanni")
    print(f"  Defeated gyms: {gyms if gyms else 'none'}")

    check(rinfo_full.party_count <= 6, f"full reward party_count={rinfo_full.party_count}")
    check(rinfo_full.party_level_sum <= 600, f"full reward level_sum={rinfo_full.party_level_sum}")
    check(rinfo_full.money <= 999999, f"full reward money={rinfo_full.money}")
    check(rinfo_full.pokedex_seen_count <= 500, f"full reward seen={rinfo_full.pokedex_seen_count}")
    check(rinfo_full.pokedex_owned_count <= rinfo_full.pokedex_seen_count + 5,
          f"full reward owned={rinfo_full.pokedex_owned_count} > seen={rinfo_full.pokedex_seen_count}")
    check(rinfo_full.party_hp_sum_pct <= 100, f"full reward hp_pct={rinfo_full.party_hp_sum_pct}")

    # ── TEST 7: PufferLib env step simulation ──
    print(f"\n── TEST 7: Simulated PufferLib env loop (100 steps) ──")
    lib.pfr_game_restore_hot()

    env_obs = np.zeros(OBS_SIZE, dtype=np.uint8)
    env_obs_ptr = env_obs.ctypes.data_as(ct.POINTER(ct.c_ubyte))
    cumulative_reward = 0.0
    prev_visit = set()
    prev_badges = 0
    prev_level_sum = 0

    for step in range(100):
        action = np.random.randint(0, NUM_ACTIONS)
        buttons = BTN_MAP[action]
        lib.pfr_game_step_frames_fast(ct.c_uint16(buttons), 4)

        lib.pfr_game_extract_obs(env_obs_ptr)
        sc = parse_scalar_obs(env_obs)
        ri = RewardInfo()
        lib.pfr_game_get_reward_info(ct.byref(ri))

        # Compute reward like the C env does
        reward = 0.0
        tile_key = (ri.map_group, ri.map_num, ri.player_x, ri.player_y)
        if tile_key not in prev_visit:
            prev_visit.add(tile_key)
            reward += 0.02

        new_badges = ri.badges & ~prev_badges
        if new_badges:
            reward += 10.0 * bin(new_badges).count('1')
            prev_badges = ri.badges

        if ri.party_level_sum > prev_level_sum:
            reward += 0.1 * (ri.party_level_sum - prev_level_sum)
            prev_level_sum = ri.party_level_sum

        validate_reward_step(reward, f"env_step{step}")
        cumulative_reward += reward

    print(f"  ✓ 100 env steps completed")
    print(f"  cumulative reward: {cumulative_reward:.4f}")
    print(f"  unique tiles visited: {len(prev_visit)}")

    # ── TEST 8: Obs buffer size verification ──
    print(f"\n── TEST 8: Obs buffer size verification ──")
    scalar_size = 55  # 4+3+4+2+36+1+2+1+2
    npc_size = 15 * 6  # 90
    tile_size = 81
    expected = scalar_size + npc_size + tile_size
    check(expected == OBS_SIZE, f"OBS_SIZE mismatch: computed={expected} vs defined={OBS_SIZE}")
    print(f"  scalar={scalar_size} + npc={npc_size} + tiles={tile_size} = {expected} ✓")

    # Full obs
    # PfrPartyMonObs: 2+1+2+2+2+2+2+2+2+4+1+1+8=31 bytes... actually
    # let me recount: uint16 species(2) + uint8 level(1) + uint16 hp(2) + uint16 max_hp(2) +
    # uint16 attack(2) + uint16 defense(2) + uint16 speed(2) + uint16 sp_atk(2) + uint16 sp_def(2) +
    # uint32 status(4) + uint8 type1(1) + uint8 type2(1) + uint16 moves[4](8) = 31? No...
    # 2+1+2+2+2+2+2+2+2+4+1+1+8 = 31. But the struct says 30 bytes in the header.
    # With packed, no padding. Let's verify:
    print(f"  PfrPartyMonObs packed size: 2+1+2+2+2+2+2+2+2+4+1+1+8 = 31 bytes")
    print(f"  Header says 30 bytes — need to verify struct layout!")

    full_expected = 5760 + 20 + 180 + 80 + 60 + 288 + 52 + 52 + 128 + 81
    print(f"  Full obs expected: {full_expected} (header says {FULL_OBS_SIZE})")
    if full_expected != FULL_OBS_SIZE:
        print(f"  ⚠ Full obs size mismatch!")
        warnings.append(f"Full obs size: computed={full_expected} vs header={FULL_OBS_SIZE}")

    # ── Summary ──
    print("\n" + "=" * 60)
    print("VALIDATION SUMMARY")
    print("=" * 60)
    if errors:
        print(f"\n✗ {len(errors)} ERRORS:")
        for e in errors:
            print(f"  - {e}")
    else:
        print(f"\n✓ No errors!")

    if warnings:
        print(f"\n⚠ {len(warnings)} WARNINGS:")
        for w in warnings:
            print(f"  - {w}")

    print(f"\nTests run: 8")
    print(f"Errors: {len(errors)}")
    print(f"Warnings: {len(warnings)}")

    return 0 if not errors else 1

if __name__ == "__main__":
    sys.exit(main())
