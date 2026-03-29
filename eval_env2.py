"""Detailed env eval - check what happens with focused action sequences."""
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
print("INIT: pos=(%d,%d) map=%d.%d dir=%d party=%d badges=%d money=%d" % (px,py,mg,mn,d,len(party),badges,money))
for sp,lv,hp in party:
    print("  pokemon: species=%d lv=%d hp=%d" % (sp, lv, hp))

# Test 1: Spam A button to clear any dialog
print("\n--- TEST 1: Spam A button 50 times ---")
for step in range(50):
    obs, rewards, terms, truncs, infos = env.step(np.array([5], dtype=np.int32))  # A button
    reward = float(rewards[0])
    px,py,mg,mn,d,ib,party,badges,money = parse_obs(obs[0])
    if reward != 0.0 or step < 5 or step % 10 == 0:
        print("  step=%d A rew=%+.4f pos=(%d,%d) map=%d.%d party=%d battle=%d" % (
            step, reward, px, py, mg, mn, len(party), ib))

# Test 2: Now try to walk down
print("\n--- TEST 2: Walk DOWN 30 times ---")
for step in range(30):
    obs, rewards, terms, truncs, infos = env.step(np.array([2], dtype=np.int32))  # DOWN
    reward = float(rewards[0])
    px,py,mg,mn,d,ib,party,badges,money = parse_obs(obs[0])
    print("  step=%d DOWN rew=%+.4f pos=(%d,%d) map=%d.%d party=%d" % (
        step, reward, px, py, mg, mn, len(party)))

# Test 3: Walk right
print("\n--- TEST 3: Walk RIGHT 10 times ---")
for step in range(10):
    obs, rewards, terms, truncs, infos = env.step(np.array([4], dtype=np.int32))  # RIGHT
    reward = float(rewards[0])
    px,py,mg,mn,d,ib,party,badges,money = parse_obs(obs[0])
    print("  step=%d RIGHT rew=%+.4f pos=(%d,%d) map=%d.%d" % (
        step, reward, px, py, mg, mn))

# Test 4: Walk up and try to leave house
print("\n--- TEST 4: Walk LEFT then DOWN to exit bedroom ---")
for act_name, act_id, count in [("LEFT", 3, 5), ("DOWN", 2, 15), ("DOWN", 2, 5)]:
    for step in range(count):
        obs, rewards, terms, truncs, infos = env.step(np.array([act_id], dtype=np.int32))
        reward = float(rewards[0])
        px,py,mg,mn,d,ib,party,badges,money = parse_obs(obs[0])
        print("  %s rew=%+.4f pos=(%d,%d) map=%d.%d" % (
            act_name, reward, px, py, mg, mn))

env.close()
print("\nDone.")
