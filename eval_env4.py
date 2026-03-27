"""Test with post_oak savestate - should start with pokemon and be able to move."""
import os, sys, struct, numpy as np
sys.path.insert(0, os.path.expanduser("~/pokefirered-native"))
os.chdir(os.path.expanduser("~/pokefirered-native"))

# Use the post_oak save
os.environ["PFR_SAVE_PATH"] = os.path.expanduser("~/pokefirered-native/post_oak.sav")
os.environ.pop("PFRN_SAVESTATE_PATH", None)

from pfrn_native import binding

OBS_SIZE = 226

print("Creating 1 SO instance...")
so_path = os.path.join(os.path.expanduser("~/pokefirered-native"), "build", "libpfr_game.so")
tmp_dir = "/tmp/pfrn_eval_postoak"
os.makedirs(tmp_dir, exist_ok=True)
binding.init_instances(so_path, tmp_dir, 1)

obs = np.zeros((1, OBS_SIZE), dtype=np.uint8)
actions = np.zeros(1, dtype=np.int32)
rewards = np.zeros(1, dtype=np.float32)
terminals = np.zeros(1, dtype=np.uint8)
truncations = np.zeros(1, dtype=np.uint8)

c_envs = binding.vec_init(obs, actions, rewards, terminals, truncations, 1, 0,
                           frames_per_step=4, max_steps=24576, savestate_path="")

binding.vec_reset(c_envs, 0)
# Verify post_oak save provides starter pokemon

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
for sp,lv,hp in party:
    print("  pokemon: species=%d lv=%d hp%%=%d" % (sp, lv, hp))

print("\n--- Walk DOWN 20 times ---")
for step in range(20):
    actions[0] = 2  # DOWN
    binding.vec_step(c_envs)
    px,py,mg,mn,d,ib,party,badges,money = parse_obs(obs[0])
    print("  step=%d DOWN pos=(%d,%d) map=%d.%d dir=%d rew=%.4f party=%d battle=%d" % (
        step, px, py, mg, mn, d, float(rewards[0]), len(party), ib))

print("\n--- Walk LEFT 10 ---")
for step in range(10):
    actions[0] = 3
    binding.vec_step(c_envs)
    px,py,mg,mn,d,ib,party,badges,money = parse_obs(obs[0])
    print("  step=%d LEFT pos=(%d,%d) map=%d.%d" % (step, px, py, mg, mn))

print("\n--- Walk DOWN 10 more (try to leave house) ---")
for step in range(10):
    actions[0] = 2
    binding.vec_step(c_envs)
    px,py,mg,mn,d,ib,party,badges,money = parse_obs(obs[0])
    print("  step=%d DOWN pos=(%d,%d) map=%d.%d" % (step, px, py, mg, mn))

binding.vec_close(c_envs)
binding.destroy_instances()
print("\nDone.")
