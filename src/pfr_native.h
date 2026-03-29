#ifndef PFR_NATIVE_H
#define PFR_NATIVE_H

#include <stddef.h>
#include <stdint.h>

#include "pfr_native_data.h"
#include "pfr_battle.h"
#include "pfr_item_tables.h"

#define PFR_NATIVE_SCREEN_WIDTH 240
#define PFR_NATIVE_SCREEN_HEIGHT 160
#define PFR_NATIVE_PLAYER_NAME_LENGTH 8
#define PFR_NATIVE_INVALID_OBJECT 0xFF

/* Bag limits */
#define PFR_BAG_POCKET_COUNT   3   /* Items, Key Items, Poke Balls */
#define PFR_BAG_ITEMS_MAX      20
#define PFR_BAG_KEY_ITEMS_MAX  20
#define PFR_BAG_BALLS_MAX      16
#define PFR_BAG_SLOT_MAX       20  /* largest pocket */

/* PC box storage */
#define PFR_PC_BOX_SIZE        30
#define PFR_PC_BOX_COUNT       1   /* 1 box for now */

/* Shop limits */
#define PFR_SHOP_MAX_ITEMS     16

typedef enum {
    PFR_NATIVE_ACTION_NONE = 0,
    PFR_NATIVE_ACTION_UP = 1,
    PFR_NATIVE_ACTION_DOWN = 2,
    PFR_NATIVE_ACTION_LEFT = 3,
    PFR_NATIVE_ACTION_RIGHT = 4,
    PFR_NATIVE_ACTION_A = 5,
    PFR_NATIVE_ACTION_B = 6,
    PFR_NATIVE_ACTION_START = 7,
    PFR_NATIVE_ACTION_SELECT = 8,
} PfrNativeAction;

typedef enum {
    PFR_NATIVE_DIR_NONE = 0,
    PFR_NATIVE_DIR_NORTH = 1,
    PFR_NATIVE_DIR_SOUTH = 2,
    PFR_NATIVE_DIR_WEST = 3,
    PFR_NATIVE_DIR_EAST = 4,
} PfrNativeDirection;

typedef enum {
    PFR_NATIVE_MODE_OVERWORLD = 0,
    PFR_NATIVE_MODE_DIALOG = 1,
    PFR_NATIVE_MODE_BATTLE = 2,
    PFR_NATIVE_MODE_START_MENU = 3,
    PFR_NATIVE_MODE_PARTY_VIEW = 4,
    PFR_NATIVE_MODE_POKEMON_SUMMARY = 5,
    PFR_NATIVE_MODE_BAG = 6,
    PFR_NATIVE_MODE_POKEDEX = 7,
    PFR_NATIVE_MODE_TRAINER_CARD = 8,
    PFR_NATIVE_MODE_SAVE_CONFIRM = 9,
    PFR_NATIVE_MODE_OPTIONS = 10,
    PFR_NATIVE_MODE_PARTY_SUBMENU = 11,  /* SUMMARY / SWITCH / CANCEL */
    PFR_NATIVE_MODE_FORCE_SWITCH = 12,   /* Must pick live mon after faint */
    PFR_NATIVE_MODE_SHOP = 13,
    PFR_NATIVE_MODE_BAG_USE_TARGET = 14, /* Select party mon to use item on */
    PFR_NATIVE_MODE_BAG_SUBMENU = 15,    /* USE / TOSS / CANCEL for bag item */
    PFR_NATIVE_MODE_PC = 16,             /* Bill's PC: DEPOSIT / WITHDRAW */
    PFR_NATIVE_MODE_PC_BOX = 17,         /* Viewing box contents */
    PFR_NATIVE_MODE_BATTLE_BAG = 18,     /* Item selection during battle */
} PfrNativeMode;

