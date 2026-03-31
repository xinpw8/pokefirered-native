# MANDATORY RULES - Pokemon FireRed RL Training

These rules are set by the project owner and are NON-NEGOTIABLE.

## ABSOLUTELY FORBIDDEN in pfr_game_api.c:

1. NO Pokemon injection - NEVER call CreateMon(). No Charmander, no any Pokemon.
2. NO random spawns - NEVER use SetWarpDestination/WarpIntoMap to diversify spawn locations. NO spawns[] arrays, NO spawn_idx. All agents start at the SAME position.
3. NO flag bypasses - NEVER call FlagSet() or VarSet() to modify game state. This includes Oak cutscene, Viridian old man, Pewter gym guide, repel, Pokemon get, Pokedex, hide Oak, running shoes.
4. NO permanent repel
5. NO running shoes injection
6. NO cutscene skips - if a cutscene hangs in headless mode, fix the headless implementation
7. NO SetLastHealLocationWarp
8. NO unauthorized modifications - if the user did not ask for it, do not add it

## What IS allowed:
- Fixing actual bugs in the headless implementation (e.g., gHostNoAudio = TRUE)
- Savestate load/save/hot operations
- Observation extraction and reward computation
- Episode reset logic


# pokefirered-native

Native x86 rehost of pret/pokefirered. Upstream C compiled against a thin host
shim layer instead of GBA hardware. Upstream game logic is correct — bugs are
in the host shim layer or the seams between upstream and host code.

## Rules

- Never declare a bug fixed without an automated check script returning exit 0.
- Never load a different savestate to "verify" a fix — use the one that triggers the bug.
- Never modify upstream .c files. Only modify host shim files.
- After any fix, rebuild and run the check script before committing.
- Do not single-shot bug fixes. Use the fixloop pattern described below.

## Build

```
cmake -S . -B build
cmake --build build --target pfr_play pfr_smoke -j
```

## Active Bugs

### 1. Trainer Bad Egg

After defeating any trainer in battle, the trainer sends out another pokemon
that displays as "Bad Egg" (species 0) as the "you beat trainer" victory
dialogue plays. This does not happen in the real GBA game — the battle should
end cleanly after the last pokemon faints.

**Correct behavior:** After the trainer's last pokemon faints, the battle
transitions directly to victory text. No additional pokemon is sent out. The
trainer's party_count field correctly limits how many pokemon are used.

**Likely involved files:** host battle stubs, pfr_game_api, anything that
initializes or reads trainer party data on the host side.

**Analysis angles (use these as diverse starting points for subagents):**
1. Trainer party initialization — is party_count correct? Is memory beyond party_count zeroed?
2. Battle loop termination — when does the host decide the trainer is out of pokemon?
3. Struct packing — compare sizeof on x86 vs GBA. Alignment gaps that look like extra party members?
4. SendOutMon / SwitchIn logic — does the host shim gate on party size before sending next mon?
5. Memory initialization — is trainer party data in EWRAM fully zeroed before population, or does garbage from a previous battle remain?

### 2. Save Menu Shows All Zeros

The save game submenu renders 0 0 0 0 00 for badges, pokedex count, playtime,
etc. regardless of actual progress. The save itself may work (continuing loads
with progress intact), but the display reads all zeros.

**Correct behavior:** Save menu displays current badge count with badge icons,
pokedex seen/caught numbers, and playtime in HH:MM format. Values come from
FLAG_BADGE01_GET through FLAG_BADGE08_GET, gSaveBlock2Ptr->pokedex bitfields,
and gSaveBlock2Ptr->playTimeHours/Minutes.

**Likely involved files:** pfr_game_api, host save stubs, anything that syncs
native state to/from the GBA-format save block structs.

**Analysis angles:**
1. Flag read path — when save_menu_util calls FlagGet(FLAG_BADGE01_GET), does it reach native flag storage or a stale GBA-format copy?
2. Save block sync — does the host write native state back into gSaveBlock1/gSaveBlock2 before the save menu reads them?
3. The execute_action boundary — are flags actually synced here before the save menu renders?
4. Pokedex counters — are GetNationalPokedexCount / GetHoennPokedexCount stubbed to return 0?
5. Playtime — is the host playtime counter writing to the same struct the save menu reads?

### 3. Trainer Bad Egg (variant — post-battle)

Same root cause as #1 but also: after beating any trainer, that trainer sends
out another pokemon "bad egg" as the "you beat trainer" dialogs play. This
variant emphasizes it happens during the dialogue sequence, not just at battle end.

## Fixloop Architecture

Do not attempt to fix these bugs with a single agent making one attempt.
Instead, build and use an evolutionary fix loop inspired by
github.com/jerber/arc-lang-public. The architecture:

### Overview

