#include <string.h>

#include "global.h"

#include "constants/flags.h"
#include "constants/fame_checker.h"
#include "constants/game_stat.h"
#include "constants/global.h"
#include "constants/items.h"
#include "constants/maps.h"
#include "constants/trainer_tower.h"
#include "constants/vars.h"
#include "characters.h"
#include "event_data.h"
#include "item.h"
#include "item_menu.h"
#include "load_save.h"
#include "mail_data.h"
#include "overworld.h"
#include "pokemon.h"
#include "pokemon_jump.h"
#include "pokemon_storage_system.h"
#include "quest_log.h"
#include "script.h"
#include "trainer_tower.h"
#include "wonder_news.h"

#include "host_new_game_stubs.h"

struct PokemonStorage gPokemonStorage = {0};
struct PokemonStorage *gPokemonStoragePtr = NULL;
struct Pokemon gPlayerParty[PARTY_SIZE] = {0};
struct Pokemon gEnemyParty[PARTY_SIZE] = {0};
u8 gPlayerPartyCount = 0;
struct BagPocket gBagPockets[NUM_BAG_POCKETS] = {0};
struct BagStruct gBagMenuState = {0};

u16 gSpecialVar_0x8000 = 0;
u16 gSpecialVar_0x8001 = 0;
u16 gSpecialVar_0x8002 = 0;
u16 gSpecialVar_0x8003 = 0;
u16 gSpecialVar_0x8004 = 0;
u16 gSpecialVar_0x8005 = 0;
u16 gSpecialVar_0x8006 = 0;
u16 gSpecialVar_0x8007 = 0;
u16 gSpecialVar_0x8008 = 0;
u16 gSpecialVar_0x8009 = 0;
u16 gSpecialVar_0x800A = 0;
u16 gSpecialVar_0x800B = 0;
u16 gSpecialVar_Facing = 0;
u16 gSpecialVar_Result = 0;
u16 gSpecialVar_ItemId = 0;
u16 gSpecialVar_LastTalked = 0;
u16 gSpecialVar_MonBoxId = 0;
u16 gSpecialVar_MonBoxPos = 0;
u16 gSpecialVar_TextColor = 0;
u16 gSpecialVar_PrevTextColor = 0;
u16 gSpecialVar_0x8014 = 0;
u16 gLastQuestLogStoredFlagOrVarIdx = 0;
u8 gQuestLogState = 0;
u16 *gQuestLogRecordingPointer = NULL;
u16 *gQuestLogDefeatedWildMonRecord = NULL;

u32 gHostNewGameSetWarpDestinationCalls = 0;
u32 gHostNewGameWarpIntoMapCalls = 0;
u32 gHostNewGameRunScriptImmediatelyCalls = 0;
const u8 *gHostNewGameLastRunScript = NULL;
struct WarpData gHostNewGameWarpDestination = {0};
struct WarpData gLastUsedWarp = {0};

const u8 EventScript_ResetAllMapFlags[] = {0};

static u16 sWildEncounterSeed = 0;

#define NUM_SPECIAL_FLAGS  (SPECIAL_FLAGS_END - SPECIAL_FLAGS_START + 1)
#define SPECIAL_FLAGS_SIZE (NUM_SPECIAL_FLAGS / 8)

static u8 sHostSpecialFlags[SPECIAL_FLAGS_SIZE] = {0};
static u16 sHostTMCaseSelectedRow = 0;
static u16 sHostTMCaseScrollOffset = 0;
static u16 sHostBerryPouchSelectedRow = 0;
static u16 sHostBerryPouchScrollOffset = 0;

