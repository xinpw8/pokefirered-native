#include "pfr_native.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <SDL.h>

#define HOLD_REPEAT_DELAY_MS 180u
#define HOLD_REPEAT_INTERVAL_MS 50u

typedef struct {
    SDL_Scancode scancode;
    PfrNativeAction action;
} KeyBinding;

typedef struct {
    int bootstrap_id;
    bool use_map_start;
    PfrNativeMapId map_id;
    int16_t x;
    int16_t y;
    PfrNativeDirection direction;
} StartSpec;

static const KeyBinding sKeyBindings[] = {
    { SDL_SCANCODE_UP, PFR_NATIVE_ACTION_UP },
    { SDL_SCANCODE_DOWN, PFR_NATIVE_ACTION_DOWN },
    { SDL_SCANCODE_LEFT, PFR_NATIVE_ACTION_LEFT },
    { SDL_SCANCODE_RIGHT, PFR_NATIVE_ACTION_RIGHT },
    { SDL_SCANCODE_Z, PFR_NATIVE_ACTION_A },
    { SDL_SCANCODE_X, PFR_NATIVE_ACTION_B },
    { SDL_SCANCODE_RETURN, PFR_NATIVE_ACTION_START },
    { SDL_SCANCODE_RSHIFT, PFR_NATIVE_ACTION_SELECT },
};

static const char *map_name(PfrNativeMapId map_id)
{
    const PfrNativeMap *map = pfr_native_get_map(map_id);
    return map != NULL ? map->name : "?";
}

static const char *action_name(PfrNativeAction action)
{
    switch (action)
    {
    case PFR_NATIVE_ACTION_NONE: return "none";
    case PFR_NATIVE_ACTION_UP: return "up";
    case PFR_NATIVE_ACTION_DOWN: return "down";
    case PFR_NATIVE_ACTION_LEFT: return "left";
    case PFR_NATIVE_ACTION_RIGHT: return "right";
    case PFR_NATIVE_ACTION_A: return "a";
    case PFR_NATIVE_ACTION_B: return "b";
    case PFR_NATIVE_ACTION_START: return "start";
    case PFR_NATIVE_ACTION_SELECT: return "select";
    default: return "?";
    }
}

static const char *event_name(uint8_t event)
{
    switch (event)
    {
    case PFR_NATIVE_EVENT_NONE: return "none";
    case PFR_NATIVE_EVENT_BLOCKED: return "blocked";
    case PFR_NATIVE_EVENT_MOVED: return "moved";
    case PFR_NATIVE_EVENT_DIALOG_OPENED: return "dialog_opened";
    case PFR_NATIVE_EVENT_DIALOG_ADVANCED: return "dialog_advanced";
    case PFR_NATIVE_EVENT_DIALOG_CLOSED: return "dialog_closed";
    case PFR_NATIVE_EVENT_WARPED: return "warped";
    case PFR_NATIVE_EVENT_UNSUPPORTED_WARP: return "unsupported_warp";
    default: return "?";
    }
}

static int ensure_dir(const char *path)
{
    if (mkdir(path, 0777) == 0 || errno == EEXIST)
        return 0;
// [DEBUG-STRIPPED]     fprintf(stderr, "pfr_native_play: mkdir(%s) failed: %s\n", path, strerror(errno));
    return -1;
}

static int write_ppm(const char *path, const uint32_t *rgba)
{
    FILE *f = fopen(path, "wb");
    size_t i;

    if (f == NULL)
    {
// [DEBUG-STRIPPED]         fprintf(stderr, "pfr_native_play: fopen(%s) failed: %s\n", path, strerror(errno));
        return -1;
    }

    fprintf(f, "P6\n%d %d\n255\n", PFR_NATIVE_SCREEN_WIDTH, PFR_NATIVE_SCREEN_HEIGHT);
    for (i = 0; i < (size_t)PFR_NATIVE_SCREEN_WIDTH * PFR_NATIVE_SCREEN_HEIGHT; i++)
    {
        uint8_t rgb[3] = {
            (uint8_t)((rgba[i] >> 16) & 0xFF),
            (uint8_t)((rgba[i] >> 8) & 0xFF),
            (uint8_t)(rgba[i] & 0xFF),
        };
        fwrite(rgb, 1, sizeof(rgb), f);
    }

    fclose(f);
    return 0;
}

