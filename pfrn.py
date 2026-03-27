"""
pfrn.py -- SO-copy native PufferEnv for pokefirered-native

Uses one `libpfr_game.so` instance per environment via `dlopen` on copied
shared objects, so each environment has truly independent game globals.
"""

import json
import os

import gymnasium
import numpy as np

import pufferlib

# The compiled C extension -- built from pfrn_binding.h + pfrn_env.h
# via a setup.py or CMake that produces `binding.so` in this package dir.
from pfrn_native import binding

OBS_SIZE = 226
NUM_ACTIONS = 8

# Default paths
DEFAULT_SO_PATH = os.path.join(os.path.dirname(__file__), "build", "libpfr_game.so")
DEFAULT_TMP_DIR = "/tmp/pfrn_instances"
DEFAULT_SAV_PATH = os.path.join(os.path.dirname(__file__), "pokefirered.sav")

MAP_DATA_PATH = os.path.join(os.path.dirname(__file__), "pfr_map_data.json")
with open(MAP_DATA_PATH, encoding="utf-8") as _f:
    _map_data = json.load(_f)

GLOBAL_MAP_SHAPE = tuple(_map_data["global_map_shape"])
_MAP_REGIONS = {r["id"]: r for r in _map_data["regions"] if r["id"] >= 0}
PAD = 20
PADDED_SHAPE = (GLOBAL_MAP_SHAPE[0] + PAD * 2, GLOBAL_MAP_SHAPE[1] + PAD * 2)


def _default_state_path():
    override = os.environ.get("PFRN_SAVESTATE_PATH", "")
    if override:
        return override
    return ""


def local_to_global(y, x, map_group, map_num):
    map_id = map_group * 256 + map_num
    region = _MAP_REGIONS.get(map_id)
    if region is None:
        return -1, -1
    gx = x + region["coordinates"][0] + PAD
    gy = y + region["coordinates"][1] + PAD
    if 0 <= gy < PADDED_SHAPE[0] and 0 <= gx < PADDED_SHAPE[1]:
        return gy, gx
    return -1, -1


class PFRN(pufferlib.PufferEnv):
    """PufferLib ocean env for Pokemon FireRed (native C)."""

    def __init__(
        self,
        num_envs=1,
        frames_per_step=4,
        max_steps=24576,
        savestate_path=None,
        sav_path=None,
        so_path=None,
        tmp_dir=None,
        log_interval=128,
        render_mode=None,
        buf=None,
        seed=0,
    ):
        if savestate_path is None:
            savestate_path = _default_state_path()
        if so_path is None:
            so_path = DEFAULT_SO_PATH
        if tmp_dir is None:
            tmp_dir = DEFAULT_TMP_DIR
        os.makedirs(tmp_dir, exist_ok=True)

        self.log_interval = log_interval
        self.render_mode = render_mode
        self.num_agents = num_envs
        self._num_envs = num_envs
        self._frames_per_step = frames_per_step
        self._max_steps = max_steps
        self._savestate_path = savestate_path if savestate_path else ""

        self.single_observation_space = gymnasium.spaces.Box(
            low=0, high=255, shape=(OBS_SIZE,), dtype=np.uint8
        )
        self.single_action_space = gymnasium.spaces.Discrete(NUM_ACTIONS)

        super().__init__(buf)

        # Set PFR_SAVE_PATH env var for game boot (read by pfr_game_boot via getenv)
        # Default: no save (start from beginning). Set sav_path or PFR_SAVE_PATH to load a save.
        if sav_path is None:
            sav_path = os.environ.get("PFR_SAVE_PATH", "")
        if sav_path:
            os.environ["PFR_SAVE_PATH"] = sav_path
        else:
            os.environ.pop("PFR_SAVE_PATH", None)

        # 1. Create SO-copy game instances (one per env)
        binding.init_instances(so_path, tmp_dir, num_envs)

        # 2. Create vectorized C envs via env_binding.h vec_init
        #    vec_init(obs, actions, rewards, terminals, truncations,
        #             num_envs, seed, **kwargs)
        #    kwargs are passed to my_init() for each env
        self.c_envs = binding.vec_init(
            self.observations,
            self.actions,
            self.rewards,
            self.terminals,
            self.truncations,
            num_envs,
            seed,
            frames_per_step=frames_per_step,
            max_steps=max_steps,
            savestate_path=savestate_path,
        )

        self.tick = 0
        self.explore_map = np.zeros(PADDED_SHAPE, dtype=np.float32)

    def reset(self, seed=0):
        self.rewards.fill(0)
        binding.vec_reset(self.c_envs, seed)
        self.explore_map.fill(0)
        self._update_explore_map()
        info = [{"pokemon_exploration_map": self.explore_map}]
        return self.observations.copy(), info

    def step(self, actions):
        self.rewards.fill(0)
        self.actions[:] = actions
        binding.vec_step(self.c_envs)
        self._update_explore_map()

        log = binding.vec_log(self.c_envs) if self.tick % self.log_interval == 0 else {}
        info = [log]
        info[0]["pokemon_exploration_map"] = self.explore_map
        self.tick += 1

        return (
            self.observations.copy(),
            self.rewards.copy(),
            self.terminals.copy(),
            self.truncations.copy(),
            info,
        )

    def _update_explore_map(self):
        for i in range(self.num_agents):
            obs = self.observations[i]
            px = int(np.int16(obs[0] | (obs[1] << 8)))
            py = int(np.int16(obs[2] | (obs[3] << 8)))
            mg = int(obs[4])
            mn = int(obs[5])
            gy, gx = local_to_global(py, px, mg, mn)
            if gy >= 0 and gx >= 0:
                self.explore_map[gy, gx] += 1.0

    def render(self):
        binding.vec_render(self.c_envs, 0)

    def close(self):
        binding.vec_close(self.c_envs)
        binding.destroy_instances()


def test_performance(timeout=10, num_envs=1, atn_cache=1024):
    """Quick SPS benchmark."""
    import time

    env = PFRN(num_envs=num_envs)
    env.reset()
    tick = 0
    actions = np.random.randint(0, NUM_ACTIONS, (atn_cache, env.num_agents))

    start = time.time()
    while time.time() - start < timeout:
        atn = actions[tick % atn_cache]
        env.step(atn)
        tick += 1

    elapsed = time.time() - start
    sps = env.num_agents * tick / elapsed
    print(f"PFRN: {num_envs} envs, {tick} steps, {sps:.0f} SPS")
    env.close()


if __name__ == "__main__":
    test_performance()