u16 *const gSpecialVars[] = {
    &gSpecialVar_0x8000,
    &gSpecialVar_0x8001,
    &gSpecialVar_0x8002,
    &gSpecialVar_0x8003,
    &gSpecialVar_0x8004,
    &gSpecialVar_0x8005,
    &gSpecialVar_0x8006,
    &gSpecialVar_0x8007,
    &gSpecialVar_0x8008,
    &gSpecialVar_0x8009,
    &gSpecialVar_0x800A,
    &gSpecialVar_0x800B,
    &gSpecialVar_Facing,
    &gSpecialVar_Result,
    &gSpecialVar_ItemId,
    &gSpecialVar_LastTalked,
    &gSpecialVar_MonBoxId,
    &gSpecialVar_MonBoxPos,
    &gSpecialVar_TextColor,
    &gSpecialVar_PrevTextColor,
    &gSpecialVar_0x8014,
};

void ClearMailStruct(struct Mail *mail)
{
    s32 i;

    for (i = 0; i < MAIL_WORDS_COUNT; i++)
        mail->words[i] = 0xFFFF;
    for (i = 0; i < PLAYER_NAME_LENGTH + 1; i++)
        mail->playerName[i] = EOS;
    for (i = 0; i < 4; i++)
        mail->trainerId[i] = 0;
    mail->species = SPECIES_BULBASAUR;
    mail->itemId = ITEM_NONE;
}

void ClearItemSlots(struct ItemSlot *slots, u8 capacity)
{
    u16 i;

    for (i = 0; i < capacity; i++)
    {
        slots[i].itemId = ITEM_NONE;
        slots[i].quantity = 0;
    }
}

void ClearPCItemSlots(void)
{
    u16 i;

    for (i = 0; i < PC_ITEMS_COUNT; i++)
    {
        gSaveBlock1Ptr->pcItems[i].itemId = ITEM_NONE;
        gSaveBlock1Ptr->pcItems[i].quantity = 0;
    }
}

u8 *StringCopy(u8 *dest, const u8 *src)
{
    while (*src != EOS && *src != 0)
        *dest++ = *src++;
    *dest = EOS;
    return dest;
}

void SetBagItemQuantity(u16 *ptr, u16 value)
{
    *ptr = value ^ gSaveBlock2Ptr->encryptionKey;
}

void SetPcItemQuantity(u16 *ptr, u16 value)
{
    *ptr = value;
}

void SetBagPocketsPointers(void)
{
    gBagPockets[POCKET_ITEMS - 1].itemSlots = gSaveBlock1Ptr->bagPocket_Items;
    gBagPockets[POCKET_ITEMS - 1].capacity = BAG_ITEMS_COUNT;
    gBagPockets[POCKET_KEY_ITEMS - 1].itemSlots = gSaveBlock1Ptr->bagPocket_KeyItems;
    gBagPockets[POCKET_KEY_ITEMS - 1].capacity = BAG_KEYITEMS_COUNT;
    gBagPockets[POCKET_POKE_BALLS - 1].itemSlots = gSaveBlock1Ptr->bagPocket_PokeBalls;
    gBagPockets[POCKET_POKE_BALLS - 1].capacity = BAG_POKEBALLS_COUNT;
    gBagPockets[POCKET_TM_CASE - 1].itemSlots = gSaveBlock1Ptr->bagPocket_TMHM;
    gBagPockets[POCKET_TM_CASE - 1].capacity = BAG_TMHM_COUNT;
    gBagPockets[POCKET_BERRY_POUCH - 1].itemSlots = gSaveBlock1Ptr->bagPocket_Berries;
    gBagPockets[POCKET_BERRY_POUCH - 1].capacity = BAG_BERRIES_COUNT;
}

