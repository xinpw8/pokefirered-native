/*
 * host_display.c — Display backend for pokefirered-native
 *
 * Two modes:
 *   1. PPM frame dump (always available, no dependencies)
 *   2. SDL2 window (when HOST_DISPLAY_SDL2 is defined and SDL2 is linked)
 *
 * The PPM path writes each frame as /tmp/pfr_frame_NNNN.ppm for verification.
 * The SDL2 path opens a window and maps keyboard input to REG_KEYINPUT.
 */

/*
 * Include system headers BEFORE our project headers to avoid the
 * upstream abs() macro (defined in global.h) colliding with stdlib's
 * abs() function declaration when SDL2 is active.
 */
#ifdef HOST_DISPLAY_SDL2
#include <SDL2/SDL.h>
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Undo upstream abs macro before any further system header interaction */
#ifdef abs
#undef abs
#endif

#include "host_display.h"
#include "host_renderer.h"
#include "host_savestate.h"
#include "gba/gba.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

/* ---------- Frame dump backend (always available) ---------- */

static u32 sFrameCount = 0;
static char sDumpDir[256] = "/tmp";
static char sStateDir[PATH_MAX] = "pfr_debug_states";
static bool8 sDumpEnabled = FALSE;
static bool8 sForceHeadless = FALSE;
static bool8 sUnthrottled = FALSE;
static u32 sActionBits = 0;
static u32 sFastForwardFactor = 1;
static u32 sBaseFastForwardFactor = 1;
static const u32 sTurboPresetOptions[] = { 2, 4, 8, 16, 32, 64, 128, 256 };
static u32 sTurboFastForwardFactor = 16;
static size_t sTurboPresetIndex = 3;
static bool8 sTurboEnabled = FALSE;
static char sStatusText[64];
static u32 sStatusTextFrames = 0;

#define HOST_DISPLAY_OVERLAY_MAX_ENTRIES 256
#define HOST_DISPLAY_OVERLAY_ROWS 10

struct HostDisplayOverlayEntry
{
    char path[PATH_MAX];
    char name[NAME_MAX + 1];
    bool8 isDirectory;
    time_t mtime;
};

struct HostDisplayOverlayState
{
    bool8 active;
    bool8 loadMode;
    bool8 refreshRequested;
    bool8 truncated;
    int selectedIndex;
    int scrollIndex;
    int entryCount;
    int viewCount;
    int viewIndices[HOST_DISPLAY_OVERLAY_MAX_ENTRIES];
    char currentDir[PATH_MAX];
    char input[PATH_MAX];
    char lastFilter[PATH_MAX];
    char message[160];
    char committedPath[PATH_MAX];
    bool8 committedPathReady;
    struct HostDisplayOverlayEntry entries[HOST_DISPLAY_OVERLAY_MAX_ENTRIES];
};

static struct HostDisplayOverlayState sOverlayState;

static bool8 ConfigFlagEnabled(const char *name)
{
    const char *value = getenv(name);

    return value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
}

static u32 ConfigU32(const char *name, u32 fallback)
{
    const char *value = getenv(name);
    char *end = NULL;
    unsigned long parsed;

    if (value == NULL || value[0] == '\0')
        return fallback;

    parsed = strtoul(value, &end, 10);
    if (end == NULL || *end != '\0' || parsed == 0 || parsed > 1000000ul)
        return fallback;

    return (u32)parsed;
}

static size_t FirstTurboPresetIndex(void)
{
    size_t count = sizeof(sTurboPresetOptions) / sizeof(sTurboPresetOptions[0]);
    size_t i;

    for (i = 0; i < count; i++)
    {
        if (sTurboPresetOptions[i] >= sBaseFastForwardFactor)
            return i;
    }

    return count - 1;
}

static void SyncTurboPresetFromFactor(void)
{
    size_t count = sizeof(sTurboPresetOptions) / sizeof(sTurboPresetOptions[0]);
    size_t first = FirstTurboPresetIndex();
    size_t i;

    if (sBaseFastForwardFactor > sTurboPresetOptions[count - 1])
    {
        sTurboPresetIndex = count - 1;
        sTurboFastForwardFactor = sBaseFastForwardFactor;
        return;
    }

    sTurboPresetIndex = first;
    for (i = first; i < count; i++)
    {
        sTurboPresetIndex = i;
        if (sTurboPresetOptions[i] >= sTurboFastForwardFactor)
            break;
    }

    sTurboFastForwardFactor = sTurboPresetOptions[sTurboPresetIndex];
}

static void SetStatusText(const char *text)
{
    snprintf(sStatusText, sizeof(sStatusText), "%s", text);
    sStatusTextFrames = 180;
}

static void UpdateTurboToggleStatus(void)
{
    char buffer[64];

    if (sTurboEnabled)
        snprintf(buffer, sizeof(buffer), "TURBO ON X%u", sTurboFastForwardFactor);
    else
        snprintf(buffer, sizeof(buffer), "TURBO OFF");

    SetStatusText(buffer);
}

static void CycleTurboPreset(void)
{
    size_t count = sizeof(sTurboPresetOptions) / sizeof(sTurboPresetOptions[0]);
    size_t first = FirstTurboPresetIndex();
    char buffer[64];

    if (sBaseFastForwardFactor > sTurboPresetOptions[count - 1])
    {
        sTurboFastForwardFactor = sBaseFastForwardFactor;
        snprintf(buffer, sizeof(buffer), sTurboEnabled ? "TURBO X%u" : "TURBO READY X%u", sTurboFastForwardFactor);
        SetStatusText(buffer);
        return;
    }

    if (sTurboPresetIndex < first || sTurboPresetIndex >= count)
        sTurboPresetIndex = first;
    else
        sTurboPresetIndex++;

    if (sTurboPresetIndex >= count)
        sTurboPresetIndex = first;

    sTurboFastForwardFactor = sTurboPresetOptions[sTurboPresetIndex];
    snprintf(buffer, sizeof(buffer), sTurboEnabled ? "TURBO X%u" : "TURBO READY X%u", sTurboFastForwardFactor);
    SetStatusText(buffer);
}

