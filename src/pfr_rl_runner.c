#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <execinfo.h>

#include "global.h"

#include "pfr_firered_state.h"
#include "pfr_rl_core.h"

static struct PfrRlCore sCore;

struct RunnerArgs
{
    char savePath[4096];
    char statePath[4096];
};

static void crash_handler(int sig)
{
    void *frames[32];
    int n = backtrace(frames, 32);
    fprintf(stderr, "\n=== CRASH: signal %d ===\n", sig);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    _exit(128 + sig);
}

static bool8 WritePacket(void)
{
    struct PfrRlPacket packet;

    PfrRlCoreFillPacket(&sCore, &packet);
    if (fwrite(&packet, sizeof(packet), 1, stdout) != 1)
        return FALSE;
    fflush(stdout);
    return TRUE;
}

static bool8 HandleCommand(const char *line)
{
    unsigned buttons;
    unsigned frames;

    if (strncmp(line, "reset", 5) == 0)
    {
        if (!PfrRlCoreReset(&sCore))
            return FALSE;
        return WritePacket();
    }

    if (sscanf(line, "step %u %u", &buttons, &frames) == 2)
    {
        if (!PfrRlCoreStep(&sCore, (u16)buttons, frames))
            return FALSE;
        return WritePacket();
    }

    if (strncmp(line, "quit", 4) == 0)
        return FALSE;

    fprintf(stderr, "runner command failed: unknown command: %s", line);
    return FALSE;
}

static bool8 ParseArgs(int argc, char **argv, struct RunnerArgs *args)
{
    int i;

    memset(args, 0, sizeof(*args));
    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--save") == 0 && i + 1 < argc)
        {
            snprintf(args->savePath, sizeof(args->savePath), "%s", argv[++i]);
        }
        else if (strcmp(argv[i], "--state") == 0 && i + 1 < argc)
        {
            snprintf(args->statePath, sizeof(args->statePath), "%s", argv[++i]);
        }
        else
        {
            fprintf(stderr, "usage: %s --save <path> [--state <path>]\n", argv[0]);
            return FALSE;
        }
    }

    if (args->savePath[0] == '\0')
    {
        fprintf(stderr, "usage: %s --save <path> [--state <path>]\n", argv[0]);
        return FALSE;
    }
    return TRUE;
}

int main(int argc, char **argv)
{
    struct RunnerArgs args;
    char line[128];

    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (!ParseArgs(argc, argv, &args))
        return 2;

    if (!PfrRlCoreInit(&sCore, args.savePath, args.statePath[0] != '\0' ? args.statePath : NULL))
    {
        fprintf(stderr, "runner boot failed: %s\n", PfrRlCoreLastError(&sCore));
        return 1;
    }
    if (!WritePacket())
    {
        fprintf(stderr, "runner failed to write initial packet\n");
        return 1;
    }

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        if (!HandleCommand(line))
            break;
    }

    return 0;
}