```
GENERATE (N independent subagents with clean context)
    → each approaches the bug from a different angle
    → each produces a candidate fix (complete modified file)
SCORE (build → run headless from savestate → check script)
    → automated pass/fail per candidate
    → structured failure output (what was expected vs actual)
REVISE (top K candidates + their failure diffs → fresh subagents)
    → individual revisions: improve on each top candidate
    → pooled revisions: synthesize best elements from multiple candidates
REPEAT until check script exits 0 or max rounds exhausted
```

### Key design principles

1. **Each initial candidate comes from a FRESH subagent with clean context.**
   Use `claude -p` (print mode) with `--max-turns 1` to spawn one-shot
   subagents. Do NOT generate all candidates from the same agent. The whole
   point is diverse independent hypotheses.

2. **Scoring is automated and binary.** The check script runs the game
   headless from a savestate that triggers the bug, reads game state, and
   returns exit 0 (fixed) or exit 1 (still broken) with a JSON score on
   stdout. The agent does not get to visually inspect and declare victory.

3. **Revision gets structured feedback.** The revision prompt includes the
   previous candidate's reasoning, its score, and the check script's output
   showing exactly what was wrong. Not just "try again" — the model sees
   "your fix changed X but Y was still wrong because Z."

4. **Different angles produce different hypotheses.** The angles listed under
   each bug above are different analytical framings. Agent 0 focuses on data
   flow, agent 1 on struct packing, agent 2 on initialization order, etc.
   This is what produces diversity.

### Implementation steps (do these in order)

**Step 1: Add a `dumpstate` control-file command to pfr_play.**

Look at how existing control-file commands (hotsave, statesave, quit, etc.)
are parsed. Add a `dumpstate` command that prints to stdout (or the trace log):

```
badge_flags=0x1F
badge_count=5
pokedex_caught=47
pokedex_seen=89
playtime_hours=12
playtime_minutes=34
enemy_party_count=3
enemy_party[0].species=25
enemy_party[1].species=130
enemy_party[2].species=0
enemy_party[3].species=0
enemy_party[4].species=0
enemy_party[5].species=0
player_x=15
player_y=22
map_id=3
```

Read these from the actual EWRAM structs (gSaveBlock1Ptr, gSaveBlock2Ptr,
gEnemyParty, etc.). This is the foundation everything else depends on.

**Step 2: Write check scripts.**

Each bug gets a bash script following this protocol:
- Takes `--pfr-play <path> --savestate <path> --max-frames <N>` args
- Boots pfr_play headless from the savestate
- Sends `dumpstate` via control file after enough frames
- Parses the output and checks against expected values
- Exits 0 if fixed, 1 if broken, 2 if crashed
- Last line of stdout is JSON: `{"score": 0.0-1.0, "details": "..."}`

For Bad Egg: check that enemy_party slots beyond enemy_party_count are species 0.
For Save Menu: check that badge_count, pokedex_caught, playtime are non-zero
when loading from a save that has progress.

**Step 3: Create savestates.**

Need save files positioned to trigger each bug:
- Before a trainer battle (one step away from triggering)
- With known badges/pokedex/playtime for save menu check

**Step 4: Build the orchestrator.**

A Python script (or bash) that:

```python
for round in range(max_rounds):
    if round == 0:
        # GENERATE: spawn N subagents in parallel via claude -p
        candidates = [spawn_subagent(bug, angle) for angle in angles]
    else:
        # REVISE: top K get individual revision, plus pooled synthesis
        candidates = revise_top_k(previous_candidates) + pool_synthesize(previous_candidates)

    for candidate in candidates:
        # Apply patch to source file (backup original first)
        apply_patch(candidate)
        # Build
        build_ok = cmake_build()
        if not build_ok:
            candidate.score = 0.0
            restore_original()
            continue
        # Run check script
        score = run_check_script(bug.check_script, bug.savestate)
        candidate.score = score
        # Restore original source
        restore_original()
        # Rebuild clean
        cmake_build()

    if any(c.score == 1.0 for c in candidates):
        # DONE — apply the winning fix permanently
        break
```

Each `spawn_subagent` call runs:
```bash
claude -p --output-format json --model claude-opus-4-6 --max-turns 1 <<PROMPT
You are fixing a bug in pokefirered-native...
[bug description, source file contents, analysis angle]
Output complete fixed file as JSON: {"source_file": "...", "fixed_content": "...", "reasoning": "..."}
PROMPT
```

The `--max-turns 1` and `claude -p` ensure each subagent is a fresh context
with no memory of other attempts.

**Step 5: Run it.**

```bash
python3 pfr-fixloop/fixloop.py --bug pfr-fixloop/bugs/trainer_bad_egg.toml \
    --candidates 5 --rounds 3
```

### What this prevents

The old failure mode: agent writes a fix → loads a different savestate where
the bug can't trigger → declares victory → writes "✔ fixed" to progress file
→ next session trusts the false claim.

The new mode: agent writes a fix → automated check script runs from the exact
savestate that triggers the bug → check fails → structured diff feeds back
exactly what's still wrong → revision round with that feedback → repeat until
the check actually passes. No human in the loop to fool, no savestate to swap.