static void InitDisplayConfig(void)
{
    static bool8 sConfigured = FALSE;

    if (sConfigured)
        return;

    sForceHeadless = ConfigFlagEnabled("PFR_HEADLESS");
    sUnthrottled = ConfigFlagEnabled("PFR_UNTHROTTLED");
    if (sUnthrottled)
    {
        if (sForceHeadless)
            sFastForwardFactor = ConfigU32("PFR_FAST_FORWARD_FACTOR", 4096);
        else
            sFastForwardFactor = ConfigU32("PFR_FAST_FORWARD_FACTOR", 16);
    }
    else
    {
        sFastForwardFactor = 1;
    }
    sBaseFastForwardFactor = sFastForwardFactor;
    if (sForceHeadless)
    {
        sTurboFastForwardFactor = ConfigU32("PFR_TURBO_FACTOR", 4096);
    }
    else
    {
        sTurboFastForwardFactor = ConfigU32("PFR_TURBO_FACTOR", 16);
        SyncTurboPresetFromFactor();
    }
    if (sTurboFastForwardFactor < sBaseFastForwardFactor)
        sTurboFastForwardFactor = sBaseFastForwardFactor;
    sConfigured = TRUE;
}

void HostDisplaySetDumpDir(const char *dir)
{
    snprintf(sDumpDir, sizeof(sDumpDir), "%s", dir);
}

void HostDisplayEnableDump(bool8 enable)
{
    sDumpEnabled = enable;
}

static void DumpFramePPM(const u32 *fb)
{
    char path[512];
    FILE *f;

    snprintf(path, sizeof(path), "%s/pfr_frame_%04u.ppm", sDumpDir, sFrameCount);
    f = fopen(path, "wb");
    if (!f)
        return;

    fprintf(f, "P6\n%d %d\n255\n", GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT);

    {
        int i;
        for (i = 0; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; i++)
        {
            u32 pixel = fb[i];
            u8 rgb[3];
            rgb[0] = (pixel >> 16) & 0xFF;  /* R */
            rgb[1] = (pixel >> 8) & 0xFF;   /* G */
            rgb[2] = pixel & 0xFF;           /* B */
            fwrite(rgb, 1, 3, f);
        }
    }

    fclose(f);
}

static bool8 HeadlessDisplayInit(void)
{
    if (sDumpEnabled)
        fprintf(stderr, "host_display: headless mode with frame dump to %s/\n", sDumpDir);
    return TRUE;
}

static bool8 HeadlessDisplayPresentImpl(void)
{
    const u32 *fb = HostRendererGetFramebuffer();

    if (sDumpEnabled)
        DumpFramePPM(fb);

    sFrameCount++;
    return TRUE;
}

/* ---------- SDL2 backend (conditional) ---------- */

#ifdef HOST_DISPLAY_SDL2

static SDL_Window *sWindow = NULL;
static SDL_Renderer *sSDLRenderer = NULL;
static SDL_Texture *sTexture = NULL;
static bool8 sSDLInit = FALSE;

/* GBA button → SDL scancode mapping */
static const struct {
    SDL_Scancode key;
    u16 gba_bit;
} sKeyMap[] = {
    { SDL_SCANCODE_Z,      0x0001 },  /* A */
    { SDL_SCANCODE_X,      0x0002 },  /* B */
    { SDL_SCANCODE_RSHIFT, 0x0004 },  /* Select */
    { SDL_SCANCODE_RETURN, 0x0008 },  /* Start */
    { SDL_SCANCODE_RIGHT,  0x0010 },  /* Right */
    { SDL_SCANCODE_LEFT,   0x0020 },  /* Left */
    { SDL_SCANCODE_UP,     0x0040 },  /* Up */
    { SDL_SCANCODE_DOWN,   0x0080 },  /* Down */
    { SDL_SCANCODE_S,      0x0100 },  /* R */
    { SDL_SCANCODE_A,      0x0200 },  /* L */
};

static bool8 EnsureDirectoryPath(const char *path)
{
    char scratch[PATH_MAX];
    size_t len;
    size_t i;

    if (path == NULL || path[0] == '\0')
        return FALSE;

    len = strlen(path);
    if (len >= sizeof(scratch))
        return FALSE;

    snprintf(scratch, sizeof(scratch), "%s", path);
    for (i = 1; i < len; i++)
    {
        if (scratch[i] != '/')
            continue;

        scratch[i] = '\0';
        if (scratch[0] != '\0' && mkdir(scratch, 0775) != 0 && errno != EEXIST)
            return FALSE;
        scratch[i] = '/';
    }

    if (mkdir(scratch, 0775) != 0 && errno != EEXIST)
        return FALSE;

    return TRUE;
}

static bool8 BuildChildPath(char *outPath, size_t outSize, const char *dirPath, const char *name)
{
    size_t dirLen;
    size_t nameLen;

    if (outPath == NULL || outSize == 0 || dirPath == NULL || name == NULL)
        return FALSE;

    dirLen = strlen(dirPath);
    nameLen = strlen(name);
    if (dirLen == 0 || dirLen + 1 + nameLen + 1 > outSize)
        return FALSE;

    memcpy(outPath, dirPath, dirLen);
    outPath[dirLen] = '/';
    memcpy(outPath + dirLen + 1, name, nameLen + 1);
    return TRUE;
}