static int dump_frame(const PfrNativeCore *core, const char *dir, int index,
                      PfrNativeAction action, uint8_t event)
{
    uint32_t framebuffer[PFR_NATIVE_SCREEN_WIDTH * PFR_NATIVE_SCREEN_HEIGHT];
    char path[PATH_MAX];

    c_render(core, framebuffer, PFR_NATIVE_SCREEN_WIDTH);
    snprintf(path, sizeof(path), "%s/%04d_%s_%s.ppm", dir, index,
             action_name(action), event_name(event));
    return write_ppm(path, framebuffer);
}

static int reset_core(PfrNativeCore *core, const StartSpec *start)
{
    if (start->use_map_start)
        return pfr_native_reset_to_map(core, start->map_id, start->x, start->y, start->direction);
    return c_reset(core, start->bootstrap_id, NULL);
}

static PfrNativeAction parse_action_char(char ch)
{
    switch (ch)
    {
    case 'U': case 'u': return PFR_NATIVE_ACTION_UP;
    case 'D': case 'd': return PFR_NATIVE_ACTION_DOWN;
    case 'L': case 'l': return PFR_NATIVE_ACTION_LEFT;
    case 'R': case 'r': return PFR_NATIVE_ACTION_RIGHT;
    case 'A': case 'a': return PFR_NATIVE_ACTION_A;
    case 'B': case 'b': return PFR_NATIVE_ACTION_B;
    case 'T': case 't': return PFR_NATIVE_ACTION_START;
    case 'C': case 'c': return PFR_NATIVE_ACTION_SELECT;
    case '.': case '0': case 'N': case 'n': return PFR_NATIVE_ACTION_NONE;
    default: return (PfrNativeAction)-1;
    }
}

static int run_dump_mode(const StartSpec *start, const char *dir, const char *script)
{
    PfrNativeCore core;
    const PfrNativeState *state;
    FILE *manifest;
    char manifest_path[PATH_MAX];
    int frame_index = 0;
    size_t i;

    if (ensure_dir(dir) != 0)
        return 1;

    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.txt", dir);
    manifest = fopen(manifest_path, "w");
    if (manifest == NULL)
    {
// [DEBUG-STRIPPED]         fprintf(stderr, "pfr_native_play: fopen(%s) failed: %s\n",
// [DEBUG-STRIPPED]                 manifest_path, strerror(errno));
        return 1;
    }

    c_init(&core);
    if (reset_core(&core, start) != 0)
    {
        fclose(manifest);
        return 1;
    }

    state = pfr_native_state(&core);
    fprintf(manifest, "%04d action=%s event=%s map=%u name=%s x=%d y=%d mode=%u\n",
            frame_index, action_name(PFR_NATIVE_ACTION_NONE), event_name(PFR_NATIVE_EVENT_NONE),
            (unsigned)state->current_map, map_name(state->current_map),
            state->player_x, state->player_y, state->mode);
    if (dump_frame(&core, dir, frame_index, PFR_NATIVE_ACTION_NONE, PFR_NATIVE_EVENT_NONE) != 0)
    {
        fclose(manifest);
        return 1;
    }

    if (script != NULL)
    {
        for (i = 0; script[i] != '\0'; i++)
        {
            PfrNativeAction action;
            PfrNativeStepResult result;
            if (strchr(" \t\r\n,_-|", script[i]) != NULL)
                continue;
            action = parse_action_char(script[i]);
            if ((int)action < 0)
            {
// [DEBUG-STRIPPED]                 fprintf(stderr, "pfr_native_play: unsupported action char '%c'\n", script[i]);
                fclose(manifest);
                return 1;
            }
            result = c_step(&core, action);
            frame_index++;
            state = pfr_native_state(&core);
            fprintf(manifest, "%04d action=%s event=%s map=%u name=%s x=%d y=%d mode=%u\n",
                    frame_index, action_name(action), event_name(result.event),
                    (unsigned)state->current_map, map_name(state->current_map),
                    state->player_x, state->player_y, state->mode);
            if (dump_frame(&core, dir, frame_index, action, result.event) != 0)
            {
                fclose(manifest);
                return 1;
            }
        }
    }

    fclose(manifest);
    printf("wrote capture to %s\n", dir);
    return 0;
}

