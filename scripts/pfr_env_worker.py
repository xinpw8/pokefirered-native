import argparse
import base64
import ctypes as ct
import json
import os
import sys

import numpy as np

OBS_SIZE = 226
NUM_ACTIONS = 10

BTN_A = 1 << 0
BTN_B = 1 << 1
BTN_SELECT = 1 << 2
BTN_START = 1 << 3
BTN_RIGHT = 1 << 4
BTN_LEFT = 1 << 5
BTN_UP = 1 << 6
BTN_DOWN = 1 << 7
BTN_R = 1 << 8
BTN_L = 1 << 9

ACTION_TO_BUTTONS = np.array(
    [
        0,
        BTN_UP,
        BTN_DOWN,
        BTN_LEFT,
        BTN_RIGHT,
        BTN_A,
        BTN_B,
        BTN_START,
        BTN_SELECT,
        BTN_L | BTN_R,
    ],
    dtype=np.uint16,
)


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


def load_lib(so_path):
    lib = ct.CDLL(os.path.abspath(so_path))
    lib.pfr_game_boot.argtypes = []
    lib.pfr_game_boot.restype = None
    lib.pfr_game_load_state.argtypes = [ct.c_char_p]
    lib.pfr_game_load_state.restype = ct.c_int
    lib.pfr_game_restore_hot.argtypes = []
    lib.pfr_game_restore_hot.restype = None
    lib.pfr_game_step_frames_fast.argtypes = [ct.c_uint16, ct.c_int]
    lib.pfr_game_step_frames_fast.restype = None
    lib.pfr_game_extract_obs.argtypes = [ct.POINTER(ct.c_ubyte)]
    lib.pfr_game_extract_obs.restype = None
    lib.pfr_game_get_reward_info.argtypes = [ct.POINTER(RewardInfo)]
    lib.pfr_game_get_reward_info.restype = None
    return lib


def reward_info_to_dict(raw):
    return {
        "player_x": int(raw.player_x),
        "player_y": int(raw.player_y),
        "map_group": int(raw.map_group),
        "map_num": int(raw.map_num),
        "badges": int(raw.badges),
        "party_count": int(raw.party_count),
        "party_level_sum": int(raw.party_level_sum),
        "money": int(raw.money),
        "in_battle": int(raw.in_battle),
    }


def emit_state(obs, info):
    payload = {
        "status": "ok",
        "obs_b64": base64.b64encode(obs.tobytes()).decode("ascii"),
        "info": info,
    }
    print(json.dumps(payload), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--so-path", required=True)
    parser.add_argument("--frames-per-step", type=int, required=True)
    parser.add_argument("--savestate-path", default="")
    args = parser.parse_args()

    lib = load_lib(args.so_path)
    lib.pfr_game_boot()

    obs = np.zeros(OBS_SIZE, dtype=np.uint8)
    obs_ptr = obs.ctypes.data_as(ct.POINTER(ct.c_ubyte))
    savestate_bytes = os.fsencode(args.savestate_path) if args.savestate_path else None

    while True:
        line = sys.stdin.readline()
        if not line:
            break
        msg = json.loads(line)
        cmd = msg["cmd"]

        if cmd == "close":
            break

        if cmd == "reset":
            if savestate_bytes:
                if lib.pfr_game_load_state(savestate_bytes) != 0:
                    lib.pfr_game_restore_hot()
            else:
                lib.pfr_game_restore_hot()
        elif cmd == "step":
            action = int(msg["action"])
            if action < 0 or action >= NUM_ACTIONS:
                action = 0
            buttons = int(ACTION_TO_BUTTONS[action])
            lib.pfr_game_step_frames_fast(buttons, args.frames_per_step)
        else:
            print(json.dumps({"status": "error", "error": f"unknown command: {cmd}"}), flush=True)
            continue

        lib.pfr_game_extract_obs(obs_ptr)
        reward_info = RewardInfo()
        lib.pfr_game_get_reward_info(ct.byref(reward_info))
        emit_state(obs, reward_info_to_dict(reward_info))


if __name__ == "__main__":
    try:
        main()
    except BaseException as exc:
        print(json.dumps({"status": "error", "error": repr(exc)}), flush=True)
        raise
