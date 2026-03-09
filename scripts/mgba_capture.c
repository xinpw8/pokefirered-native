/*
 * mgba_capture.c — Headless golden frame capture using mGBA core library
 *
 * Boots a GBA ROM with no save data, replays scripted input, and captures
 * screenshots at specified frame numbers as PPM files.
 *
 * Build (handled by capture_golden_frames.sh):
 *   gcc -O2 -o mgba_capture scripts/mgba_capture.c \
 *       -I/path/to/mgba/include \
 *       -L/path/to/mgba/build -lmgba -lm -lpthread
 *
 * Usage:
 *   mgba_capture <rom.gba> <manifest.txt> <input_file> <output_dir>
 *
 * The input file argument is kept for CLI compatibility with older scripts,
 * but the canonical input path is taken from the manifest and validated
 * against the passed path.
 */

/* Include mGBA build flags first to match struct layouts with the library */
#include <mgba/flags.h>

#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba-util/vfs.h>
#include <mgba-util/image.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GBA_W 240
#define GBA_H 160

#include "host_capture_manifest.h"

static void write_ppm(const char *path, const mColor *pixels, unsigned w, unsigned h, size_t stride) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Cannot write: %s\n", path); return; }
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    unsigned x, y;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            mColor c = pixels[y * stride + x];
            /* mGBA native 32-bit color is XBGR8888:
             * bits  0-7  = red
             * bits  8-15 = green
             * bits 16-23 = blue
             * bits 24-31 = unused
             */
            unsigned char rgb[3];
            rgb[0] = (c >>  0) & 0xFF;
            rgb[1] = (c >>  8) & 0xFF;
            rgb[2] = (c >> 16) & 0xFF;
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

/* Suppress mGBA log spam */
static void _nullLog(struct mLogger *logger, int category, enum mLogLevel level,
                     const char *format, va_list args) {
    (void)logger; (void)category; (void)level; (void)format; (void)args;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Usage: mgba_capture <rom.gba> <manifest.txt> <input_file> <output_dir>\n");
        return 2;
    }
    const char *rom_path = argv[1];
    const char *manifest_path = argv[2];
    const char *input_path = argv[3];
    const char *output_dir = argv[4];
    struct HostCaptureManifest manifest;
    struct HostCaptureInputScript input_script;
    int num_milestones;
    int num_inputs;

    if (!HostCaptureLoadManifest(manifest_path, &manifest)) {
        fprintf(stderr, "No milestones\n");
        return 2;
    }
    if (strcmp(input_path, manifest.input_path) != 0) {
        fprintf(stderr,
                "Input mismatch: manifest requests %s but argv supplied %s\n",
                manifest.input_path, input_path);
        return 2;
    }
    if (!HostCaptureLoadInputScript(manifest.input_path, &input_script)) {
        return 2;
    }

    num_milestones = manifest.milestone_count;
    num_inputs = input_script.range_count;

    /* Set up quiet logger */
    struct mLogger logger = { .log = _nullLog };
    mLogSetDefaultLogger(&logger);

    struct mCore *core = mCoreFind(rom_path);
    if (!core) { fprintf(stderr, "Cannot identify ROM\n"); return 1; }
    core->init(core);

    /* Configure core (needed to avoid crashes from uninitialized subsystems) */
    mCoreInitConfig(core, "capture");
    struct mCoreOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.audioSync = false;
    opts.videoSync = false;
    mCoreConfigLoadDefaults(&core->config, &opts);

    /* Allocate video buffer (critical for headless mode) */
    unsigned w, h;
    core->currentVideoSize(core, &w, &h);
    size_t stride = w;
    mColor *videoBuffer = calloc(w * h, sizeof(mColor));
    if (!videoBuffer) { fprintf(stderr, "Out of memory\n"); return 1; }
    core->setVideoBuffer(core, videoBuffer, stride);

    /* Set audio buffer size to avoid NULL audio state */
    core->setAudioBufferSize(core, 1024);

    if (!mCoreLoadFile(core, rom_path)) {
        fprintf(stderr, "Cannot load ROM: %s\n", rom_path);
        return 1;
    }

    core->reset(core);

    int last_frame = manifest.milestones[num_milestones - 1].frame + 10;
    int captured = 0;
    int next_ms = 0;

    printf("mgba_capture: %d milestones, %d input ranges, running to frame %d\n",
           num_milestones, num_inputs, last_frame);

    int frame;
    for (frame = 1; frame <= last_frame; frame++) {
        /* Apply input for this frame */
        unsigned keys = HostCaptureButtonsForFrame(&input_script, frame);
        core->setKeys(core, keys);

        /* Run one frame */
        core->runFrame(core);

        /* Check milestones */
        if (next_ms < num_milestones && frame == manifest.milestones[next_ms].frame) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s.ppm", output_dir, manifest.milestones[next_ms].name);

            size_t out_stride;
            const void *pixels;
            core->getPixels(core, &pixels, &out_stride);
            if (pixels) {
                write_ppm(path, (const mColor *)pixels, w, h, out_stride);
                captured++;
                printf("  [%d/%d] Captured '%s' at frame %d -> %s\n",
                       captured, num_milestones, manifest.milestones[next_ms].name, frame, path);
            } else {
                fprintf(stderr, "  WARNING: No pixels at frame %d for '%s'\n",
                        frame, manifest.milestones[next_ms].name);
            }
            next_ms++;
        }
    }

    core->unloadROM(core);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    free(videoBuffer);

    printf("mgba_capture: done (%d/%d captured)\n", captured, num_milestones);
    return (captured == num_milestones) ? 0 : 1;
}
