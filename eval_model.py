#!/usr/bin/env python3
"""
eval_model.py — Evaluate a trained PFRN policy checkpoint

Loads a trained model from model_latest.pt (or a CLI-specified path),
runs a single environment for N steps using the learned policy,
captures FPV frames as an animated GIF, tracks exploration metrics,
and writes live stats + trajectory GIF to /tmp/pfr_dashboard/.

Usage:
    source /home/spark-advantage/pufferlib-4.0/.venv/bin/activate
    cd /home/spark-advantage/pokefirered-native
    python3 eval_model.py model_latest.pt
    python3 eval_model.py --checkpoint model_latest.pt --steps 16384 --temperature 0.5
"""

import argparse
import json
import os
import struct
import sys
import time

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pfr_policy import PfrPolicy, OBS_SIZE, NUM_ACTIONS

DASH_DIR = "/tmp/pfr_dashboard"
GBA_W, GBA_H = 240, 160
FPV_CAPTURE_INTERVAL = 16   # capture a frame every N steps
FPV_SCALE = 3               # upscale factor for GBA frames
MAX_FPV_FRAMES = 512        # max frames to keep for GIF


# ── Helpers ──────────────────────────────────────────────────────────────────

def write_stats_json(path, data):
    """Atomic write of stats.json."""
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f, indent=2)
    os.replace(tmp, path)


def save_gif(path, frames, duration_ms=150, loop=0):
    """Save list of PIL Images as animated GIF."""
    if not frames or len(frames) < 2:
        return
    from PIL import Image
    tmp = path + ".tmp.gif"
    frames[0].save(
        tmp, save_all=True, append_images=frames[1:],
        duration=duration_ms, loop=loop,
    )
    os.replace(tmp, path)


def obs_to_info(obs_bytes):
    """Extract game state from a single 226-byte observation."""
    o = obs_bytes
    px = struct.unpack_from('<h', o, 0)[0]
    py = struct.unpack_from('<h', o, 2)[0]
    mg, mn = int(o[4]), int(o[5])
    direction = int(o[7])
    in_battle = int(o[11])
    party_count = 0
    party_level_sum = 0
    party_levels = []
    party_species = []
    for i in range(6):
        base = 13 + i * 6
        sp = int(o[base]) | (int(o[base + 1]) << 8)
        if sp > 0:
            party_count += 1
            lv = int(o[base + 2])
            party_level_sum += lv
            party_levels.append(lv)
            party_species.append(sp)
    badges = int(o[49])
    money = int(o[50]) | (int(o[51]) << 8)
    step_counter = int(o[53]) | (int(o[54]) << 8)
    return {
        "x": px, "y": py,
        "map_group": mg, "map_num": mn,
        "map_id": f"{mg}.{mn}",
        "direction": direction,
        "in_battle": in_battle,
        "party_count": party_count,
        "party_level_sum": party_level_sum,
        "party_levels": party_levels,
        "party_species": party_species,
        "badges": badges,
        "badges_earned": bin(badges).count('1'),
        "money": money,
        "step_counter": step_counter,
    }


def capture_fpv_frame(binding, env_index=0):
    """Capture the GBA screen as an RGB numpy array (H, W, 3)."""
    if binding is None:
        return None
    try:
        frame_argb = binding.capture_frame(env_index)
        # Frame is (160, 240, 4) with channels B, G, R, A
        frame_rgb = frame_argb[:, :, 2::-1]  # BGR -> RGB
        return frame_rgb.copy()
    except Exception as e:
        print(f"[WARN] FPV capture failed: {e}", flush=True)
        return None


def make_env(backend="parallel"):
    """Create a single-env PFRN environment."""
    if backend == "parallel":
        from pfrn import PFRN
        env = PFRN(num_envs=1)
    else:
        from pfr_env import PfrEnv
        env = PfrEnv(num_envs=1)
    return env


def load_checkpoint(ckpt_path, device):
    """Load model checkpoint and return (state_dict, metadata)."""
    print(f"[EVAL] Loading checkpoint: {ckpt_path}")
    ckpt = torch.load(ckpt_path, map_location=device, weights_only=False)
    print(f"[EVAL] Checkpoint keys: {list(ckpt.keys())}")
    if "global_step" in ckpt:
        print(f"[EVAL] Trained for {ckpt['global_step']:,} steps")
    if "explored_tiles" in ckpt:
        print(f"[EVAL] Training explored {ckpt['explored_tiles']:,} tiles")
    if "args" in ckpt:
        train_args = ckpt["args"]
        print(f"[EVAL] Training config: backend={train_args.get('backend', '?')}, "
              f"num_envs={train_args.get('num_envs', '?')}, "
              f"lr={train_args.get('lr', '?')}")
    return ckpt