typedef enum {
    PFR_NATIVE_EVENT_NONE = 0,
    PFR_NATIVE_EVENT_BLOCKED = 1,
    PFR_NATIVE_EVENT_MOVED = 2,
    PFR_NATIVE_EVENT_DIALOG_OPENED = 3,
    PFR_NATIVE_EVENT_DIALOG_ADVANCED = 4,
    PFR_NATIVE_EVENT_DIALOG_CLOSED = 5,
    PFR_NATIVE_EVENT_WARPED = 6,
    PFR_NATIVE_EVENT_UNSUPPORTED_WARP = 7,
} PfrNativeEvent;

typedef enum {
    PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F = 0,
    PFR_NATIVE_BOOTSTRAP_PALLET_TOWN = 1,
} PfrNativeBootstrapId;

/* Bag slot: item ID + quantity */
typedef struct {
    uint16_t item_id;
    uint16_t count;
} PfrBagSlot;

/* Bag: 3 pockets (Items, Key Items, Poke Balls) */
typedef struct {
    PfrBagSlot items[PFR_BAG_ITEMS_MAX];
    PfrBagSlot key_items[PFR_BAG_KEY_ITEMS_MAX];
    PfrBagSlot balls[PFR_BAG_BALLS_MAX];
    uint8_t item_count;       /* # filled slots in items pocket */
    uint8_t key_item_count;
    uint8_t ball_count;
    uint8_t pocket;           /* current pocket (0=items, 1=key, 2=balls) */
    uint8_t cursor[3];        /* cursor per pocket */
    uint8_t scroll[3];        /* scroll offset per pocket */
} PfrBag;

/* Shop state */
typedef struct {
    uint16_t inventory[PFR_SHOP_MAX_ITEMS]; /* item IDs for sale */
    uint8_t inv_count;
    uint8_t menu;      /* 0=BUY/SELL/CANCEL, 1=buy list, 2=sell list */
    uint8_t cursor;
    uint8_t scroll;
    uint8_t quantity;  /* quantity being bought/sold */
    uint8_t _pad;
    uint16_t selected_item; /* item being bought/sold */
} PfrShopState;

typedef struct {
    uint8_t active;
    uint8_t local_id;
    uint8_t graphics_id;
    uint8_t facing;
    uint8_t default_facing;
    uint8_t movement_type;
    uint8_t script_id;
    uint8_t reserved;
    int16_t x;
    int16_t y;
} PfrNativeObjectState;

