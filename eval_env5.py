"""Test: run A/B frames to clear scripts after reset."""
import os, sys, struct, numpy as np
sys.path.insert(0, os.path.expanduser("~/pokefirered-native"))
os.chdir(os.path.expanduser("~/pokefirered-native"))
os.environ.pop("PFR_SAVE_PATH", None)
os.environ.pop("PFRN_SAVESTATE_PATH", None)

from pfrn_native import binding

OBS_SIZE = 226

so_path = os.path.join(os.path.expanduser("~/pokefirered-native"), "build", "libpfr_game.so")
tmp_dir = "/tmp/pfrn_eval5"
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
print("After reset: pos=(%d,%d) party=%d" % (px,py,len(party)))


# Now spam A/B to clear any triggered scripts
print("Spamming A/B to clear scripts...")
for step in range(500):
    if step % 2 == 0:
        actions[0] = 5  # A
    else:
        actions[0] = 6  # B
    binding.vec_step(c_envs)
    px,py,mg,mn,d,ib,party,badges,money = parse_obs(obs[0])
    if step < 10 or step % 50 == 0:
        print("  clear_step=%d pos=(%d,%d) map=%d.%d party=%d battle=%d" % (
            step, px, py, mg, mn, len(party), ib))

print("\nAfter clearing: pos=(%d,%d) map=%d.%d party=%d money=%d badges=%d" % (
    px,py,mg,mn,len(party),money,badges))

# Now try walking
print("\n--- Walk DOWN 15 ---")
for step in range(15):
    actions[0] = 2
    binding.vec_step(c_envs)
    px,py,mg,mn,d,ib,party,badges,money = parse_obs(obs[0])
    print("  step=%d DOWN pos=(%d,%d) map=%d.%d party=%d" % (
        step, px, py, mg, mn, len(party)))

# Walk RIGHT 10
print("\n--- Walk RIGHT 10 ---")
for step in range(10):
    actions[0] = 4
    binding.vec_step(c_envs)
    px,py,mg,mn,d,ib,party,badges,money = parse_obs(obs[0])
    print("  step=%d RIGHT pos=(%d,%d) map=%d.%d" % (step, px, py, mg, mn))

binding.vec_close(c_envs)
binding.destroy_instances()
print("\nDone.")