static bool8 PathIsDirectory(const char *path)
{
    struct stat st;

    return path != NULL && path[0] != '\0' && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void OverlaySetMessage(const char *message)
{
    snprintf(sOverlayState.message, sizeof(sOverlayState.message), "%s", message);
}

static void OverlayAdjustScroll(void)
{
    int count = sOverlayState.viewCount;

    if (sOverlayState.selectedIndex < 0)
        sOverlayState.selectedIndex = 0;
    if (sOverlayState.selectedIndex >= count)
        sOverlayState.selectedIndex = count - 1;
    if (sOverlayState.selectedIndex < 0)
        sOverlayState.selectedIndex = 0;

    if (sOverlayState.selectedIndex < sOverlayState.scrollIndex)
        sOverlayState.scrollIndex = sOverlayState.selectedIndex;
    if (sOverlayState.selectedIndex >= sOverlayState.scrollIndex + HOST_DISPLAY_OVERLAY_ROWS)
        sOverlayState.scrollIndex = sOverlayState.selectedIndex - HOST_DISPLAY_OVERLAY_ROWS + 1;
    if (sOverlayState.scrollIndex < 0)
        sOverlayState.scrollIndex = 0;
}

static bool8 OverlayCaseContains(const char *haystack, const char *needle)
{
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    size_t i;

    if (nlen == 0)
        return TRUE;
    if (nlen > hlen)
        return FALSE;
    for (i = 0; i <= hlen - nlen; i++)
    {
        if (strncasecmp(haystack + i, needle, nlen) == 0)
            return TRUE;
    }
    return FALSE;
}

static void OverlayRebuildView(void)
{
    int i;
    const char *filter = sOverlayState.input;

    sOverlayState.viewCount = 0;
    for (i = 0; i < sOverlayState.entryCount; i++)
    {
        if (filter[0] != '\0' && sOverlayState.loadMode
            && !sOverlayState.entries[i].isDirectory
            && !OverlayCaseContains(sOverlayState.entries[i].name, filter))
            continue;
        sOverlayState.viewIndices[sOverlayState.viewCount++] = i;
    }
    snprintf(sOverlayState.lastFilter, sizeof(sOverlayState.lastFilter), "%s", filter);

    if (sOverlayState.selectedIndex >= sOverlayState.viewCount)
        sOverlayState.selectedIndex = sOverlayState.viewCount - 1;
    if (sOverlayState.selectedIndex < 0)
        sOverlayState.selectedIndex = 0;
    OverlayAdjustScroll();
}

static void OverlayRefreshEntries(void)
{
    DIR *dir;
    struct dirent *entry;
    int i;

    sOverlayState.entryCount = 0;
    sOverlayState.truncated = FALSE;
    sOverlayState.selectedIndex = 0;
    sOverlayState.scrollIndex = 0;

    dir = opendir(sOverlayState.currentDir);
    if (dir == NULL)
    {
        OverlaySetMessage("Directory unavailable");
        return;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        struct stat st;
        char path[PATH_MAX];

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (!BuildChildPath(path, sizeof(path), sOverlayState.currentDir, entry->d_name))
            continue;
        if (stat(path, &st) != 0 || (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)))
            continue;

        if (sOverlayState.entryCount >= HOST_DISPLAY_OVERLAY_MAX_ENTRIES)
        {
            sOverlayState.truncated = TRUE;
            continue;
        }

        snprintf(sOverlayState.entries[sOverlayState.entryCount].path,
                 sizeof(sOverlayState.entries[sOverlayState.entryCount].path),
                 "%s",
                 path);
        snprintf(sOverlayState.entries[sOverlayState.entryCount].name,
                 sizeof(sOverlayState.entries[sOverlayState.entryCount].name),
                 "%s",
                 entry->d_name);
        sOverlayState.entries[sOverlayState.entryCount].isDirectory = S_ISDIR(st.st_mode);
        sOverlayState.entries[sOverlayState.entryCount].mtime = st.st_mtime;
        sOverlayState.entryCount++;
    }

    closedir(dir);

    /* Sort: directories first (alpha ascending), then files by mtime descending */
    for (i = 1; i < sOverlayState.entryCount; i++)
    {
        struct HostDisplayOverlayEntry value = sOverlayState.entries[i];
        int j = i;

        while (j > 0)
        {
            const struct HostDisplayOverlayEntry *prev = &sOverlayState.entries[j - 1];

            if (prev->isDirectory != value.isDirectory)
            {
                if (prev->isDirectory)
                    break;
            }
            else if (prev->isDirectory)
            {
                if (strcasecmp(prev->name, value.name) <= 0)
                    break;
            }
            else
            {
                /* Files: newest first */
                if (prev->mtime >= value.mtime)
                    break;
            }

            sOverlayState.entries[j] = sOverlayState.entries[j - 1];
            j--;
        }

        sOverlayState.entries[j] = value;
    }

    sOverlayState.refreshRequested = FALSE;
    sOverlayState.lastFilter[0] = '\0';
    OverlayRebuildView();
    OverlaySetMessage("");
}

static bool8 ResolveOverlayPath(const char *input, char *outPath, size_t outSize)
{
    if (input == NULL || input[0] == '\0')
        return FALSE;

    if (input[0] == '/')
        snprintf(outPath, outSize, "%s", input);
    else if (!BuildChildPath(outPath, outSize, sOverlayState.currentDir, input))
        return FALSE;

    return TRUE;
}

static bool8 MaybeAppendStateExtension(char *path, size_t pathSize)
{
    const char *slash;
    const char *base;
    const char *dot;
    size_t len;
    static const char sExtension[] = ".pfrstate";

    if (path == NULL || path[0] == '\0')
        return FALSE;

    slash = strrchr(path, '/');
    base = (slash != NULL) ? slash + 1 : path;
    if (base[0] == '\0')
        return FALSE;

    dot = strrchr(base, '.');
    if (dot != NULL)
        return TRUE;

    len = strlen(path);
    if (len + sizeof(sExtension) > pathSize)
        return FALSE;

    memcpy(path + len, sExtension, sizeof(sExtension));
    return TRUE;
}