void HostNewGameStubReset(void)
{
    memset(&gSaveBlock1, 0, sizeof(gSaveBlock1));
    memset(&gSaveBlock2, 0, sizeof(gSaveBlock2));
    memset(&gPokemonStorage, 0, sizeof(gPokemonStorage));
    memset(gPlayerParty, 0, sizeof(gPlayerParty));
    memset(gEnemyParty, 0, sizeof(gEnemyParty));
    memset(&gBagMenuState, 0, sizeof(gBagMenuState));
    memset(sHostSpecialFlags, 0, sizeof(sHostSpecialFlags));
    memset(&gLastUsedWarp, 0, sizeof(gLastUsedWarp));
    memset(&gHostNewGameWarpDestination, 0, sizeof(gHostNewGameWarpDestination));
    gSaveBlock1Ptr = &gSaveBlock1;
    gSaveBlock2Ptr = &gSaveBlock2;
    gPokemonStoragePtr = &gPokemonStorage;
    gPlayerPartyCount = 0;
    gHostNewGameSetWarpDestinationCalls = 0;
    gHostNewGameWarpIntoMapCalls = 0;
    gHostNewGameRunScriptImmediatelyCalls = 0;
    gHostNewGameLastRunScript = NULL;
    sWildEncounterSeed = 0;
    SetBagPocketsPointers();
    ResetSpecialVars();
}

void ClearSav2(void)
{
    CpuFill16(0, &gSaveBlock2, sizeof(gSaveBlock2));
}

void ClearSav1(void)
{
    CpuFill16(0, &gSaveBlock1, sizeof(gSaveBlock1));
}

u16 *GetVarPointer(u16 idx)
{
    if (idx < VARS_START)
        return NULL;
    if (idx < SPECIAL_VARS_START)
        return &gSaveBlock1Ptr->vars[idx - VARS_START];
    if (idx <= SPECIAL_VARS_END)
        return gSpecialVars[idx - SPECIAL_VARS_START];
    return NULL;
}

u16 VarGet(u16 idx)
{
    u16 *ptr = GetVarPointer(idx);

    if (ptr == NULL)
        return idx;
    return *ptr;
}

bool8 VarSet(u16 idx, u16 value)
{
    u16 *ptr = GetVarPointer(idx);

    if (ptr == NULL)
        return FALSE;
    *ptr = value;
    return TRUE;
}

u8 *GetFlagPointer(u16 idx)
{
    if (idx == 0)
        return NULL;
    if (idx < SPECIAL_FLAGS_START)
        return &gSaveBlock1Ptr->flags[idx / 8];
    if (idx <= SPECIAL_FLAGS_END)
        return &sHostSpecialFlags[(idx - SPECIAL_FLAGS_START) / 8];
    return NULL;
}

u8 FlagSet(u16 idx)
{
    u8 *ptr = GetFlagPointer(idx);

    if (ptr != NULL)
        *ptr |= 1 << (idx & 7);
    return FALSE;
}

u8 FlagClear(u16 idx)
{
    u8 *ptr = GetFlagPointer(idx);

    if (ptr != NULL)
        *ptr &= ~(1 << (idx & 7));
    return FALSE;
}

void InitEventData(void)
{
    memset(gSaveBlock1Ptr->flags, 0, sizeof(gSaveBlock1Ptr->flags));
    memset(gSaveBlock1Ptr->vars, 0, sizeof(gSaveBlock1Ptr->vars));
    memset(sHostSpecialFlags, 0, sizeof(sHostSpecialFlags));
}

void EnableNationalPokedex_RSE(void)
{
    u16 *ptr = GetVarPointer(VAR_0x403C);

    gSaveBlock2Ptr->pokedex.unused = 0xDA;
    if (ptr != NULL)
        *ptr = 0x0302;
    FlagSet(FLAG_0x838);
}

void ResetSpecialVars(void)
{
    gSpecialVar_0x8000 = 0;
    gSpecialVar_0x8001 = 0;
    gSpecialVar_0x8002 = 0;
    gSpecialVar_0x8003 = 0;
    gSpecialVar_0x8004 = 0;
    gSpecialVar_0x8005 = 0;
    gSpecialVar_0x8006 = 0;
    gSpecialVar_0x8007 = 0;
    gSpecialVar_0x8008 = 0;
    gSpecialVar_0x8009 = 0;
    gSpecialVar_0x800A = 0;
    gSpecialVar_0x800B = 0;
    gSpecialVar_Facing = 0;
    gSpecialVar_Result = 0;
    gSpecialVar_ItemId = 0;
    gSpecialVar_LastTalked = 0;
    gSpecialVar_MonBoxId = 0;
    gSpecialVar_MonBoxPos = 0;
    gSpecialVar_TextColor = 0;
    gSpecialVar_PrevTextColor = 0;
    gSpecialVar_0x8014 = 0;
}

