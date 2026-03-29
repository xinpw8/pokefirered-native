#ifndef PFR_BATTLE_H
#define PFR_BATTLE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ============================================================
 * Forward declarations from pfr_native.h (will be included before this)
 * ============================================================ */
/* PfrNativeMapId, pfr_native_find_map_by_name etc. are already available */

/* ============================================================
 * Type definitions (must come before pfr_battle_tables.h)
 * ============================================================ */

/* Species base stats */
typedef struct {
    uint8_t base_hp, base_atk, base_def, base_spa, base_spd, base_spe;
    uint8_t type1, type2;
    uint8_t catch_rate;
    uint8_t base_exp;
    uint8_t growth_rate;
    uint8_t _pad;
} PfrSpeciesData;  /* 12 bytes */

/* Move data */
typedef struct {
    uint8_t power;
    uint8_t type;
    uint8_t accuracy;
    uint8_t pp;
    uint8_t effect;
    int8_t priority;
    uint8_t flags;
    uint8_t secondary_chance;
} PfrMoveData;  /* 8 bytes */

/* Encounter slot */
typedef struct {
    uint16_t species;
    uint8_t min_lv;
    uint8_t max_lv;
} PfrEncounterSlot;

/* Encounter table */
typedef struct {
    uint16_t map_id;
    uint8_t type;       /* 0=land, 1=water */
    uint8_t rate;       /* encounter rate */
    uint16_t slot_start;
    uint8_t slot_count;
    uint8_t _pad;
} PfrEncounterTable;

/* Trainer mon */
typedef struct {
    uint16_t species;
    uint8_t level;
    uint8_t _pad;
    uint16_t moves[4];
} PfrTrainerMon;  /* 12 bytes */

/* Trainer data */
typedef struct {
    uint16_t trainer_id;
    uint8_t ai_flags;
    uint8_t mon_count;
    uint16_t first_mon;
    uint16_t _pad;
} PfrTrainerData;  /* 8 bytes */

/* Learnset index */
typedef struct {
    uint16_t start;
    uint16_t count;
} PfrLearnsetIndex;

/* Evolution entry */
typedef struct {
    uint16_t target;
    uint8_t method;
    uint8_t param;
} PfrEvolutionEntry;

/* Evolution index */
typedef struct {
    uint16_t start;
    uint16_t count;
} PfrEvolutionIndex;

/* ---- Include generated data tables ---- */
#include "pfr_battle_tables.h"

/* ============================================================
 * Pokemon struct (~48 bytes)
 * ============================================================ */
typedef struct {
    uint16_t species;
    uint8_t level;
    uint8_t nature;        /* 0-24 */
    uint16_t hp;           /* current HP */
    uint16_t max_hp;
    uint16_t stats[5];     /* Atk, Def, SpA, SpD, Spe (indices 0-4) */
    uint16_t moves[4];
    uint8_t pp[4];
    uint32_t exp;
    uint8_t ivs[6];        /* HP, Atk, Def, SpA, SpD, Spe */
    uint16_t evs[6];       /* HP, Atk, Def, SpA, SpD, Spe */
    uint8_t status;        /* 0=none, 1=SLP, 2=PSN, 4=BRN, 8=FRZ, 16=PAR, 32=TOX */
    uint8_t _pad;
} PfrPokemon;  /* ~48 bytes */

/* ============================================================
 * Battle menu states
 * ============================================================ */
typedef enum {
    PFR_BATTLE_MENU_MAIN  = 0,  /* FIGHT / BAG / POKEMON / RUN */
    PFR_BATTLE_MENU_FIGHT = 1,  /* Move select (4 moves, 2x2 grid) */
    PFR_BATTLE_MENU_PARTY = 2,  /* Party select (6 slots) */
    PFR_BATTLE_MENU_BAG   = 3,  /* Stub for future bag system */
    PFR_BATTLE_MENU_TURN_RESULT = 4,  /* Showing turn result messages */
} PfrBattleMenuState;

/* ============================================================
 * Battle state
 * ============================================================ */
typedef struct {
    uint8_t active;         /* battle in progress? */
    uint8_t type;           /* 0=wild, 1=trainer */
    uint16_t trainer_id;
    uint8_t player_slot;    /* active player pokemon index 0-5 */
    uint8_t opp_slot;       /* active opponent pokemon index 0-5 */
    uint8_t opp_count;      /* opponent party size */
    uint8_t weather;        /* 0=none, 1=rain, 2=sun, 3=sand, 4=hail */
    uint8_t weather_turns;
    uint8_t turn;
    uint8_t _pad[2];
    int8_t stat_stages[2][8]; /* [side][stat] -6 to +6: Atk,Def,SpA,SpD,Spe,Acc,Eva,Crit */
    PfrPokemon opponent[6];
    /* Menu state machine */
    uint8_t menu_state;         /* PfrBattleMenuState */
    uint8_t menu_cursor;        /* cursor position in current menu (0-based) */
    uint8_t menu_party_cursor;  /* cursor in party select (0-5) */
    uint8_t menu_item_cursor;   /* reserved for bag */
    /* Turn result tracking (for message display) */
    uint16_t last_player_move;   /* move ID player used */
    uint16_t last_opp_move;      /* move ID opponent used */
    uint16_t last_player_dmg;    /* damage player dealt to opponent */
    uint16_t last_opp_dmg;       /* damage player received from opponent */
    uint8_t  last_eff;           /* type effectiveness of player's move: 0,5,10,20,40 */
    uint8_t  last_was_crit;      /* 1 if player's move was a critical hit */
    uint8_t  msg_page;           /* which message page we're showing (0-based) */
    uint8_t  msg_total;          /* total message pages for this turn result */
    uint8_t  last_player_missed; /* 1 if player's attack missed */
    uint8_t  last_opp_missed;    /* 1 if opponent's attack missed */
    uint8_t  opp_fainted;        /* 1 if opponent fainted this turn */
    uint8_t  player_fainted;     /* 1 if player's active mon fainted this turn */
    uint32_t last_exp_gained;    /* exp points gained this turn */
    uint8_t  last_new_level;     /* new level after level up (0 if no level up) */
    uint8_t  intro_phase;        /* 0=not in intro, 1=wild appeared, 2=go mon!, 3=ready */
    uint8_t  _pad3[2];
    /* Track battle results for rewards */
    uint8_t damage_dealt;   /* total damage dealt this turn (scaled 0-255) */
    uint8_t kos_scored;     /* KOs this turn */
    uint8_t own_fainted;    /* own mons fainted this turn */
    uint8_t levels_gained;  /* levels gained this turn */
    uint8_t caught;         /* caught a pokemon this turn */
    uint8_t battle_won;     /* battle ended in victory */
    uint8_t battle_fled;    /* fled from battle */
    uint8_t _pad2;
} PfrBattleState;

/* ============================================================
 * Move effect IDs (from decomp battle_move_effects.h)
 * ============================================================ */