typedef struct {
    uint32_t rng_value;
    int16_t player_x;
    int16_t player_y;
    char player_name[PFR_NATIVE_PLAYER_NAME_LENGTH];
    PfrNativeMapId current_map;
    uint8_t mode;
    uint8_t player_direction;
    uint8_t player_gender;
    uint8_t active_dialog_id;
    uint8_t dialog_page_index;
    uint8_t queued_dialog_id;
    uint8_t dialog_restore_object_index;
    uint8_t dialog_restore_object_facing;
    uint8_t party_count;    /* Pokemon in party (0-6) */
    uint8_t starter_mon;    /* 0=bulba, 1=squirt, 2=char, 0xFF=none */
    uint8_t object_count;
    uint8_t start_menu_cursor;   /* 0-5 when start menu open */
    uint8_t party_view_cursor;   /* 0-5 when viewing party */
    uint8_t summary_pokemon_idx; /* which party slot we're viewing (0-5) */
    uint8_t summary_page;        /* 0=stats, 1=moves */
    uint8_t battle_end_phase;    /* 0=not ending, 1+=message sequence step */
    uint16_t last_heal_map;      /* map to return to on whiteout */
    int16_t last_heal_x;         /* x coord for whiteout return */
    int16_t last_heal_y;         /* y coord for whiteout return */
    uint32_t money;              /* player money */
    uint16_t trainer_id_num;     /* player trainer ID */
    uint16_t pokedex_seen[26];   /* 416-bit bitfield: seen pokemon */
    uint16_t pokedex_caught[26]; /* 416-bit bitfield: caught pokemon */
    uint8_t text_speed;          /* 0=slow, 1=mid, 2=fast */
    uint8_t battle_scene;        /* 0=on, 1=off */
    uint8_t battle_style;        /* 0=shift, 1=set */
    uint8_t sound_mode;          /* 0=mono, 1=stereo */
    uint8_t party_submenu_cursor; /* 0=SUMMARY, 1=SWITCH, 2=CANCEL */
    uint8_t party_switch_source;  /* source slot for party swap (-1=none) */
    uint8_t options_cursor;       /* cursor in options menu */
    uint8_t save_cursor;          /* 0=YES, 1=NO in save confirm */
    uint8_t bag_submenu_cursor;   /* 0=USE, 1=TOSS, 2=CANCEL */
    uint8_t bag_use_target;       /* party slot selected for item use */
    uint8_t bag_pending_item_idx; /* bag slot index of item being used */
    uint8_t bag_context;          /* 0=overworld, 1=battle */
    uint8_t summary_move_cursor;  /* cursor on moves page for reorder */
    uint8_t summary_move_selected;/* 0xFF=none, else slot being swapped */
    uint8_t pokedex_cursor;       /* scroll cursor in pokedex list */
    uint8_t pokedex_scroll;       /* scroll offset for pokedex */
    uint8_t pc_menu;              /* 0=main, 1=deposit, 2=withdraw */
    uint8_t pc_cursor;            /* cursor in PC menus */
    uint8_t pc_box_cursor;        /* cursor in box view */
    uint8_t repel_steps;          /* remaining repel steps (0=none) */
    uint64_t flags;         /* 64-bit bitfield for progression flags */
    uint16_t vars[PFR_NATIVE_MAX_VARS]; /* 32 scene/progression variables */
    PfrNativeObjectState objects[PFR_NATIVE_MAX_OBJECTS];
    PfrPokemon party[PFR_NATIVE_MAX_PARTY];  /* Player's pokemon party */
    PfrBattleState battle;                     /* Active battle state */
    PfrBag bag;                                /* Player's bag */
    PfrShopState shop;                         /* Active shop state */
    PfrPokemon pc_box[PFR_PC_BOX_SIZE];        /* PC box storage */
    uint8_t pc_box_count;                      /* # pokemon in PC box */
} PfrNativeState;

typedef PfrNativeState PfrNativeSnapshot;

typedef struct {
    uint8_t mode;
    uint8_t event;
    uint8_t changed;
    uint8_t reserved;
} PfrNativeStepResult;

typedef struct {
    PfrNativeState state;
    PfrNativeStepResult last_step;
} PfrNativeCore;

/* Count bits set in pokedex bitfield */
static inline int pokedex_count(const uint16_t *bits)
{
    int count = 0;
    for (int i = 0; i < 26; i++) {
        uint16_t v = bits[i];
        while (v) { count += v & 1; v >>= 1; }
    }
    return count;
}

void c_init(PfrNativeCore *core);
int c_reset(PfrNativeCore *core, int bootstrap_id, const PfrNativeSnapshot *snapshot);
PfrNativeStepResult c_step(PfrNativeCore *core, PfrNativeAction action);
void c_render(const PfrNativeCore *core, uint32_t *rgba, int stride_pixels);
void c_close(PfrNativeCore *core);
void c_save_snapshot(const PfrNativeCore *core, PfrNativeSnapshot *snapshot);
const PfrNativeState *pfr_native_state(const PfrNativeCore *core);
size_t pfr_native_state_size(void);
const PfrNativeMap *pfr_native_get_map(PfrNativeMapId map_id);
PfrNativeMapId pfr_native_find_map_by_name(const char *name);
int pfr_native_reset_to_map(PfrNativeCore *core, PfrNativeMapId map_id, int16_t x, int16_t y,
                            PfrNativeDirection direction);
void pfr_native_format_dialog_page(const PfrNativeCore *core, uint8_t dialog_id,
                                   uint8_t page_index, char *buffer,
                                   size_t buffer_size);

#endif
