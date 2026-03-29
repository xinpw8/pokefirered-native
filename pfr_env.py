"""
pfr_env.py -- Native single-env wrapper for pokefirered-native

The native game runtime lives in a dedicated worker subprocess that exposes
`reset` and `step` over a simple JSON-lines protocol. This keeps the game
fully native while isolating the hosted runtime from the PPO process.
"""

import base64
import json
import os
import subprocess
import sys
import time

import gymnasium
import numpy as np

OBS_SIZE = 226
NUM_ACTIONS = 10
DEFAULT_SO_PATH = os.path.join(os.path.dirname(__file__), "build", "libpfr_game.so")
DEFAULT_WORKER_PATH = os.path.join(os.path.dirname(__file__), "scripts", "pfr_env_worker.py")

# Global map coordinate system
MAP_DATA_PATH = os.path.join(os.path.dirname(__file__), "pfr_map_data.json")
with open(MAP_DATA_PATH, encoding="utf-8") as _f:
    _map_data = json.load(_f)
GLOBAL_MAP_SHAPE = tuple(_map_data["global_map_shape"])
_MAP_REGIONS = {r["id"]: r for r in _map_data["regions"] if r["id"] >= 0}
PAD = 20
PADDED_SHAPE = (GLOBAL_MAP_SHAPE[0] + PAD * 2, GLOBAL_MAP_SHAPE[1] + PAD * 2)


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