static void OverlayClose(bool8 keepMessage)
{
    sOverlayState.active = FALSE;
    sOverlayState.refreshRequested = FALSE;
    if (!keepMessage)
        OverlaySetMessage("");
    SDL_StopTextInput();
}

static void OverlayCommitPath(u32 actionBit, const char *path)
{
    snprintf(sOverlayState.committedPath, sizeof(sOverlayState.committedPath), "%s", path);
    sOverlayState.committedPathReady = TRUE;
    sActionBits |= actionBit;
    OverlayClose(TRUE);
}

static void OverlayOpen(bool8 loadMode)
{
    InitDisplayConfig();
    sOverlayState.active = TRUE;
    sOverlayState.loadMode = loadMode;
    sOverlayState.committedPathReady = FALSE;
    sOverlayState.input[0] = '\0';
    sOverlayState.selectedIndex = 0;
    sOverlayState.scrollIndex = 0;
    sOverlayState.refreshRequested = TRUE;
    OverlaySetMessage(loadMode ? "Select a state file to load" : "Type a name or select a directory");

    if (sOverlayState.currentDir[0] == '\0')
        snprintf(sOverlayState.currentDir, sizeof(sOverlayState.currentDir), "%s", sStateDir);
    if (!PathIsDirectory(sOverlayState.currentDir))
    {
        if (!EnsureDirectoryPath(sStateDir))
            OverlaySetMessage("Could not create state directory");
        snprintf(sOverlayState.currentDir, sizeof(sOverlayState.currentDir), "%s", sStateDir);
    }

    SDL_StartTextInput();
    OverlayRefreshEntries();
}

static void OverlayStepToParent(void)
{
    char *slash;

    if (strcmp(sOverlayState.currentDir, "/") == 0)
        return;

    slash = strrchr(sOverlayState.currentDir, '/');
    if (slash == sOverlayState.currentDir)
        slash[1] = '\0';
    else if (slash != NULL)
        *slash = '\0';
    sOverlayState.refreshRequested = TRUE;
}

static void OverlayActivateSelection(void)
{
    char path[PATH_MAX];

    if (sOverlayState.refreshRequested)
        OverlayRefreshEntries();

    if (sOverlayState.input[0] != '\0')
    {
        /* In load mode, input acts as a filter — Enter activates the
         * selected entry in the filtered view rather than trying to
         * resolve the typed text as a file path. */
        if (sOverlayState.loadMode && sOverlayState.viewCount > 0)
        {
            /* Fall through to the selection handling below */
        }
        else if (sOverlayState.loadMode)
        {
            /* Filter active but no matches — try as literal path */
            if (!ResolveOverlayPath(sOverlayState.input, path, sizeof(path)))
            {
                OverlaySetMessage("Path is too long");
                return;
            }
            if (PathIsDirectory(path))
            {
                snprintf(sOverlayState.currentDir, sizeof(sOverlayState.currentDir), "%s", path);
                sOverlayState.input[0] = '\0';
                sOverlayState.refreshRequested = TRUE;
                OverlayRefreshEntries();
                return;
            }
            if (access(path, F_OK) != 0)
            {
                OverlaySetMessage("No matching files");
                return;
            }
            OverlayCommitPath(HOST_DISPLAY_ACTION_STATE_LOAD_AS, path);
            return;
        }
        else
        {
            /* Save mode — input is a file name */
            if (!ResolveOverlayPath(sOverlayState.input, path, sizeof(path)))
            {
                OverlaySetMessage("Path is too long");
                return;
            }
            if (PathIsDirectory(path))
            {
                snprintf(sOverlayState.currentDir, sizeof(sOverlayState.currentDir), "%s", path);
                sOverlayState.input[0] = '\0';
                sOverlayState.refreshRequested = TRUE;
                OverlayRefreshEntries();
                return;
            }
            if (!MaybeAppendStateExtension(path, sizeof(path)))
            {
                OverlaySetMessage("Save path is too long");
                return;
            }
            if (access(path, F_OK) == 0)
            {
                OverlaySetMessage("Refusing to overwrite existing state");
                return;
            }
            OverlayCommitPath(HOST_DISPLAY_ACTION_STATE_SAVE_AS, path);
            return;
        }
    }

    if (sOverlayState.viewCount == 0)
    {
        OverlaySetMessage("No matching files");
        return;
    }

    if (sOverlayState.selectedIndex < 0 || sOverlayState.selectedIndex >= sOverlayState.viewCount)
        return;

    {
        int realIdx = sOverlayState.viewIndices[sOverlayState.selectedIndex];
        const struct HostDisplayOverlayEntry *sel = &sOverlayState.entries[realIdx];

        if (sel->isDirectory)
        {
            snprintf(sOverlayState.currentDir, sizeof(sOverlayState.currentDir), "%s", sel->path);
            sOverlayState.input[0] = '\0';
            sOverlayState.refreshRequested = TRUE;
            OverlayRefreshEntries();
            return;
        }

        if (sOverlayState.loadMode)
        {
            OverlayCommitPath(HOST_DISPLAY_ACTION_STATE_LOAD_AS, sel->path);
        }
        else
        {
            snprintf(sOverlayState.input, sizeof(sOverlayState.input), "%s", sel->name);
            OverlaySetMessage("Edit the name and press Enter to save");
        }
    }
}