#define PFR_EFFECT_HIT              0
#define PFR_EFFECT_SLEEP            1
#define PFR_EFFECT_POISON_HIT       2
#define PFR_EFFECT_ABSORB           3
#define PFR_EFFECT_BURN_HIT         4
#define PFR_EFFECT_FREEZE_HIT       5
#define PFR_EFFECT_PARALYZE_HIT     6
#define PFR_EFFECT_EXPLOSION        7
#define PFR_EFFECT_ATTACK_UP        10
#define PFR_EFFECT_DEFENSE_UP       11
#define PFR_EFFECT_SPEED_UP         12
#define PFR_EFFECT_SP_ATK_UP        13
#define PFR_EFFECT_SP_DEF_UP        14
#define PFR_EFFECT_ACCURACY_UP      15
#define PFR_EFFECT_EVASION_UP       16
#define PFR_EFFECT_ALWAYS_HIT       17
#define PFR_EFFECT_ATTACK_DOWN      18
#define PFR_EFFECT_DEFENSE_DOWN     19
#define PFR_EFFECT_SPEED_DOWN       20
#define PFR_EFFECT_ACCURACY_DOWN    23
#define PFR_EFFECT_EVASION_DOWN     24
#define PFR_EFFECT_MULTI_HIT        29
#define PFR_EFFECT_FLINCH_HIT       31
#define PFR_EFFECT_RESTORE_HP       32
#define PFR_EFFECT_TOXIC            33
#define PFR_EFFECT_OHKO             38
#define PFR_EFFECT_SUPER_FANG       40
#define PFR_EFFECT_HIGH_CRITICAL    43
#define PFR_EFFECT_DOUBLE_HIT       44
#define PFR_EFFECT_RECOIL           48  /* EFFECT_RECOIL from decomp */

/* Status bits */
#define PFR_STATUS_NONE   0
#define PFR_STATUS_SLP    1
#define PFR_STATUS_PSN    2
#define PFR_STATUS_BRN    4
#define PFR_STATUS_FRZ    8
#define PFR_STATUS_PAR    16
#define PFR_STATUS_TOX    32

/* Stat indices */
#define PFR_STAT_ATK   0
#define PFR_STAT_DEF   1
#define PFR_STAT_SPA   2
#define PFR_STAT_SPD   3
#define PFR_STAT_SPE   4
#define PFR_STAT_ACC   5
#define PFR_STAT_EVA   6
#define PFR_STAT_CRIT  7

/* Move flag bits */
#define PFR_FLAG_CONTACT   0x01
#define PFR_FLAG_PROTECT   0x02

/* Tile behaviors for encounter checks */
#define PFR_MB_TALL_GRASS  0x02
#define PFR_MB_CAVE        0x08
#define PFR_MB_POND_WATER  0x10
#define PFR_MB_FAST_WATER  0x11
#define PFR_MB_DEEP_WATER  0x12
#define PFR_MB_OCEAN_WATER 0x15

/* Evolution methods (only EVO_LEVEL matters for RL) */
#define PFR_EVO_LEVEL 4

/* Encounter slot weights for land (12 slots): 20,20,10,10,10,10,5,5,4,4,1,1 */
static const uint8_t PFR_LAND_SLOT_WEIGHTS[12] = {20,20,10,10,10,10,5,5,4,4,1,1};
/* Water (5 slots): 60,30,5,4,1 */
static const uint8_t PFR_WATER_SLOT_WEIGHTS[5] = {60,30,5,4,1};

/* ============================================================
 * RNG -- GBA Linear Congruential Random Number Generator
 * ============================================================ */
static inline uint16_t pfr_random(uint32_t *rng)
{
    *rng = 1103515245u * (*rng) + 24691u;
    return (uint16_t)(*rng >> 16);
}

/* Random in range [0, max) */
static inline uint16_t pfr_random_range(uint32_t *rng, uint16_t max)
{
    if (max <= 1) return 0;
    return pfr_random(rng) % max;
}

/* ============================================================
 * Stat Calculation -- Gen 3 formulas
 * ============================================================ */

/* HP = ((2*Base + IV + EV/4) * Level / 100) + Level + 10
 * Exception: Shedinja (species 292) always has 1 HP */
static inline uint16_t pfr_calc_hp(uint8_t base, uint8_t iv, uint16_t ev,
                                    uint8_t level, uint16_t species)
{
    if (species == 292) return 1;  /* Shedinja */
    if (base == 0) return 0;
    uint32_t val = ((2u * (uint32_t)base + iv + ev / 4) * level) / 100 + level + 10;
    return (uint16_t)(val > 65535 ? 65535 : val);
}

/* Other stat = (((2*Base + IV + EV/4) * Level / 100) + 5) * NatureMod
 * NatureMod: 1.0 (neutral), 1.1 (+), 0.9 (-) */
static inline uint16_t pfr_calc_stat(uint8_t base, uint8_t iv, uint16_t ev,
                                      uint8_t level, int8_t nature_mod)
{
    if (base == 0) return 0;
    uint32_t val = ((2u * (uint32_t)base + iv + ev / 4) * level) / 100 + 5;
    if (nature_mod > 0) val = val * 11 / 10;
    else if (nature_mod < 0) val = val * 9 / 10;
    return (uint16_t)(val > 65535 ? 65535 : val);
}

/* Recalculate all stats for a pokemon from its base data */
static inline void pfr_recalc_stats(PfrPokemon *mon)
{
    if (mon->species == 0 || mon->species >= PFR_NUM_SPECIES) return;
    const PfrSpeciesData *base = &PFR_SPECIES[mon->species];
    const int8_t *nature = PFR_NATURES[mon->nature % PFR_NUM_NATURES];
    /* nature: [Atk, Def, Spe, SpA, SpD] */

    mon->max_hp = pfr_calc_hp(base->base_hp, mon->ivs[0], mon->evs[0],
                               mon->level, mon->species);
    mon->stats[PFR_STAT_ATK] = pfr_calc_stat(base->base_atk, mon->ivs[1],
                                               mon->evs[1], mon->level, nature[0]);
    mon->stats[PFR_STAT_DEF] = pfr_calc_stat(base->base_def, mon->ivs[2],
                                               mon->evs[2], mon->level, nature[1]);
    mon->stats[PFR_STAT_SPA] = pfr_calc_stat(base->base_spa, mon->ivs[3],
                                               mon->evs[3], mon->level, nature[3]);
    mon->stats[PFR_STAT_SPD] = pfr_calc_stat(base->base_spd, mon->ivs[4],
                                               mon->evs[4], mon->level, nature[4]);
    mon->stats[PFR_STAT_SPE] = pfr_calc_stat(base->base_spe, mon->ivs[5],
                                               mon->evs[5], mon->level, nature[2]);
}

/* ============================================================
 * Pokemon Generation
 * ============================================================ */

/* Assign level-up moves for a pokemon at the given level */
static inline void pfr_assign_level_moves(PfrPokemon *mon)
{
    if (mon->species >= PFR_NUM_SPECIES) return;
    const PfrLearnsetIndex *ls = &PFR_LEARNSETS[mon->species];
    if (ls->count == 0) return;

    /* Clear moves */
    memset(mon->moves, 0, sizeof(mon->moves));
    memset(mon->pp, 0, sizeof(mon->pp));

    /* Walk through learnset, keep the last 4 moves learned at or below current level */
    int slot = 0;
    for (uint16_t i = 0; i < ls->count; i++) {
        uint16_t packed = PFR_LEARNSET_DATA[ls->start + i];
        uint8_t learn_lv = (uint8_t)(packed >> 9);
        uint16_t move_id = packed & 0x1FFu;
        if (learn_lv > mon->level) break;
        if (move_id == 0 || move_id >= PFR_NUM_MOVES) continue;

        /* Shift moves if all 4 slots full */
        if (slot < 4) {
            mon->moves[slot] = move_id;
            mon->pp[slot] = PFR_MOVES[move_id].pp;
            slot++;
        } else {
            mon->moves[0] = mon->moves[1];
            mon->moves[1] = mon->moves[2];
            mon->moves[2] = mon->moves[3];
            mon->pp[0] = mon->pp[1];
            mon->pp[1] = mon->pp[2];
            mon->pp[2] = mon->pp[3];
            mon->moves[3] = move_id;
            mon->pp[3] = PFR_MOVES[move_id].pp;
        }
    }
}

