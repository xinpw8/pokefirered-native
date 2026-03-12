#ifndef POKEFIRERED_NATIVE_PFR_FIRERED_STATE_H
#define POKEFIRERED_NATIVE_PFR_FIRERED_STATE_H

#include "global.h"
#include "constants/pokemon.h"

#define PFR_RL_MAGIC 0x50524652u

struct PfrRlPartyMonState
{
    u16 species;
    u16 heldItem;
    u16 hp;
    u16 maxHp;
    u32 status;
    u32 exp;
    u8 level;
    u8 friendship;
    u8 types[2];
    u16 moves[MAX_MON_MOVES];
    u8 pp[MAX_MON_MOVES];
};

struct __attribute__((packed)) PfrRlPacket
{
    u32 magic;
    u32 frame;
    u32 heldButtons;
    s16 x;
    s16 y;
    u8 mapGroup;
    u8 mapNum;
    u16 mapLayoutId;
    u8 facingDirection;
    u8 playerAvatarFlags;
    u8 playerRunningState;
    u8 playerTileTransitionState;
    u16 registeredItem;
    u16 trainerRematchStepCounter;
    u32 money;
    u16 moneyRemainder;
    u16 coins;
    u8 badges;
    u8 partyCount;
    u8 playerGender;
    u8 optionsButtonMode;
    u8 optionsBattleStyle;
    u8 optionsBattleSceneOff;
    u8 optionsSound;
    u16 pokedexSeen;
    u16 pokedexCaught;
    u16 totalPartyLevel;
    u16 playTimeHours;
    u8 playTimeMinutes;
    u8 playTimeSeconds;
    u8 playTimeVBlanks;
    u8 inOverworld;
    u8 inBattle;
    u32 steps;
    u32 totalBattles;
    u32 wildBattles;
    u32 trainerBattles;
    u32 pokemonCaptures;
    struct PfrRlPartyMonState party[PARTY_SIZE];
};

void PfrRlCapturePacket(struct PfrRlPacket *packet, u32 frame, u16 heldButtons, bool8 inOverworld, bool8 inBattle);

#endif /* POKEFIRERED_NATIVE_PFR_FIRERED_STATE_H */
