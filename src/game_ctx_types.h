#ifndef GAME_CTX_TYPES_H
#define GAME_CTX_TYPES_H

/*
 * game_ctx_types.h -- Type/constant definitions for GameCtx struct members
 *
 * Provides file-local struct definitions and constants that are needed by
 * game_ctx.h but are originally defined only in upstream .c files.
 *
 * This file is hand-maintained. If gen_game_ctx.py discovers new EWRAM_DATA
 * variables with file-local types, their definitions must be added here.
 */

/* --- File-local constants from upstream .c files --- */

#ifndef ANIM_SPRITE_INDEX_COUNT
#define ANIM_SPRITE_INDEX_COUNT 8  /* battle_anim.c */
#endif

#ifndef OAM_MATRIX_COUNT
#define OAM_MATRIX_COUNT 32  /* sprite.c */
#endif

#ifndef NUM_PALETTE_STRUCTS
#define NUM_PALETTE_STRUCTS 16  /* palette.c */
#endif

#ifndef PICS_COUNT
#define PICS_COUNT 8  /* trainer_pokemon_sprites.c */
#endif

#ifndef SAVEBLOCK_MOVE_RANGE
#define SAVEBLOCK_MOVE_RANGE 128  /* load_save.c */
#endif

#ifndef NUM_CLOUDS
#define NUM_CLOUDS 2  /* dodrio_berry_picking.c */
#endif

#ifndef NUM_BERRY_TYPES
#define NUM_BERRY_TYPES 4  /* dodrio_berry_picking.c */
#endif

/* MAX_STARTMENU_ITEMS, SPR_COUNT, NUM_MENU_TEXT_SPRITES: enum values in .c files.
 * gen_game_ctx.py resolves these to literal values in game_ctx.h field declarations.
 * Do NOT define them here -- they conflict with enums in the source files. */

/* SPECIAL_FLAGS_SIZE: from event_data.c */
#ifndef SPECIAL_FLAGS_SIZE
#define SPECIAL_FLAGS_SIZE ((SPECIAL_FLAGS_END - SPECIAL_FLAGS_START + 7) / 8)
#endif

/* SCRIPT_BUFFER_SIZE: resolved to literal 128 in game_ctx.h via FILE_LOCAL_CONSTANTS.
 * Do NOT define it here -- conflicts with the #define in quest_log.c. */

/* VIRTUAL_MAP_SIZE: from fieldmap.h -> MAX_MAP_DATA_SIZE */
/* Should already be available via fieldmap.h include */

/* WIN_COUNT: enum values in multiple files (quest_log.c = 4, naming_screen.c, etc.)
 * gen_game_ctx.py resolves this to a literal value in game_ctx.h field declarations.
 * Do NOT define WIN_COUNT here -- it conflicts with the enum in those files. */

/* MAX_SPRITE_COPY_REQUESTS: from sprite.c */
#ifndef MAX_SPRITE_COPY_REQUESTS
#define MAX_SPRITE_COPY_REQUESTS 64
#endif

/* --- File-local struct definitions from upstream .c files --- */

/* From teachy_tv.c */
struct TeachyTvCtrlBlk
{
    MainCallback callback;
    u8 mode;
    u8 whichScript;
    u16 scrollOffset;
    u16 selectedRow;
};

/* From wild_encounter.c */
struct WildEncounterData
{
    u32 rngState;
    u16 prevMetatileBehavior;
    u16 encounterRateBuff;
    u8 stepsSinceLastEncounter;
    u8 abilityEffect;
    u16 leadMonHeldItem;
};

/* From fieldmap.c */
struct ConnectionFlags
{
    u8 south:1;
    u8 north:1;
    u8 west:1;
    u8 east:1;
};

/* From help_system_util.c */
struct HelpSystemVideoState
{
    MainCallback savedVblankCb;
    MainCallback savedHblankCb;
    u16 savedDispCnt;
    u16 savedBg0Cnt;
    u16 savedBg0Hofs;
    u16 savedBg0Vofs;
    u16 savedBldCnt;
    u8 savedTextColor[3];
    u8 state;
};