/* Generate a pokemon with random IVs/nature, proper stats and moves */
static inline void pfr_generate_pokemon(PfrPokemon *mon, uint16_t species,
                                         uint8_t level, uint32_t *rng)
{
    memset(mon, 0, sizeof(*mon));
    if (species == 0 || species >= PFR_NUM_SPECIES) return;

    mon->species = species;
    mon->level = level;
    mon->nature = (uint8_t)(pfr_random(rng) % PFR_NUM_NATURES);

    /* Random IVs (0-31 each) */
    for (int i = 0; i < 6; i++)
        mon->ivs[i] = (uint8_t)(pfr_random(rng) & 31);

    /* EVs start at 0 */
    /* Set experience to match current level */
    const PfrSpeciesData *base = &PFR_SPECIES[species];
    if (base->growth_rate < PFR_NUM_GROWTH_RATES && level <= 100)
        mon->exp = PFR_EXP_TABLES[base->growth_rate][level];

    pfr_recalc_stats(mon);
    mon->hp = mon->max_hp;
    pfr_assign_level_moves(mon);
}

/* ============================================================
 * Type Effectiveness
 * ============================================================ */

/* Returns effectiveness multiplier: 0, 5, 10, or 20
 * For dual types, multiply both: result = chart[atk][def1] * chart[atk][def2] / 10 */
static inline uint8_t pfr_type_effectiveness(uint8_t atk_type, uint8_t def_type1,
                                              uint8_t def_type2)
{
    if (atk_type >= PFR_NUM_TYPES) return 10;
    uint16_t mul = 10;
    if (def_type1 < PFR_NUM_TYPES)
        mul = PFR_TYPE_CHART[atk_type][def_type1];
    if (def_type2 < PFR_NUM_TYPES && def_type2 != def_type1)
        mul = (uint16_t)(mul * PFR_TYPE_CHART[atk_type][def_type2] / 10);
    return (uint8_t)(mul > 255 ? 255 : mul);
}

/* ============================================================
 * Stat Stage Multipliers
 * ============================================================ */

/* Stat stages: -6 to +6. Multiplier = max(2, 2+stage) / max(2, 2-stage) */
static inline uint16_t pfr_apply_stat_stage(uint16_t stat, int8_t stage)
{
    /* Gen 3 stage multipliers as numerator/denominator:
     * -6: 2/8, -5: 2/7, -4: 2/6, -3: 2/5, -2: 2/4, -1: 2/3,
     *  0: 2/2, +1: 3/2, +2: 4/2, +3: 5/2, +4: 6/2, +5: 7/2, +6: 8/2 */
    int num, den;
    if (stage >= 0) {
        num = 2 + (int)stage;
        den = 2;
    } else {
        num = 2;
        den = 2 - (int)stage;
    }
    return (uint16_t)((uint32_t)stat * (uint32_t)num / (uint32_t)den);
}

/* Accuracy/evasion stage multiplier */
static inline uint16_t pfr_apply_acc_stage(uint16_t val, int8_t stage)
{
    /* Acc/Eva: -6: 3/9, -5: 3/8, -4: 3/7, -3: 3/6, -2: 3/5, -1: 3/4,
     *           0: 3/3, +1: 4/3, +2: 5/3, +3: 6/3, +4: 7/3, +5: 8/3, +6: 9/3 */
    int num, den;
    if (stage >= 0) {
        num = 3 + (int)stage;
        den = 3;
    } else {
        num = 3;
        den = 3 - (int)stage;
    }
    return (uint16_t)((uint32_t)val * (uint32_t)num / (uint32_t)den);
}

/* ============================================================
 * Accuracy Check
 * ============================================================ */

static inline int pfr_accuracy_check(const PfrBattleState *battle, int atk_side,
                                      uint16_t move_id, uint32_t *rng)
{
    if (move_id == 0 || move_id >= PFR_NUM_MOVES) return 0;
    const PfrMoveData *move = &PFR_MOVES[move_id];

    /* Always-hit moves */
    if (move->accuracy == 0 || move->effect == PFR_EFFECT_ALWAYS_HIT) return 1;

    int8_t acc_stage = battle->stat_stages[atk_side][PFR_STAT_ACC];
    int8_t eva_stage = battle->stat_stages[1 - atk_side][PFR_STAT_EVA];
    int8_t combined = (int8_t)(acc_stage - eva_stage);
    if (combined > 6) combined = 6;
    if (combined < -6) combined = -6;

    uint16_t threshold = pfr_apply_acc_stage(move->accuracy, combined);
    if (threshold >= 100) return 1;
    return (pfr_random(rng) % 100) < threshold;
}

/* ============================================================
 * Critical Hit Check
 * ============================================================ */

static inline int pfr_crit_check(const PfrBattleState *battle, int atk_side,
                                  uint16_t move_id, uint32_t *rng)
{
    /* Gen 3 crit stages: 1/16, 1/8, 1/4, 1/3, 1/2 */
    static const uint8_t crit_chance[] = {16, 8, 4, 3, 2};
    int stage = battle->stat_stages[atk_side][PFR_STAT_CRIT];
    if (move_id < PFR_NUM_MOVES && PFR_MOVES[move_id].effect == PFR_EFFECT_HIGH_CRITICAL)
        stage += 1;
    if (stage < 0) stage = 0;
    if (stage > 4) stage = 4;
    return (pfr_random(rng) % crit_chance[stage]) == 0;
}

/* ============================================================
 * Damage Calculation -- Gen 3 formula
 * ============================================================ */

/* Returns damage dealt. Full Gen 3 formula:
 * damage = ((2*Level/5 + 2) * Power * A/D) / 50 + 2
 * Then apply: STAB (x1.5), type effectiveness, crit (x2), random (85-100)/100 */
static inline uint16_t pfr_calc_damage(const PfrPokemon *attacker,
                                        const PfrPokemon *defender,
                                        const PfrBattleState *battle, int atk_side,
                                        uint16_t move_id, uint32_t *rng)
{
    if (move_id == 0 || move_id >= PFR_NUM_MOVES) return 0;
    const PfrMoveData *move = &PFR_MOVES[move_id];
    if (move->power == 0) return 0;  /* Status moves do no damage */

    if (attacker->species >= PFR_NUM_SPECIES ||
        defender->species >= PFR_NUM_SPECIES) return 0;
    const PfrSpeciesData *atk_base = &PFR_SPECIES[attacker->species];
    const PfrSpeciesData *def_base = &PFR_SPECIES[defender->species];

    /* Determine physical vs special (Gen 3: type-based split)
     * Types 0-8 = physical, 9+ = special */
    int is_special = (move->type >= 9);
    uint16_t atk_stat, def_stat;
    if (is_special) {
        atk_stat = attacker->stats[PFR_STAT_SPA];
        def_stat = defender->stats[PFR_STAT_SPD];
    } else {
        atk_stat = attacker->stats[PFR_STAT_ATK];
        def_stat = defender->stats[PFR_STAT_DEF];
    }

    /* Apply stat stages */
    int is_crit = pfr_crit_check(battle, atk_side, move_id, rng);
    if (is_crit) {
        /* Crit ignores negative atk stages and positive def stages */
        int8_t atk_stage = battle->stat_stages[atk_side][is_special ? PFR_STAT_SPA : PFR_STAT_ATK];
        int8_t def_stage = battle->stat_stages[1 - atk_side][is_special ? PFR_STAT_SPD : PFR_STAT_DEF];
        if (atk_stage > 0) atk_stat = pfr_apply_stat_stage(atk_stat, atk_stage);
        if (def_stage < 0) def_stat = pfr_apply_stat_stage(def_stat, def_stage);
    } else {
        int8_t atk_stage = battle->stat_stages[atk_side][is_special ? PFR_STAT_SPA : PFR_STAT_ATK];
        int8_t def_stage = battle->stat_stages[1 - atk_side][is_special ? PFR_STAT_SPD : PFR_STAT_DEF];
        atk_stat = pfr_apply_stat_stage(atk_stat, atk_stage);
        def_stat = pfr_apply_stat_stage(def_stat, def_stage);
    }

    /* Burn halves physical attack */
    if (!is_special && (attacker->status & PFR_STATUS_BRN))
        atk_stat /= 2;

    if (def_stat == 0) def_stat = 1;

    /* Base damage: ((2*Level/5 + 2) * Power * A / D) / 50 + 2 */
    uint32_t damage = ((2u * (uint32_t)attacker->level / 5 + 2) *
                        (uint32_t)move->power * atk_stat) / def_stat / 50 + 2;

    /* STAB (Same Type Attack Bonus): x1.5 */
    if (move->type == atk_base->type1 || move->type == atk_base->type2)
        damage = damage * 3 / 2;

    /* Type effectiveness */
    uint8_t eff = pfr_type_effectiveness(move->type, def_base->type1, def_base->type2);
    damage = damage * eff / 10;

    /* Critical hit: x2 */
    if (is_crit)
        damage *= 2;

    /* Random factor: 85-100 / 100 */
    if (damage > 1) {
        uint16_t rand_factor = (uint16_t)(85 + pfr_random(rng) % 16);
        damage = damage * rand_factor / 100;
    }

    /* Minimum 1 damage if move has power and type isn't immune */
    if (damage == 0 && eff > 0) damage = 1;

    return (uint16_t)(damage > 65535 ? 65535 : damage);
}

