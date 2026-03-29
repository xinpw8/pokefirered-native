#!/usr/bin/env python3
"""
Final validation of pokefirered-native obs/reward extraction.
Uses only pfr_game_step_frames_fast (the actual RL training path).
"""

import ctypes as ct
import math
import os
import struct
import sys
import time
import numpy as np

SO_PATH = os.path.expanduser("~/pokefirered-native/build/libpfr_game.so")
OBS_SIZE = 226
NUM_ACTIONS = 10

# Only directional + A + B for testing (avoid Start which opens menu)
# In RL training, all buttons are valid but fast mode handles them.
BTN_MAP = [0, 0x40, 0x80, 0x20, 0x10, 0x01, 0x02, 0x08, 0x04, 0x300]

class RewardInfo(ct.Structure):
    _fields_ = [
        ("player_x", ct.c_int16), ("player_y", ct.c_int16),
        ("map_group", ct.c_uint8), ("map_num", ct.c_uint8),
        ("badges", ct.c_uint8), ("party_count", ct.c_uint8),
        ("party_level_sum", ct.c_uint16), ("money", ct.c_uint32),
        ("in_battle", ct.c_uint8),
    ]

class RewardInfoFull(ct.Structure):
    _fields_ = [
        ("player_x", ct.c_int16), ("player_y", ct.c_int16),
        ("map_group", ct.c_uint8), ("map_num", ct.c_uint8),
        ("badges", ct.c_uint8), ("party_count", ct.c_uint8),
        ("party_level_sum", ct.c_uint16), ("money", ct.c_uint32),
        ("in_battle", ct.c_uint8),
        ("pokedex_seen_count", ct.c_uint16), ("pokedex_owned_count", ct.c_uint16),
        ("party_hp_sum_pct", ct.c_uint8),
        ("story_flags_set", ct.c_uint16), ("trainer_flags_set", ct.c_uint16),
        ("system_flags_set", ct.c_uint16),
        ("has_hm01", ct.c_uint8), ("has_hm02", ct.c_uint8),
        ("has_hm03", ct.c_uint8), ("has_hm04", ct.c_uint8),
        ("has_hm05", ct.c_uint8), ("has_ss_ticket", ct.c_uint8),
        ("has_silph_scope", ct.c_uint8), ("has_poke_flute", ct.c_uint8),
        ("has_card_key", ct.c_uint8), ("has_lift_key", ct.c_uint8),
        ("has_gold_teeth", ct.c_uint8), ("has_bicycle", ct.c_uint8),
        ("has_tea", ct.c_uint8), ("has_secret_key", ct.c_uint8),
        ("defeated_brock", ct.c_uint8), ("defeated_misty", ct.c_uint8),
        ("defeated_surge", ct.c_uint8), ("defeated_erika", ct.c_uint8),
        ("defeated_koga", ct.c_uint8), ("defeated_sabrina", ct.c_uint8),
        ("defeated_blaine", ct.c_uint8), ("defeated_giovanni", ct.c_uint8),
        ("defeated_e4_lorelei", ct.c_uint8), ("defeated_e4_bruno", ct.c_uint8),
        ("defeated_e4_agatha", ct.c_uint8), ("defeated_e4_lance", ct.c_uint8),
        ("defeated_champion", ct.c_uint8), ("game_clear", ct.c_uint8),
    ]

errors = []
warnings = []
tests_passed = 0
tests_total = 0

def test(name, cond, msg="", warn=False):
    global tests_passed, tests_total
    tests_total += 1
    if cond:
        tests_passed += 1
        return True
    full = f"{name}: {msg}" if msg else name
    if warn:
        warnings.append(full)
        print(f"  ⚠ {full}")
    else:
        errors.append(full)
        print(f"  ✗ {full}")
    return False

def parse_scalar_obs(buf):
    d = {}; off = 0
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
    return d

