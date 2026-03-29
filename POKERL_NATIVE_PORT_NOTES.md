# Pokerl Native Port Notes

## What `pokerl` / `pokemonred_puffer` do today

`pokemonred_puffer` is a Gymnasium + PufferLib environment around Pokemon Red via PyBoy.
Its core design is:

- Emulator-backed reset from a boot/save state.
- A discrete action space over directional inputs plus `A`, `B`, and `START`.
- Dense shaping for exploration and progression, with sparse high-value rewards for major milestones.
- A rich `spaces.Dict` observation:
  - screen / visited mask / optional global map
  - direction / battle state / map ids
  - bag items + quantities
  - full party stat block
  - event / progression bitfields
- A convolutional policy that consumes that Dict observation.

The main PyBoy-specific pieces are:

- stepping the emulator and reading Game Boy RAM/screen
- hook registration for scripted automation and event tracking
- save-state reset behavior

## Native replacement status

The PyBoy stepping layer is now replaced for native RL training in this repo by:

- `build/libpfr_game.so`
  - native FireRed runtime exported as a callable game API
- `scripts/pfr_env_worker.py`
  - isolated subprocess worker around the native runtime
- `pfr_env.py`
  - vectorized Python env that manages one native worker per environment
- `train_quick.py`
  - PPO smoke-training entrypoint against the native backend

The direct native backend now supports:

- hot-reset style episode starts via fresh worker processes
- `num_envs > 1` using one native worker per env
- native stepping, observation extraction, reward shaping, terminal handling
- recovery from worker crashes by terminating the affected episode instead of killing training

## Verified native training

Single-env smoke:

```bash
source /home/spark-advantage/pufferlib-4.0/.venv/bin/activate
cd /home/spark-advantage/pokefirered-native
PYTHONPATH=. python -u scripts/smoke_direct_env.py
```

Verified PPO smoke:

```bash
source /home/spark-advantage/pufferlib-4.0/.venv/bin/activate
cd /home/spark-advantage/pokefirered-native
python -u train_quick.py --backend direct --num-envs 2 \
  --total-timesteps 512 --batch-size 32 --minibatch-size 64 \
  --update-epochs 1 --frames-per-step 4 --max-steps 16
```

Observed result:

- native PPO completed end-to-end
- training ran at roughly 33 SPS with `num_envs=2`
- exploration reward accrued and explored tile count increased during training

## Current parity gap versus upstream `pokemonred_puffer`

The native runtime is training-capable, but it is not yet a drop-in observation/reward clone of upstream `pokemonred_puffer`.

Still missing for full parity:

- screen pixels / visited mask / optional global map image observations
- full bag extraction
- full party stat block at `pokemonred_puffer` fidelity
- 320-bit event vector and special progression flags
- PyBoy hook-based scripted automations reimplemented against native game state
- wiring the native backend into the upstream `pokemonred_puffer` env/policy stack as a true backend swap

The current native backend is therefore:

- a working native RL environment for FireRed
- sufficient to train and learn without PyBoy
- not yet a strict API-compatible replacement for the full `pokemonred_puffer` Dict observation stack

## Recommended next step

To reach true `pokemonred_puffer` parity, extend the native observation API in `libpfr_game.so` so the Python env can emit:

- screen / visited mask
- bag arrays
- full party stats and moves
- event/progression bitfields

Once that exists, add a native `RedGymEnv` backend in the `pokemonred_puffer` repo and point the existing policy stack at it.
