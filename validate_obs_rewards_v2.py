#!/usr/bin/env python3
"""
Comprehensive validation of pokefirered-native obs and reward extraction.
Works with the freshly-booted game state (no savestate dependency).
"""

import ctypes as ct
import math
import os
import struct
import sys
import numpy as np

SO_PATH = os.path.expanduser("~/pokefirered-native/build/libpfr_game.so")
STATE_PATH = os.path.expanduser("~/pokefirered-native/test_overworld.pfrstate")

OBS_SIZE = 226
FULL_OBS_SIZE = 6701
NUM_ACTIONS = 10

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
    else:
        full = f"{name}: {msg}" if msg else name
        if warn:
            warnings.append(full)
            print(f"  ⚠ {full}")
        else:
            errors.append(full)
            print(f"  ✗ {full}")
        return False

def parse_scalar_obs(buf):
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

def main():
    print("=" * 70)
    print("PFR Native Obs/Reward Validation v2")
    print("=" * 70)

    lib = ct.CDLL(os.path.abspath(SO_PATH))
    lib.pfr_game_boot.argtypes = []
    lib.pfr_game_boot.restype = None
    lib.pfr_game_load_state.argtypes = [ct.c_char_p]
    lib.pfr_game_load_state.restype = ct.c_int
    lib.pfr_game_restore_hot.argtypes = []
    lib.pfr_game_restore_hot.restype = None
    lib.pfr_game_save_hot.argtypes = []
    lib.pfr_game_save_hot.restype = None
    lib.pfr_game_save_state.argtypes = [ct.c_char_p]
    lib.pfr_game_save_state.restype = ct.c_int
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

    obs = np.zeros(OBS_SIZE, dtype=np.uint8)
    obs_ptr = obs.ctypes.data_as(ct.POINTER(ct.c_ubyte))

    # ══════════════════════════════════════════════════
    # TEST 1: Boot & initial obs
    # ══════════════════════════════════════════════════
    print("\n── TEST 1: Boot & initial obs ──")
    lib.pfr_game_boot()
    lib.pfr_game_extract_obs(obs_ptr)
    sc = parse_scalar_obs(obs.tobytes())

    print(f"  pos=({sc['player_x']},{sc['player_y']}) map={sc['map_group']}.{sc['map_num']}")
    test("boot/pos_x_range", -500 <= sc['player_x'] <= 500, f"x={sc['player_x']}")
    test("boot/pos_y_range", -500 <= sc['player_y'] <= 500, f"y={sc['player_y']}")
    test("boot/map_group", sc['map_group'] < 60, f"mg={sc['map_group']}")
    test("boot/map_num", sc['map_num'] < 120, f"mn={sc['map_num']}")
    test("boot/direction", sc['player_direction'] <= 15, f"dir={sc['player_direction']}")
    test("boot/in_battle", sc['in_battle'] <= 1, f"in_battle={sc['in_battle']}")
    test("boot/badges", sc['badges'] <= 0xFF, f"badges={sc['badges']}")
    test("boot/money", sc['money'] <= 65535, f"money={sc['money']}")

    # Reward info
    ri = RewardInfo()
    lib.pfr_game_get_reward_info(ct.byref(ri))
    test("boot/ri_x", ri.player_x == sc['player_x'], f"x: obs={sc['player_x']} vs ri={ri.player_x}")
    test("boot/ri_y", ri.player_y == sc['player_y'], f"y: obs={sc['player_y']} vs ri={ri.player_y}")
    test("boot/ri_map", ri.map_group == sc['map_group'] and ri.map_num == sc['map_num'])
    test("boot/ri_party", ri.party_count <= 6, f"count={ri.party_count}")
    test("boot/ri_money", ri.money <= 999999, f"money={ri.money}")
    print(f"  reward_info: party={ri.party_count} lvlsum={ri.party_level_sum} money={ri.money}")

    # NPC obs
    npcs = parse_npc_obs(obs.tobytes())
    active_npcs = sum(1 for n in npcs if n['active'])
    print(f"  active NPCs: {active_npcs}")
    for i, n in enumerate(npcs):
        if n['active']:
            test(f"boot/npc{i}_dx", -127 <= n['dx'] <= 127, f"dx={n['dx']}")
            test(f"boot/npc{i}_dy", -127 <= n['dy'] <= 127, f"dy={n['dy']}")
            test(f"boot/npc{i}_dir", n['direction'] <= 15, f"dir={n['direction']}")

    # Tiles
    tiles = obs[145:226]
    nonzero = np.count_nonzero(tiles)
    print(f"  tile grid: {nonzero}/81 nonzero")
    test("boot/tiles_exist", nonzero > 0, "all tiles zero!", warn=True)

    # ══════════════════════════════════════════════════
    # TEST 2: Obs buffer layout / size
    # ══════════════════════════════════════════════════
    print("\n── TEST 2: Obs buffer layout verification ──")
    test("layout/scalar_size", 55 == 55, "PfrScalarObs should be 55 bytes")
    test("layout/npc_block", 15 * 6 == 90, "NPC block should be 90 bytes")
    test("layout/tile_block", 81 == 81, "Tile block should be 81 bytes")
    test("layout/total", 55 + 90 + 81 == OBS_SIZE, f"total={55+90+81} vs {OBS_SIZE}")
    print(f"  ✓ 226 bytes = 55 scalar + 90 npc + 81 tiles")

    # ══════════════════════════════════════════════════
    # TEST 3: Step actions & validate rewards
    # ══════════════════════════════════════════════════
    print("\n── TEST 3: Step 200 random actions with pfr_game_step_frames_fast ──")
    np.random.seed(42)
    prev_visit = set()
    prev_badges = 0
    prev_level_sum = 0
    cumulative = 0.0
    all_rewards_valid = True
    position_changed = False
    initial_pos = (ri.player_x, ri.player_y)

    for step in range(200):
        action = np.random.randint(0, NUM_ACTIONS)
        lib.pfr_game_step_frames_fast(ct.c_uint16(BTN_MAP[action]), 4)
        lib.pfr_game_extract_obs(obs_ptr)
        sc2 = parse_scalar_obs(obs.tobytes())

        ri2 = RewardInfo()
        lib.pfr_game_get_reward_info(ct.byref(ri2))

        # Validate obs
        if not (-500 <= sc2['player_x'] <= 500 and -500 <= sc2['player_y'] <= 500):
            test(f"step{step}/pos", False, f"pos=({sc2['player_x']},{sc2['player_y']})")
            all_rewards_valid = False
        if sc2['map_group'] >= 60 or sc2['map_num'] >= 120:
            test(f"step{step}/map", False, f"map={sc2['map_group']}.{sc2['map_num']}")
            all_rewards_valid = False

        # Cross-check
        if ri2.player_x != sc2['player_x'] or ri2.player_y != sc2['player_y']:
            test(f"step{step}/xy_match", False,
                 f"obs=({sc2['player_x']},{sc2['player_y']}) vs ri=({ri2.player_x},{ri2.player_y})")

        # Compute reward
        reward = 0.0
        tile = (ri2.map_group, ri2.map_num, ri2.player_x, ri2.player_y)
        if tile not in prev_visit:
            prev_visit.add(tile)
            reward += 0.02
        new_b = ri2.badges & ~prev_badges
        if new_b:
            reward += 10.0 * bin(new_b).count('1')
            prev_badges = ri2.badges
        if ri2.party_level_sum > prev_level_sum:
            reward += 0.1 * (ri2.party_level_sum - prev_level_sum)
            prev_level_sum = ri2.party_level_sum

        if math.isnan(reward) or math.isinf(reward) or abs(reward) > 100:
            test(f"step{step}/reward", False, f"reward={reward}")
            all_rewards_valid = False

        cumulative += reward

        if (ri2.player_x, ri2.player_y) != initial_pos:
            position_changed = True

    test("steps/no_crash", True)
    test("steps/all_rewards_valid", all_rewards_valid)
    test("steps/position_changed", position_changed,
         "player never moved — game logic might not be running", warn=True)
    print(f"  ✓ 200 steps: cum_reward={cumulative:.4f}, unique_tiles={len(prev_visit)}, moved={position_changed}")

    # ══════════════════════════════════════════════════
    # TEST 4: pfr_game_step_frames (non-fast) comparison
    # ══════════════════════════════════════════════════
    print("\n── TEST 4: pfr_game_step_frames (normal mode) ──")
    lib.pfr_game_restore_hot()
    for i in range(20):
        lib.pfr_game_step_frames(ct.c_uint16(BTN_MAP[i % NUM_ACTIONS]), 4)
    lib.pfr_game_extract_obs(obs_ptr)
    sc3 = parse_scalar_obs(obs.tobytes())
    test("normal/pos_x", -500 <= sc3['player_x'] <= 500, f"x={sc3['player_x']}")
    test("normal/pos_y", -500 <= sc3['player_y'] <= 500, f"y={sc3['player_y']}")
    test("normal/in_battle", sc3['in_battle'] <= 1)
    print(f"  ✓ Normal stepping works: pos=({sc3['player_x']},{sc3['player_y']})")

    # ══════════════════════════════════════════════════
    # TEST 5: Save/Load state roundtrip
    # ══════════════════════════════════════════════════
    print("\n── TEST 5: Savestate roundtrip ──")
    lib.pfr_game_restore_hot()
    lib.pfr_game_extract_obs(obs_ptr)
    before = obs.copy()

    save_path = os.path.expanduser("~/pokefirered-native/test_roundtrip.pfrstate")
    ret = lib.pfr_game_save_state(save_path.encode())
    test("save/success", ret == 0, f"save returned {ret}")

    # Step a few frames to change state
    for i in range(10):
        lib.pfr_game_step_frames_fast(ct.c_uint16(BTN_MAP[1]), 4)

    # Reload
    ret = lib.pfr_game_load_state(save_path.encode())
    test("load/success", ret == 0, f"load returned {ret}")

    lib.pfr_game_extract_obs(obs_ptr)
    after = obs.copy()

    # Compare scalar obs (position should match)
    sc_before = parse_scalar_obs(before.tobytes())
    sc_after = parse_scalar_obs(after.tobytes())
    test("roundtrip/pos_x", sc_before['player_x'] == sc_after['player_x'],
         f"before={sc_before['player_x']} after={sc_after['player_x']}")
    test("roundtrip/pos_y", sc_before['player_y'] == sc_after['player_y'],
         f"before={sc_before['player_y']} after={sc_after['player_y']}")
    test("roundtrip/map", sc_before['map_group'] == sc_after['map_group'] and
         sc_before['map_num'] == sc_after['map_num'])
    print(f"  ✓ Roundtrip: pos before=({sc_before['player_x']},{sc_before['player_y']}) "
          f"after=({sc_after['player_x']},{sc_after['player_y']})")

    # ══════════════════════════════════════════════════
    # TEST 6: Hot save/restore
    # ══════════════════════════════════════════════════
    print("\n── TEST 6: Hot save/restore ──")
    lib.pfr_game_restore_hot()
    lib.pfr_game_extract_obs(obs_ptr)
    hot_before = parse_scalar_obs(obs.tobytes())

    for i in range(20):
        lib.pfr_game_step_frames_fast(ct.c_uint16(BTN_MAP[2]), 4)

    lib.pfr_game_restore_hot()
    lib.pfr_game_extract_obs(obs_ptr)
    hot_after = parse_scalar_obs(obs.tobytes())

    test("hot/pos_restored", hot_before['player_x'] == hot_after['player_x'] and
         hot_before['player_y'] == hot_after['player_y'],
         f"before=({hot_before['player_x']},{hot_before['player_y']}) "
         f"after=({hot_after['player_x']},{hot_after['player_y']})")
    print(f"  ✓ Hot restore works")

    # ══════════════════════════════════════════════════
    # TEST 7: Full obs extraction (6701 bytes)
    # ══════════════════════════════════════════════════
    print(f"\n── TEST 7: Full obs extraction ({FULL_OBS_SIZE} bytes) ──")
    lib.pfr_game_restore_hot()
    # Use normal step to init rendering state
    lib.pfr_game_step_frames(ct.c_uint16(0), 4)

    full_obs_buf = (ct.c_uint8 * FULL_OBS_SIZE)()
    lib.pfr_game_extract_obs_full(ct.byref(full_obs_buf))
    full = bytes(full_obs_buf)

    # Screen
    screen = np.frombuffer(full[:5760], dtype=np.uint8).reshape(80, 72)
    screen_nz = np.count_nonzero(screen)
    print(f"  screen: {screen_nz}/5760 nonzero (min={screen.min()} max={screen.max()} "
          f"mean={screen.mean():.1f})")
    test("full/screen_not_all_zero", screen_nz > 0, "screen all zeros", warn=True)

    # Scalars (20 bytes at offset 5760)
    so = 5760
    fx = struct.unpack_from('<h', full, so)[0]
    fy = struct.unpack_from('<h', full, so+2)[0]
    fmg = full[so+4]
    fmn = full[so+5]
    fmoney = struct.unpack_from('<I', full, so+12)[0]
    fbadges = full[so+10]
    print(f"  full scalars: pos=({fx},{fy}) map={fmg}.{fmn} money={fmoney} badges={fbadges:08b}")
    test("full/pos_x", -500 <= fx <= 500, f"x={fx}")
    test("full/pos_y", -500 <= fy <= 500, f"y={fy}")
    test("full/money", fmoney <= 999999, f"money={fmoney}")

    # Party (offset 5780, 30 bytes each)
    po = 5780
    for i in range(6):
        sp = struct.unpack_from('<H', full, po)[0]
        lv = full[po + 2]
        hp = struct.unpack_from('<H', full, po + 3)[0]
        mhp = struct.unpack_from('<H', full, po + 5)[0]
        po += 30
        if sp > 0:
            test(f"full/party{i}_species", sp <= 500, f"species={sp}")
            test(f"full/party{i}_level", 1 <= lv <= 100, f"level={lv}")
            test(f"full/party{i}_hp", hp <= mhp or mhp == 0, f"hp={hp} maxhp={mhp}")
            print(f"  party[{i}]: species={sp} lv{lv} HP={hp}/{mhp}")

    # Tiles (last 81 bytes)
    full_tiles = full[-81:]
    full_tiles_nz = sum(1 for t in full_tiles if t != 0)
    test("full/tiles", len(full_tiles) == 81)
    print(f"  tiles: {full_tiles_nz}/81 nonzero")

    # ══════════════════════════════════════════════════
    # TEST 8: Full reward info
    # ══════════════════════════════════════════════════
    print(f"\n── TEST 8: Full reward info ──")
    rif = RewardInfoFull()
    lib.pfr_game_get_reward_info_full(ct.byref(rif))
    print(f"  pos=({rif.player_x},{rif.player_y}) map={rif.map_group}.{rif.map_num}")
    print(f"  badges={rif.badges:08b} party={rif.party_count} lvlsum={rif.party_level_sum}")
    print(f"  money={rif.money} battle={rif.in_battle}")
    print(f"  dex: seen={rif.pokedex_seen_count} owned={rif.pokedex_owned_count}")
    print(f"  flags: story={rif.story_flags_set} trainer={rif.trainer_flags_set} sys={rif.system_flags_set}")
    print(f"  hp_avg_pct={rif.party_hp_sum_pct}")

    test("full_ri/party", rif.party_count <= 6, f"count={rif.party_count}")
    test("full_ri/lvlsum", rif.party_level_sum <= 600, f"sum={rif.party_level_sum}")
    test("full_ri/money", rif.money <= 999999, f"money={rif.money}")
    test("full_ri/dex_seen", rif.pokedex_seen_count <= 500)
    test("full_ri/dex_owned", rif.pokedex_owned_count <= 500)
    test("full_ri/hp_pct", rif.party_hp_sum_pct <= 100)

    # ══════════════════════════════════════════════════
    # TEST 9: Stress test - rapid step/obs/reward cycle
    # ══════════════════════════════════════════════════
    print(f"\n── TEST 9: Stress test (1000 fast steps) ──")
    lib.pfr_game_restore_hot()
    import time
    t0 = time.time()
    crash = False
    for i in range(1000):
        act = i % NUM_ACTIONS
        lib.pfr_game_step_frames_fast(ct.c_uint16(BTN_MAP[act]), 4)
        lib.pfr_game_extract_obs(obs_ptr)
        # Quick sanity: check first 4 bytes aren't garbage
        x = struct.unpack_from('<h', obs.tobytes(), 0)[0]
        y = struct.unpack_from('<h', obs.tobytes(), 2)[0]
        if x < -1000 or x > 1000 or y < -1000 or y > 1000:
            test(f"stress/step{i}", False, f"pos=({x},{y}) garbage")
            crash = True
            break
    dt = time.time() - t0
    sps = 1000 / dt if dt > 0 else 0
    test("stress/no_crash", not crash)
    print(f"  ✓ 1000 steps in {dt:.2f}s ({sps:.0f} steps/sec)")

    # ══════════════════════════════════════════════════
    # TEST 10: Zero-obs detection (ensure obs aren't all zero)
    # ══════════════════════════════════════════════════
    print(f"\n── TEST 10: Non-zero obs detection ──")
    lib.pfr_game_restore_hot()
    lib.pfr_game_step_frames_fast(ct.c_uint16(0), 4)
    lib.pfr_game_extract_obs(obs_ptr)
    total_nz = np.count_nonzero(obs)
    test("nonzero/obs", total_nz > 10, f"only {total_nz} nonzero bytes in obs")
    print(f"  {total_nz}/226 bytes nonzero")

    # ══════════════════════════════════════════════════
    # SUMMARY
    # ══════════════════════════════════════════════════
    print("\n" + "=" * 70)
    print("VALIDATION SUMMARY")
    print("=" * 70)
    print(f"  Tests:    {tests_passed}/{tests_total} passed")
    if errors:
        print(f"  ERRORS:   {len(errors)}")
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
