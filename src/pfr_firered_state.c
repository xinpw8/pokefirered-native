#include <string.h>

#include "global.h"
#include "constants/flags.h"
#include "constants/game_stat.h"
#include "event_data.h"
#include "field_player_avatar.h"
#include "global.fieldmap.h"
#include "main.h"
#include "overworld.h"
#include "pokedex.h"
#include "pokemon.h"
#include "save.h"

#include "pfr_firered_state.h"

static u8 CountBadges(void)
{
    return FlagGet(FLAG_BADGE01_GET)
         + FlagGet(FLAG_BADGE02_GET)
         + FlagGet(FLAG_BADGE03_GET)
         + FlagGet(FLAG_BADGE04_GET)
         + FlagGet(FLAG_BADGE05_GET)
         + FlagGet(FLAG_BADGE06_GET)
         + FlagGet(FLAG_BADGE07_GET)
         + FlagGet(FLAG_BADGE08_GET);
}

static u16 TotalPartyLevel(void)
{
    u16 total = 0;
    u8 i;

    for (i = 0; i < gPlayerPartyCount && i < PARTY_SIZE; i++)
        total += GetMonData(&gPlayerParty[i], MON_DATA_LEVEL, NULL);

    return total;
}

static u8 GetPlayerFacingDirectionSafe(void)
{
    if (gPlayerAvatar.objectEventId >= OBJECT_EVENTS_COUNT)
        return DIR_NONE;

    return gObjectEvents[gPlayerAvatar.objectEventId].facingDirection;
}

void PfrRlCapturePacket(struct PfrRlPacket *packet, u32 frame, u16 heldButtons, bool8 inOverworld, bool8 inBattle)
{
    static const s32 sMoveDataIds[MAX_MON_MOVES] =
    {
        MON_DATA_MOVE1,
        MON_DATA_MOVE2,
        MON_DATA_MOVE3,
        MON_DATA_MOVE4,
    };
    static const s32 sPpDataIds[MAX_MON_MOVES] =
    {
        MON_DATA_PP1,
        MON_DATA_PP2,
        MON_DATA_PP3,
        MON_DATA_PP4,
    };
    u8 i;
    u8 partyCount = gPlayerPartyCount;

    memset(packet, 0, sizeof(*packet));
    packet->magic = PFR_RL_MAGIC;
    packet->frame = frame;
    packet->heldButtons = heldButtons;
    packet->x = gSaveBlock1Ptr->pos.x;
    packet->y = gSaveBlock1Ptr->pos.y;
    packet->mapGroup = gSaveBlock1Ptr->location.mapGroup;
    packet->mapNum = gSaveBlock1Ptr->location.mapNum;
    packet->mapLayoutId = gSaveBlock1Ptr->mapLayoutId;
    packet->facingDirection = GetPlayerFacingDirectionSafe();
    packet->playerAvatarFlags = gPlayerAvatar.flags;
    packet->playerRunningState = gPlayerAvatar.runningState;
    packet->playerTileTransitionState = gPlayerAvatar.tileTransitionState;
    packet->registeredItem = gSaveBlock1Ptr->registeredItem;
    packet->trainerRematchStepCounter = gSaveBlock1Ptr->trainerRematchStepCounter;
    packet->money = gSaveBlock1Ptr->money;
    packet->moneyRemainder = (u16)(gSaveBlock1Ptr->money % 1000u);
    packet->coins = gSaveBlock1Ptr->coins;
    packet->badges = CountBadges();
    packet->partyCount = partyCount;
    packet->playerGender = gSaveBlock2Ptr->playerGender;
    packet->optionsButtonMode = gSaveBlock2Ptr->optionsButtonMode;
    packet->optionsBattleStyle = gSaveBlock2Ptr->optionsBattleStyle;
    packet->optionsBattleSceneOff = gSaveBlock2Ptr->optionsBattleSceneOff;
    packet->optionsSound = gSaveBlock2Ptr->optionsSound;
    packet->pokedexSeen = GetNationalPokedexCount(FLAG_GET_SEEN);
    packet->pokedexCaught = GetNationalPokedexCount(FLAG_GET_CAUGHT);
    packet->totalPartyLevel = TotalPartyLevel();
    packet->playTimeHours = gSaveBlock2Ptr->playTimeHours;
    packet->playTimeMinutes = gSaveBlock2Ptr->playTimeMinutes;
    packet->playTimeSeconds = gSaveBlock2Ptr->playTimeSeconds;
    packet->playTimeVBlanks = gSaveBlock2Ptr->playTimeVBlanks;
    packet->inOverworld = inOverworld;
    packet->inBattle = inBattle;
    packet->steps = GetGameStat(GAME_STAT_STEPS);
    packet->totalBattles = GetGameStat(GAME_STAT_TOTAL_BATTLES);
    packet->wildBattles = GetGameStat(GAME_STAT_WILD_BATTLES);
    packet->trainerBattles = GetGameStat(GAME_STAT_TRAINER_BATTLES);
    packet->pokemonCaptures = GetGameStat(GAME_STAT_POKEMON_CAPTURES);

    for (i = 0; i < partyCount && i < PARTY_SIZE; i++)
    {
        struct PfrRlPartyMonState mon;
        u8 moveIndex;
        u16 species;

        memset(&mon, 0, sizeof(mon));
        species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES, NULL);
        mon.species = species;
        mon.heldItem = GetMonData(&gPlayerParty[i], MON_DATA_HELD_ITEM, NULL);
        mon.hp = GetMonData(&gPlayerParty[i], MON_DATA_HP, NULL);
        mon.maxHp = GetMonData(&gPlayerParty[i], MON_DATA_MAX_HP, NULL);
        mon.status = GetMonData(&gPlayerParty[i], MON_DATA_STATUS, NULL);
        mon.exp = GetMonData(&gPlayerParty[i], MON_DATA_EXP, NULL);
        mon.level = GetMonData(&gPlayerParty[i], MON_DATA_LEVEL, NULL);
        mon.friendship = GetMonData(&gPlayerParty[i], MON_DATA_FRIENDSHIP, NULL);
        mon.types[0] = gSpeciesInfo[species].types[0];
        mon.types[1] = gSpeciesInfo[species].types[1];

        for (moveIndex = 0; moveIndex < MAX_MON_MOVES; moveIndex++)
        {
            mon.moves[moveIndex] = GetMonData(&gPlayerParty[i], sMoveDataIds[moveIndex], NULL);
            mon.pp[moveIndex] = GetMonData(&gPlayerParty[i], sPpDataIds[moveIndex], NULL);
        }

        memcpy(&packet->party[i], &mon, sizeof(mon));
    }
}