/* ============================================================
 * Apply Status Effects
 * ============================================================ */

static inline void pfr_try_apply_secondary(PfrPokemon *target,
                                            const PfrMoveData *move,
                                            uint32_t *rng)
{
    if (move->secondary_chance == 0) return;
    if ((pfr_random(rng) % 100) >= move->secondary_chance) return;

    switch (move->effect) {
    case PFR_EFFECT_BURN_HIT:
        if (!(target->status)) target->status = PFR_STATUS_BRN;
        break;
    case PFR_EFFECT_FREEZE_HIT:
        if (!(target->status)) target->status = PFR_STATUS_FRZ;
        break;
    case PFR_EFFECT_PARALYZE_HIT:
        if (!(target->status)) target->status = PFR_STATUS_PAR;
        break;
    case PFR_EFFECT_POISON_HIT:
        if (!(target->status)) target->status = PFR_STATUS_PSN;
        break;
    case PFR_EFFECT_FLINCH_HIT:
        /* Flinch only prevents the target from moving this turn --
         * simplified: no effect since we resolve both sides */
        break;
    default:
        break;
    }
}

/* Apply stat stage changes from moves */
static inline void pfr_apply_stat_move(PfrBattleState *battle, int user_side,
                                        uint8_t effect)
{
    int target_side = user_side;
    int stat = -1;
    int delta = 0;

    switch (effect) {
    case PFR_EFFECT_ATTACK_UP:     stat = PFR_STAT_ATK; delta =  1; break;
    case PFR_EFFECT_DEFENSE_UP:    stat = PFR_STAT_DEF; delta =  1; break;
    case PFR_EFFECT_SPEED_UP:      stat = PFR_STAT_SPE; delta =  1; break;
    case PFR_EFFECT_SP_ATK_UP:     stat = PFR_STAT_SPA; delta =  1; break;
    case PFR_EFFECT_SP_DEF_UP:     stat = PFR_STAT_SPD; delta =  1; break;
    case PFR_EFFECT_ACCURACY_UP:   stat = PFR_STAT_ACC; delta =  1; break;
    case PFR_EFFECT_EVASION_UP:    stat = PFR_STAT_EVA; delta =  1; break;
    case PFR_EFFECT_ATTACK_DOWN:   stat = PFR_STAT_ATK; delta = -1; target_side = 1 - user_side; break;
    case PFR_EFFECT_DEFENSE_DOWN:  stat = PFR_STAT_DEF; delta = -1; target_side = 1 - user_side; break;
    case PFR_EFFECT_SPEED_DOWN:    stat = PFR_STAT_SPE; delta = -1; target_side = 1 - user_side; break;
    case PFR_EFFECT_ACCURACY_DOWN: stat = PFR_STAT_ACC; delta = -1; target_side = 1 - user_side; break;
    case PFR_EFFECT_EVASION_DOWN:  stat = PFR_STAT_EVA; delta = -1; target_side = 1 - user_side; break;
    default: return;
    }

    if (stat >= 0 && stat < 8) {
        int8_t *s = &battle->stat_stages[target_side][stat];
        *s = (int8_t)(*s + delta);
        if (*s > 6) *s = 6;
        if (*s < -6) *s = -6;
    }
}

/* ============================================================
 * Execute Single Move
 * ============================================================ */

/* Execute one side's move. Returns damage dealt (0 for status/miss). */
static inline uint16_t pfr_execute_move(PfrPokemon *attacker, PfrPokemon *defender,
                                         PfrBattleState *battle, int atk_side,
                                         uint16_t move_id, uint32_t *rng)
{
    if (move_id == 0 || move_id >= PFR_NUM_MOVES) return 0;
    if (attacker->hp == 0) return 0;

    const PfrMoveData *move = &PFR_MOVES[move_id];

    /* Status check: frozen can't move (1/5 thaw chance),
     * paralyzed 1/4 can't move, asleep decrements counter */
    if (attacker->status & PFR_STATUS_FRZ) {
        if (pfr_random(rng) % 5 == 0)
            attacker->status = (uint8_t)(attacker->status & (uint8_t)~PFR_STATUS_FRZ);
        else
            return 0;
    }
    if (attacker->status & PFR_STATUS_PAR) {
        if (pfr_random(rng) % 4 == 0)
            return 0;  /* Fully paralyzed */
    }
    if (attacker->status & PFR_STATUS_SLP) {
        /* Simplified: 1/3 chance to wake each turn */
        if (pfr_random(rng) % 3 == 0)
            attacker->status = (uint8_t)(attacker->status & (uint8_t)~PFR_STATUS_SLP);
        else
            return 0;
    }

    /* Deduct PP -- find move slot */
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        if (attacker->moves[i] == move_id) { slot = i; break; }
    }
    if (slot >= 0 && attacker->pp[slot] > 0)
        attacker->pp[slot]--;

    /* Stat-change moves */
    if (move->power == 0) {
        /* Pure status/stat move */
        pfr_apply_stat_move(battle, atk_side, move->effect);

        /* Status-inflicting moves */
        if (move->effect == PFR_EFFECT_SLEEP && !(defender->status))
            defender->status = PFR_STATUS_SLP;
        else if (move->effect == PFR_EFFECT_TOXIC && !(defender->status))
            defender->status = PFR_STATUS_TOX;
        else if (move->effect == PFR_EFFECT_RESTORE_HP) {
            uint16_t heal = attacker->max_hp / 2;
            attacker->hp += heal;
            if (attacker->hp > attacker->max_hp) attacker->hp = attacker->max_hp;
        }
        return 0;
    }

    /* Accuracy check */
    if (!pfr_accuracy_check(battle, atk_side, move_id, rng))
        return 0;

    /* Damage calculation */
    uint16_t damage = pfr_calc_damage(attacker, defender, battle, atk_side,
                                       move_id, rng);

    /* OHKO moves */
    if (move->effect == PFR_EFFECT_OHKO) {
        if (attacker->level >= defender->level)
            damage = defender->hp;
        else
            damage = 0;
    }

    /* Super Fang: half HP */
    if (move->effect == PFR_EFFECT_SUPER_FANG) {
        damage = defender->hp / 2;
        if (damage == 0) damage = 1;
    }

    /* Apply damage */
    if (damage > defender->hp)
        damage = defender->hp;
    defender->hp -= damage;

    /* Recoil */
    if (move->effect == PFR_EFFECT_RECOIL || move->effect == PFR_EFFECT_EXPLOSION) {
        uint16_t recoil = damage / 4;
        if (move->effect == PFR_EFFECT_EXPLOSION) recoil = attacker->hp;
        if (recoil > attacker->hp) recoil = attacker->hp;
        attacker->hp -= recoil;
    }

    /* Drain (absorb) */
    if (move->effect == PFR_EFFECT_ABSORB) {
        uint16_t heal = damage / 2;
        attacker->hp += heal;
        if (attacker->hp > attacker->max_hp) attacker->hp = attacker->max_hp;
    }

    /* Multi-hit: 2-5 hits */
    if (move->effect == PFR_EFFECT_MULTI_HIT) {
        int hits = 2 + (int)(pfr_random(rng) % 4);  /* 2-5 hits */
        for (int i = 1; i < hits && defender->hp > 0; i++) {
            uint16_t extra = pfr_calc_damage(attacker, defender, battle,
                                              atk_side, move_id, rng);
            if (extra > defender->hp) extra = defender->hp;
            defender->hp -= extra;
            damage += extra;
        }
    }

    /* Double hit */
    if (move->effect == PFR_EFFECT_DOUBLE_HIT && defender->hp > 0) {
        uint16_t extra = pfr_calc_damage(attacker, defender, battle,
                                          atk_side, move_id, rng);
        if (extra > defender->hp) extra = defender->hp;
        defender->hp -= extra;
        damage += extra;
    }

    /* Secondary effects (burn, paralyze, etc.) */
    pfr_try_apply_secondary(defender, move, rng);

    /* Stat moves that also do damage (e.g., stat-down chance moves) */
    if (move->effect >= PFR_EFFECT_ATTACK_DOWN &&
        move->effect <= PFR_EFFECT_EVASION_DOWN) {
        if (move->secondary_chance > 0 &&
            (pfr_random(rng) % 100) < move->secondary_chance)
            pfr_apply_stat_move(battle, atk_side, move->effect);
    }

    return damage;
}

