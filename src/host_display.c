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

#include <stdio.h>
#include <string.h>

/* Undo upstream abs macro before any further system header interaction */
#ifdef abs
#undef abs
#endif

#include "host_display.h"
#include "host_renderer.h"
#include "gba/gba.h"

/* ---------- Frame dump backend (always available) ---------- */

static u32 sFrameCount = 0;
static char sDumpDir[256] = "/tmp";
static bool8 sDumpEnabled = FALSE;

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

bool8 HostDisplayInit(void)
{
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

    sSDLRenderer = SDL_CreateRenderer(sWindow, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
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
    return TRUE;
}

static void PollInput(void)
{
    const u8 *keys;
    u16 keyinput = 0x03FF; /* all released (active-low) */
    size_t i;

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

    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT)
            return FALSE;
        if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
            return FALSE;
    }

    PollInput();

    HostRendererRenderFrame();
    fb = HostRendererGetFramebuffer();

    if (sDumpEnabled)
        DumpFramePPM(fb);

    SDL_UpdateTexture(sTexture, NULL, fb, GBA_SCREEN_WIDTH * 4);
    SDL_RenderClear(sSDLRenderer);
    SDL_RenderCopy(sSDLRenderer, sTexture, NULL, NULL);
    SDL_RenderPresent(sSDLRenderer);

    sFrameCount++;
    return TRUE;
}

void HostDisplayDestroy(void)
{
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
    fprintf(stderr, "host_display: SDL2 not available, using PPM frame dump to %s/\n", sDumpDir);
    sDumpEnabled = TRUE;
    return TRUE;
}

bool8 HostDisplayPresent(void)
{
    const u32 *fb;

    HostRendererRenderFrame();
    fb = HostRendererGetFramebuffer();

    if (sDumpEnabled)
        DumpFramePPM(fb);

    sFrameCount++;
    return TRUE;
}

void HostDisplayDestroy(void)
{
    /* nothing to clean up */
}

#endif /* HOST_DISPLAY_SDL2 */

u32 HostDisplayGetFrameCount(void)
{
    return sFrameCount;
}
