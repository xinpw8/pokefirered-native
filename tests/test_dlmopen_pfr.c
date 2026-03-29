/*
 * test_dlmopen_pfr.c — Verify that dlmopen gives independent game instances.
 *
 * Build: gcc -O2 -o test_dlmopen_pfr test_dlmopen_pfr.c -ldl
 * Run:   ./test_dlmopen_pfr /path/to/libpfr_game.so /path/to/savestate.pfrstate
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

typedef void (*boot_fn)(void);
typedef int  (*load_fn)(const char *);
typedef void (*step_fn)(uint16_t, int);
typedef void (*save_hot_fn)(void);
typedef void (*restore_hot_fn)(void);

typedef struct {
    int16_t  player_x;
    int16_t  player_y;
    uint8_t  map_group;
    uint8_t  map_num;
    uint8_t  badges;
    uint8_t  party_count;
    uint16_t party_level_sum;
    uint32_t money;
    uint8_t  in_battle;
} PfrRewardInfo;

typedef void (*reward_fn)(PfrRewardInfo *);

struct GameInstance {
    void *handle;
    boot_fn        boot;
    load_fn        load_state;
    step_fn        step_frames;
    save_hot_fn    save_hot;
    restore_hot_fn restore_hot;
    reward_fn      get_reward_info;
};

static int load_instance(struct GameInstance *inst, const char *so_path)
{
    inst->handle = dlmopen(LM_ID_NEWLM, so_path, RTLD_NOW);
    if (!inst->handle) {
        fprintf(stderr, "dlmopen failed: %s\n", dlerror());
        return -1;
    }

    inst->boot           = dlsym(inst->handle, "pfr_game_boot");
    inst->load_state     = dlsym(inst->handle, "pfr_game_load_state");
    inst->step_frames    = dlsym(inst->handle, "pfr_game_step_frames");
    inst->save_hot       = dlsym(inst->handle, "pfr_game_save_hot");
    inst->restore_hot    = dlsym(inst->handle, "pfr_game_restore_hot");
    inst->get_reward_info = dlsym(inst->handle, "pfr_game_get_reward_info");

    if (!inst->boot || !inst->load_state || !inst->step_frames ||
        !inst->save_hot || !inst->restore_hot || !inst->get_reward_info) {
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <libpfr_game.so> <savestate.pfrstate>\n", argv[0]);
        return 1;
    }

    const char *so_path = argv[1];
    const char *state_path = argv[2];

    struct GameInstance inst_a, inst_b;

    printf("=== Loading instance A ===\n");
    if (load_instance(&inst_a, so_path) != 0) return 1;
    printf("Instance A loaded: handle=%p\n", inst_a.handle);

    printf("=== Loading instance B ===\n");
    if (load_instance(&inst_b, so_path) != 0) return 1;
    printf("Instance B loaded: handle=%p\n", inst_b.handle);

    printf("\n=== Booting instance A ===\n");
    inst_a.boot();
    printf("=== Booting instance B ===\n");
    inst_b.boot();

    printf("\n=== Loading savestate into instance A ===\n");
    if (inst_a.load_state(state_path) != 0) {
        fprintf(stderr, "Failed to load savestate into A\n");
        return 1;
    }
    printf("=== Loading savestate into instance B ===\n");
    if (inst_b.load_state(state_path) != 0) {
        fprintf(stderr, "Failed to load savestate into B\n");
        return 1;
    }

    /* Step A with DPAD_RIGHT (0x10), step B with DPAD_UP (0x40) */
    printf("\n=== Stepping A right 30 frames, B up 30 frames ===\n");
    inst_a.step_frames(0x10, 30);  /* A: DPAD_RIGHT */
    inst_b.step_frames(0x40, 30);  /* B: DPAD_UP */

    PfrRewardInfo info_a, info_b;
    inst_a.get_reward_info(&info_a);
    inst_b.get_reward_info(&info_b);

    printf("A: pos=(%d,%d) map=(%d,%d) badges=0x%02x party=%d money=%u\n",
           info_a.player_x, info_a.player_y,
           info_a.map_group, info_a.map_num,
           info_a.badges, info_a.party_count, info_a.money);
    printf("B: pos=(%d,%d) map=(%d,%d) badges=0x%02x party=%d money=%u\n",
           info_b.player_x, info_b.player_y,
           info_b.map_group, info_b.map_num,
           info_b.badges, info_b.party_count, info_b.money);

    int independent = (info_a.player_x != info_b.player_x) ||
                      (info_a.player_y != info_b.player_y);
    printf("\nInstances independent: %s\n",
           independent ? "YES" : "MAYBE (same pos, try different inputs)");

    /* Benchmark: step A for 1200 frames */
    printf("\n=== Benchmark: 1200 fast-frames on instance A ===\n");
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    inst_a.step_frames(0, 1200);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("1200 frames in %.4f s = %.0f FPS\n", elapsed, 1200.0 / elapsed);

    dlclose(inst_a.handle);
    dlclose(inst_b.handle);

    printf("\nTest complete.\n");
    return 0;
}