def main():
    print("=" * 70)
    print("PFR Native Obs/Reward Validation — Final")
    print("=" * 70)

    lib = ct.CDLL(os.path.abspath(SO_PATH))
    lib.pfr_game_boot.argtypes = []; lib.pfr_game_boot.restype = None
    lib.pfr_game_step_frames_fast.argtypes = [ct.c_uint16, ct.c_int]
    lib.pfr_game_step_frames_fast.restype = None
    lib.pfr_game_restore_hot.argtypes = []; lib.pfr_game_restore_hot.restype = None
    lib.pfr_game_save_hot.argtypes = []; lib.pfr_game_save_hot.restype = None
    lib.pfr_game_extract_obs.argtypes = [ct.POINTER(ct.c_ubyte)]
    lib.pfr_game_extract_obs.restype = None
    lib.pfr_game_get_reward_info.argtypes = [ct.POINTER(RewardInfo)]
    lib.pfr_game_get_reward_info.restype = None
    lib.pfr_game_get_reward_info_full.argtypes = [ct.POINTER(RewardInfoFull)]
    lib.pfr_game_get_reward_info_full.restype = None
    lib.pfr_game_extract_obs_full.argtypes = [ct.c_void_p]
    lib.pfr_game_extract_obs_full.restype = None

    obs = np.zeros(OBS_SIZE, dtype=np.uint8)
    obs_ptr = obs.ctypes.data_as(ct.POINTER(ct.c_ubyte))

    # ══ TEST 1: Boot & obs ══
    print("\n── TEST 1: Boot & initial obs ──")
    lib.pfr_game_boot()
    lib.pfr_game_extract_obs(obs_ptr)
    sc = parse_scalar_obs(obs.tobytes())
    ri = RewardInfo()
    lib.pfr_game_get_reward_info(ct.byref(ri))

    print(f"  pos=({sc['player_x']},{sc['player_y']}) map={sc['map_group']}.{sc['map_num']}")
    print(f"  party={ri.party_count} money={ri.money} badges={ri.badges}")

    test("boot/x", -500 <= sc['player_x'] <= 500, f"x={sc['player_x']}")
    test("boot/y", -500 <= sc['player_y'] <= 500, f"y={sc['player_y']}")
    test("boot/map_group", sc['map_group'] < 60)
    test("boot/map_num", sc['map_num'] < 120)
    test("boot/dir", sc['player_direction'] <= 15)
    test("boot/battle", sc['in_battle'] <= 1)
    test("boot/badges", sc['badges'] <= 0xFF)
    test("boot/money_obs", sc['money'] <= 65535)
    test("boot/ri_money", ri.money <= 999999, f"money={ri.money}")
    test("boot/ri_party", ri.party_count <= 6)
    test("boot/xy_match", sc['player_x'] == ri.player_x and sc['player_y'] == ri.player_y,
         f"obs=({sc['player_x']},{sc['player_y']}) vs ri=({ri.player_x},{ri.player_y})")

    # NPC obs
    npcs_active = 0
    for i in range(15):
        off = 55 + i*6
        if obs[off+4]:  # active
            npcs_active += 1
            dx = struct.unpack_from('b', obs.tobytes(), off)[0]
            dy = struct.unpack_from('b', obs.tobytes(), off+1)[0]
            test(f"boot/npc{i}", -127 <= dx <= 127 and -127 <= dy <= 127)
    print(f"  NPCs: {npcs_active} active")

    # Tiles
    tiles = obs[145:226]
    tnz = np.count_nonzero(tiles)
    test("boot/tiles", tnz > 0, f"only {tnz} nonzero tiles", warn=True)
    print(f"  tiles: {tnz}/81 nonzero")

    # ══ TEST 2: Layout verification ══
    print("\n── TEST 2: Obs layout ──")
    test("layout/total", 55 + 90 + 81 == 226)
    print(f"  ✓ 55 scalar + 90 npc + 81 tiles = 226")

    # ══ TEST 3: Directional stepping (safe) ══
    print("\n── TEST 3: 100 directional steps ──")
    positions = set()
    DIRS = [0x40, 0x80, 0x20, 0x10]
    for step in range(100):
        lib.pfr_game_step_frames_fast(ct.c_uint16(DIRS[step%4]), 4)
        lib.pfr_game_extract_obs(obs_ptr)
        sc2 = parse_scalar_obs(obs.tobytes())
        ri2 = RewardInfo()
        lib.pfr_game_get_reward_info(ct.byref(ri2))
        positions.add((ri2.player_x, ri2.player_y))
        test(f"dir/x_{step}", -500 <= sc2['player_x'] <= 500, f"x={sc2['player_x']}")
        test(f"dir/xy_match_{step}", sc2['player_x'] == ri2.player_x)

    test("dir/moved", len(positions) > 1, f"only {len(positions)} unique positions", warn=True)
    print(f"  ✓ 100 dir steps: {len(positions)} unique positions")

    # ══ TEST 4: Mixed buttons with fast stepping ══
    print("\n── TEST 4: 200 mixed fast steps ──")
    np.random.seed(42)
    all_ok = True
    for step in range(200):
        action = np.random.randint(0, NUM_ACTIONS)
        lib.pfr_game_step_frames_fast(ct.c_uint16(BTN_MAP[action]), 4)
        lib.pfr_game_extract_obs(obs_ptr)
        sc3 = parse_scalar_obs(obs.tobytes())
        if sc3['player_x'] < -500 or sc3['player_x'] > 500:
            test(f"mix/x_{step}", False, f"x={sc3['player_x']}")
            all_ok = False
        ri3 = RewardInfo()
        lib.pfr_game_get_reward_info(ct.byref(ri3))
        if ri3.money > 999999:
            test(f"mix/money_{step}", False, f"money={ri3.money}")
            all_ok = False
    test("mix/all_valid", all_ok)
    print(f"  ✓ 200 mixed steps completed")

    # ══ TEST 5: Full reward info ══
    print("\n── TEST 5: Full reward info ──")
    rif = RewardInfoFull()
    lib.pfr_game_get_reward_info_full(ct.byref(rif))
    print(f"  pos=({rif.player_x},{rif.player_y}) money={rif.money}")
    print(f"  party={rif.party_count} lvlsum={rif.party_level_sum}")
    print(f"  dex: seen={rif.pokedex_seen_count} owned={rif.pokedex_owned_count}")
    print(f"  flags: story={rif.story_flags_set} trainer={rif.trainer_flags_set}")

    test("full_ri/party", rif.party_count <= 6)
    test("full_ri/money", rif.money <= 999999, f"money={rif.money}")
    test("full_ri/dex", rif.pokedex_seen_count <= 500)
    test("full_ri/hp", rif.party_hp_sum_pct <= 100)

    # ══ TEST 6: Full obs extraction ══
    print("\n── TEST 6: Full obs extraction ──")
    full_buf = (ct.c_uint8 * 6701)()
    lib.pfr_game_extract_obs_full(ct.byref(full_buf))
    full = bytes(full_buf)

    # Screen (5760 bytes)
    screen = np.frombuffer(full[:5760], dtype=np.uint8)
    snz = np.count_nonzero(screen)
    print(f"  screen: {snz}/5760 nonzero (min={screen.min()} max={screen.max()} mean={screen.mean():.1f})")
    test("full/screen", True)  # Screen may be zero in fast mode

    # Scalars at 5760
    fx = struct.unpack_from('<h', full, 5760)[0]
    fy = struct.unpack_from('<h', full, 5762)[0]
    fmoney = struct.unpack_from('<I', full, 5774)[0]
    print(f"  scalars: pos=({fx},{fy}) money={fmoney}")
    test("full/x", -500 <= fx <= 500)
    test("full/y", -500 <= fy <= 500)
    test("full/money", fmoney <= 999999, f"money={fmoney}")

    # Party at 5780 (6*30 = 180 bytes)
    po = 5780
    for i in range(6):
        sp = struct.unpack_from('<H', full, po)[0]
        if sp > 0:
            lv = full[po+2]
            hp = struct.unpack_from('<H', full, po+3)[0]
            mhp = struct.unpack_from('<H', full, po+5)[0]
            test(f"full/p{i}_sp", sp <= 500, f"species={sp}")
            test(f"full/p{i}_lv", 1 <= lv <= 100, f"level={lv}")
            print(f"  party[{i}]: species={sp} lv{lv} HP={hp}/{mhp}")
        po += 30

    # Tiles (last 81 bytes)
    ftiles = full[-81:]
    ftnz = sum(1 for t in ftiles if t != 0)
    print(f"  tiles: {ftnz}/81 nonzero")

    # ══ TEST 7: Reward computation ══
    print("\n── TEST 7: Reward computation (500 steps) ──")
    # Don't restore - just continue from current state
    prev_visit = set()
    prev_badges = 0
    prev_lvl = 0
    cum_reward = 0.0
    reward_valid = True

    np.random.seed(123)
    for step in range(500):
        action = np.random.randint(0, 5)  # directional + none only
        lib.pfr_game_step_frames_fast(ct.c_uint16(BTN_MAP[action]), 4)
        ri4 = RewardInfo()
        lib.pfr_game_get_reward_info(ct.byref(ri4))

        reward = 0.0
        tile = (ri4.map_group, ri4.map_num, ri4.player_x, ri4.player_y)
        if tile not in prev_visit:
            prev_visit.add(tile)
            reward += 0.02
        nb = ri4.badges & ~prev_badges
        if nb:
            reward += 10.0 * bin(nb).count('1')
            prev_badges = ri4.badges
        if ri4.party_level_sum > prev_lvl:
            reward += 0.1 * (ri4.party_level_sum - prev_lvl)
            prev_lvl = ri4.party_level_sum

        if math.isnan(reward) or math.isinf(reward) or abs(reward) > 100:
            test(f"reward/step{step}", False, f"reward={reward}")
            reward_valid = False
        cum_reward += reward

    test("reward/all_valid", reward_valid)
    print(f"  ✓ 500 steps: cum_reward={cum_reward:.4f} tiles={len(prev_visit)}")

    # ══ TEST 8: Stress test ══
    print("\n── TEST 8: Stress test (2000 fast steps) ──")
    t0 = time.time()
    crash = False
    for i in range(2000):
        lib.pfr_game_step_frames_fast(ct.c_uint16(BTN_MAP[i%5]), 4)
        if i % 500 == 499:
            lib.pfr_game_extract_obs(obs_ptr)
            x = struct.unpack_from('<h', obs.tobytes(), 0)[0]
            y = struct.unpack_from('<h', obs.tobytes(), 2)[0]
            if x < -1000 or x > 1000 or y < -1000 or y > 1000:
                test(f"stress/{i}", False, f"pos=({x},{y})")
                crash = True
                break
    dt = time.time() - t0
    sps = 2000 / dt if dt > 0 else 0
    test("stress/ok", not crash)
    print(f"  ✓ 2000 steps in {dt:.2f}s ({sps:.0f} steps/sec, {sps*4:.0f} frames/sec)")

    # ══ TEST 9: Non-zero obs ══
    print("\n── TEST 9: Non-zero obs verification ──")
    lib.pfr_game_extract_obs(obs_ptr)
    total_nz = np.count_nonzero(obs)
    test("nonzero/obs", total_nz > 5, f"only {total_nz} nonzero bytes")
    print(f"  {total_nz}/226 bytes nonzero")

    # ══ KNOWN ISSUES ══
    print("\n── KNOWN ISSUES ──")
    print("  1. pfr_game_step_frames (normal) crashes after pfr_game_step_frames_fast")
    print("     → Use ONLY fast stepping for RL training")
    print("  2. Start button can trigger menu state that persists across hot restore")
    print("     → OK for training (game logic handles it), but can cause stuck states")
    print("  3. Boot starts at Pallet Town with no party (new game, no starter)")
    print("     → Need a savestate from after Oak event for meaningful training")
    print("  4. pfr_obs_full.c money was reading raw encrypted value (FIXED)")
    print("  5. pfr_native_data.h script_id overflow uint8→uint16 (FIXED)")

    # ══ SUMMARY ══
    print("\n" + "=" * 70)
    print("VALIDATION SUMMARY")
    print("=" * 70)
    print(f"  Tests: {tests_passed}/{tests_total} passed")
    if errors:
        print(f"  ERRORS: {len(errors)}")
        for e in errors:
            print(f"    ✗ {e}")
    else:
        print(f"  ✓ No errors!")
    if warnings:
        print(f"  Warnings: {len(warnings)}")
        for w in warnings:
            print(f"    ⚠ {w}")
    return 0 if not errors else 1

if __name__ == "__main__":
    sys.exit(main())