static PfrNativeAction key_to_action(SDL_Keycode key)
{
    switch (key)
    {
    case SDLK_UP: return PFR_NATIVE_ACTION_UP;
    case SDLK_DOWN: return PFR_NATIVE_ACTION_DOWN;
    case SDLK_LEFT: return PFR_NATIVE_ACTION_LEFT;
    case SDLK_RIGHT: return PFR_NATIVE_ACTION_RIGHT;
    case SDLK_z: return PFR_NATIVE_ACTION_A;
    case SDLK_x: return PFR_NATIVE_ACTION_B;
    case SDLK_RETURN: return PFR_NATIVE_ACTION_START;
    case SDLK_RSHIFT: return PFR_NATIVE_ACTION_SELECT;
    default: return (PfrNativeAction)-1;
    }
}

static bool binding_for_scancode(SDL_Scancode scancode, KeyBinding *binding)
{
    size_t i;
    for (i = 0; i < sizeof(sKeyBindings) / sizeof(sKeyBindings[0]); i++)
    {
        if (sKeyBindings[i].scancode != scancode)
            continue;
        if (binding != NULL)
            *binding = sKeyBindings[i];
        return true;
    }
    return false;
}

static bool find_pressed_binding(const Uint8 *keyboard, SDL_Scancode preferred,
                                 KeyBinding *binding)
{
    KeyBinding candidate;
    size_t i;

    if (preferred != SDL_SCANCODE_UNKNOWN
        && keyboard[preferred]
        && binding_for_scancode(preferred, &candidate))
    {
        if (binding != NULL)
            *binding = candidate;
        return true;
    }

    for (i = 0; i < sizeof(sKeyBindings) / sizeof(sKeyBindings[0]); i++)
    {
        if (!keyboard[sKeyBindings[i].scancode])
            continue;
        if (binding != NULL)
            *binding = sKeyBindings[i];
        return true;
    }

    return false;
}

/* Returns the mode AFTER the step so callers can detect transitions */
static uint8_t perform_action(PfrNativeCore *core, PfrNativeAction action)
{
    PfrNativeStepResult result = c_step(core, action);
    const PfrNativeState *state = pfr_native_state(core);
// [DEBUG-STRIPPED]     fprintf(stderr, "action=%s event=%s map=%u name=%s x=%d y=%d mode=%u\n",
// [DEBUG-STRIPPED]             action_name(action), event_name(result.event),
// [DEBUG-STRIPPED]             (unsigned)state->current_map, map_name(state->current_map),
// [DEBUG-STRIPPED]             state->player_x, state->player_y, state->mode);
    return state->mode;
}

