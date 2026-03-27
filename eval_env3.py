"""Test env stepping to verify walkability."""
import os, sys, struct, numpy as np
sys.path.insert(0, os.path.expanduser("~/pokefirered-native"))
os.chdir(os.path.expanduser("~/pokefirered-native"))
os.environ.pop("PFR_SAVE_PATH", None)
os.environ.pop("PFRN_SAVESTATE_PATH", None)

import gymnasium
import pufferlib
from pfrn_native import binding

OBS_SIZE = 226
NUM_ACTIONS = 8

# Directly use the binding without PFRN class
print("Creating 1 SO instance...")
so_path = os.path.join(os.path.expanduser("~/pokefirered-native"), "build", "libpfr_game.so")
tmp_dir = "/tmp/pfrn_eval_test"
os.makedirs(tmp_dir, exist_ok=True)
binding.init_instances(so_path, tmp_dir, 1)

# Allocate buffers
obs = np.zeros((1, OBS_SIZE), dtype=np.uint8)
actions = np.zeros(1, dtype=np.int32)
rewards = np.zeros(1, dtype=np.float32)
terminals = np.zeros(1, dtype=np.uint8)
truncations = np.zeros(1, dtype=np.uint8)

# Init
c_envs = binding.vec_init(obs, actions, rewards, terminals, truncations, 1, 0,
                           frames_per_step=4, max_steps=24576, savestate_path="")
print("vec_init done")

# Reset env
binding.vec_reset(c_envs, 0)
print("vec_reset done")

ACTION_NAMES = ["none","up","down","left","right","A","B","start","select","LR"]

def parse_obs(o):
    px = struct.unpack_from("<h", o, 0)[0]
    py = struct.unpack_from("<h", o, 2)[0]
    mg, mn = int(o[4]), int(o[5])
    direction = int(o[7])
    in_battle = int(o[11])
    party = []
    for i in range(6):
        base = 13 + i * 6
        sp = int(o[base]) | (int(o[base+1]) << 8)
        if sp > 0:
            party.append((sp, int(o[base+2]), int(o[base+3])))
    badges = int(o[49])
    money = int(o[50]) | (int(o[51]) << 8)
    return px, py, mg, mn, direction, in_battle, party, badges, money

px,py,mg,mn,d,ib,party,badges,money = parse_obs(obs[0])
print("INIT: pos=(%d,%d) map=%d.%d dir=%d party=%d badges=%d money=%d" % (px,py,mg,mn,d,len(party),badges,money))

# Walk DOWN 15 times
print("\n--- Walk DOWN (walk test) ---")
for step in range(15):
    actions[0] = 2  # DOWN
    binding.vec_step(c_envs)
    px,py,mg,mn,d,ib,party,badges,money = parse_obs(obs[0])
    print("  step=%d DOWN pos=(%d,%d) map=%d.%d dir=%d rew=%.4f party=%d" % (
        step, px, py, mg, mn, d, float(rewards[0]), len(party)))

# Walk LEFT 5 times
print("\n--- Walk LEFT ---")
for step in range(5):
    actions[0] = 3  # LEFT
    binding.vec_step(c_envs)
    px,py,mg,mn,d,ib,party,badges,money = parse_obs(obs[0])
    print("  step=%d LEFT pos=(%d,%d) map=%d.%d dir=%d" % (
        step, px, py, mg, mn, d))

binding.vec_close(c_envs)
binding.destroy_instances()
print("\nDone.")
