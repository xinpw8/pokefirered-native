# pokefirered-native Session Log — 2025-03-10

## Verification Results

### 1. Overworld Rendering ✅ VERIFIED
- CB2_NewGame reached at frame 6269
- CB1_Overworld + CB2_Overworld installed at frame 6270
- First non-black framebuffer at frame 6272 (fade-in)
- Player's room (PalletTown_PlayersHouse_2F) renders pixel-perfect
- All 282 upstream .c files compile and link with zero unresolved symbols

### 2. Player Movement ✅ VERIFIED
- D-pad scripted input reaches gMain.newKeys correctly
- REG_KEYINPUT polarity handled correctly (active-low write, XOR read)
- Player sprite moves in response to D-pad input
- Movement speed: ~16 frames per tile (standard GBA rate)
- Player can walk in all 4 directions

### 3. Map Transitions ✅ VERIFIED
- **Stair warp (2F→1F)**: CB2_LoadMap → CB2_DoChangeMap → CB2_LoadMap2 sequence
  - Takes 3 frames to complete (instant map load)
  - Player correctly appears at 1F stair position (10,2)
  - Tested in run 8: stair warp at frame 7063
- **Door warp (1F→Pallet Town)**: Same CB2_LoadMap sequence
  - Tested in run 8: door warp at frame 7318
  - Player correctly appears at Pallet Town entrance (6,7)
  - Town name banner "PALLET" displays correctly
- **Pallet Town rendering**: Trees, houses, fences, flowers all pixel-perfect
- Both indoor→indoor (stairs) and indoor→outdoor (door) warps functional

### 4. NPC/Sign Interactions ✅ VERIFIED  
- "It's a posted notice..." text box displayed when reading sign on 1F (frame 6800, run 5)
- Text rendering works with proper font and message box
- Mom NPC sprite visible and correctly positioned at table on 1F
- Oak event script triggers when trying to leave Pallet Town (expected behavior)

### 5. Menu Screens ✅ VERIFIED (from earlier session)
- Start menu: BAG, RED, SAVE, OPTION, EXIT all render
- Bag menu opens and returns to field
- Overworld stable through 20000+ frames with menu interaction

### 6. Wild Encounters / Battle System — NOT YET TESTED
- Cannot reach tall grass because Oak's "don't go into grass" event triggers
- This is CORRECT game behavior — need to complete Oak lab sequence first
- Oak's script locks player controls; requires A-button presses to dismiss dialogue
- Battle system testing requires: Oak lab → choose starter → walk into Route 1 grass

## Stability
- Game runs stable from frame 0 through 20000+ with no crashes or soft-resets
- CB2_Overworld maintained continuously after overworld load
- No callback anomalies detected in any run

## Technical Notes
- Game runs at ~1000 frames/sec (wall clock) on DGX Spark aarch64
- Trace logging via pfr_play_trace.log captures callback changes and input events
- Frame captures saved as PPM to build/pfr_play_frames/
- ApplyScriptedInput writes REG_KEYINPUT (active-low), PlayReadKeys reads via XOR

## Next Steps
1. Script the Oak lab sequence (choose starter Pokémon) to enable battle testing
2. Walk into Route 1 tall grass to trigger wild encounter
3. Verify battle system renders and processes correctly
4. Test save/load functionality