class PfrEnv:
    def __init__(
        self,
        num_envs=1,
        render_mode=None,
        buf=None,
        seed=0,
        frames_per_step=4,
        max_steps=24576,
        savestate_path="",
        so_path=None,
    ):
        if buf is not None:
            raise ValueError("the direct native env does not support external buffers")

        self.single_observation_space = gymnasium.spaces.Box(
            low=0, high=255, shape=(OBS_SIZE,), dtype=np.uint8
        )
        self.single_action_space = gymnasium.spaces.Discrete(NUM_ACTIONS)
        self.observation_space = self.single_observation_space
        self.action_space = self.single_action_space
        self.render_mode = render_mode
        self.num_agents = num_envs
        self.agent_ids = np.arange(num_envs)

        self.rewards = np.zeros(num_envs, dtype=np.float32)
        self.terminals = np.zeros(num_envs, dtype=bool)
        self.truncations = np.zeros(num_envs, dtype=bool)
        self.masks = np.ones(num_envs, dtype=bool)
        self.actions = np.zeros(num_envs, dtype=np.int32)

        self._obs_raw = np.zeros((num_envs, OBS_SIZE), dtype=np.uint8)
        self._frames_per_step = frames_per_step or 4
        self._max_steps = max_steps or 24576
        self._seed = seed
        self._closed = False

        self.explore_map = np.zeros(PADDED_SHAPE, dtype=np.float32)
        self._visited_tiles = [set() for _ in range(num_envs)]
        self._prev_badges = np.zeros(num_envs, dtype=np.uint16)
        self._prev_party_count = np.zeros(num_envs, dtype=np.uint16)
        self._prev_party_level_sum = np.zeros(num_envs, dtype=np.uint32)
        self._episode_return = np.zeros(num_envs, dtype=np.float32)
        self._step_count = np.zeros(num_envs, dtype=np.int32)

        if so_path is None:
            so_path = DEFAULT_SO_PATH

        self._worker_cmd = [
            sys.executable,
            "-u",
            DEFAULT_WORKER_PATH,
            "--so-path",
            so_path,
            "--frames-per-step",
            str(self._frames_per_step),
            "--savestate-path",
            savestate_path,
        ]
        self._workers = [None] * num_envs

    def _start_worker(self, idx):
        self._stop_worker(idx)
        self._workers[idx] = subprocess.Popen(
            self._worker_cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            cwd=os.path.dirname(__file__),
        )

    def _stop_worker(self, idx):
        worker = self._workers[idx]
        if worker is None:
            return

        try:
            if worker.stdin is not None and worker.poll() is None:
                worker.stdin.write(json.dumps({"cmd": "close"}) + "\n")
                worker.stdin.flush()
        except Exception:
            pass

        try:
            worker.wait(timeout=1.0)
        except Exception:
            try:
                worker.kill()
            except Exception:
                pass
            try:
                worker.wait(timeout=1.0)
            except Exception:
                pass

        if worker.stdin is not None:
            try:
                worker.stdin.close()
            except Exception:
                pass
        if worker.stdout is not None:
            try:
                worker.stdout.close()
            except Exception:
                pass

        self._workers[idx] = None

    def _send_cmd(self, idx, payload):
        worker = self._workers[idx]
        if worker is None:
            raise RuntimeError(f"native worker {idx} is not running")
        if worker.stdin is None:
            raise RuntimeError(f"native worker {idx} stdin is closed")
        worker.stdin.write(json.dumps(payload) + "\n")
        worker.stdin.flush()

    def _recv_state(self, idx):
        worker = self._workers[idx]
        if worker is None or worker.stdout is None:
            raise RuntimeError(f"native worker {idx} stdout is closed")
        while True:
            line = worker.stdout.readline()
            if not line:
                rc = worker.poll()
                raise EOFError(f"native worker {idx} terminated unexpectedly (rc={rc})")

            line = line.strip()
            if not line or not line.startswith("{"):
                continue

            msg = json.loads(line)
            if msg.get("status") != "ok":
                raise RuntimeError(f"native worker {idx} failed: {msg}")

            obs_bytes = base64.b64decode(msg["obs_b64"])
            self._obs_raw[idx, :] = np.frombuffer(obs_bytes, dtype=np.uint8)
            return msg["info"]

    def _episode_log(self, idx, worker_crash=0.0):
        log = {
            "episode_return": float(self._episode_return[idx]),
            "episode_length": float(self._step_count[idx]),
            "badges": float(int(self._prev_badges[idx]).bit_count()),
            "exploration": float(len(self._visited_tiles[idx])),
            "party_level_sum": float(self._prev_party_level_sum[idx]),
        }
        if worker_crash:
            log["worker_crash"] = worker_crash
        return log

    def _restore_single_episode(self, idx):
        last_error = None
        for attempt in range(20):
            self._start_worker(idx)
            try:
                self._send_cmd(idx, {"cmd": "reset"})
                info = self._recv_state(idx)
                break
            except (EOFError, RuntimeError) as exc:
                last_error = exc
                self._stop_worker(idx)
                time.sleep(0.05 * (attempt + 1))
        else:
            raise RuntimeError(
                f"failed to restore native episode {idx}: {last_error}"
            ) from last_error

        self._visited_tiles[idx] = {
            (info["map_group"], info["map_num"], info["player_x"], info["player_y"])
        }
        self._prev_badges[idx] = info["badges"]
        self._prev_party_count[idx] = info["party_count"]
        self._prev_party_level_sum[idx] = info["party_level_sum"]
        self._episode_return[idx] = 0.0
        self._step_count[idx] = 0
        self.rewards[idx] = 0.0
        self.terminals[idx] = False
        self.truncations[idx] = False
        self._update_explore_map([idx])

    def _restore_episode(self, indices=None):
        if indices is None:
            indices = range(self.num_agents)

        for idx in indices:
            self._restore_single_episode(idx)

        return self._obs_raw.copy(), [{} for _ in range(self.num_agents)]

    def reset(self, seed=0):
        del seed
        return self._restore_episode()

    def _compute_reward(self, idx, info):
        reward = 0.0

        tile = (info["map_group"], info["map_num"], info["player_x"], info["player_y"])
        if tile not in self._visited_tiles[idx]:
            self._visited_tiles[idx].add(tile)
            reward += 0.02

        new_badges = info["badges"] & ~int(self._prev_badges[idx])
        if new_badges:
            reward += 10.0 * int(new_badges.bit_count())
            self._prev_badges[idx] = info["badges"]

        if info["party_level_sum"] > self._prev_party_level_sum[idx]:
            reward += 0.1 * (info["party_level_sum"] - self._prev_party_level_sum[idx])
            self._prev_party_level_sum[idx] = info["party_level_sum"]

        if info["party_count"] > self._prev_party_count[idx]:
            reward += 1.0 * (info["party_count"] - self._prev_party_count[idx])
            self._prev_party_count[idx] = info["party_count"]

        return reward

    def step(self, actions):
        action_array = np.asarray(actions, dtype=np.int32).reshape(self.num_agents)
        info_list = [{} for _ in range(self.num_agents)]
        self.rewards.fill(0.0)
        self.terminals.fill(False)
        self.truncations.fill(False)

        failed = set()
        for idx, action in enumerate(action_array):
            try:
                self._send_cmd(idx, {"cmd": "step", "action": int(action)})
            except (EOFError, RuntimeError):
                failed.add(idx)

        infos = [None] * self.num_agents
        for idx in range(self.num_agents):
            if idx in failed:
                continue
            try:
                infos[idx] = self._recv_state(idx)
            except (EOFError, RuntimeError):
                failed.add(idx)

        for idx in range(self.num_agents):
            if idx in failed:
                info_list[idx] = self._episode_log(idx, worker_crash=1.0)
                self._restore_single_episode(idx)
                self.terminals[idx] = True
                continue

            info = infos[idx]
            reward = self._compute_reward(idx, info)
            self.rewards[idx] = reward
            self._episode_return[idx] += reward
            self._step_count[idx] += 1
            self._update_explore_map([idx])

            terminal = self._step_count[idx] >= self._max_steps
            self.terminals[idx] = terminal
            if terminal:
                info_list[idx] = self._episode_log(idx)
                terminal_reward = float(self.rewards[idx])
                self._restore_single_episode(idx)
                self.rewards[idx] = terminal_reward
                self.terminals[idx] = True

        return (
            self._obs_raw.copy(),
            self.rewards.copy(),
            self.terminals.copy(),
            self.truncations.copy(),
            info_list,
        )

    def _update_explore_map(self, indices=None):
        if indices is None:
            indices = range(self.num_agents)
        for idx in indices:
            obs = self._obs_raw[idx]
            px = int(np.int16(obs[0] | (obs[1] << 8)))
            py = int(np.int16(obs[2] | (obs[3] << 8)))
            mg = int(obs[4])
            mn = int(obs[5])
            gy, gx = local_to_global(py, px, mg, mn)
            if gy >= 0 and gx >= 0:
                self.explore_map[gy, gx] += 1.0

    def render(self):
        return None

    def close(self):
        if self._closed:
            return None
        self._closed = True
        for idx in range(self.num_agents):
            self._stop_worker(idx)
        self.actions = None
        self.rewards = None
        self.terminals = None
        self.truncations = None
        self.masks = None
        self.agent_ids = None
        self.explore_map = None
        self._obs_raw = None
        return None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass
