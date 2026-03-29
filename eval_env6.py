"""Test: after vec_reset, what happens if we just press B repeatedly to dismiss dialogs?"""
import os, sys, struct, numpy as np
sys.path.insert(0, os.path.expanduser("~/pokefirered-native"))
os.chdir(os.path.expanduser("~/pokefirered-native"))
os.environ.pop("PFR_SAVE_PATH", None)
os.environ.pop("PFRN_SAVESTATE_PATH", None)

from pfrn import PFRN

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
    return px, py, mg, mn, direction, in_battle, party

env = PFRN(num_envs=1, frames_per_step=4, max_steps=24576)
obs, info = env.reset()
px,py,mg,mn,d,ib,party = parse_obs(obs[0])
print("After reset: pos=(%d,%d) party=%d" % (px,py,len(party)))

# Spam B to dismiss any dialog
print("\n--- Spam B 60 times to dismiss dialogs ---")
for step in range(60):
    obs, rewards, terms, truncs, infos = env.step(np.array([6], dtype=np.int32))  # B
    px,py,mg,mn,d,ib,party = parse_obs(obs[0])
    if step < 5 or step % 10 == 0:
        print("  step=%d B pos=(%d,%d) dir=%d" % (step, px, py, d))

# Now try DOWN
print("\n--- Walk DOWN 15 ---")
for step in range(15):
    obs, rewards, terms, truncs, infos = env.step(np.array([2], dtype=np.int32))
    px,py,mg,mn,d,ib,party = parse_obs(obs[0])
    print("  step=%d DOWN pos=(%d,%d) map=%d.%d rew=%.4f" % (
        step, px, py, mg, mn, float(rewards[0])))

env.close()
print("Done.")
