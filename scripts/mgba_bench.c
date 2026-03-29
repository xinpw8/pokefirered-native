#include <mgba/flags.h>

#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GBA_W 240
#define GBA_H 160

static void NullLog(struct mLogger *logger, int category, enum mLogLevel level,
                    const char *format, va_list args)
{
    (void)logger;
    (void)category;
    (void)level;
    (void)format;
    (void)args;
}

static int ParseFramesOption(const char *value, unsigned *outFrames)
{
    char *end = NULL;
    unsigned long parsed;

    if (value == NULL || value[0] == '\0')
        return 0;

    parsed = strtoul(value, &end, 10);
    if (end == NULL || *end != '\0' || parsed == 0 || parsed > 1000000UL)
        return 0;

    *outFrames = (unsigned)parsed;
    return 1;
}

static double TimespecDiffSeconds(const struct timespec *start, const struct timespec *end)
{
    time_t seconds = end->tv_sec - start->tv_sec;
    long nanoseconds = end->tv_nsec - start->tv_nsec;

    if (nanoseconds < 0)
    {
        seconds--;
        nanoseconds += 1000000000L;
    }

    return (double)seconds + (double)nanoseconds / 1000000000.0;
}

int main(int argc, char **argv)
{
    const char *romPath;
    const char *savePath = NULL;
    unsigned frames = 240000;
    struct mLogger logger = { .log = NullLog };
    struct mCore *core;
    struct mCoreOptions opts;
    unsigned width;
    unsigned height;
    size_t stride;
    mColor *videoBuffer;
    struct timespec startTime;
    struct timespec endTime;
    double elapsedSeconds;
    double emulatedFps;
    int argi;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <rom.gba> [--frames N] [--save path]\n", argv[0]);
        return 2;
    }

    romPath = argv[1];
    for (argi = 2; argi < argc; argi++)
    {
        if (strcmp(argv[argi], "--frames") == 0 && argi + 1 < argc)
        {
            if (!ParseFramesOption(argv[argi + 1], &frames))
            {
                fprintf(stderr, "mgba_bench: invalid frame count %s\n", argv[argi + 1]);
                return 2;
            }
            argi++;
        }
        else if (strcmp(argv[argi], "--save") == 0 && argi + 1 < argc)
        {
            savePath = argv[++argi];
        }
        else if (strcmp(argv[argi], "--help") == 0)
        {
            fprintf(stderr, "Usage: %s <rom.gba> [--frames N] [--save path]\n", argv[0]);
            return 0;
        }
        else
        {
            fprintf(stderr, "mgba_bench: unknown option %s\n", argv[argi]);
            return 2;
        }
    }

    mLogSetDefaultLogger(&logger);

    core = mCoreFind(romPath);
    if (!core)
    {
        fprintf(stderr, "mgba_bench: cannot identify ROM %s\n", romPath);
        return 1;
    }
    if (!core->init(core))
    {
        fprintf(stderr, "mgba_bench: core init failed\n");
        return 1;
    }

    mCoreInitConfig(core, "benchmark");
    memset(&opts, 0, sizeof(opts));
    opts.audioSync = false;
    opts.videoSync = false;
    mCoreConfigLoadDefaults(&core->config, &opts);

    core->currentVideoSize(core, &width, &height);
    if (width == 0)
        width = GBA_W;
    if (height == 0)
        height = GBA_H;
    stride = width;
    videoBuffer = calloc(width * height, sizeof(*videoBuffer));
    if (!videoBuffer)
    {
        fprintf(stderr, "mgba_bench: out of memory\n");
        mCoreConfigDeinit(&core->config);
        core->deinit(core);
        return 1;
    }

    core->setVideoBuffer(core, videoBuffer, stride);
    core->setAudioBufferSize(core, 1024);

    if (!mCoreLoadFile(core, romPath))
    {
        fprintf(stderr, "mgba_bench: cannot load ROM %s\n", romPath);
        free(videoBuffer);
        mCoreConfigDeinit(&core->config);
        core->deinit(core);
        return 1;
    }

    if (savePath != NULL)
        mCoreLoadSaveFile(core, savePath, false);
    else
        mCoreAutoloadSave(core);

    core->reset(core);
    core->setKeys(core, 0);

    if (clock_gettime(CLOCK_MONOTONIC, &startTime) != 0)
    {
        fprintf(stderr, "mgba_bench: failed to read benchmark clock\n");
        core->unloadROM(core);
        free(videoBuffer);
        mCoreConfigDeinit(&core->config);
        core->deinit(core);
        return 1;
    }

    for (unsigned frame = 0; frame < frames; frame++)
        core->runFrame(core);

    if (clock_gettime(CLOCK_MONOTONIC, &endTime) != 0)
    {
        fprintf(stderr, "mgba_bench: failed to read benchmark clock\n");
        core->unloadROM(core);
        free(videoBuffer);
        mCoreConfigDeinit(&core->config);
        core->deinit(core);
        return 1;
    }

    elapsedSeconds = TimespecDiffSeconds(&startTime, &endTime);
    emulatedFps = (elapsedSeconds > 0.0) ? (double)frames / elapsedSeconds : 0.0;

    printf("benchmark_result mode=mgba emulated_frames=%u presents=%u elapsed_seconds=%.6f emulated_fps=%.2f speedup_vs_realtime=%.2f\n",
           frames,
           frames,
           elapsedSeconds,
           emulatedFps,
           emulatedFps / 60.0);

    core->unloadROM(core);
    free(videoBuffer);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    return 0;
}
