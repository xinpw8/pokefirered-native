import numpy as np

from pfr_env import PfrEnv


def main():
    env = PfrEnv(num_envs=1, frames_per_step=4, max_steps=8, savestate_path="")
    obs, _ = env.reset(seed=123)
    print("reset", obs[0, :8].tolist(), flush=True)
    for i in range(4):
        obs, rew, term, trunc, _ = env.step(np.array([4], dtype=np.int32))
        print(
            "step",
            i,
            float(rew[0]),
            int(term[0]),
            int(trunc[0]),
            obs[0, :8].tolist(),
            flush=True,
        )
    print("explored", int((env.explore_map > 0).sum()), flush=True)
    env.close()
    print("closed", flush=True)


if __name__ == "__main__":
    main()