void ClearMailData(void)
{
    u8 i;

    for (i = 0; i < MAIL_COUNT; i++)
        ClearMailStruct(&gSaveBlock1Ptr->mail[i]);
}

void ZeroPlayerPartyMons(void)
{
    memset(gPlayerParty, 0, sizeof(gPlayerParty));
    gPlayerPartyCount = 0;
}

void ZeroEnemyPartyMons(void)
{
    memset(gEnemyParty, 0, sizeof(gEnemyParty));
}

void ResetBagCursorPositions(void)
{
    u8 i;

    gBagMenuState.pocket = POCKET_ITEMS - 1;
    gBagMenuState.bagOpen = FALSE;
    for (i = 0; i < NUM_BAG_POCKETS_NO_CASES; i++)
    {
        gBagMenuState.itemsAbove[i] = 0;
        gBagMenuState.cursorPos[i] = 0;
    }
}

void ResetTMCaseCursorPos(void)
{
    sHostTMCaseSelectedRow = 0;
    sHostTMCaseScrollOffset = 0;
}

void BerryPouch_CursorResetToTop(void)
{
    sHostBerryPouchSelectedRow = 0;
    sHostBerryPouchScrollOffset = 0;
}

void ResetEncounterRateModifiers(void)
{
}

void SeedWildEncounterRng(u16 seed)
{
    sWildEncounterSeed = seed;
    ResetEncounterRateModifiers();
}

void ResetQuestLog(void)
{
    memset(gSaveBlock1Ptr->questLog, 0, sizeof(gSaveBlock1Ptr->questLog));
    gQuestLogState = 0;
    gQuestLogRecordingPointer = NULL;
    gQuestLogDefeatedWildMonRecord = NULL;
}

void QL_ResetEventStates(void)
{
}

void ResetDeferredLinkEvent(void)
{
}

void ClearBag(void)
{
    u16 i;

    for (i = 0; i < NUM_BAG_POCKETS; i++)
        ClearItemSlots(gBagPockets[i].itemSlots, gBagPockets[i].capacity);
}

void NewGameInitPCItems(void)
{
    ClearPCItemSlots();
    gSaveBlock1Ptr->pcItems[0].itemId = ITEM_POTION;
    SetPcItemQuantity(&gSaveBlock1Ptr->pcItems[0].quantity, 1);
}

void ClearEnigmaBerries(void)
{
    CpuFill16(0, &gSaveBlock1Ptr->enigmaBerry, sizeof(gSaveBlock1Ptr->enigmaBerry));
}

void UnionRoomChat_InitializeRegisteredTexts(void)
{
    static const u8 sHello[] = "HELLO";
    static const u8 sPokemon[] = "POKEMON";
    static const u8 sTrade[] = "TRADE";
    static const u8 sBattle[] = "BATTLE";
    static const u8 sLets[] = "LET'S";
    static const u8 sOk[] = "OK!";
    static const u8 sSorry[] = "SORRY";
    static const u8 sYay[] = "YAY!";
    static const u8 sThankYou[] = "THANK YOU";
    static const u8 sByeBye[] = "BYE-BYE!";

    StringCopy(gSaveBlock1Ptr->registeredTexts[0], sHello);
    StringCopy(gSaveBlock1Ptr->registeredTexts[1], sPokemon);
    StringCopy(gSaveBlock1Ptr->registeredTexts[2], sTrade);
    StringCopy(gSaveBlock1Ptr->registeredTexts[3], sBattle);
    StringCopy(gSaveBlock1Ptr->registeredTexts[4], sLets);
    StringCopy(gSaveBlock1Ptr->registeredTexts[5], sOk);
    StringCopy(gSaveBlock1Ptr->registeredTexts[6], sSorry);
    StringCopy(gSaveBlock1Ptr->registeredTexts[7], sYay);
    StringCopy(gSaveBlock1Ptr->registeredTexts[8], sThankYou);
    StringCopy(gSaveBlock1Ptr->registeredTexts[9], sByeBye);
}

