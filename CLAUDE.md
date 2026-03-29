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
