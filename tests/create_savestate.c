/*
 * create_savestate.c - Create a savestate from libpfr_game.so
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

int main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (argc < 3) {
        fprintf(stderr, "Usage: %s so_path output_path [skip_frames]\n", argv[0]);
        return 1;
    }

    const char *so_path = argv[1];
    const char *out_path = argv[2];
    int skip_frames = (argc >= 4) ? atoi(argv[3]) : 3000;

    fprintf(stderr, "Loading %s...\n", so_path);
    void *h = dlmopen(LM_ID_NEWLM, so_path, RTLD_NOW);
    if (!h) { fprintf(stderr, "dlmopen: %s\n", dlerror()); return 1; }

    void (*boot)(void) = dlsym(h, "pfr_game_boot");
    int  (*save_state)(const char*) = dlsym(h, "pfr_game_save_state");
    void (*step)(uint16_t, int) = dlsym(h, "pfr_game_step_frames");

    if (!boot || !step) {
        fprintf(stderr, "Missing boot/step symbols\n");
        return 1;
    }

    fprintf(stderr, "Booting game...\n");
    boot();

    fprintf(stderr, "Fast-forwarding %d frames...\n", skip_frames);
    for (int i = 0; i < skip_frames; i += 30) {
        step(0x01, 15);
        step(0x00, 15);
        if (i % 300 == 0)
            fprintf(stderr, "  frame %d/%d\n", i, skip_frames);
    }

    fprintf(stderr, "Saving state to %s...\n", out_path);
    if (!save_state) {
        fprintf(stderr, "pfr_game_save_state not found\n");
        return 1;
    }

    int rc = save_state(out_path);
    if (rc != 0) {
        fprintf(stderr, "Save failed\n");
        return 1;
    }

    fprintf(stderr, "Done.\n");
    dlclose(h);
    return 0;
}
