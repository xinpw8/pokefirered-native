"""Full eval: navigate from bedroom to outside, verify everything works."""
import os, sys, struct, numpy as np
sys.path.insert(0, os.path.expanduser("~/pokefirered-native"))
os.chdir(os.path.expanduser("~/pokefirered-native"))
os.environ.pop("PFR_SAVE_PATH", None)
os.environ.pop("PFRN_SAVESTATE_PATH", None)

from pfrn import PFRN

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

env = PFRN(num_envs=1, frames_per_step=4, max_steps=24576)
obs, info = env.reset()
px,py,mg,mn,d,ib,party,badges,money = parse_obs(obs[0])
print("INIT: pos=(%d,%d) map=%d.%d party=%d money=%d" % (px,py,mg,mn,len(party),money))

def step(action_id):
    obs, rewards, terms, truncs, infos = env.step(np.array([action_id], dtype=np.int32))
    px,py,mg,mn,d,ib,party,badges,money = parse_obs(obs[0])
    return px, py, mg, mn, d, ib, party, badges, money, float(rewards[0]), bool(terms[0]) or bool(truncs[0])

# Navigate: bedroom (6,6) -> stairs down -> leave house -> outside
# Bedroom layout: starts at (6,6), door/stairs at bottom

# Step 1: Walk down to exit bedroom
print("\n=== Walk down to leave bedroom ===")
for i in range(20):
    px,py,mg,mn,d,ib,party,badges,money,rew,done = step(2)  # DOWN
    print("  DOWN pos=(%d,%d) map=%d.%d rew=%.4f" % (px,py,mg,mn,rew))
    if mg != 4 or mn != 1:
        print("  ** MAP CHANGED! Left bedroom!")
        break

# Step 2: Continue walking down/left to reach front door  
print("\n=== Navigate to front door ===")
for i in range(10):
    px,py,mg,mn,d,ib,party,badges,money,rew,done = step(2)  # DOWN
    print("  DOWN pos=(%d,%d) map=%d.%d rew=%.4f" % (px,py,mg,mn,rew))

for i in range(5):
    px,py,mg,mn,d,ib,party,badges,money,rew,done = step(3)  # LEFT
    print("  LEFT pos=(%d,%d) map=%d.%d rew=%.4f" % (px,py,mg,mn,rew))

for i in range(10):
    px,py,mg,mn,d,ib,party,badges,money,rew,done = step(2)  # DOWN
    print("  DOWN pos=(%d,%d) map=%d.%d rew=%.4f" % (px,py,mg,mn,rew))

# Step 3: Walk around outside
print("\n=== Walk around outside (should be Pallet Town) ===")
for act, name, count in [(2, "DOWN", 20), (4, "RIGHT", 10), (1, "UP", 10), (3, "LEFT", 10)]:
    for i in range(count):
        px,py,mg,mn,d,ib,party,badges,money,rew,done = step(act)
        if i % 3 == 0 or rew > 0:
            print("  %s pos=(%d,%d) map=%d.%d rew=%.4f party=%d battle=%d" % (
                name, px, py, mg, mn, rew, len(party), ib))

print("\nExplore map tiles: %d" % int((env.explore_map > 0).sum()))
env.close()
print("Done.")
