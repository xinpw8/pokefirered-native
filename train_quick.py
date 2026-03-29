"""
train_quick.py — Quick standalone PPO training for native FireRed envs

Writes live stats to /tmp/pfr_dashboard/ for the dashboard at port 53580.
Captures agent FPV (GBA screen) and heatmap snapshots as GIFs.

Usage:
    source /home/spark-advantage/pufferlib-4.0/.venv/bin/activate
    cd /home/spark-advantage/pokefirered-native
    python3 train_quick.py --backend parallel --num-envs 8
"""

import argparse
import json
import os
import struct
import sys
import time
import threading

import numpy as np
import torch
import torch.nn as nn

sys.path.insert(0, os.path.dirname(__file__))

from pfr_policy import PfrPolicy
from pfr_heatmap import make_exploration_overlay

DASH_DIR = "/tmp/pfr_dashboard"
GBA_W, GBA_H = 240, 160


# ── Dashboard helpers ──

def write_stats_json(path, data):
    """Atomic write of stats.json."""
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f)
    os.replace(tmp, path)


def save_png(path, rgb_array):
    """Save RGB numpy array as PNG using PIL."""
    from PIL import Image
    img = Image.fromarray(rgb_array)
    tmp = path + ".tmp.png"
    img.save(tmp)
    os.replace(tmp, path)


def save_gif(path, frames, duration_ms=200, loop=0):
    """Save list of PIL Images as animated GIF."""
    if not frames:
        return
    from PIL import Image
    tmp = path + ".tmp.gif"
    frames[0].save(tmp, save_all=True, append_images=frames[1:],
                   duration=duration_ms, loop=loop)
    os.replace(tmp, path)


def argb_to_rgb(argb_array):
    """Convert (H, W, 4) ARGB uint8 to (H, W, 3) RGB uint8."""
    # Framebuffer is ARGB8888 stored as uint32 little-endian.
    # When viewed as uint8 array: bytes are B, G, R, A per pixel.
    # So channel 0=B, 1=G, 2=R, 3=A. Reverse first 3 for RGB.
    return argb_array[:, :, 2::-1].copy()


def make_heatmap_png(explore_map, scale=2):
    """Generate a heatmap RGB image from explore_map counts."""
    counts = explore_map
    if counts.ndim == 3:
        counts = np.sum(counts, axis=0)

    max_val = np.max(counts)
    if max_val == 0:
        h, w = counts.shape
        return np.zeros((h * scale, w * scale, 3), dtype=np.uint8)

    # sqrt scaling for better gradient visibility
    scaled = np.sqrt(counts.astype(np.float32)) / np.sqrt(max_val)
    nonzero = (counts > 0).astype(np.float32)

    # HSV: blue (low) to red (high)
    h_channel = 2.0 * (1.0 - scaled) / 3.0
    s_channel = nonzero
    v_channel = nonzero * 0.9 + 0.1 * nonzero  # bright where visited

    # Simple HSV→RGB
    from pfr_heatmap import hsv_to_rgb_simple
    hsv = np.stack([h_channel, s_channel, v_channel], axis=-1)
    rgb = (255 * hsv_to_rgb_simple(hsv)).astype(np.uint8)

    if scale > 1:
        r = np.kron(rgb[..., 0], np.ones((scale, scale), dtype=np.uint8))
        g = np.kron(rgb[..., 1], np.ones((scale, scale), dtype=np.uint8))
        b = np.kron(rgb[..., 2], np.ones((scale, scale), dtype=np.uint8))
        rgb = np.stack([r, g, b], axis=-1)

    return rgb


def crop_to_content(img, margin=5):
    """Crop RGB image to non-black content with margin."""
    mask = np.any(img > 0, axis=2)
    rows = np.any(mask, axis=1)
    cols = np.any(mask, axis=0)
    if not np.any(rows):
        return img
    rmin, rmax = np.where(rows)[0][[0, -1]]
    cmin, cmax = np.where(cols)[0][[0, -1]]
    rmin = max(0, rmin - margin)
    cmin = max(0, cmin - margin)
    rmax = min(img.shape[0], rmax + margin + 1)
    cmax = min(img.shape[1], cmax + margin + 1)
    return img[rmin:rmax, cmin:cmax]