/* From link_rfu_2.c */
struct RfuDebug
{
    u8 unused0[6];
    u16 recvCount;
    u8 unused1[6];
    vu8 unkFlag;
    bool8 childJoined;
    u8 unused2[84];
    u16 blockSendFailures;
    u8 unused3[29];
    u8 blockSendTime;
    u8 unused4[88];
};

/* From load_save.c */
struct LoadedSaveData
{
    struct ItemSlot items[BAG_ITEMS_COUNT];
    struct ItemSlot keyItems[BAG_KEYITEMS_COUNT];
    struct ItemSlot pokeBalls[BAG_POKEBALLS_COUNT];
    struct ItemSlot TMsHMs[BAG_TMHM_COUNT];
    struct ItemSlot berries[BAG_BERRIES_COUNT];
    struct Mail mail[MAIL_COUNT];
};

/* From menu.c */
struct Menu
{
    u8 left;
    u8 top;
    s8 cursorPos;
    s8 minCursorPos;
    s8 maxCursorPos;
    u8 windowId;
    u8 fontId;
    u8 optionWidth;
    u8 optionHeight;
    u8 columns;
    u8 rows;
    bool8 APressMuted;
};

/* From overworld.c */
struct InitialPlayerAvatarState
{
    u8 transitionFlags;
    u8 direction;
    bool8 hasDirectionSet;
};

/* From quest_log.c */
struct PlaybackControl
{
    u8 state:4;
    u8 playingEvent:2;
    u8 endMode:2;
    u8 cursor;
    u8 timer;
    u8 overlapTimer;
};

/* From quest_log_events.c */
struct DeferredLinkEvent
{
    u16 id;
    u16 ALIGNED(4) data[14];
};

/* From shop.c */
struct ShopData
{
    void (*callback)(void);
    const u16 *itemList;
    u32 itemPrice;
    u16 selectedRow;
    u16 scrollOffset;
    u16 itemCount;
    u16 itemsShowed;
    u16 maxQuantity;
    u16 martType:4;
    u16 fontId:5;
    u16 itemSlot:2;
    u16 unk16_11:5;
    u16 unk18;
};

/* From sprite.c */
struct SpriteCopyRequest
{
    const u8 *src;
    u8 *dest;
    u16 size;
};

/* From list_menu.c */
struct MysteryGiftLinkMenuStruct
{
    u32 currItemId;
    u8 state;
    u8 windowId;
    u8 listTaskId;
};

/* From berry_pouch.c */
struct BerryPouchStruct_203F370
{
    void (*savedCallback)(void);
    u8 type;
    u8 allowSelect;
    u8 unused_06;
    u16 listMenuSelectedRow;
    u16 listMenuScrollOffset;
};

/* From list_menu.c */
struct ListMenuState
{
    u8 cursorPos;
    u8 itemsAbove;
};

/* From item_pc.c */
struct ItemPcStaticResources
{
    MainCallback savedCallback;
    u16 scroll;
    u16 row;
    u8 initialized;
};

/* From palette.c */
struct PaletteStructTemplate
{
    u16 id;
    u16 *src;
    bool16 pst_field_8_0:1;
    u16 unused:9;
    u16 size:5;
    u8 time1;
    u8 srcCount:5;
    u8 state:3;
    u8 time2;
};

/* From palette.c */
struct PaletteStruct
{
    const struct PaletteStructTemplate *template;
    bool32 active:1;
    bool32 flag:1;
    u32 baseDestOffset:9;
    u16 destOffset:10;
    u16 srcIndex:7;
    u8 countdown1;
    u8 countdown2;
};

/* From trainer_pokemon_sprites.c */
struct PicData
{
    u8 *frames;
    struct SpriteFrameImage *images;
    u16 paletteTag;
    u8 spriteId;
    u8 active;
};

#endif /* GAME_CTX_TYPES_H */