def main():
    parser = argparse.ArgumentParser(
        description="Evaluate a trained PFRN policy checkpoint"
    )
    parser.add_argument(
        "checkpoint", nargs="?", default="model_latest.pt",
        help="Path to model checkpoint (default: model_latest.pt)"
    )
    parser.add_argument(
        "--checkpoint", dest="checkpoint_flag", default=None,
        help="Alternate way to specify checkpoint path"
    )
    parser.add_argument("--steps", type=int, default=8192,
                        help="Number of eval steps to run (default: 8192)")
    parser.add_argument("--device", type=str,
                        default="cuda" if torch.cuda.is_available() else "cpu")
    parser.add_argument("--backend", choices=("parallel", "direct"), default="parallel",
                        help="Environment backend (default: parallel)")
    parser.add_argument("--temperature", type=float, default=1.0,
                        help="Sampling temperature (1.0=policy, 0.0=greedy)")
    parser.add_argument("--fpv-interval", type=int, default=FPV_CAPTURE_INTERVAL,
                        help="Capture FPV frame every N steps (default: 16)")
    parser.add_argument("--no-gif", action="store_true",
                        help="Disable GIF generation")
    parser.add_argument("--gif-duration", type=int, default=150,
                        help="GIF frame duration in ms (default: 150)")
    parser.add_argument("--seed", type=int, default=42,
                        help="Random seed")
    args = parser.parse_args()

    # Resolve checkpoint path
    ckpt_path = args.checkpoint_flag or args.checkpoint
    if not os.path.isabs(ckpt_path):
        ckpt_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), ckpt_path)
    if not os.path.isfile(ckpt_path):
        print(f"[ERROR] Checkpoint not found: {ckpt_path}")
        sys.exit(1)

    device = torch.device(args.device)
    print(f"[EVAL] Device: {device}")
    print(f"[EVAL] Steps: {args.steps:,}")
    print(f"[EVAL] Temperature: {args.temperature}")
    print(f"[EVAL] FPV interval: {args.fpv_interval} steps")
    print()

    # ── Load checkpoint ──
    ckpt = load_checkpoint(ckpt_path, device)

    # ── Create environment ──
    print(f"[EVAL] Creating environment (backend={args.backend}, num_envs=1)")
    env = make_env(backend=args.backend)

    # ── Build policy and load weights ──
    policy = PfrPolicy(env).to(device)
    policy.load_state_dict(ckpt["model_state_dict"])
    policy.eval()
    param_count = sum(p.numel() for p in policy.parameters())
    print(f"[EVAL] Policy loaded: {param_count:,} parameters")

    # ── Try to get binding for FPV capture ──
    binding = None
    try:
        from pfrn_native import binding as _binding
        binding = _binding
        print("[EVAL] FPV capture: available (pfrn_native.binding)")
    except ImportError:
        try:
            import pfrn_binding as _binding
            binding = _binding
            print("[EVAL] FPV capture: available (pfrn_binding)")
        except ImportError:
            print("[EVAL] FPV capture: unavailable (no binding module)")

    # ── Ensure dashboard directory ──
    os.makedirs(DASH_DIR, exist_ok=True)

    # ── Reset environment ──
    obs_np, _ = env.reset()
    obs = torch.from_numpy(obs_np.copy())

    # ── Tracking state ──
    visited_tiles = set()         # (x, y, map_group, map_num)
    visited_maps = set()          # (map_group, map_num)
    trajectory = []               # list of (x, y, map_id) for trajectory viz
    fpv_frames = []               # PIL Image list for GIF
    total_reward = 0.0
    episode_rewards = []          # per-episode returns
    current_episode_reward = 0.0
    episode_count = 0
    battle_count = 0
    was_in_battle = False
    max_badges = 0
    max_party_count = 0
    max_party_level_sum = 0
    action_counts = np.zeros(NUM_ACTIONS, dtype=np.int64)

    # Parse initial observation
    init_info = obs_to_info(obs_np[0])
    tile_key = (init_info["x"], init_info["y"], init_info["map_group"], init_info["map_num"])
    visited_tiles.add(tile_key)
    visited_maps.add((init_info["map_group"], init_info["map_num"]))
    print(f"[EVAL] Start position: ({init_info['x']}, {init_info['y']}) "
          f"map={init_info['map_id']} party={init_info['party_count']} "
          f"badges={init_info['badges_earned']}")
    print()

    # ── Main eval loop ──
    start_time = time.time()
    print_interval = max(args.steps // 20, 100)  # print ~20 progress updates

    for step in range(args.steps):
        # ── Forward pass ──
        with torch.no_grad():
            obs_dev = obs.to(device)
            features = policy.encode_observations(obs_dev)
            logits, value = policy.decode_actions(features)

            # Apply temperature
            if args.temperature <= 0.0:
                # Greedy
                action = logits.argmax(dim=-1)
            elif args.temperature != 1.0:
                scaled_logits = logits / args.temperature
                probs = torch.distributions.Categorical(logits=scaled_logits)
                action = probs.sample()
            else:
                probs = torch.distributions.Categorical(logits=logits)
                action = probs.sample()

        action_np = action.cpu().numpy().astype(np.int32)
        action_counts[action_np[0]] += 1

        # ── Step environment ──
        obs_np, rewards, terms, truncs, infos = env.step(action_np)
        obs = torch.from_numpy(obs_np.copy())

        reward = float(rewards[0])
        total_reward += reward
        current_episode_reward += reward
        done = bool(terms[0]) or bool(truncs[0])

        # ── Parse observation ──
        info = obs_to_info(obs_np[0])
        tile_key = (info["x"], info["y"], info["map_group"], info["map_num"])
        visited_tiles.add(tile_key)
        visited_maps.add((info["map_group"], info["map_num"]))
        trajectory.append((info["x"], info["y"], info["map_id"]))

        # Track game progression
        max_badges = max(max_badges, info["badges_earned"])
        max_party_count = max(max_party_count, info["party_count"])
        max_party_level_sum = max(max_party_level_sum, info["party_level_sum"])

        # Track battles
        if info["in_battle"] and not was_in_battle:
            battle_count += 1
        was_in_battle = bool(info["in_battle"])

        # ── Episode boundary ──
        if done:
            episode_rewards.append(current_episode_reward)
            episode_count += 1
            current_episode_reward = 0.0

        # ── FPV capture ──
        if step % args.fpv_interval == 0 and not args.no_gif:
            frame_rgb = capture_fpv_frame(binding, env_index=0)
            if frame_rgb is not None:
                from PIL import Image
                img = Image.fromarray(frame_rgb).resize(
                    (GBA_W * FPV_SCALE, GBA_H * FPV_SCALE), Image.NEAREST
                )
                fpv_frames.append(img)
                if len(fpv_frames) > MAX_FPV_FRAMES:
                    fpv_frames = fpv_frames[-MAX_FPV_FRAMES:]

        # ── Live dashboard update ──
        if step % print_interval == 0 or step == args.steps - 1:
            elapsed = time.time() - start_time
            sps = (step + 1) / elapsed if elapsed > 0 else 0
            pct = 100.0 * (step + 1) / args.steps

            stats = {
                "mode": "eval",
                "run_name": f"eval-{os.path.basename(ckpt_path)}",
                "env_name": "pfr_native",
                "checkpoint": os.path.basename(ckpt_path),
                "trained_steps": ckpt.get("global_step", 0),
                "eval_step": step + 1,
                "eval_total_steps": args.steps,
                "eval_progress_pct": round(pct, 1),
                "sps": round(sps, 1),
                "uptime": round(elapsed, 1),
                "temperature": args.temperature,
                # Exploration
                "unique_tiles": len(visited_tiles),
                "unique_maps": len(visited_maps),
                "maps_visited": sorted(
                    [f"{mg}.{mn}" for mg, mn in visited_maps]
                ),
                # Rewards
                "total_reward": round(total_reward, 4),
                "mean_reward_per_step": round(total_reward / max(step + 1, 1), 6),
                "episode_count": episode_count,
                "mean_episode_return": round(
                    np.mean(episode_rewards) if episode_rewards else 0.0, 4
                ),
                # Game state
                "badges_earned": max_badges,
                "party_count": max_party_count,
                "party_level_sum": max_party_level_sum,
                "battle_count": battle_count,
                "in_battle": info["in_battle"],
                "agent_x": info["x"],
                "agent_y": info["y"],
                "agent_map": info["map_id"],
                # Current party snapshot
                "current_party_levels": info["party_levels"],
                "money": info["money"],
            }
            write_stats_json(os.path.join(DASH_DIR, "stats.json"), stats)

            # Progress print
            print(
                f"[{pct:5.1f}%] step={step + 1:>6,}/{args.steps:,} "
                f"SPS={sps:>5.0f} "
                f"tiles={len(visited_tiles):>4} "
                f"maps={len(visited_maps):>2} "
                f"rew={total_reward:>8.3f} "
                f"pos=({info['x']:>4},{info['y']:>4}) "
                f"map={info['map_id']:<6} "
                f"badges={max_badges} "
                f"battles={battle_count}",
                flush=True,
            )

    # ── Finalize ──
    elapsed = time.time() - start_time
    sps = args.steps / elapsed if elapsed > 0 else 0

    # Save trajectory GIF
    gif_path = os.path.join(DASH_DIR, "eval_trajectory.gif")
    if fpv_frames and not args.no_gif:
        print(f"\n[EVAL] Saving trajectory GIF ({len(fpv_frames)} frames) ...", flush=True)
        save_gif(gif_path, fpv_frames, duration_ms=args.gif_duration, loop=0)
        print(f"[EVAL] GIF saved: {gif_path}")

        # Also save last frame as PNG for quick preview
        fpv_frames[-1].save(os.path.join(DASH_DIR, "eval_last_frame.png"))

    # Close environment
    env.close()

    # ── Action distribution ──
    action_names = [
        "None", "Up", "Down", "Left", "Right",
        "A", "B", "Start", "Select", "L/R"
    ]
    action_pcts = 100.0 * action_counts / max(action_counts.sum(), 1)

    # ── Final summary ──
    print()
    print("=" * 70)
    print("  EVAL SUMMARY")
    print("=" * 70)
    print(f"  Checkpoint:         {os.path.basename(ckpt_path)}")
    print(f"  Trained steps:      {ckpt.get('global_step', 'N/A'):,}")
    print(f"  Eval steps:         {args.steps:,}")
    print(f"  Elapsed:            {elapsed:.1f}s ({sps:.0f} SPS)")
    print(f"  Temperature:        {args.temperature}")
    print()
    print("  --- Exploration ---")
    print(f"  Unique tiles:       {len(visited_tiles)}")
    print(f"  Unique maps:        {len(visited_maps)}")
    print(f"  Maps visited:       {', '.join(sorted(f'{mg}.{mn}' for mg, mn in visited_maps))}")
    print()
    print("  --- Rewards ---")
    print(f"  Total reward:       {total_reward:.4f}")
    print(f"  Mean reward/step:   {total_reward / max(args.steps, 1):.6f}")
    print(f"  Episodes completed: {episode_count}")
    if episode_rewards:
        print(f"  Mean ep return:     {np.mean(episode_rewards):.4f}")
        print(f"  Best ep return:     {max(episode_rewards):.4f}")
        print(f"  Worst ep return:    {min(episode_rewards):.4f}")
    print()
    print("  --- Game Progress ---")
    print(f"  Badges earned:      {max_badges}")
    print(f"  Party size (max):   {max_party_count}")
    print(f"  Party levels (max): {max_party_level_sum}")
    print(f"  Battles entered:    {battle_count}")
    print(f"  Money:              {info['money']}")
    print()
    print("  --- Action Distribution ---")
    for i in range(NUM_ACTIONS):
        bar = "#" * int(action_pcts[i] / 2)
        name = action_names[i] if i < len(action_names) else f"Act{i}"
        print(f"    {name:<8} {action_counts[i]:>6,} ({action_pcts[i]:>5.1f}%) {bar}")
    print()
    if fpv_frames:
        print(f"  Trajectory GIF:     {gif_path} ({len(fpv_frames)} frames)")
    print(f"  Dashboard stats:    {os.path.join(DASH_DIR, 'stats.json')}")
    print("=" * 70)

    # ── Write final stats ──
    final_stats = {
        "mode": "eval",
        "status": "complete",
        "run_name": f"eval-{os.path.basename(ckpt_path)}",
        "env_name": "pfr_native",
        "checkpoint": os.path.basename(ckpt_path),
        "trained_steps": ckpt.get("global_step", 0),
        "eval_steps": args.steps,
        "elapsed_s": round(elapsed, 1),
        "sps": round(sps, 1),
        "temperature": args.temperature,
        # Exploration
        "unique_tiles": len(visited_tiles),
        "unique_maps": len(visited_maps),
        "maps_visited": sorted([f"{mg}.{mn}" for mg, mn in visited_maps]),
        # Rewards
        "total_reward": round(total_reward, 4),
        "mean_reward_per_step": round(total_reward / max(args.steps, 1), 6),
        "episode_count": episode_count,
        "mean_episode_return": round(
            float(np.mean(episode_rewards)) if episode_rewards else 0.0, 4
        ),
        # Game
        "badges_earned": max_badges,
        "max_party_count": max_party_count,
        "max_party_level_sum": max_party_level_sum,
        "battle_count": battle_count,
        "final_position": {"x": info["x"], "y": info["y"], "map": info["map_id"]},
        "final_party_levels": info["party_levels"],
        "money": info["money"],
        # Action distribution
        "action_distribution": {
            action_names[i] if i < len(action_names) else f"Act{i}": int(action_counts[i])
            for i in range(NUM_ACTIONS)
        },
        "trajectory_gif": gif_path if fpv_frames else None,
    }
    write_stats_json(os.path.join(DASH_DIR, "stats.json"), final_stats)
    print("\n[EVAL] Done.")


if __name__ == "__main__":
    main()
