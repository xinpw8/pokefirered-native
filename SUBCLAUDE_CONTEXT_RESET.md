# PFR Subclaude Context Reset — 2026-03-17

## What happened

1. **pfr_play ran unchecked for 24+ hours** (PID 1151760, started Mar 16). It dumped **1.7 TERABYTES** of frame images to `pfr_play_frames/` and wrote an **11 GB** trace log. It consumed 97.8% CPU the entire time. This was NOT training — it was a headless play session dumping every frame to disk with no size limit.

2. **DM Grandmaster chess training is gone.** The `/home/spark-advantage/dm_grandmaster/` directory no longer exists. Last known state: epoch 11, step 1000, action_acc=40.0%, SPS=484. All checkpoints lost.

3. **Disk was at 74% (930GB free of 3.7TB)** before cleanup. After killing pfr_play and deleting the 1.7TB of frames, disk is now at 27% (2.6TB free).

4. **Hondaputer (192.168.0.19) is unreachable** — no route to host. Likely powered off.

## What was cleaned up

- Killed PIDs 1151760 and 1151758 (pfr_play processes)
- Deleted `/home/spark-advantage/pokefirered-native/pfr_play_frames/` (1.7TB)
- Deleted `/home/spark-advantage/pokefirered-native/pfr_play_trace.log` (11GB)

## What the GPT-5 Codex agent already completed (DO NOT REDO)

- env bindings in `native/binding.h` and `native/pfr_puffer_env.c` — 216-byte packed observation, 9-button action space, exploration/seen/caught/level/badge reward
- Fixed null crash in `third_party/pokefirered/src/sprite.c`
- Fixed smoke test expectations in `tests/smoke.c`
- All smoke tests passing: `TestPfrEnvSlotsPreserveDistinctRamStates`, `TestPfrEnvSlotsReadPackedObservation`, `TestDestroySpriteAndFreeResourcesNullSafe`, `TestMainRuntime`, `TestIntroBootCallbacks`
- Worker roundtrip passing: `pokemon_firered_native_puffer_worker_roundtrip`
- Train.sh minimal run confirmed: `steps=16 updates=2 last_sps=59.000`
- eval.py fixed and confirmed: `episodes_completed=1`
- `train.sh`, `setup.py`, `train.py`, `train.ini` all updated to point at `/home/spark-advantage/pufferlib-4.0`

## Current focus

Getting a REAL PFR training run working on PufferLib 4.0 native. Not more pfr_play debugging.

## Hardware

- **DGX Spark**: 20 cores, 119GB RAM, NVIDIA GB10 GPU (sm_121), 3.7TB NVMe
- GPU is currently FREE (0% util)
- RAM: 16GB/119GB used

## ABSOLUTE RULES

1. **NEVER run any process that writes unbounded data to disk.** No frame dumps, no unlimited trace logs. Every write path MUST have a size cap.
2. **Every long-running process MUST be monitored.** Check disk/RAM/GPU before launching. Set timeouts.
3. **Kill before crash.** If RAM > 100GB or disk > 80% or GPU mem > 110GB, kill the offending process immediately.
4. **The system watchdog at `/home/spark-advantage/spark_watchdog.sh` will kill your processes if they exceed limits.** Do not disable it.
5. **Log everything.** Every run, every eval, every experiment — log the command, PID, start time, and result.