/* ============================================================
 * Status Damage (end of turn)
 * ============================================================ */

static inline void pfr_apply_status_damage(PfrPokemon *mon)
{
    if (mon->hp == 0) return;

    if (mon->status & PFR_STATUS_BRN) {
        uint16_t dmg = mon->max_hp / 8;
        if (dmg == 0) dmg = 1;
        if (dmg > mon->hp) dmg = mon->hp;
        mon->hp -= dmg;
    }
    if (mon->status & PFR_STATUS_PSN) {
        uint16_t dmg = mon->max_hp / 8;
        if (dmg == 0) dmg = 1;
        if (dmg > mon->hp) dmg = mon->hp;
        mon->hp -= dmg;
    }
    if (mon->status & PFR_STATUS_TOX) {
        /* Toxic damage increases each turn -- simplified to 1/8 */
        uint16_t dmg = mon->max_hp / 8;
        if (dmg == 0) dmg = 1;
        if (dmg > mon->hp) dmg = mon->hp;
        mon->hp -= dmg;
    }
}

/* ============================================================
 * Get Valid Move (handle no PP, no moves -> Struggle)
 * ============================================================ */

#define PFR_MOVE_STRUGGLE 165  /* Struggle move ID from decomp */

static inline uint16_t pfr_get_valid_move(const PfrPokemon *mon, int move_index)
{
    /* Try requested move */
    if (move_index >= 0 && move_index < 4) {
        uint16_t mid = mon->moves[move_index];
        if (mid != 0 && mid < PFR_NUM_MOVES && mon->pp[move_index] > 0)
            return mid;
    }

    /* Fall back to first move with PP */
    for (int i = 0; i < 4; i++) {
        if (mon->moves[i] != 0 && mon->moves[i] < PFR_NUM_MOVES && mon->pp[i] > 0)
            return mon->moves[i];
    }

    /* No PP left -> Struggle */
    return PFR_MOVE_STRUGGLE;
}

/* ============================================================
 * Trainer AI -- Pick best move
 * ============================================================ */

static inline uint16_t pfr_trainer_ai(const PfrPokemon *attacker,
                                       const PfrPokemon *defender,
                                       uint8_t ai_flags, uint32_t *rng)
{
    if (attacker->species >= PFR_NUM_SPECIES ||
        defender->species >= PFR_NUM_SPECIES)
        return pfr_get_valid_move(attacker, 0);

    const PfrSpeciesData *def_base = &PFR_SPECIES[defender->species];
    int best_slot = -1;
    int best_score = -1;

    for (int i = 0; i < 4; i++) {
        uint16_t mid = attacker->moves[i];
        if (mid == 0 || mid >= PFR_NUM_MOVES || attacker->pp[i] == 0)
            continue;

        const PfrMoveData *move = &PFR_MOVES[mid];
        int score = 10;

        /* AI_SCRIPT_CHECK_BAD_MOVE: avoid immune/not-effective moves */
        if (ai_flags & 0x01) {
            uint8_t eff = pfr_type_effectiveness(move->type,
                                                  def_base->type1,
                                                  def_base->type2);
            if (eff == 0) score -= 100;
            else if (eff == 5) score -= 5;
            else if (eff >= 20) score += 10;
        }

        /* AI_SCRIPT_TRY_TO_FAINT: prefer high-power moves */
        if (ai_flags & 0x02) {
            score += move->power / 10;
        }

        /* AI_SCRIPT_CHECK_VIABILITY: consider STAB */
        if (ai_flags & 0x04) {
            const PfrSpeciesData *atk_base = &PFR_SPECIES[attacker->species];
            if (move->type == atk_base->type1 || move->type == atk_base->type2)
                score += 5;
        }

        /* Random tiebreaker */
        score += (int)(pfr_random(rng) % 3);

        if (score > best_score) {
            best_score = score;
            best_slot = i;
        }
    }

    return pfr_get_valid_move(attacker, best_slot);
}

/* ============================================================
 * Experience & Level Up
 * ============================================================ */

/* Gen 3 exp formula: (base_exp * defeated_level / 7) * trainer_mult
 * trainer_mult = 1.5 for trainer battles, 1.0 for wild */
static inline uint32_t pfr_calc_exp(uint16_t defeated_species,
                                     uint8_t defeated_level, int is_trainer)
{
    if (defeated_species == 0 || defeated_species >= PFR_NUM_SPECIES) return 0;
    uint32_t base = PFR_SPECIES[defeated_species].base_exp;
    uint32_t exp = (base * defeated_level) / 7;
    if (is_trainer) exp = exp * 3 / 2;
    if (exp == 0) exp = 1;
    return exp;
}

