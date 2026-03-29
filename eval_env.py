"""Run env exactly as training does - step with random actions, log everything."""
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
    npcs = []
    for i in range(15):
        base = 55 + i * 6
        active = int(o[base+4])
        if active:
            dx = struct.unpack_from("b", o, base)[0]
            dy = struct.unpack_from("b", o, base+1)[0]
            npcs.append((dx, dy, int(o[base+2])))
    tiles = np.frombuffer(o[145:226], dtype=np.uint8).reshape(9, 9)
    return px, py, mg, mn, direction, in_battle, party, badges, money, npcs, tiles

print("Creating env (1 env, 4 frames/step)...")
env = PFRN(num_envs=1, frames_per_step=4, max_steps=24576)

print("Resetting...")
obs, info = env.reset()
px,py,mg,mn,d,ib,party,badges,money,npcs,tiles = parse_obs(obs[0])
print("=== INITIAL STATE ===")
print("  pos=(%d,%d) map=%d.%d dir=%d battle=%d" % (px,py,mg,mn,d,ib))
print("  party=%d badges=%d money=%d" % (len(party), badges, money))
for sp,lv,hp in party:
    print("    species=%d lv=%d hp%%=%d" % (sp, lv, hp))
print("  npcs=%d: %s" % (len(npcs), str(npcs[:5])))
print("  tiles (9x9):")
for row in tiles:
    print("    " + " ".join("%3d" % v for v in row))

total_rew = 0.0
positions = set()
positions.add((px,py,mg,mn))
rng = np.random.default_rng(42)

print("\n=== STEPPING 200 times ===")
for step in range(200):
    r = rng.random()
    if r < 0.70:
        action = rng.integers(1, 5)
    elif r < 0.85:
        action = 5
    elif r < 0.95:
        action = 0
    else:
        action = rng.integers(6, 10)

    obs, rewards, terms, truncs, infos = env.step(np.array([action], dtype=np.int32))
    reward = float(rewards[0])
    total_rew += reward
    done = bool(terms[0]) or bool(truncs[0])
    px,py,mg,mn,d,ib,party,badges,money,npcs,tiles = parse_obs(obs[0])
    positions.add((px,py,mg,mn))

    if step < 30 or step % 10 == 0 or abs(reward) > 0.001 or done:
        print("step=%4d act=%-6s rew=%+.4f pos=(%d,%d) map=%d.%d battle=%d party=%d badges=%d money=%d unique=%d" % (
            step, ACTION_NAMES[action], reward, px, py, mg, mn, ib, len(party), badges, money, len(positions)))

    if done:
        print(">>> EPISODE DONE step=%d total=%.4f" % (step, total_rew))
        obs, info = env.reset()
        px,py,mg,mn,d,ib,party,badges,money,npcs,tiles = parse_obs(obs[0])
        total_rew = 0.0
        positions.clear()
        positions.add((px,py,mg,mn))
        print(">>> RESET pos=(%d,%d) map=%d.%d party=%d" % (px,py,mg,mn,len(party)))

print("\n=== SUMMARY ===")
print("total_rew=%.4f unique_positions=%d explore_tiles=%d" % (
    total_rew, len(positions), int((env.explore_map > 0).sum())))
env.close()
print("Done.")
