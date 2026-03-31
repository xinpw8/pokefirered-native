/*
 * game_ctx_header_fixups.h
 *
 * Redefines upstream header macros that use bare EWRAM_DATA global names
 * to use g_ctx-> instead. Included ONLY by GameCtx-transformed files
 * (which use g_ctx-> directly and don't have game_ctx_macros.h).
 *
 * Files with game_ctx_macros.h must NOT include this header, as their
 * token-level macro redirects handle header macro expansions correctly.
 */
#ifndef GAME_CTX_HEADER_FIXUPS_H
#define GAME_CTX_HEADER_FIXUPS_H

#ifdef HOST_NATIVE

/* ============================================================
 * battle.h macros
 * ============================================================ */

#ifdef GET_BATTLER_POSITION
#undef GET_BATTLER_POSITION
#define GET_BATTLER_POSITION(battler) ((g_ctx->gBattlerPositions[battler]))
#endif

#ifdef GET_BATTLER_SIDE
#undef GET_BATTLER_SIDE
#define GET_BATTLER_SIDE(battler) (GET_BATTLER_POSITION(battler) & BIT_SIDE)
#endif

#ifdef GET_BATTLER_SIDE2
#undef GET_BATTLER_SIDE2
#define GET_BATTLER_SIDE2(battler) ((GET_BATTLER_POSITION(battler) & BIT_SIDE))
#endif

#ifdef MOVE_IS_PERMANENT
#undef MOVE_IS_PERMANENT
#define MOVE_IS_PERMANENT(battler, moveSlot) \
    (!(g_ctx->gBattleMons[battler].status2 & STATUS2_TRANSFORMED) \
     && !(g_ctx->gDisableStructs[battler].mimickedMoves & gBitTable[moveSlot]))
#endif

#ifdef GET_MOVE_TYPE
#undef GET_MOVE_TYPE
#define GET_MOVE_TYPE(move, typeArg)                                          \
    {                                                                         \
        if (g_ctx->gBattleStruct->dynamicMoveType)                           \
            typeArg = g_ctx->gBattleStruct->dynamicMoveType & DYNAMIC_TYPE_MASK; \
        else                                                                  \
            typeArg = gBattleMoves[move].type;                               \
    }
#endif

#ifdef TARGET_TURN_DAMAGED
#undef TARGET_TURN_DAMAGED
#define TARGET_TURN_DAMAGED \
    ((g_ctx->gSpecialStatuses[g_ctx->gBattlerTarget].physicalDmg != 0 \
   || g_ctx->gSpecialStatuses[g_ctx->gBattlerTarget].specialDmg != 0))
#endif

#ifdef IS_BATTLER_OF_TYPE
#undef IS_BATTLER_OF_TYPE
#define IS_BATTLER_OF_TYPE(battlerId, type) \
    ((g_ctx->gBattleMons[battlerId].type1 == type || g_ctx->gBattleMons[battlerId].type2 == type))
#endif

#ifdef SET_BATTLER_TYPE
#undef SET_BATTLER_TYPE
#define SET_BATTLER_TYPE(battlerId, type)            \
    {                                                \
        g_ctx->gBattleMons[battlerId].type1 = type; \
        g_ctx->gBattleMons[battlerId].type2 = type; \
    }
#endif

#ifdef SET_STATCHANGER
#undef SET_STATCHANGER
#define SET_STATCHANGER(statId, stage, goesDown) \
    (g_ctx->gBattleScripting.statChanger = (statId) + ((stage) << 4) + ((goesDown) << 7))
#endif

/* ============================================================
 * constants/quest_log.h macros
 * ============================================================ */

#ifdef QL_IS_PLAYBACK_STATE
#undef QL_IS_PLAYBACK_STATE
#define QL_IS_PLAYBACK_STATE \
    (g_ctx->gQuestLogState == QL_STATE_PLAYBACK || g_ctx->gQuestLogState == QL_STATE_PLAYBACK_LAST)
#endif

/* ============================================================
 * palette.h macros
 * ============================================================ */

#ifdef gPaletteFade_selectedPalettes
#undef gPaletteFade_selectedPalettes
#define gPaletteFade_selectedPalettes (g_ctx->gPaletteFade.multipurpose1)
#endif

#ifdef gPaletteFade_blendCnt
#undef gPaletteFade_blendCnt
#define gPaletteFade_blendCnt (g_ctx->gPaletteFade.multipurpose1)
#endif

#ifdef gPaletteFade_delay
#undef gPaletteFade_delay
#define gPaletteFade_delay (g_ctx->gPaletteFade.multipurpose2)
#endif

#ifdef gPaletteFade_submode
#undef gPaletteFade_submode
#define gPaletteFade_submode (g_ctx->gPaletteFade.multipurpose2)
#endif

#endif /* HOST_NATIVE */
#endif /* GAME_CTX_HEADER_FIXUPS_H */