/* Try to level up. Returns number of levels gained. */
static inline uint8_t pfr_try_level_up(PfrPokemon *mon)
{
    if (mon->species == 0 || mon->species >= PFR_NUM_SPECIES) return 0;
    if (mon->level >= 100) return 0;

    const PfrSpeciesData *base = &PFR_SPECIES[mon->species];
    if (base->growth_rate >= PFR_NUM_GROWTH_RATES) return 0;

    uint8_t levels = 0;
    uint16_t old_hp_pct_x256 = (uint16_t)(
        (uint32_t)mon->hp * 256 / (mon->max_hp ? mon->max_hp : 1));

    while (mon->level < 100) {
        uint32_t needed = PFR_EXP_TABLES[base->growth_rate][mon->level + 1];
        if (mon->exp < needed) break;
        mon->level++;
        levels++;

        /* Learn new moves at this level */
        const PfrLearnsetIndex *ls = &PFR_LEARNSETS[mon->species];
        for (uint16_t i = 0; i < ls->count; i++) {
            uint16_t packed = PFR_LEARNSET_DATA[ls->start + i];
            uint8_t learn_lv = (uint8_t)(packed >> 9);
            uint16_t move_id = packed & 0x1FFu;
            if (learn_lv != mon->level) continue;
            if (move_id == 0 || move_id >= PFR_NUM_MOVES) continue;

            /* Check if already known */
            int known = 0;
            for (int j = 0; j < 4; j++)
                if (mon->moves[j] == move_id) { known = 1; break; }
            if (known) continue;

            /* Find empty slot or replace first slot */
            int eslot = -1;
            for (int j = 0; j < 4; j++)
                if (mon->moves[j] == 0) { eslot = j; break; }
            if (eslot < 0) {
                /* Shift moves left, put new move in slot 3 */
                mon->moves[0] = mon->moves[1];
                mon->moves[1] = mon->moves[2];
                mon->moves[2] = mon->moves[3];
                mon->pp[0] = mon->pp[1];
                mon->pp[1] = mon->pp[2];
                mon->pp[2] = mon->pp[3];
                eslot = 3;
            }
            mon->moves[eslot] = move_id;
            mon->pp[eslot] = PFR_MOVES[move_id].pp;
        }

        /* Check evolution */
        const PfrEvolutionIndex *evo = &PFR_EVOLUTIONS[mon->species];
        for (uint16_t i = 0; i < evo->count; i++) {
            const PfrEvolutionEntry *e = &PFR_EVOLUTION_DATA[evo->start + i];
            if (e->method == PFR_EVO_LEVEL && mon->level >= e->param &&
                e->target < PFR_NUM_SPECIES) {
                mon->species = e->target;
                pfr_assign_level_moves(mon);  /* Species changed, might have new moves */
                break;
            }
        }
    }

    /* Recalculate stats */
    pfr_recalc_stats(mon);

    /* Scale HP proportionally */
    if (levels > 0 && mon->max_hp > 0) {
        mon->hp = (uint16_t)((uint32_t)old_hp_pct_x256 * mon->max_hp / 256);
        if (mon->hp == 0 && old_hp_pct_x256 > 0) mon->hp = 1;
        if (mon->hp > mon->max_hp) mon->hp = mon->max_hp;
    }

    return levels;
}

/* ============================================================
 * Turn Execution
 * ============================================================ */

/* Battle action IDs (mapped from the 9-action discrete space) */
#define PFR_BATTLE_ACT_MOVE1  0
#define PFR_BATTLE_ACT_MOVE2  1
#define PFR_BATTLE_ACT_MOVE3  2
#define PFR_BATTLE_ACT_MOVE4  3
#define PFR_BATTLE_ACT_SWITCH 4
#define PFR_BATTLE_ACT_RUN    5
#define PFR_BATTLE_ACT_ITEM   6  /* Use item (ball, potion, etc.) */

/* Execute one full battle turn. player_action is 0-5 (mapped from env action space).
 * Modifies battle state, player party, opponent party.
 * Returns: 0 = battle continues, 1 = player won, 2 = player lost,
 *          3 = fled, 4 = caught */
static inline int pfr_execute_turn(PfrPokemon *player_party,
                                    PfrBattleState *battle,
                                    int player_action, uint32_t *rng)
{
    /* Clear per-turn tracking */
    battle->damage_dealt = 0;
    battle->kos_scored = 0;
    battle->own_fainted = 0;
    battle->levels_gained = 0;
    battle->caught = 0;
    battle->battle_won = 0;
    battle->battle_fled = 0;

    /* Clear turn result tracking */
    battle->last_player_move = 0;
    battle->last_opp_move = 0;
    battle->last_player_dmg = 0;
    battle->last_opp_dmg = 0;
    battle->last_eff = 10; /* neutral */
    battle->last_was_crit = 0;
    battle->last_player_missed = 0;
    battle->last_opp_missed = 0;
    battle->opp_fainted = 0;
    battle->player_fainted = 0;
    battle->last_exp_gained = 0;
    battle->last_new_level = 0;

    PfrPokemon *player = &player_party[battle->player_slot];
    PfrPokemon *opp = &battle->opponent[battle->opp_slot];

    /* Handle run attempt */
    if (player_action == PFR_BATTLE_ACT_RUN && battle->type == 0) {
        /* Wild: flee chance based on speed. Simplified: always succeed. */
        battle->active = 0;
        battle->battle_fled = 1;
        return 3;
    }

    /* Handle switch */
    if (player_action == PFR_BATTLE_ACT_SWITCH) {
        /* player_slot already set by menu system (menu_party_cursor -> player_slot).
         * Just refresh the pointer. */
        player = &player_party[battle->player_slot];
        /* After switching, opponent still attacks */
        uint16_t opp_move = pfr_trainer_ai(opp, player, 0x01, rng);
        battle->last_opp_move = opp_move;
        uint16_t player_hp_before_sw = player->hp;
        pfr_execute_move(opp, player, battle, 1, opp_move, rng);
        battle->last_opp_dmg = (player_hp_before_sw > player->hp) ?
            (player_hp_before_sw - player->hp) : 0;
        goto end_of_turn;
    }

    /* Handle item use (potion/ball already applied by caller, opponent attacks) */
    if (player_action == PFR_BATTLE_ACT_ITEM) {
        /* Opponent still gets to attack after item use */
        uint16_t opp_move = pfr_trainer_ai(opp, player, 0x01, rng);
        battle->last_opp_move = opp_move;
        uint16_t player_hp_before = player->hp;
        pfr_execute_move(opp, player, battle, 1, opp_move, rng);
        battle->last_opp_dmg = (player_hp_before > player->hp) ?
            (player_hp_before - player->hp) : 0;
        goto end_of_turn;
    }

    /* Get moves */
    int player_move_slot = player_action;  /* 0-3 */
    if (player_move_slot < 0 || player_move_slot > 3) player_move_slot = 0;
    uint16_t player_move = pfr_get_valid_move(player, player_move_slot);
    uint16_t opp_move = pfr_trainer_ai(opp, player,
        (uint8_t)((battle->type == 1) ? 0x07 : 0x01), rng);

    /* Record move IDs for turn result display */
    battle->last_player_move = player_move;
    battle->last_opp_move = opp_move;

    /* Determine turn order by speed (priority -> speed -> random) */
    int8_t p_pri = (player_move < PFR_NUM_MOVES) ?
                    PFR_MOVES[player_move].priority : 0;
    int8_t o_pri = (opp_move < PFR_NUM_MOVES) ?
                    PFR_MOVES[opp_move].priority : 0;

    int player_first;
    if (p_pri != o_pri) {
        player_first = (p_pri > o_pri);
    } else {
        uint16_t p_spe = pfr_apply_stat_stage(
            player->stats[PFR_STAT_SPE],
            battle->stat_stages[0][PFR_STAT_SPE]);
        uint16_t o_spe = pfr_apply_stat_stage(
            opp->stats[PFR_STAT_SPE],
            battle->stat_stages[1][PFR_STAT_SPE]);
        if (player->status & PFR_STATUS_PAR) p_spe /= 4;
        if (opp->status & PFR_STATUS_PAR) o_spe /= 4;
        if (p_spe != o_spe)
            player_first = (p_spe > o_spe);
        else
            player_first = (pfr_random(rng) & 1);
    }

    /* Execute moves in order, tracking damage for turn result display */
    uint16_t dmg;
    if (player_first) {
        uint16_t opp_hp_before = opp->hp;
        dmg = pfr_execute_move(player, opp, battle, 0, player_move, rng);
        battle->last_player_dmg = (opp_hp_before > opp->hp) ?
            (opp_hp_before - opp->hp) : 0;
        battle->damage_dealt += (uint8_t)(dmg > 255 ? 255 : dmg);
        if (opp->hp > 0) {
            uint16_t player_hp_before = player->hp;
            pfr_execute_move(opp, player, battle, 1, opp_move, rng);
            battle->last_opp_dmg = (player_hp_before > player->hp) ?
                (player_hp_before - player->hp) : 0;
        }
    } else {
        uint16_t player_hp_before = player->hp;
        pfr_execute_move(opp, player, battle, 1, opp_move, rng);
        battle->last_opp_dmg = (player_hp_before > player->hp) ?
            (player_hp_before - player->hp) : 0;
        if (player->hp > 0) {
            uint16_t opp_hp_before = opp->hp;
            dmg = pfr_execute_move(player, opp, battle, 0, player_move, rng);
            battle->last_player_dmg = (opp_hp_before > opp->hp) ?
                (opp_hp_before - opp->hp) : 0;
            battle->damage_dealt += (uint8_t)(dmg > 255 ? 255 : dmg);
        }
    }

    /* Compute type effectiveness of player's move for display */
    if (battle->last_player_move > 0 && battle->last_player_move < PFR_NUM_MOVES
        && opp->species < PFR_NUM_SPECIES) {
        const PfrSpeciesData *def_sp = &PFR_SPECIES[opp->species];
        battle->last_eff = pfr_type_effectiveness(
            PFR_MOVES[battle->last_player_move].type, def_sp->type1, def_sp->type2);
    }

end_of_turn:
    /* End-of-turn status damage */
    pfr_apply_status_damage(player);
    pfr_apply_status_damage(opp);

    battle->turn++;

    /* Check if opponent fainted */
    if (opp->hp == 0) {
        battle->kos_scored++;
        battle->opp_fainted = 1;

        /* Award experience */
        uint32_t exp = pfr_calc_exp(opp->species, opp->level, battle->type);
        player->exp += exp;
        battle->last_exp_gained = exp;
        uint8_t old_level = player->level;
        battle->levels_gained += pfr_try_level_up(player);
        if (player->level > old_level)
            battle->last_new_level = player->level;

        /* Check if more opponent pokemon */
        int next_opp = -1;
        for (int i = 0; i < (int)battle->opp_count; i++) {
            if (i != (int)battle->opp_slot && battle->opponent[i].hp > 0) {
                next_opp = i;
                break;
            }
        }

        if (next_opp >= 0) {
            battle->opp_slot = (uint8_t)next_opp;
            /* Reset stat stages for new opponent */
            memset(battle->stat_stages[1], 0, 8);
        } else {
            /* All opponents defeated -- battle won! */
            battle->active = 0;
            battle->battle_won = 1;
            return 1;
        }
    }

    /* Check if player fainted */
    if (player->hp == 0) {
        battle->own_fainted++;
        battle->player_fainted = 1;

        /* Find next alive party member */
        int next_player = -1;
        for (int i = 0; i < 6; i++) {
            if (i != (int)battle->player_slot &&
                player_party[i].species != 0 &&
                player_party[i].hp > 0) {
                next_player = i;
                break;
            }
        }

        if (next_player >= 0) {
            battle->player_slot = (uint8_t)next_player;
            memset(battle->stat_stages[0], 0, 8);
        } else {
            /* All player pokemon fainted -- battle lost */
            battle->active = 0;
            return 2;
        }
    }

    return 0;  /* Battle continues */
}