static void OverlayHandleTextInput(const char *text)
{
    size_t len;
    size_t i;

    if (!sOverlayState.active || text == NULL)
        return;

    len = strlen(sOverlayState.input);
    for (i = 0; text[i] != '\0' && len + 1 < sizeof(sOverlayState.input); i++)
    {
        unsigned char c = (unsigned char)text[i];

        if (c < 32 || c > 126)
            continue;
        sOverlayState.input[len++] = (char)c;
    }
    sOverlayState.input[len] = '\0';
    if (sOverlayState.loadMode && strcmp(sOverlayState.input, sOverlayState.lastFilter) != 0)
        OverlayRebuildView();
    OverlaySetMessage("");
}

static void OverlayHandleKeyDown(SDL_Scancode scancode)
{
    switch (scancode)
    {
    case SDL_SCANCODE_ESCAPE:
        OverlayClose(FALSE);
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
    case SDL_SCANCODE_RIGHT:
        OverlayActivateSelection();
        break;
    case SDL_SCANCODE_BACKSPACE:
        if (sOverlayState.input[0] != '\0')
        {
            size_t len = strlen(sOverlayState.input);
            sOverlayState.input[len - 1] = '\0';
            if (sOverlayState.loadMode)
                OverlayRebuildView();
            OverlaySetMessage("");
        }
        else
        {
            OverlayStepToParent();
            OverlayRefreshEntries();
        }
        break;
    case SDL_SCANCODE_LEFT:
        OverlayStepToParent();
        OverlayRefreshEntries();
        break;
    case SDL_SCANCODE_UP:
        if (sOverlayState.selectedIndex > 0)
            sOverlayState.selectedIndex--;
        OverlayAdjustScroll();
        break;
    case SDL_SCANCODE_DOWN:
        if (sOverlayState.selectedIndex + 1 < sOverlayState.viewCount)
            sOverlayState.selectedIndex++;
        OverlayAdjustScroll();
        break;
    case SDL_SCANCODE_PAGEUP:
        sOverlayState.selectedIndex -= HOST_DISPLAY_OVERLAY_ROWS;
        OverlayAdjustScroll();
        break;
    case SDL_SCANCODE_PAGEDOWN:
        sOverlayState.selectedIndex += HOST_DISPLAY_OVERLAY_ROWS;
        OverlayAdjustScroll();
        break;
    case SDL_SCANCODE_HOME:
        sOverlayState.selectedIndex = 0;
        OverlayAdjustScroll();
        break;
    case SDL_SCANCODE_END:
        sOverlayState.selectedIndex = sOverlayState.viewCount - 1;
        OverlayAdjustScroll();
        break;
    default:
        break;
    }
}

static const u8 *GlyphRows(char c)
{
    static const u8 sSpace[7]      = {0,0,0,0,0,0,0};
    static const u8 sUnknown[7]    = {14,17,2,4,4,0,4};
    static const u8 sA[7]          = {14,17,17,31,17,17,17};
    static const u8 sB[7]          = {30,17,17,30,17,17,30};
    static const u8 sC[7]          = {14,17,16,16,16,17,14};
    static const u8 sD[7]          = {30,17,17,17,17,17,30};
    static const u8 sE[7]          = {31,16,16,30,16,16,31};
    static const u8 sF[7]          = {31,16,16,30,16,16,16};
    static const u8 sG[7]          = {14,17,16,16,19,17,15};
    static const u8 sH[7]          = {17,17,17,31,17,17,17};
    static const u8 sI[7]          = {31,4,4,4,4,4,31};
    static const u8 sJ[7]          = {31,2,2,2,18,18,12};
    static const u8 sK[7]          = {17,18,20,24,20,18,17};
    static const u8 sL[7]          = {16,16,16,16,16,16,31};
    static const u8 sM[7]          = {17,27,21,21,17,17,17};
    static const u8 sN[7]          = {17,17,25,21,19,17,17};
    static const u8 sO[7]          = {14,17,17,17,17,17,14};
    static const u8 sP[7]          = {30,17,17,30,16,16,16};
    static const u8 sQ[7]          = {14,17,17,17,21,18,13};
    static const u8 sR[7]          = {30,17,17,30,20,18,17};
    static const u8 sS[7]          = {15,16,16,14,1,1,30};
    static const u8 sT[7]          = {31,4,4,4,4,4,4};
    static const u8 sU[7]          = {17,17,17,17,17,17,14};
    static const u8 sV[7]          = {17,17,17,17,17,10,4};
    static const u8 sW[7]          = {17,17,17,21,21,21,10};
    static const u8 sX[7]          = {17,17,10,4,10,17,17};
    static const u8 sY[7]          = {17,17,10,4,4,4,4};
    static const u8 sZ[7]          = {31,1,2,4,8,16,31};
    static const u8 s0[7]          = {14,17,19,21,25,17,14};
    static const u8 s1[7]          = {4,12,4,4,4,4,14};
    static const u8 s2[7]          = {14,17,1,2,4,8,31};
    static const u8 s3[7]          = {30,1,1,14,1,1,30};
    static const u8 s4[7]          = {2,6,10,18,31,2,2};
    static const u8 s5[7]          = {31,16,16,30,1,1,30};
    static const u8 s6[7]          = {14,16,16,30,17,17,14};
    static const u8 s7[7]          = {31,1,2,4,8,8,8};
    static const u8 s8[7]          = {14,17,17,14,17,17,14};
    static const u8 s9[7]          = {14,17,17,15,1,1,14};
    static const u8 sSlash[7]      = {1,1,2,4,8,16,16};
    static const u8 sDash[7]       = {0,0,0,31,0,0,0};
    static const u8 sUnderscore[7] = {0,0,0,0,0,0,31};
    static const u8 sDot[7]        = {0,0,0,0,0,12,12};
    static const u8 sColon[7]      = {0,12,12,0,12,12,0};
    static const u8 sLBracket[7]   = {14,8,8,8,8,8,14};
    static const u8 sRBracket[7]   = {14,2,2,2,2,2,14};
    static const u8 sLParen[7]     = {2,4,8,8,8,4,2};
    static const u8 sRParen[7]     = {8,4,2,2,2,4,8};
    static const u8 sLT[7]         = {2,4,8,16,8,4,2};
    static const u8 sGT[7]         = {8,4,2,1,2,4,8};
    static const u8 sEqual[7]      = {0,31,0,31,0,0,0};
    static const u8 sPlus[7]       = {0,4,4,31,4,4,0};
    static const u8 sBang[7]       = {4,4,4,4,4,0,4};
    static const u8 sQuestion[7]   = {14,17,1,2,4,0,4};
    static const u8 sHash[7]       = {10,10,31,10,31,10,10};

    if (c >= 'a' && c <= 'z')
        c = (char)toupper((unsigned char)c);

    switch (c)
    {
    case ' ': return sSpace;
    case 'A': return sA;
    case 'B': return sB;
    case 'C': return sC;
    case 'D': return sD;
    case 'E': return sE;
    case 'F': return sF;
    case 'G': return sG;
    case 'H': return sH;
    case 'I': return sI;
    case 'J': return sJ;
    case 'K': return sK;
    case 'L': return sL;
    case 'M': return sM;
    case 'N': return sN;
    case 'O': return sO;
    case 'P': return sP;
    case 'Q': return sQ;
    case 'R': return sR;
    case 'S': return sS;
    case 'T': return sT;
    case 'U': return sU;
    case 'V': return sV;
    case 'W': return sW;
    case 'X': return sX;
    case 'Y': return sY;
    case 'Z': return sZ;
    case '0': return s0;
    case '1': return s1;
    case '2': return s2;
    case '3': return s3;
    case '4': return s4;
    case '5': return s5;
    case '6': return s6;
    case '7': return s7;
    case '8': return s8;
    case '9': return s9;
    case '/': return sSlash;
    case '-': return sDash;
    case '_': return sUnderscore;
    case '.': return sDot;
    case ':': return sColon;
    case '[': return sLBracket;
    case ']': return sRBracket;
    case '(': return sLParen;
    case ')': return sRParen;
    case '<': return sLT;
    case '>': return sGT;
    case '=': return sEqual;
    case '+': return sPlus;
    case '!': return sBang;
    case '?': return sQuestion;
    case '#': return sHash;
    default:  return sUnknown;
    }
}