void ResetPokemonJumpRecords(void)
{
    struct PokemonJumpRecords *records = &gSaveBlock2Ptr->pokeJump;

    records->jumpsInRow = 0;
    records->bestJumpScore = 0;
    records->excellentsInRow = 0;
    records->gamesWithMaxPlayers = 0;
    records->unused2 = 0;
    records->unused1 = 0;
}

void ClearPlayerLinkBattleRecords(void)
{
    memset(&gSaveBlock2Ptr->linkBattleRecords, 0, sizeof(gSaveBlock2Ptr->linkBattleRecords));
    gSaveBlock1Ptr->gameStats[GAME_STAT_LINK_BATTLE_WINS] = 0;
    gSaveBlock1Ptr->gameStats[GAME_STAT_LINK_BATTLE_LOSSES] = 0;
    gSaveBlock1Ptr->gameStats[GAME_STAT_LINK_BATTLE_DRAWS] = 0;
}

void ResetFameChecker(void)
{
    u8 i;

    for (i = 0; i < NUM_FAMECHECKER_PERSONS; i++)
    {
        gSaveBlock1Ptr->fameChecker[i].pickState = FCPICKSTATE_NO_DRAW;
        gSaveBlock1Ptr->fameChecker[i].flavorTextFlags = 0;
        gSaveBlock1Ptr->fameChecker[i].unk_0_E = 0;
    }
    gSaveBlock1Ptr->fameChecker[FAMECHECKER_OAK].pickState = FCPICKSTATE_COLORED;
}

void ResetGameStats(void)
{
    int i;

    for (i = 0; i < NUM_GAME_STATS; i++)
        gSaveBlock1Ptr->gameStats[i] = 0;
}

void ResetPokemonStorageSystem(void)
{
    CpuFill16(0, &gPokemonStorage, sizeof(gPokemonStorage));
    gPokemonStorage.currentBox = 0;
    gPokemonStoragePtr = &gPokemonStorage;
}

void RunScriptImmediately(const u8 *ptr)
{
    gHostNewGameRunScriptImmediatelyCalls++;
    gHostNewGameLastRunScript = ptr;
}

void SetWarpDestination(s8 mapGroup, s8 mapNum, s8 warpId, s8 x, s8 y)
{
    gHostNewGameSetWarpDestinationCalls++;
    gHostNewGameWarpDestination.mapGroup = mapGroup;
    gHostNewGameWarpDestination.mapNum = mapNum;
    gHostNewGameWarpDestination.warpId = warpId;
    gHostNewGameWarpDestination.x = x;
    gHostNewGameWarpDestination.y = y;
}

void WarpIntoMap(void)
{
    gHostNewGameWarpIntoMapCalls++;
    gLastUsedWarp = gSaveBlock1Ptr->location;
    gSaveBlock1Ptr->location = gHostNewGameWarpDestination;
    gSaveBlock1Ptr->pos.x = gHostNewGameWarpDestination.x;
    gSaveBlock1Ptr->pos.y = gHostNewGameWarpDestination.y;
}

void ResetTrainerTowerResults(void)
{
    s32 i;

    for (i = 0; i < NUM_TOWER_CHALLENGE_TYPES; i++)
        gSaveBlock1Ptr->trainerTower[i].bestTime = TRAINER_TOWER_MAX_TIME ^ gSaveBlock2Ptr->encryptionKey;
}

void WonderNews_Reset(void)
{
    struct WonderNewsMetadata *data = &gSaveBlock1Ptr->mysteryGift.newsMetadata;

    data->newsType = WONDER_NEWS_NONE;
    data->sentRewardCounter = 0;
    data->rewardCounter = 0;
    data->berry = 0;
    VarSet(VAR_WONDER_NEWS_STEP_COUNTER, 0);
}