/* ============================================================
 * Encounter System
 * ============================================================ */

/* Check if a tile behavior triggers an encounter */
static inline int pfr_is_encounter_tile(uint16_t behavior, int *out_type)
{
    switch (behavior) {
    case PFR_MB_TALL_GRASS:
        *out_type = 0;  /* land */
        return 1;
    case PFR_MB_CAVE:
        *out_type = 0;  /* land (cave encounters use land table) */
        return 1;
    case PFR_MB_POND_WATER:
    case PFR_MB_FAST_WATER:
    case PFR_MB_DEEP_WATER:
    case PFR_MB_OCEAN_WATER:
        *out_type = 1;  /* water */
        return 1;
    default:
        return 0;
    }
}

/* Roll encounter slot using weighted selection (Gen 3 rates) */
static inline int pfr_roll_encounter_slot(int slot_count,
                                           const uint8_t *weights,
                                           uint32_t *rng)
{
    uint16_t total = 0;
    for (int i = 0; i < slot_count; i++) total += weights[i];
    if (total == 0) return 0;

    uint16_t roll = pfr_random(rng) % total;
    uint16_t cumulative = 0;
    for (int i = 0; i < slot_count; i++) {
        cumulative += weights[i];
        if (roll < cumulative) return i;
    }
    return 0;
}

/* Check for a wild encounter after stepping on an encounter tile.
 * Returns 1 if encounter triggered, fills out_species and out_level. */
static inline int pfr_check_encounter(uint16_t map_id, uint16_t tile_behavior,
                                       uint16_t *out_species, uint8_t *out_level,
                                       uint32_t *rng)
{
    int enc_type = 0;
    if (!pfr_is_encounter_tile(tile_behavior, &enc_type))
        return 0;

    /* Look up encounter table for this map */
    const int16_t *lookup = (enc_type == 0) ?
                             PFR_ENCOUNTER_LAND_BY_MAP :
                             PFR_ENCOUNTER_WATER_BY_MAP;

    /* Bounds check */
#ifdef PFR_MAX_ENCOUNTER_MAP_ID
    if (map_id >= PFR_MAX_ENCOUNTER_MAP_ID) return 0;
#else
    return 0;
#endif

    int16_t table_idx = lookup[map_id];
    if (table_idx < 0) return 0;

    const PfrEncounterTable *table = &PFR_ENCOUNTERS[table_idx];

    /* Roll against encounter rate: random(0-255) < rate */
    if (pfr_random(rng) % 256 >= table->rate)
        return 0;

    /* Roll encounter slot */
    const uint8_t *weights = (enc_type == 0) ?
                              PFR_LAND_SLOT_WEIGHTS :
                              PFR_WATER_SLOT_WEIGHTS;
    int max_slots = (enc_type == 0) ? 12 : 5;
    if (table->slot_count < (uint8_t)max_slots) max_slots = table->slot_count;
    if (max_slots <= 0) return 0;

    int eslot = pfr_roll_encounter_slot(max_slots, weights, rng);
    const PfrEncounterSlot *enc = &PFR_ENCOUNTER_SLOTS[table->slot_start + eslot];

    *out_species = enc->species;
    /* Random level in range [min_lv, max_lv] */
    if (enc->max_lv > enc->min_lv)
        *out_level = (uint8_t)(enc->min_lv +
            (uint8_t)(pfr_random(rng) % (uint16_t)(enc->max_lv - enc->min_lv + 1)));
    else
        *out_level = enc->min_lv;

    return 1;
}

/* ============================================================
 * Battle Initialization
 * ============================================================ */