static int run_interactive(const StartSpec *start, int scale)
{
    PfrNativeCore core;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    uint32_t framebuffer[PFR_NATIVE_SCREEN_WIDTH * PFR_NATIVE_SCREEN_HEIGHT];
    bool running = true;
    SDL_Scancode held_scancode = SDL_SCANCODE_UNKNOWN;
    PfrNativeAction held_action = (PfrNativeAction)-1;
    Uint32 next_repeat_ms = 0;

    if (scale <= 0)
        scale = 3;

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
// [DEBUG-STRIPPED]         fprintf(stderr, "pfr_native_play: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    c_init(&core);
    if (reset_core(&core, start) != 0)
    {
        SDL_Quit();
        return 1;
    }

    window = SDL_CreateWindow(
        "pfr_native_play",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        PFR_NATIVE_SCREEN_WIDTH * scale,
        PFR_NATIVE_SCREEN_HEIGHT * scale,
        SDL_WINDOW_SHOWN
    );
    if (window == NULL)
    {
// [DEBUG-STRIPPED]         fprintf(stderr, "pfr_native_play: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer == NULL)
    {
// [DEBUG-STRIPPED]         fprintf(stderr, "pfr_native_play: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                PFR_NATIVE_SCREEN_WIDTH,
                                PFR_NATIVE_SCREEN_HEIGHT);
    if (texture == NULL)
    {
// [DEBUG-STRIPPED]         fprintf(stderr, "pfr_native_play: SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

// [DEBUG-STRIPPED]     fprintf(stderr,
// [DEBUG-STRIPPED]             "controls: arrows move, z=A, x=B, enter=START, rshift=SELECT, "
// [DEBUG-STRIPPED]             "hold action keys to repeat, "
// [DEBUG-STRIPPED]             "r=reset, f5=dump frame, esc=quit\n");

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                running = false;
            }
            else if (event.type == SDL_WINDOWEVENT
                     && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            {
                held_scancode = SDL_SCANCODE_UNKNOWN;
                held_action = (PfrNativeAction)-1;
                next_repeat_ms = 0;
            }
            else if (event.type == SDL_KEYDOWN)
            {
                PfrNativeAction action = key_to_action(event.key.keysym.sym);
                if (event.key.keysym.sym == SDLK_ESCAPE)
                {
                    running = false;
                }
                else if (event.key.keysym.sym == SDLK_r && event.key.repeat == 0)
                {
                    reset_core(&core, start);
                }
                else if (event.key.keysym.sym == SDLK_F5 && event.key.repeat == 0)
                {
                    if (dump_frame(&core, ".", 0, PFR_NATIVE_ACTION_NONE, core.last_step.event) == 0)
// [DEBUG-STRIPPED]                         fprintf(stderr, "wrote ./0000_none_%s.ppm\n", event_name(core.last_step.event));
                }
                else if ((int)action >= 0 && event.key.repeat == 0)
                {
                    uint8_t new_mode = perform_action(&core, action);
                    /* On mode change (e.g. entering battle), clear held key
                     * so the player must press a fresh key to act */
                    if (new_mode == PFR_NATIVE_MODE_BATTLE) {
                        held_scancode = SDL_SCANCODE_UNKNOWN;
                        held_action = (PfrNativeAction)-1;
                        next_repeat_ms = 0;
                    } else {
                        held_scancode = event.key.keysym.scancode;
                        held_action = action;
                        next_repeat_ms = SDL_GetTicks() + HOLD_REPEAT_DELAY_MS;
                    }
                }
            }
            else if (event.type == SDL_KEYUP)
            {
                KeyBinding binding;
                if (binding_for_scancode(event.key.keysym.scancode, &binding))
                {
                    const Uint8 *keyboard = SDL_GetKeyboardState(NULL);
                    if (find_pressed_binding(keyboard, SDL_SCANCODE_UNKNOWN, &binding))
                    {
                        held_scancode = binding.scancode;
                        held_action = binding.action;
                        next_repeat_ms = SDL_GetTicks() + HOLD_REPEAT_DELAY_MS;
                    }
                    else
                    {
                        held_scancode = SDL_SCANCODE_UNKNOWN;
                        held_action = (PfrNativeAction)-1;
                        next_repeat_ms = 0;
                    }
                }
            }
        }

        if ((int)held_action >= 0 && SDL_TICKS_PASSED(SDL_GetTicks(), next_repeat_ms))
        {
            const Uint8 *keyboard = SDL_GetKeyboardState(NULL);
            KeyBinding binding;
            if (find_pressed_binding(keyboard, held_scancode, &binding))
            {
                held_scancode = binding.scancode;
                held_action = binding.action;
                uint8_t new_mode = perform_action(&core, held_action);
                if (new_mode == PFR_NATIVE_MODE_BATTLE) {
                    /* Entering battle from held key — stop repeating */
                    held_scancode = SDL_SCANCODE_UNKNOWN;
                    held_action = (PfrNativeAction)-1;
                    next_repeat_ms = 0;
                } else {
                    next_repeat_ms = SDL_GetTicks() + HOLD_REPEAT_INTERVAL_MS;
                }
            }
            else
            {
                held_scancode = SDL_SCANCODE_UNKNOWN;
                held_action = (PfrNativeAction)-1;
                next_repeat_ms = 0;
            }
        }

        c_render(&core, framebuffer, PFR_NATIVE_SCREEN_WIDTH);
        SDL_UpdateTexture(texture, NULL, framebuffer, PFR_NATIVE_SCREEN_WIDTH * (int)sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

static void usage(const char *argv0)
{
// [DEBUG-STRIPPED]     fprintf(stderr,
// [DEBUG-STRIPPED]             "usage: %s [--scale N] [--bootstrap house2f|pallet]\n"
// [DEBUG-STRIPPED]             "          [--map NAME --x N --y N [--dir north|south|west|east]]\n"
// [DEBUG-STRIPPED]             "          [--dump-dir DIR --script ACTIONS]\n"
// [DEBUG-STRIPPED]             "  interactive: requires DISPLAY/WAYLAND\n"
// [DEBUG-STRIPPED]             "  dump mode: writes PPM frames + manifest without opening a window\n"
// [DEBUG-STRIPPED]             "  --bootstrap pallet starts outside the house in Pallet Town\n"
// [DEBUG-STRIPPED]             "  --map requires exact generated map name, for example PalletTown or Route1\n"
// [DEBUG-STRIPPED]             "  action chars: U D L R A B T(start) C(select) .(noop)\n",
// [DEBUG-STRIPPED]             argv0);
}

static int parse_i16(const char *text, int16_t *out_value)
{
    char *end = NULL;
    long value;

    if (text == NULL || out_value == NULL)
        return -1;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < INT16_MIN || value > INT16_MAX)
        return -1;

    *out_value = (int16_t)value;
    return 0;
}

static int parse_bootstrap(const char *text)
{
    if (strcmp(text, "house2f") == 0)
        return PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F;
    if (strcmp(text, "pallet") == 0)
        return PFR_NATIVE_BOOTSTRAP_PALLET_TOWN;
    return -1;
}

static PfrNativeDirection parse_direction(const char *text)
{
    if (strcmp(text, "north") == 0 || strcmp(text, "up") == 0 || strcmp(text, "n") == 0)
        return PFR_NATIVE_DIR_NORTH;
    if (strcmp(text, "south") == 0 || strcmp(text, "down") == 0 || strcmp(text, "s") == 0)
        return PFR_NATIVE_DIR_SOUTH;
    if (strcmp(text, "west") == 0 || strcmp(text, "left") == 0 || strcmp(text, "w") == 0)
        return PFR_NATIVE_DIR_WEST;
    if (strcmp(text, "east") == 0 || strcmp(text, "right") == 0 || strcmp(text, "e") == 0)
        return PFR_NATIVE_DIR_EAST;
    return PFR_NATIVE_DIR_NONE;
}

int main(int argc, char **argv)
{
    const char *dump_dir = NULL;
    const char *script = NULL;
    int scale = 3;
    StartSpec start = {
        .bootstrap_id = PFR_NATIVE_BOOTSTRAP_PLAYERS_HOUSE_2F,
        .use_map_start = false,
        .map_id = PFR_NATIVE_MAP_INVALID,
        .x = 0,
        .y = 0,
        .direction = PFR_NATIVE_DIR_SOUTH,
    };
    bool have_x = false;
    bool have_y = false;
    int i;

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--dump-dir") == 0 && i + 1 < argc)
        {
            dump_dir = argv[++i];
        }
        else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc)
        {
            script = argv[++i];
        }
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
        {
            scale = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--bootstrap") == 0 && i + 1 < argc)
        {
            start.bootstrap_id = parse_bootstrap(argv[++i]);
            if (start.bootstrap_id < 0)
            {
                usage(argv[0]);
                return 1;
            }
            start.use_map_start = false;
        }
        else if (strcmp(argv[i], "--map") == 0 && i + 1 < argc)
        {
            start.map_id = pfr_native_find_map_by_name(argv[++i]);
            if (start.map_id == PFR_NATIVE_MAP_INVALID)
            {
// [DEBUG-STRIPPED]                 fprintf(stderr, "pfr_native_play: unknown map name\n");
                return 1;
            }
            start.use_map_start = true;
        }
        else if (strcmp(argv[i], "--x") == 0 && i + 1 < argc)
        {
            if (parse_i16(argv[++i], &start.x) != 0)
            {
                usage(argv[0]);
                return 1;
            }
            have_x = true;
        }
        else if (strcmp(argv[i], "--y") == 0 && i + 1 < argc)
        {
            if (parse_i16(argv[++i], &start.y) != 0)
            {
                usage(argv[0]);
                return 1;
            }
            have_y = true;
        }
        else if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc)
        {
            start.direction = parse_direction(argv[++i]);
            if (start.direction == PFR_NATIVE_DIR_NONE)
            {
                usage(argv[0]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        else
        {
            usage(argv[0]);
            return 1;
        }
    }

    if (start.use_map_start && (!have_x || !have_y))
    {
// [DEBUG-STRIPPED]         fprintf(stderr, "pfr_native_play: --map requires --x and --y\n");
        return 1;
    }

    if (dump_dir != NULL)
        return run_dump_mode(&start, dump_dir, script);

    if (getenv("DISPLAY") == NULL && getenv("WAYLAND_DISPLAY") == NULL)
    {
// [DEBUG-STRIPPED]         fprintf(stderr,
// [DEBUG-STRIPPED]                 "pfr_native_play: no DISPLAY/WAYLAND_DISPLAY set.\n"
// [DEBUG-STRIPPED]                 "use --dump-dir to render frames headlessly.\n");
        return 1;
    }

    return run_interactive(&start, scale);
}