static void DrawGlyph(int x, int y, int scale, SDL_Color color, char c)
{
    const u8 *rows = GlyphRows(c);
    int row;
    int col;

    SDL_SetRenderDrawColor(sSDLRenderer, color.r, color.g, color.b, color.a);
    for (row = 0; row < 7; row++)
    {
        for (col = 0; col < 5; col++)
        {
            if (rows[row] & (1 << (4 - col)))
            {
                SDL_Rect pixel = { x + col * scale, y + row * scale, scale, scale };
                SDL_RenderFillRect(sSDLRenderer, &pixel);
            }
        }
    }
}

static void DrawTextClipped(int x, int y, int maxWidth, SDL_Color color, const char *text)
{
    int advance = 6;
    int cursorX = x;
    size_t i;

    if (text == NULL)
        return;

    for (i = 0; text[i] != '\0'; i++)
    {
        if (cursorX + 5 > x + maxWidth)
            break;
        DrawGlyph(cursorX, y, 1, color, text[i]);
        cursorX += advance;
    }
}

static void DrawTextTail(int x, int y, int maxWidth, SDL_Color color, const char *text)
{
    size_t len;
    size_t start = 0;
    int maxChars = maxWidth / 6;

    if (text == NULL)
        return;

    len = strlen(text);
    if ((int)len > maxChars)
        start = len - (size_t)maxChars;
    DrawTextClipped(x, y, maxWidth, color, text + start);
}