def build_map_table(explore_map, map_data):
    """Build map visit table from explore_map and map_data."""
    PAD = 20
    regions = map_data.get("regions", [])
    table = []
    for r in regions:
        rid = r.get("id", -1)
        if rid < 0:
            continue
        name = r.get("name", f"map_{rid}")
        coords = r.get("coordinates", [0, 0])
        tile_size = r.get("tileSize", [1, 1])
        gx = coords[0] + PAD
        gy = coords[1] + PAD
        tw, th = tile_size
        if tw <= 0 or th <= 0:
            continue
        # Sum visits in this map's region
        y1 = max(0, gy)
        y2 = min(explore_map.shape[0], gy + th)
        x1 = max(0, gx)
        x2 = min(explore_map.shape[1], gx + tw)
        if y1 >= y2 or x1 >= x2:
            continue
        region_counts = explore_map[y1:y2, x1:x2]
        total_visits = int(np.sum(region_counts))
        unique_tiles = int(np.count_nonzero(region_counts))
        if unique_tiles > 0:
            table.append([rid, name, unique_tiles, total_visits])
    table.sort(key=lambda x: -x[3])
    return table


def obs_to_info(obs_byte):
    """Extract game info from a single 226-byte observation."""
    o = obs_byte
    px = struct.unpack_from('<h', o, 0)[0]
    py = struct.unpack_from('<h', o, 2)[0]
    mg, mn = int(o[4]), int(o[5])
    direction = int(o[7])
    in_battle = int(o[11])
    party_count = 0
    party_level_sum = 0
    for i in range(6):
        base = 13 + i * 6
        sp = int(o[base]) | (int(o[base + 1]) << 8)
        if sp > 0:
            party_count += 1
            party_level_sum += int(o[base + 2])
    badges = int(o[49])
    money = int(o[50]) | (int(o[51]) << 8)
    return {
        "x": px, "y": py, "map_group": mg, "map_num": mn,
        "direction": direction, "in_battle": in_battle,
        "party_count": party_count, "party_level_sum": party_level_sum,
        "badges": badges, "badges_earned": bin(badges).count('1'),
        "money": money,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--total-timesteps", type=int, default=100_000)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--minibatch-size", type=int, default=128)
    parser.add_argument("--lr", type=float, default=2.5e-4)
    parser.add_argument("--gamma", type=float, default=0.999)
    parser.add_argument("--gae-lambda", type=float, default=0.95)
    parser.add_argument("--clip-coef", type=float, default=0.2)
    parser.add_argument("--ent-coef", type=float, default=0.01)
    parser.add_argument("--vf-coef", type=float, default=0.5)
    parser.add_argument("--update-epochs", type=int, default=4)
    parser.add_argument("--device", type=str, default="cuda" if torch.cuda.is_available() else "cpu")
    parser.add_argument("--backend", choices=("parallel", "direct"), default="parallel")
    parser.add_argument("--num-envs", type=int, default=8)
    parser.add_argument("--frames-per-step", type=int, default=4)
    parser.add_argument("--max-steps", type=int, default=24576)
    parser.add_argument("--savestate-path", type=str, default=None)
    parser.add_argument("--wandb", action="store_true")
    parser.add_argument("--heatmap-interval", type=int, default=5,
                        help="Epochs between heatmap updates")
    parser.add_argument("--fpv-interval", type=int, default=3,
                        help="Epochs between FPV frame captures")
    parser.add_argument("--fpv-agent", type=int, default=0,
                        help="Agent index to capture FPV from")
    parser.add_argument("--stream", action="store_true",
                        help="Stream agent positions to pokerl-map-viz")
    parser.add_argument("--stream-user", type=str, default="pfrn-agent")
    parser.add_argument("--stream-interval", type=int, default=150)
    parser.add_argument("--run-name", type=str, default=None,
                        help="Name for this training run (shown on dashboard)")
    args = parser.parse_args()

    run_name = args.run_name or f"pfrn-{time.strftime('%H%M%S')}"

    # Ensure dashboard dir exists
    os.makedirs(DASH_DIR, exist_ok=True)

    # Load map data for map table
    map_data_path = os.path.join(os.path.dirname(__file__), "pfr_map_data.json")
    map_data = {}
    if os.path.exists(map_data_path):
        with open(map_data_path) as f:
            map_data = json.load(f)

    wandb_run = None
    if args.wandb:
        import wandb
        wandb_run = wandb.init(project="pfrn", config=vars(args),
                               name=run_name)

    if args.backend == "parallel":
        from pfrn import PFRN as EnvClass
        env = EnvClass(
            num_envs=args.num_envs,
            frames_per_step=args.frames_per_step,
            max_steps=args.max_steps,
            savestate_path=args.savestate_path,
        )
    else:
        from pfr_env import PfrEnv as EnvClass
        env = EnvClass(
            num_envs=args.num_envs,
            frames_per_step=args.frames_per_step,
            max_steps=args.max_steps,
            savestate_path=args.savestate_path or "",
        )

    # Try to import the binding for FPV capture
    binding = None
    try:
        from pfrn_native import binding
    except ImportError:
        try:
            import pfrn_binding as binding
        except ImportError:
            pass

    # Set up map streaming
    streamer = None
    if args.stream:
        from pfr_stream import PfrMapStreamer
        streamer = PfrMapStreamer(
            user=args.stream_user, env_id="0",
            upload_interval=args.stream_interval,
        )
        print(f"[PFRN] Map streaming enabled (user={args.stream_user})")

    print(
        f"[PFRN] Quick training on {args.device}, backend={args.backend}, "
        f"envs={env.num_agents}, total_timesteps={args.total_timesteps}"
    )
    print(f"[PFRN] Dashboard: writing to {DASH_DIR}")
    policy = PfrPolicy(env).to(args.device)
    optimizer = torch.optim.Adam(policy.parameters(), lr=args.lr)
    print(f"[PFRN] Policy: {sum(p.numel() for p in policy.parameters()):,} params")

    num_envs = env.num_agents
    num_steps = args.batch_size
    obs_buf = torch.zeros(num_steps, num_envs, 226, dtype=torch.uint8)
    act_buf = torch.zeros(num_steps, num_envs, dtype=torch.long)
    logprob_buf = torch.zeros(num_steps, num_envs)
    reward_buf = torch.zeros(num_steps, num_envs)
    done_buf = torch.zeros(num_steps, num_envs)
    value_buf = torch.zeros(num_steps, num_envs)

    obs, _ = env.reset()

    # --- MONITOR: diagnostic dump of initial observation ---
    print(f"[MONITOR] Initial obs shape: {obs.shape}, dtype: {obs.dtype}", flush=True)
    if obs.shape[0] > 0:
        info = obs_to_info(obs[0])
        print(f"[MONITOR] Env0 initial: pos=({info['x']},{info['y']}) "
              f"map={info['map_group']}.{info['map_num']} "
              f"party={info['party_count']} badges={info['badges']:#04x} "
              f"money={info['money']}", flush=True)

    obs = torch.from_numpy(obs.copy())
    global_step = 0
    num_updates = max(args.total_timesteps // (args.batch_size * num_envs), 1)
    episode_returns = np.zeros(num_envs, dtype=np.float32)
    completed_returns = []
    start_time = time.time()

    # Loss tracking
    last_pg_loss = 0.0
    last_vf_loss = 0.0
    last_entropy = 0.0
    last_total_loss = 0.0
    last_clipfrac = 0.0
    last_approx_kl = 0.0

    # FPV and heatmap history for GIFs
    fpv_frames = []        # List of PIL Images for FPV GIF
    heatmap_frames = []    # List of PIL Images for heatmap evolution GIF
    MAX_FPV_FRAMES = 60    # Keep last N frames for FPV GIF
    MAX_HEATMAP_FRAMES = 50  # Keep last N snapshots for heatmap GIF

    for update in range(1, num_updates + 1):
        frac = 1.0 - (update - 1.0) / num_updates
        optimizer.param_groups[0]["lr"] = frac * args.lr

        for step in range(num_steps):
            global_step += num_envs
            obs_buf[step] = obs

            with torch.no_grad():
                obs_dev = obs.to(args.device)
                features = policy.encode_observations(obs_dev)
                logits, value = policy.decode_actions(features)
                probs = torch.distributions.Categorical(logits=logits)
                action = probs.sample()

            act_buf[step] = action.cpu()
            logprob_buf[step] = probs.log_prob(action).cpu()
            value_buf[step] = value.flatten().cpu()

            obs_np, rewards, terms, truncs, infos = env.step(action.cpu().numpy().astype(np.int32))

            obs = torch.from_numpy(obs_np.copy())
            reward_buf[step] = torch.from_numpy(rewards.copy())
            dones = np.logical_or(terms.astype(bool), truncs.astype(bool))
            done_buf[step] = torch.from_numpy(dones.astype(np.float32))

            episode_returns += rewards
            for idx, done in enumerate(dones):
                if done:
                    completed_returns.append(float(episode_returns[idx]))
                    episode_returns[idx] = 0.0

            # Stream agent position
            if streamer is not None:
                streamer.on_step(obs_np[0])

        # GAE
        with torch.no_grad():
            obs_dev = obs.to(args.device)
            features = policy.encode_observations(obs_dev)
            _, next_value = policy.decode_actions(features)
            next_value = next_value.flatten().cpu()

        advantages = torch.zeros(num_steps, num_envs)
        lastgaelam = torch.zeros(num_envs)
        for t in reversed(range(num_steps)):
            if t == num_steps - 1:
                next_nonterminal = 1.0 - done_buf[t]
                next_val = next_value
            else:
                next_nonterminal = 1.0 - done_buf[t + 1]
                next_val = value_buf[t + 1]
            delta = reward_buf[t] + args.gamma * next_val * next_nonterminal - value_buf[t]
            advantages[t] = lastgaelam = (
                delta + args.gamma * args.gae_lambda * next_nonterminal * lastgaelam
            )
        returns = advantages + value_buf

        # PPO
        flat_obs = obs_buf.reshape(num_steps * num_envs, 226)
        flat_actions = act_buf.reshape(num_steps * num_envs)
        flat_logprob = logprob_buf.reshape(num_steps * num_envs)
        flat_advantages = advantages.reshape(num_steps * num_envs)
        flat_returns = returns.reshape(num_steps * num_envs)
        b_inds = np.arange(num_steps * num_envs)

        clipfracs = []
        for _ in range(args.update_epochs):
            np.random.shuffle(b_inds)
            for start in range(0, num_steps * num_envs, args.minibatch_size):
                end = start + args.minibatch_size
                mb = b_inds[start:end]

                mb_obs = flat_obs[mb].to(args.device)
                features = policy.encode_observations(mb_obs)
                logits, newvalue = policy.decode_actions(features)
                probs = torch.distributions.Categorical(logits=logits)
                newlogprob = probs.log_prob(flat_actions[mb].to(args.device))
                entropy = probs.entropy()

                logratio = newlogprob - flat_logprob[mb].to(args.device)
                ratio = logratio.exp()

                with torch.no_grad():
                    old_approx_kl = (-logratio).mean()
                    approx_kl = ((ratio - 1) - logratio).mean()
                    clipfracs.append(
                        ((ratio - 1.0).abs() > args.clip_coef).float().mean().item()
                    )

                mb_adv = flat_advantages[mb].to(args.device)
                mb_adv = (mb_adv - mb_adv.mean()) / (mb_adv.std() + 1e-8)

                pg1 = -mb_adv * ratio
                pg2 = -mb_adv * torch.clamp(ratio, 1 - args.clip_coef, 1 + args.clip_coef)
                pg_loss = torch.max(pg1, pg2).mean()
                v_loss = 0.5 * (
                    (newvalue.flatten() - flat_returns[mb].to(args.device)) ** 2
                ).mean()
                ent_loss = entropy.mean()
                loss = pg_loss - args.ent_coef * ent_loss + args.vf_coef * v_loss

                if torch.isnan(loss) or torch.isinf(loss):
                    print(f"[MONITOR] NaN/Inf LOSS at update={update}!", flush=True)
                    continue

                optimizer.zero_grad()
                loss.backward()
                nn.utils.clip_grad_norm_(policy.parameters(), 0.5)
                optimizer.step()

                last_pg_loss = pg_loss.item()
                last_vf_loss = v_loss.item()
                last_entropy = ent_loss.item()
                last_total_loss = loss.item()
                last_approx_kl = approx_kl.item()

        last_clipfrac = np.mean(clipfracs) if clipfracs else 0.0

        elapsed = time.time() - start_time
        sps = global_step / elapsed
        explored = int((env.explore_map > 0).sum())
        mean_ep_ret = np.mean(completed_returns[-10:]) if completed_returns else 0.0
        mean_rew = reward_buf.mean().item()

        # Agent info from env 0's latest obs
        agent_info = obs_to_info(obs_np[0]) if obs_np.shape[0] > 0 else {}

        print(f"[{update}/{num_updates}] step={global_step:,} SPS={sps:.0f} "
              f"rew={mean_rew:.4f} ep_ret={mean_ep_ret:.3f} explored={explored} "
              f"loss={last_total_loss:.4f} ent={last_entropy:.3f}")

        # ── Write stats.json for dashboard ──
        explore_map = env.explore_map
        if explore_map.ndim == 3:
            explore_flat = np.sum(explore_map, axis=0)
        else:
            explore_flat = explore_map

        global_tiles = int(np.count_nonzero(explore_flat))
        total_possible = explore_flat.size
        coverage_pct = 100.0 * global_tiles / total_possible if total_possible > 0 else 0.0

        # Map table
        maps_table = build_map_table(explore_flat, map_data)
        unique_maps = len(maps_table)

        stats = {
            "run_name": run_name,
            "env_name": "pfr_native",
            "steps": global_step,
            "sps": round(sps, 1),
            "epoch": update,
            "num_epochs": num_updates,
            "uptime": round(elapsed, 1),
            "num_envs": num_envs,
            # Progression
            "badges_earned": agent_info.get("badges_earned", 0),
            "party_count": agent_info.get("party_count", 0),
            "party_level_sum": agent_info.get("party_level_sum", 0),
            "money": agent_info.get("money", 0),
            "in_battle": agent_info.get("in_battle", 0),
            # Exploration
            "unique_tiles": explored,
            "unique_maps": unique_maps,
            "global_tiles": global_tiles,
            "heatmap_coverage_pct": round(coverage_pct, 4),
            "episode_return": round(mean_ep_ret, 4),
            "mean_reward": round(mean_rew, 6),
            # Losses
            "pg_loss": round(last_pg_loss, 6),
            "vf_loss": round(last_vf_loss, 6),
            "entropy": round(last_entropy, 6),
            "total_loss": round(last_total_loss, 6),
            "clipfrac": round(last_clipfrac, 6),
            "approx_kl": round(last_approx_kl, 6),
            # Agent state
            "agent_x": agent_info.get("x", 0),
            "agent_y": agent_info.get("y", 0),
            "agent_map": f"{agent_info.get('map_group', 0)}.{agent_info.get('map_num', 0)}",
            # Maps table
            "maps": maps_table[:20],
        }
        write_stats_json(os.path.join(DASH_DIR, "stats.json"), stats)

        # ── Heatmap update ──
        if update % args.heatmap_interval == 0 and explored > 0:
            try:
                from PIL import Image
                heatmap_rgb = make_heatmap_png(explore_flat, scale=2)
                heatmap_cropped = crop_to_content(heatmap_rgb, margin=10)
                # Ensure minimum display size for dashboard
                from PIL import Image as PILImage
                hm_disp = PILImage.fromarray(heatmap_cropped)
                min_dim = 200
                if hm_disp.width < min_dim or hm_disp.height < min_dim:
                    scale_up = max(min_dim / max(hm_disp.width, 1), min_dim / max(hm_disp.height, 1))
                    hm_disp = hm_disp.resize(
                        (max(int(hm_disp.width * scale_up), min_dim),
                         max(int(hm_disp.height * scale_up), min_dim)),
                        PILImage.NEAREST)
                hm_disp.save(os.path.join(DASH_DIR, "heatmap.png"))

                # Zoom: Pallet Town -> Pewter City (PADDED coords)
                # PalletTown@(60,260) Route1@(60,220) ViridianCity@(60,180) Route2@(60,160) PewterCity@(60,60)
                PAD = 20
                zoom_y1, zoom_y2 = 50 + PAD, 290 + PAD
                zoom_x1, zoom_x2 = 45 + PAD, 100 + PAD
                # Scale to heatmap pixel coords
                s = 2
                zy1, zy2 = zoom_y1 * s, zoom_y2 * s
                zx1, zx2 = zoom_x1 * s, zoom_x2 * s
                zy1 = max(0, min(zy1, heatmap_rgb.shape[0]))
                zy2 = max(0, min(zy2, heatmap_rgb.shape[0]))
                zx1 = max(0, min(zx1, heatmap_rgb.shape[1]))
                zx2 = max(0, min(zx2, heatmap_rgb.shape[1]))
                if zy2 > zy1 and zx2 > zx1:
                    zoom_region = heatmap_rgb[zy1:zy2, zx1:zx2]
                    # 3x upscale for zoom
                    zoom_img = Image.fromarray(zoom_region).resize(
                        (zoom_region.shape[1] * 3, zoom_region.shape[0] * 3),
                        Image.NEAREST)
                    zoom_img.save(os.path.join(DASH_DIR, "heatmap_zoom.png"))

                # Add to heatmap history GIF — use FULL heatmap (not cropped)
                # so the evolution shows spatial context and is always a visible size
                hm_frame = Image.fromarray(heatmap_rgb)
                # Resize to fixed 400px wide for consistent GIF frames
                target_w = 400
                ratio = target_w / max(hm_frame.width, 1)
                target_h = max(int(hm_frame.height * ratio), 1)
                hm_frame = hm_frame.resize((target_w, target_h), Image.NEAREST)
                heatmap_frames.append(hm_frame)
                if len(heatmap_frames) > MAX_HEATMAP_FRAMES:
                    heatmap_frames = heatmap_frames[-MAX_HEATMAP_FRAMES:]

                # Save heatmap evolution GIF
                if len(heatmap_frames) >= 2:
                    save_gif(os.path.join(DASH_DIR, "heatmap_evolution.gif"),
                             heatmap_frames, duration_ms=500, loop=0)
            except Exception as e:
                print(f"[DASH] Heatmap error: {e}", flush=True)

        # ── FPV capture ──
        if update % args.fpv_interval == 0 and binding is not None:
            try:
                from PIL import Image
                frame_argb = binding.capture_frame(args.fpv_agent)
                frame_rgb = argb_to_rgb(frame_argb)
                # Upscale 3x for visibility
                img = Image.fromarray(frame_rgb).resize(
                    (GBA_W * 3, GBA_H * 3), Image.NEAREST)
                fpv_frames.append(img)
                if len(fpv_frames) > MAX_FPV_FRAMES:
                    fpv_frames = fpv_frames[-MAX_FPV_FRAMES:]

                # Save latest frame as PNG
                img.save(os.path.join(DASH_DIR, "agent_fpv.png"))

                # Save FPV GIF (last N frames)
                if len(fpv_frames) >= 2:
                    save_gif(os.path.join(DASH_DIR, "agent_fpv.gif"),
                             fpv_frames, duration_ms=150, loop=0)
            except Exception as e:
                print(f"[DASH] FPV capture error: {e}", flush=True)

        # ── Wandb logging ──
        if wandb_run:
            import wandb as wb
            log_dict = {
                "charts/SPS": sps,
                "charts/mean_reward": mean_rew,
                "charts/mean_ep_return": mean_ep_ret,
                "charts/explored_tiles": explored,
                "charts/entropy": last_entropy,
                "losses/pg_loss": last_pg_loss,
                "losses/vf_loss": last_vf_loss,
                "losses/total": last_total_loss,
                "losses/clipfrac": last_clipfrac,
                "losses/approx_kl": last_approx_kl,
                "global_step": global_step,
            }
            if update % args.heatmap_interval == 0 and explored > 0:
                overlay = make_exploration_overlay(env.explore_map)
                log_dict["media/exploration_heatmap"] = wb.Image(overlay)
            wandb_run.log(log_dict)

    # Flush remaining streamed coordinates
    if streamer is not None:
        streamer.close()

    env.close()
    if wandb_run:
        wandb_run.finish()

    # Save model checkpoint
    ckpt_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "model_latest.pt")
    torch.save({
        "model_state_dict": policy.state_dict(),
        "optimizer_state_dict": optimizer.state_dict(),
        "global_step": global_step,
        "explored_tiles": explored,
        "args": vars(args),
    }, ckpt_path)
    print(f"[PFRN] Model saved to {ckpt_path}")

    print(f"[PFRN] Done. {global_step:,} steps, {elapsed:.1f}s, {sps:.0f} SPS, "
          f"{explored} tiles explored, {len(heatmap_frames)} heatmap snapshots")


if __name__ == "__main__":
    main()