static inline void pfr_init_battle_common(PfrBattleState *battle)
{
    battle->active = 1;
    battle->player_slot = 0;
    battle->opp_slot = 0;
    battle->weather = 0;
    battle->weather_turns = 0;
    battle->turn = 0;
    memset(battle->stat_stages, 0, sizeof(battle->stat_stages));
    battle->damage_dealt = 0;
    battle->kos_scored = 0;
    battle->own_fainted = 0;
    battle->levels_gained = 0;
    battle->caught = 0;
    battle->battle_won = 0;
    battle->battle_fled = 0;
    battle->menu_state = PFR_BATTLE_MENU_MAIN;
    battle->menu_cursor = 0;
    battle->menu_party_cursor = 0;
    battle->menu_item_cursor = 0;
    battle->last_player_move = 0;
    battle->last_opp_move = 0;
    battle->last_player_dmg = 0;
    battle->last_opp_dmg = 0;
    battle->last_eff = 10;
    battle->last_was_crit = 0;
    battle->msg_page = 0;
    battle->msg_total = 0;
    battle->last_player_missed = 0;
    battle->last_opp_missed = 0;
    battle->opp_fainted = 0;
    battle->player_fainted = 0;
    battle->last_exp_gained = 0;
    battle->last_new_level = 0;
    battle->intro_phase = 1;  /* Start with intro sequence */
}

/* Initialize a wild battle */
static inline void pfr_init_wild_battle(PfrBattleState *battle,
                                         uint16_t species,
                                         uint8_t level, uint32_t *rng)
{
    memset(battle, 0, sizeof(*battle));
    pfr_init_battle_common(battle);
    battle->type = 0;  /* wild */
    battle->opp_count = 1;
    pfr_generate_pokemon(&battle->opponent[0], species, level, rng);
}

/* Initialize a trainer battle from the trainer table */
static inline void pfr_init_trainer_battle(PfrBattleState *battle,
                                            uint16_t trainer_id,
                                            uint32_t *rng)
{
    memset(battle, 0, sizeof(*battle));
    pfr_init_battle_common(battle);
    battle->type = 1;  /* trainer */
    battle->trainer_id = trainer_id;

    /* Find trainer in table */
    int found = -1;
    for (int i = 0; i < PFR_TRAINER_TABLE_COUNT; i++) {
        if (PFR_TRAINERS[i].trainer_id == trainer_id) {
            found = i;
            break;
        }
    }

    if (found >= 0) {
        const PfrTrainerData *td = &PFR_TRAINERS[found];
        battle->opp_count = td->mon_count;
        if (battle->opp_count > 6) battle->opp_count = 6;

        for (int i = 0; i < (int)battle->opp_count; i++) {
            const PfrTrainerMon *tm = &PFR_TRAINER_MONS[td->first_mon + i];
            pfr_generate_pokemon(&battle->opponent[i], tm->species, tm->level, rng);

            /* Override moves if trainer has custom moves */
            int has_custom = 0;
            for (int j = 0; j < 4; j++) {
                if (tm->moves[j] != 0) { has_custom = 1; break; }
            }
            if (has_custom) {
                for (int j = 0; j < 4; j++) {
                    battle->opponent[i].moves[j] = tm->moves[j];
                    if (tm->moves[j] != 0 && tm->moves[j] < PFR_NUM_MOVES)
                        battle->opponent[i].pp[j] = PFR_MOVES[tm->moves[j]].pp;
                    else
                        battle->opponent[i].pp[j] = 0;
                }
            }
        }
    } else {
        /* Fallback: generate a single level-5 Rattata */
        battle->opp_count = 1;
        pfr_generate_pokemon(&battle->opponent[0], 19, 5, rng);  /* Rattata */
    }
}

/* ============================================================
 * Catch Mechanics
 * ============================================================ */

/* Ball catch rate multipliers (x10): POKe BALL=10, GREAT=15, ULTRA=20, MASTER=255 */
static inline uint8_t pfr_ball_modifier(uint16_t item_id)
{
    switch (item_id) {
    case 1:  return 255; /* MASTER BALL - guaranteed */
    case 2:  return 20;  /* ULTRA BALL */
    case 3:  return 15;  /* GREAT BALL */
    case 4:  return 10;  /* POKe BALL */
    case 5:  return 15;  /* SAFARI BALL */
    case 6:  return 10;  /* NET BALL (simplified) */
    case 7:  return 10;  /* DIVE BALL */
    case 8:  return 10;  /* NEST BALL */
    case 9:  return 10;  /* REPEAT BALL */
    case 10: return 10;  /* TIMER BALL */
    case 11: return 10;  /* LUXURY BALL */
    case 12: return 10;  /* PREMIER BALL */
    default: return 10;
    }
}

/* Gen 3 catch formula with ball modifier and shake checks */
static inline int pfr_try_catch_ball(const PfrPokemon *wild, uint16_t ball_id, uint32_t *rng)
{
    if (wild->species == 0 || wild->species >= PFR_NUM_SPECIES) return 0;

    uint8_t ball_mod = pfr_ball_modifier(ball_id);
    if (ball_mod >= 255) return 1; /* Master Ball: guaranteed catch */

    uint8_t catch_rate = PFR_SPECIES[wild->species].catch_rate;
    uint32_t max_hp = wild->max_hp ? wild->max_hp : 1;
    uint32_t cur_hp = wild->hp ? wild->hp : 1;

    /* a = (3*maxHP - 2*curHP) * catchRate * ballMod / (3*maxHP * 10) */
    uint32_t a = (3u * max_hp - 2u * cur_hp) * (uint32_t)catch_rate * ball_mod / (3u * max_hp * 10u);
    /* Status bonus: sleep/freeze x2, other x1.5 */
    if (wild->status & (PFR_STATUS_SLP | PFR_STATUS_FRZ))
        a = a * 2;
    else if (wild->status & (PFR_STATUS_PSN | PFR_STATUS_BRN | PFR_STATUS_PAR | PFR_STATUS_TOX))
        a = a * 3 / 2;
    if (a == 0) a = 1;
    if (a >= 255) return 1;

    /* 4 shake checks: b = 1048560 / sqrt(sqrt(16711680 / a)) */
    /* Simplified: each shake passes with probability a/255 */
    for (int shake = 0; shake < 4; shake++) {
        if ((pfr_random(rng) % 256) >= a)
            return 0; /* Shake failed */
    }
    return 1; /* Caught! */
}

/* Legacy wrapper */
static inline int pfr_try_catch(const PfrPokemon *wild, uint32_t *rng)
{
    return pfr_try_catch_ball(wild, 4 /* POKe BALL */, rng);
}

/* ============================================================
 * Heal party (pokecenter / Mom)
 * ============================================================ */

/* Get trainer display name. Uses static buffer. */
static inline const char *pfr_trainer_name(uint16_t trainer_id)
{
    static char buf[24];
    /* Known gym leaders / E4 / Champion by trainer_id */
    switch (trainer_id) {
    case 414: return "BROCK";
    case 415: return "MISTY";
    case 416: return "LT.SURGE";
    case 417: return "ERIKA";
    case 418: return "KOGA";
    case 419: return "SABRINA";
    case 420: return "BLAINE";
    case 421: return "GIOVANNI";
    case 410: return "LORELEI";
    case 411: return "BRUNO";
    case 412: return "AGATHA";
    case 413: return "LANCE";
    case 422: return "RIVAL";
    default:
        snprintf(buf, sizeof(buf), "TRAINER %u", trainer_id);
        return buf;
    }
}

/* Heal all party pokemon to full HP, restore PP, clear status */
static inline void pfr_heal_party(PfrPokemon *party, int max_party)
{
    for (int i = 0; i < max_party; i++) {
        if (party[i].species == 0) continue;
        party[i].hp = party[i].max_hp;
        party[i].status = PFR_STATUS_NONE;
        for (int m = 0; m < 4; m++) {
            uint16_t mid = party[i].moves[m];
            if (mid > 0 && mid < PFR_NUM_MOVES)
                party[i].pp[m] = PFR_MOVES[mid].pp;
        }
    }
}

#endif /* PFR_BATTLE_H */