static void DrawOverlayPanel(void)
{
    static const SDL_Color sWhite  = {255,255,255,255};
    static const SDL_Color sMuted  = {180,190,205,255};
    static const SDL_Color sGold   = {255,214,102,255};
    static const SDL_Color sRed    = {255,120,120,255};
    SDL_Rect dim = {0, 0, GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT};
    SDL_Rect panel = {10, 8, GBA_SCREEN_WIDTH - 20, GBA_SCREEN_HEIGHT - 16};
    SDL_Rect inputBox = {16, 38, GBA_SCREEN_WIDTH - 32, 12};
    int i;
    int startRow;
    int endRow;
    char title[24];

    SDL_SetRenderDrawBlendMode(sSDLRenderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sSDLRenderer, 0, 0, 0, 140);
    SDL_RenderFillRect(sSDLRenderer, &dim);

    SDL_SetRenderDrawColor(sSDLRenderer, 11, 16, 28, 235);
    SDL_RenderFillRect(sSDLRenderer, &panel);
    SDL_SetRenderDrawColor(sSDLRenderer, 120, 136, 170, 255);
    SDL_RenderDrawRect(sSDLRenderer, &panel);

    snprintf(title, sizeof(title), "%s STATE", sOverlayState.loadMode ? "LOAD" : "SAVE");
    DrawTextClipped(16, 14, panel.w - 12, sWhite, title);
    DrawTextClipped(96, 14, panel.w - 96, sMuted, "ENTER OPEN  ESC CANCEL");

    DrawTextClipped(16, 26, panel.w - 12, sMuted, "ARROWS NAVIGATE  BACKSPACE DELETE/UP");
    DrawTextClipped(16, 32, 22, sGold, "DIR");
    DrawTextTail(40, 32, panel.w - 34, sWhite, sOverlayState.currentDir);

    SDL_SetRenderDrawColor(sSDLRenderer, 28, 36, 58, 255);
    SDL_RenderFillRect(sSDLRenderer, &inputBox);
    SDL_SetRenderDrawColor(sSDLRenderer, 80, 92, 124, 255);
    SDL_RenderDrawRect(sSDLRenderer, &inputBox);
    DrawTextClipped(18, 40, 24, sGold, sOverlayState.loadMode ? "FIND" : "NAME");
    DrawTextTail(48, 40, inputBox.w - 34, sWhite, sOverlayState.input);
    DrawGlyph(18 + inputBox.w - 10, 40, 1, sMuted, '_');

    startRow = sOverlayState.scrollIndex;
    endRow = startRow + HOST_DISPLAY_OVERLAY_ROWS;
    if (endRow > sOverlayState.viewCount)
        endRow = sOverlayState.viewCount;

    for (i = startRow; i < endRow; i++)
    {
        SDL_Rect row = {16, 56 + (i - startRow) * 9, panel.w - 12, 8};
        int realIdx = sOverlayState.viewIndices[i];
        const struct HostDisplayOverlayEntry *entry = &sOverlayState.entries[realIdx];
        char timeBuf[18]; /* "HH:MM MM/DD/YYYY" */

        if (i == sOverlayState.selectedIndex)
        {
            SDL_SetRenderDrawColor(sSDLRenderer, 42, 60, 92, 255);
            SDL_RenderFillRect(sSDLRenderer, &row);
            SDL_SetRenderDrawColor(sSDLRenderer, 98, 126, 180, 255);
            SDL_RenderDrawRect(sSDLRenderer, &row);
        }

        DrawTextClipped(20, row.y + 1, 20, entry->isDirectory ? sGold : sMuted, entry->isDirectory ? "[D]" : "[F]");

        if (!entry->isDirectory && entry->mtime > 0)
        {
            struct tm *tm = localtime(&entry->mtime);
            if (tm != NULL)
                strftime(timeBuf, sizeof(timeBuf), "%H:%M %m/%d/%Y", tm);
            else
                timeBuf[0] = '\0';
            /* Name on left, timestamp right-aligned */
            DrawTextClipped(42, row.y + 1, row.w - 120, sWhite, entry->name);
            if (timeBuf[0] != '\0')
                DrawTextClipped(row.w - 76, row.y + 1, 96, sMuted, timeBuf);
        }
        else
        {
            DrawTextClipped(42, row.y + 1, row.w - 26, sWhite, entry->name);
        }
    }

    if (sOverlayState.viewCount == 0)
        DrawTextClipped(16, 58, panel.w - 12, sMuted,
                        sOverlayState.input[0] != '\0' ? "[NO MATCHING FILES]" : "[NO FILES]");
    if (sOverlayState.truncated)
        DrawTextClipped(16, 149, panel.w - 12, sMuted, "LIST TRUNCATED TO 256 ENTRIES");

    if (sOverlayState.message[0] != '\0')
        DrawTextClipped(16, 140, panel.w - 12, sRed, sOverlayState.message);
}

static void DrawTurboIndicator(void)
{
    SDL_Rect box;
    SDL_Color color = {255, 214, 102, 255};
    char label[32];
    const char *text = NULL;
    int width;

    if (sStatusTextFrames != 0)
        text = sStatusText;
    else if (sTurboEnabled)
    {
        snprintf(label, sizeof(label), "TURBO X%u", sTurboFastForwardFactor);
        text = label;
    }

    if (text == NULL || text[0] == '\0')
        return;

    width = (int)strlen(text) * 6 + 8;
    box.x = GBA_SCREEN_WIDTH - width - 8;
    box.y = 6;
    box.w = width;
    box.h = 12;

    SDL_SetRenderDrawBlendMode(sSDLRenderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sSDLRenderer, 24, 18, 6, 210);
    SDL_RenderFillRect(sSDLRenderer, &box);
    SDL_SetRenderDrawColor(sSDLRenderer, 196, 154, 52, 255);
    SDL_RenderDrawRect(sSDLRenderer, &box);
    DrawTextClipped(box.x + 4, box.y + 2, box.w - 8, color, text);
}

bool8 HostDisplayInit(void)
{
    u32 rendererFlags;

    InitDisplayConfig();
    if (sForceHeadless)
        return HeadlessDisplayInit();

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return FALSE;
    }

    sWindow = SDL_CreateWindow(
        "pokefirered-native",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        GBA_SCREEN_WIDTH * 3, GBA_SCREEN_HEIGHT * 3,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!sWindow)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return FALSE;
    }

    rendererFlags = SDL_RENDERER_ACCELERATED;
    if (!sUnthrottled)
        rendererFlags |= SDL_RENDERER_PRESENTVSYNC;

    sSDLRenderer = SDL_CreateRenderer(sWindow, -1, rendererFlags);
    if (!sSDLRenderer)
    {
        /* Fall back to software renderer */
        sSDLRenderer = SDL_CreateRenderer(sWindow, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!sSDLRenderer)
    {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return FALSE;
    }

    SDL_RenderSetLogicalSize(sSDLRenderer, GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT);

    sTexture = SDL_CreateTexture(sSDLRenderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT);
    if (!sTexture)
    {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return FALSE;
    }

    sSDLInit = TRUE;

    /* Protect SDL handles from savestate restore (they live in .bss
     * which gets overwritten, but must keep current-session values). */
    HostSavestateProtectRegion(&sWindow, sizeof(sWindow));
    HostSavestateProtectRegion(&sSDLRenderer, sizeof(sSDLRenderer));
    HostSavestateProtectRegion(&sTexture, sizeof(sTexture));
    HostSavestateProtectRegion(&sSDLInit, sizeof(sSDLInit));

    return TRUE;
}

