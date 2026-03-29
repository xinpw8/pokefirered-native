"""Test multi-env to check for the segfault."""
import os, sys, struct, numpy as np
sys.path.insert(0, os.path.expanduser("~/pokefirered-native"))
os.chdir(os.path.expanduser("~/pokefirered-native"))
os.environ.pop("PFR_SAVE_PATH", None)
os.environ.pop("PFRN_SAVESTATE_PATH", None)

from pfrn import PFRN

NUM_ENVS = 4

env = PFRN(num_envs=NUM_ENVS, frames_per_step=4, max_steps=24576)
print("Created %d envs" % NUM_ENVS)

obs, info = env.reset()
print("Reset OK, obs shape=%s" % str(obs.shape))

rng = np.random.default_rng(42)
for step in range(100):
    actions = rng.integers(0, 10, NUM_ENVS).astype(np.int32)
    obs, rewards, terms, truncs, infos = env.step(actions)
    if step % 20 == 0:
        print("step=%d rewards=%s" % (step, str(rewards)))

print("100 steps done, explore tiles=%d" % int((env.explore_map > 0).sum()))
env.close()
print("PASS: multi-env works without segfault")
