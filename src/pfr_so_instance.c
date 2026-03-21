/*
 * pfr_so_instance.c — SO-copy instance manager implementation
 *
 * Strategy: make N file copies of libpfr_game.so, dlopen each one.
 * Each copy gets its own .data/.bss (independent globals) because
 * dlopen sees distinct inodes. Symlinks would NOT work — they share
 * the inode and dlopen would deduplicate them.
 *
 * RTLD_LOCAL is critical: it prevents symbol interposition between
 * instances, so each copy's globals are truly isolated.
 */

#include "pfr_so_instance.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

/* Copy a file byte-by-byte. Returns 0 on success. */
static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in) {
        fprintf(stderr, "pfr_so_instance: cannot open %s: %s\n",
                src, strerror(errno));
        return -1;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fprintf(stderr, "pfr_so_instance: cannot create %s: %s\n",
                dst, strerror(errno));
        fclose(in);
        return -1;
    }

    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fprintf(stderr, "pfr_so_instance: write error on %s\n", dst);
            fclose(in);
            fclose(out);
            return -1;
        }
    }

    fclose(in);
    fclose(out);
    return 0;
}

/* Resolve all 7 exported function pointers from the dlopen'd handle. */
static int resolve_symbols(PfrInstance *inst)
{
    #define RESOLVE(field, name) do { \
        *(void **)(&inst->field) = dlsym(inst->dl_handle, name); \
        if (!inst->field) { \
            fprintf(stderr, "pfr_so_instance[%d]: dlsym(%s) failed: %s\n", \
                    inst->instance_id, name, dlerror()); \
            return -1; \
        } \
    } while (0)

    RESOLVE(boot,            "pfr_game_boot");
    RESOLVE(load_state,      "pfr_game_load_state");
    RESOLVE(save_state,      "pfr_game_save_state");
    RESOLVE(save_hot,        "pfr_game_save_hot");
    RESOLVE(restore_hot,     "pfr_game_restore_hot");
    RESOLVE(step_frames,     "pfr_game_step_frames");
    RESOLVE(step_frames_fast, "pfr_game_step_frames_fast");
    RESOLVE(extract_obs,     "pfr_game_extract_obs");
    RESOLVE(get_reward_info, "pfr_game_get_reward_info");

    /* Native-format obs adapter (optional — not fatal if missing) */
    *(void **)(&inst->extract_obs_native) = dlsym(inst->dl_handle,
        "pfr_game_extract_obs_native_format");
    /* extract_obs_native is allowed to be NULL for backward compat */

    /* Exact rendering exports */
    RESOLVE(step_frames_exact, "pfr_game_step_frames_exact");
    RESOLVE(get_framebuffer,   "pfr_game_get_framebuffer");
    RESOLVE(copy_framebuffer,  "pfr_game_copy_framebuffer");

    #undef RESOLVE
    return 0;
}

int pfr_instances_create(const char *base_so_path, const char *tmp_dir,
                         PfrInstance *instances, int n)
{
    pid_t pid = getpid();

    /* Ensure tmp_dir exists (ignore EEXIST) */
    mkdir(tmp_dir, 0755);

    for (int i = 0; i < n; i++) {
        PfrInstance *inst = &instances[i];
        memset(inst, 0, sizeof(*inst));
        inst->instance_id = i;

        /* Generate unique path: {tmp_dir}/pfr_game_{pid}_{id}.so */
        snprintf(inst->so_path, sizeof(inst->so_path),
                 "%s/pfr_game_%d_%d.so", tmp_dir, (int)pid, i);

        /* Copy the .so file (must be a real copy, not a symlink) */
        if (copy_file(base_so_path, inst->so_path) != 0) {
            fprintf(stderr, "pfr_so_instance: failed to copy %s -> %s\n",
                    base_so_path, inst->so_path);
            pfr_instances_destroy(instances, i);
            return -1;
        }

        /* dlopen with RTLD_LOCAL to prevent symbol interposition */
        inst->dl_handle = dlopen(inst->so_path, RTLD_NOW | RTLD_LOCAL);
        if (!inst->dl_handle) {
            fprintf(stderr, "pfr_so_instance[%d]: dlopen failed: %s\n",
                    i, dlerror());
            unlink(inst->so_path);
            pfr_instances_destroy(instances, i);
            return -1;
        }

        /* Resolve all function pointers */
        if (resolve_symbols(inst) != 0) {
            dlclose(inst->dl_handle);
            unlink(inst->so_path);
            pfr_instances_destroy(instances, i);
            return -1;
        }

        fprintf(stderr, "pfr_so_instance[%d]: loaded %s\n", i, inst->so_path);
    }

    return 0;
}

void pfr_instances_destroy(PfrInstance *instances, int n)
{
    for (int i = 0; i < n; i++) {
        PfrInstance *inst = &instances[i];
        if (inst->dl_handle) {
            dlclose(inst->dl_handle);
            inst->dl_handle = NULL;
        }
        if (inst->so_path[0]) {
            unlink(inst->so_path);
            inst->so_path[0] = '\0';
        }
    }
}