static void PollInput(void)
{
    const u8 *keys;
    u16 keyinput = 0x03FF; /* all released (active-low) */
    size_t i;

    if (sOverlayState.active)
    {
        REG_KEYINPUT = keyinput;
        return;
    }

    SDL_PumpEvents();
    keys = SDL_GetKeyboardState(NULL);

    for (i = 0; i < sizeof(sKeyMap) / sizeof(sKeyMap[0]); i++)
    {
        if (keys[sKeyMap[i].key])
            keyinput &= ~sKeyMap[i].gba_bit;
    }

    REG_KEYINPUT = keyinput;
}

bool8 HostDisplayPresent(void)
{
    SDL_Event event;
    const u32 *fb;

    if (sForceHeadless)
        return HeadlessDisplayPresentImpl();

    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT)
            return FALSE;
        if (sOverlayState.active)
        {
            if (event.type == SDL_KEYDOWN)
                OverlayHandleKeyDown(event.key.keysym.scancode);
            else if (event.type == SDL_TEXTINPUT)
                OverlayHandleTextInput(event.text.text);
            continue;
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
            return FALSE;
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0)
        {
            if (event.key.keysym.scancode == SDL_SCANCODE_F1)
            {
                if (event.key.keysym.mod & KMOD_SHIFT)
                    OverlayOpen(TRUE);
                else
                    sActionBits |= HOST_DISPLAY_ACTION_STATE_LOAD;
            }
            else if (event.key.keysym.scancode == SDL_SCANCODE_F2)
            {
                if (event.key.keysym.mod & KMOD_SHIFT)
                    OverlayOpen(FALSE);
                else
                    sActionBits |= HOST_DISPLAY_ACTION_STATE_SAVE;
            }
            else if (event.key.keysym.scancode == SDL_SCANCODE_F5)
                sActionBits |= HOST_DISPLAY_ACTION_QUICKSAVE;
            else if (event.key.keysym.scancode == SDL_SCANCODE_F9)
                sActionBits |= HOST_DISPLAY_ACTION_QUICKLOAD;
            else if (event.key.keysym.scancode == SDL_SCANCODE_F10)
                sActionBits |= HOST_DISPLAY_ACTION_REPAIR_BAG;
            else if (event.key.keysym.scancode == SDL_SCANCODE_SPACE)
            {
                if (event.key.keysym.mod & KMOD_SHIFT)
                    CycleTurboPreset();
                else
                {
                    sTurboEnabled = !sTurboEnabled;
                    UpdateTurboToggleStatus();
                }
            }
        }
    }

    PollInput();

    /* The caller is responsible for rendering (either per-scanline via
     * HostRendererRenderScanline or full-frame via HostRendererRenderFrame).
     * We just present whatever is in the framebuffer. */
    fb = HostRendererGetFramebuffer();

    if (sDumpEnabled)
        DumpFramePPM(fb);

    SDL_UpdateTexture(sTexture, NULL, fb, GBA_SCREEN_WIDTH * 4);
    SDL_RenderClear(sSDLRenderer);
    SDL_RenderCopy(sSDLRenderer, sTexture, NULL, NULL);
    if (sOverlayState.active)
        DrawOverlayPanel();
    DrawTurboIndicator();
    SDL_RenderPresent(sSDLRenderer);
    if (sStatusTextFrames != 0)
        sStatusTextFrames--;

    sFrameCount++;
    return TRUE;
}

void HostDisplayDestroy(void)
{
    if (sForceHeadless)
        return;

    if (sTexture) SDL_DestroyTexture(sTexture);
    if (sSDLRenderer) SDL_DestroyRenderer(sSDLRenderer);
    if (sWindow) SDL_DestroyWindow(sWindow);
    if (sSDLInit) SDL_Quit();
    sTexture = NULL;
    sSDLRenderer = NULL;
    sWindow = NULL;
    sSDLInit = FALSE;
}

#else /* !HOST_DISPLAY_SDL2 — headless frame-dump only */

bool8 HostDisplayInit(void)
{
    InitDisplayConfig();
    if (!sDumpEnabled)
        fprintf(stderr, "host_display: SDL2 not available, headless mode active\n");
    else
        fprintf(stderr, "host_display: SDL2 not available, using PPM frame dump to %s/\n", sDumpDir);
    return TRUE;
}

bool8 HostDisplayPresent(void)
{
    return HeadlessDisplayPresentImpl();
}

void HostDisplayDestroy(void)
{
    /* nothing to clean up */
}

#endif /* HOST_DISPLAY_SDL2 */

u32 HostDisplayConsumeActions(void)
{
    u32 actions = sActionBits;
    sActionBits = 0;
    return actions;
}

bool8 HostDisplayConsumeSelectedStatePath(char *outPath, size_t outSize)
{
    if (!sOverlayState.committedPathReady)
        return FALSE;

    snprintf(outPath, outSize, "%s", sOverlayState.committedPath);
    sOverlayState.committedPathReady = FALSE;
    sOverlayState.committedPath[0] = '\0';
    return TRUE;
}

bool8 HostDisplayHasModalOverlay(void)
{
    return sOverlayState.active;
}

void HostDisplaySetStateDir(const char *dir)
{
    if (dir == NULL || dir[0] == '\0')
        return;

    snprintf(sStateDir, sizeof(sStateDir), "%s", dir);
    if (!sOverlayState.active)
        snprintf(sOverlayState.currentDir, sizeof(sOverlayState.currentDir), "%s", sStateDir);
}

u32 HostDisplayGetFrameCount(void)
{
    return sFrameCount;
}

u32 HostDisplayGetFastForwardFactor(void)
{
    if (sTurboEnabled)
        return sTurboFastForwardFactor;

    return sFastForwardFactor;
}

bool8 HostDisplayNeedsFrameRender(void)
{
    return !(sForceHeadless && !sDumpEnabled);
}
