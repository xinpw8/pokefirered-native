#!/usr/bin/env python3
"""
Boot the game, walk to Oak's lab to get a starter Pokemon,
then save the state for validation testing.
"""
import ctypes as ct
import os
import struct
import sys
import time

SO_PATH = os.path.expanduser("~/pokefirered-native/build/libpfr_game.so")
SAVE_PATH = os.path.expanduser("~/pokefirered-native/test_overworld.pfrstate")

lib = ct.CDLL(os.path.abspath(SO_PATH))
lib.pfr_game_boot.argtypes = []
lib.pfr_game_boot.restype = None
lib.pfr_game_save_state.argtypes = [ct.c_char_p]
lib.pfr_game_save_state.restype = ct.c_int
lib.pfr_game_load_state.argtypes = [ct.c_char_p]
lib.pfr_game_load_state.restype = ct.c_int
lib.pfr_game_step_frames.argtypes = [ct.c_uint16, ct.c_int]
lib.pfr_game_step_frames.restype = None
lib.pfr_game_step_frames_fast.argtypes = [ct.c_uint16, ct.c_int]
lib.pfr_game_step_frames_fast.restype = None
lib.pfr_game_save_hot.argtypes = []
lib.pfr_game_save_hot.restype = None
lib.pfr_game_extract_obs.argtypes = [ct.POINTER(ct.c_ubyte)]
lib.pfr_game_extract_obs.restype = None

import numpy as np

class RewardInfo(ct.Structure):
    _fields_ = [
        ('player_x', ct.c_int16), ('player_y', ct.c_int16),
        ('map_group', ct.c_uint8), ('map_num', ct.c_uint8),
        ('badges', ct.c_uint8), ('party_count', ct.c_uint8),
        ('party_level_sum', ct.c_uint16), ('money', ct.c_uint32),
        ('in_battle', ct.c_uint8),
    ]
lib.pfr_game_get_reward_info.argtypes = [ct.POINTER(RewardInfo)]
lib.pfr_game_get_reward_info.restype = None

# Button constants (active-high)
BTN_NONE = 0
BTN_A = 0x01
BTN_B = 0x02
BTN_START = 0x08
BTN_UP = 0x40
BTN_DOWN = 0x80
BTN_LEFT = 0x20
BTN_RIGHT = 0x10

def step(buttons, frames=4):
    lib.pfr_game_step_frames(ct.c_uint16(buttons), frames)

def step_fast(buttons, frames=4):
    lib.pfr_game_step_frames_fast(ct.c_uint16(buttons), frames)

def get_state():
    obs = np.zeros(226, dtype=np.uint8)
    obs_ptr = obs.ctypes.data_as(ct.POINTER(ct.c_ubyte))
    lib.pfr_game_extract_obs(obs_ptr)
    ri = RewardInfo()
    lib.pfr_game_get_reward_info(ct.byref(ri))
    return {
        'px': ri.player_x, 'py': ri.player_y,
        'mg': ri.map_group, 'mn': ri.map_num,
        'party': ri.party_count, 'lvl': ri.party_level_sum,
        'money': ri.money, 'battle': ri.in_battle,
    }

print("Booting game...")
lib.pfr_game_boot()
s = get_state()
print(f"After boot: {s}")

# The boot sequence runs through intro/title/oak speech/new game to overworld.
# Player starts at Pallet Town (map 4.1), pos ~(6,6)
# Oak's lab is entered by walking down and then entering the building.

# Strategy: press A repeatedly to advance through any remaining dialogs,
# walk down to the grass, let Oak intercept, follow him to lab,
# choose a starter.

print("\nAttempting to navigate to get a starter Pokemon...")
print("Walking into tall grass to trigger Oak's event...")

# Walk down toward route 1
for i in range(30):
    step(BTN_DOWN, 4)

s = get_state()
print(f"After walking down: {s}")

# Press A to advance dialog
for i in range(100):
    step(BTN_A, 4)
    s = get_state()
    if s['party'] > 0:
        print(f"Got a Pokemon! {s}")
        break

if s['party'] == 0:
    # Keep trying — walk more, press A
    print("No pokemon yet, trying more A presses and movement...")
    for i in range(200):
        if i % 10 == 0:
            step(BTN_DOWN, 4)
        elif i % 20 == 5:
            step(BTN_UP, 4)
        else:
            step(BTN_A, 4)
        s = get_state()
        if s['party'] > 0:
            print(f"Got a Pokemon! {s}")
            break

if s['party'] == 0:
    print("Still no pokemon. More aggressive A mashing in Oak's lab...")
    # Walk left and up to Oak's lab area, then A mash
    for i in range(20):
        step(BTN_UP, 4)
    for i in range(500):
        step(BTN_A, 4)
        s = get_state()
        if s['party'] > 0:
            print(f"Got a Pokemon! {s}")
            break

if s['party'] == 0:
    print("WARNING: Could not get a starter pokemon automatically.")
    print("Saving state anyway (overworld, no party)...")
else:
    # Walk outside for a clean overworld state
    print("Walking to overworld...")
    for i in range(30):
        step(BTN_DOWN, 4)
    for i in range(10):
        step(BTN_A, 4)
    s = get_state()
    print(f"Final state: {s}")

# Save state
print(f"\nSaving state to {SAVE_PATH}...")
ret = lib.pfr_game_save_state(SAVE_PATH.encode())
print(f"Save returned: {ret}")

# Also save as hot state
lib.pfr_game_save_hot()

# Verify we can reload it
print("\nVerifying reload...")
ret = lib.pfr_game_load_state(SAVE_PATH.encode())
print(f"Reload returned: {ret}")
if ret == 0:
    s = get_state()
    print(f"After reload: {s}")
    print("SUCCESS!")
else:
    print("FAILED to reload!")
