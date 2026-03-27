/*
 * pfr_so_instance.h — SO-copy instance manager for parallel PFRN envs
 *
 * Each PfrInstance is a dlopen'd copy of libpfr_game.so with its own
 * .data/.bss segments (independent globals). At init time:
 *   1. cp libpfr_game.so → /tmp/pfr_game_N.so  (one per env)
 *   2. dlopen each copy → independent game instance
 *   3. Resolve function pointers
 *
 * The .text pages are shared by the kernel (copy-on-write), so N copies
 * only cost ~2.6MB mutable memory each, not 17MB.
 */

#ifndef PFR_SO_INSTANCE_H
#define PFR_SO_INSTANCE_H

#include <stdint.h>

/* Function pointer types matching pfr_game_api.h exports */
typedef void     (*pfr_boot_fn)(void);
typedef int      (*pfr_load_state_fn)(const char *path);
typedef int      (*pfr_save_state_fn)(const char *path);
typedef void     (*pfr_save_hot_fn)(void);
typedef void     (*pfr_restore_hot_fn)(void);
typedef void     (*pfr_step_frames_fn)(uint16_t keys, int n);
typedef void     (*pfr_extract_obs_fn)(unsigned char *buf);
typedef void     (*pfr_extract_obs_native_fn)(unsigned char *buf);
typedef void     (*pfr_get_reward_info_fn)(void *info);

/* Fast training function pointer type */
typedef void     (*pfr_step_frames_fast_fn)(uint16_t keys, int n);

typedef void     (*pfr_randomize_spawn_fn)(void);

/* Exact rendering function pointer types */
typedef void            (*pfr_step_frames_exact_fn)(uint16_t keys, int n);
typedef const uint32_t *(*pfr_get_framebuffer_fn)(void);
typedef void            (*pfr_copy_framebuffer_fn)(uint32_t *dst, int stride);
typedef void            (*pfr_render_current_frame_fn)(void);

typedef struct {
    void *dl_handle;                    /* dlopen handle */
    char so_path[512];                  /* path to the copied .so file */
    int  instance_id;

    /* Resolved function pointers */
    pfr_boot_fn           boot;
    pfr_load_state_fn     load_state;
    pfr_save_state_fn     save_state;
    pfr_save_hot_fn       save_hot;
    pfr_restore_hot_fn    restore_hot;
    pfr_step_frames_fn    step_frames;
    pfr_step_frames_fast_fn step_frames_fast;
    pfr_extract_obs_fn    extract_obs;
    pfr_extract_obs_native_fn extract_obs_native;
    pfr_get_reward_info_fn get_reward_info;

    /* Exact rendering */
    pfr_step_frames_exact_fn step_frames_exact;
    pfr_get_framebuffer_fn   get_framebuffer;
    pfr_copy_framebuffer_fn  copy_framebuffer;
    pfr_render_current_frame_fn render_current_frame;

    pfr_randomize_spawn_fn randomize_spawn;
} PfrInstance;

/*
 * Create N independent game instances by copying libpfr_game.so.
 * base_so_path: path to the original libpfr_game.so
 * tmp_dir: directory for copies (e.g. "/tmp/pfr_instances")
 * instances: pre-allocated array of PfrInstance[n]
 * n: number of instances to create
 * Returns 0 on success, -1 on failure.
 */
int pfr_instances_create(const char *base_so_path, const char *tmp_dir,
                         PfrInstance *instances, int n);

/*
 * Destroy all instances: dlclose handles and unlink temp .so files.
 */
void pfr_instances_destroy(PfrInstance *instances, int n);

#endif /* PFR_SO_INSTANCE_H */
