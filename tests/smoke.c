#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <execinfo.h>
#include <stdlib.h>
#include <unistd.h>

#include "global.h"
#include "bg.h"
#include "decompress.h"
#include "dma3.h"
#include "berry_fix_program.h"
#include "characters.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/maps.h"
#include "constants/songs.h"
#include "constants/trainer_tower.h"
#include "constants/vars.h"
#include "gba/macro.h"
#include "gpu_regs.h"
#include "clear_save_data_screen.h"
#include "event_data.h"
#include "help_system.h"
#include "host_agbmain.h"
#include "host_crt0.h"
#include "host_sound_init.h"
#include "host_dma.h"
#include "host_flash.h"
#include "host_intro_stubs.h"
#include "host_oak_speech_stubs.h"
#include "host_memory.h"
#include "host_new_game_stubs.h"
#include "host_runtime_stubs.h"
#include "host_title_screen_stubs.h"
#include "intro.h"
#include "link.h"
#include "link_rfu.h"
#include "load_save.h"
#include "main.h"
#include "main_menu.h"
#include "malloc.h"
#include "m4a.h"
#include "money.h"
#include "overworld.h"
#include "field_fadetransition.h"
#include "palette.h"
#include "quest_log.h"
#include "random.h"
#include "scanline_effect.h"
#include "save.h"
#include "sprite.h"
#include "strings.h"
#include "task.h"
#include "title_screen.h"
#include "window.h"

bool32 CheckHeap(void);
static volatile int sLastGoodFrame = -1;
extern u16 gBattle_BG0_X;
extern const IntrFunc gIntrTableTemplate[];
extern u32 intr_main[0x200];
extern u32 IntrMain_Buffer[0x200];
extern u8 gPcmDmaCounter;
extern u8 gWirelessCommType;
extern u16 gKeyRepeatContinueDelay;
extern bool8 gDifferentSaveFile;
extern const u8 EventScript_ResetAllMapFlags[];
extern const u8 gText_Controls[];
extern const u8 gText_ABUTTONNext[];
extern const u8 gText_ABUTTONNext_BBUTTONBack[];
extern const u8 gOakSpeech_Text_AskPlayerGender[];
extern const u8 gOakSpeech_Text_IsInhabitedFarAndWide[];
extern const u8 gOakSpeech_Text_IStudyPokemon[];
extern const u8 gOakSpeech_Text_TellMeALittleAboutYourself[];
extern const u8 gOakSpeech_Text_WelcomeToTheWorld[];
extern const u8 gOakSpeech_Text_ThisWorld[];
extern const u8 gOakSpeech_Text_YourNameWhatIsIt[];
extern const u8 gOakSpeech_Text_WhatWasHisName[];
extern const u8 *gHostLastOakSpeechSource;

void EnableVCountIntrAtLine150(void);

static bool8 ShouldRunSmokeTest(const char *name)
{
    const char *filter = getenv("PFR_SMOKE_FILTER");

    if (filter == NULL || *filter == '\0')
        return TRUE;

    return strstr(name, filter) != NULL;
}

static void PrintSmokeTestLabel(const char *name)
{
    fprintf(stderr, ">> %s\n", name);
}

#define RUN_FILTERED_TEST(name, expr)        \
    do                                       \
    {                                        \
        if (ShouldRunSmokeTest(name))        \
        {                                    \
            PrintSmokeTestLabel(name);       \
            rc |= (expr);                    \
        }                                    \
    } while (0)
void InitIntrHandlers(void);
static bool32 WindowMatches(u8 windowId, u8 bg, u8 tilemapLeft, u8 tilemapTop, u8 width, u8 height);

static int sHBlankCallbackCalls;
static int sSerialCallbackCalls;
static int sVBlankCallbackCalls;

static void TestMainCallback(void)
{
}

static void TestHBlankCallback(void)
{
    sHBlankCallbackCalls++;
}

static void TestSerialCallback(void)
{
    sSerialCallbackCalls++;
}

static void TestVBlankCallback(void)
{
    sVBlankCallbackCalls++;
}

static int Expect(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

static int CountWindowsWithTileData(void)
{
    int count = 0;
    int i;

    for (i = 0; i < WINDOWS_MAX; i++)
    {
        if (gWindows[i].tileData != NULL)
            count++;
    }

    return count;
}

static int FindWindowIdMatching(u8 bg, u8 tilemapLeft, u8 tilemapTop, u8 width, u8 height)
{
    int i;

    for (i = 0; i < WINDOWS_MAX; i++)
    {
        if (WindowMatches(i, bg, tilemapLeft, tilemapTop, width, height))
            return i;
    }

    return -1;
}

static u8 AsciiToPokemonChar(char c)
{
    if (c >= 'A' && c <= 'Z') return 0xBB + (c - 'A');
    if (c >= 'a' && c <= 'z') return 0xD5 + (c - 'a');
    if (c >= '0' && c <= '9') return 0xA1 + (c - '0');
    if (c == ' ') return 0x00;
    if (c == '.') return 0xAD;
    if (c == ',') return 0xB8;
    if (c == '!') return 0xAB;
    if (c == '?') return 0xAC;
    if (c == '\'') return 0xB4;
    return (u8)c;
}

static bool32 NameBufferEquals(const u8 *name, const char *expected)
{
    while (*expected != '\0')
    {
        u8 poke_char = AsciiToPokemonChar(*expected);
        if (*name++ != poke_char)
            return FALSE;
        expected++;
    }

    return *name == EOS || *name == 0;
}

static size_t GetWindowTileDataSize(u8 windowId)
{
    if (windowId >= WINDOWS_MAX || gWindows[windowId].tileData == NULL)
        return 0;

    return (size_t)32 * gWindows[windowId].window.width * gWindows[windowId].window.height;
}

static bool32 WindowMatches(u8 windowId, u8 bg, u8 tilemapLeft, u8 tilemapTop, u8 width, u8 height)
{
    return windowId < WINDOWS_MAX
        && gWindows[windowId].tileData != NULL
        && gWindows[windowId].window.bg == bg
        && gWindows[windowId].window.tilemapLeft == tilemapLeft
        && gWindows[windowId].window.tilemapTop == tilemapTop
        && gWindows[windowId].window.width == width
        && gWindows[windowId].window.height == height;
}

static u32 WindowTileDataHash(u8 windowId)
{
    const u8 *bytes;
    size_t size;
    size_t i;
    u32 hash = 2166136261u;

    size = GetWindowTileDataSize(windowId);
    if (size == 0)
        return 0;

    bytes = gWindows[windowId].tileData;
    for (i = 0; i < size; i++)
        hash = (hash ^ bytes[i]) * 16777619u;

    return hash;
}

static bool32 WindowTileDataHasNonZero(u8 windowId)
{
    const u8 *bytes;
    size_t size;
    size_t i;

    size = GetWindowTileDataSize(windowId);
    if (size == 0)
        return FALSE;

    bytes = gWindows[windowId].tileData;
    for (i = 0; i < size; i++)
    {
        if (bytes[i] != 0)
            return TRUE;
    }

    return FALSE;
}

static bool32 WindowTilemapHasNonZero(u8 windowId)
{
    const struct Window *window;
    u16 *tilemapBuffer;
    u32 screenSize;
    u32 screenWidth;
    u32 screenHeight;
    u8 x;
    u8 y;

    if (windowId >= WINDOWS_MAX || gWindows[windowId].tileData == NULL)
        return FALSE;

    window = &gWindows[windowId];
    tilemapBuffer = GetBgTilemapBuffer(window->window.bg);
    if (tilemapBuffer == NULL)
        return FALSE;

    screenSize = GetBgAttribute(window->window.bg, BG_ATTR_SCREENSIZE);
    screenWidth = GetBgMetricTextMode(window->window.bg, 1) * 32;
    screenHeight = GetBgMetricTextMode(window->window.bg, 2) * 32;

    for (y = 0; y < window->window.height; y++)
    {
        for (x = 0; x < window->window.width; x++)
        {
            u32 index = GetTileMapIndexFromCoords(window->window.tilemapLeft + x,
                                                  window->window.tilemapTop + y,
                                                  screenSize,
                                                  screenWidth,
                                                  screenHeight);
            if (tilemapBuffer[index] != 0)
                return TRUE;
        }
    }

    return FALSE;
}

static int TestRandom(void)
{
    int rc = 0;

    SeedRng(0x1234);
    rc |= Expect(Random() == 0x4dcb, "Random() first output mismatch");
    rc |= Expect(Random() == 0xe161, "Random() second output mismatch");
    rc |= Expect(Random() == 0x4340, "Random() third output mismatch");
    return rc;
}

static int TestHeap(void)
{
    int rc = 0;
    unsigned char local_heap[256];
    unsigned int *a;
    unsigned int *b;

    memset(local_heap, 0xCD, sizeof(local_heap));
    InitHeap(local_heap, sizeof(local_heap));
    a = AllocZeroed(24);
    b = Alloc(40);

    rc |= Expect(a != NULL, "AllocZeroed returned NULL");
    rc |= Expect(b != NULL, "Alloc returned NULL");
    rc |= Expect(a[0] == 0 && a[1] == 0, "AllocZeroed did not clear memory");
    rc |= Expect(CheckHeap(), "heap failed integrity check after allocation");

    Free(a);
    Free(b);

    rc |= Expect(CheckHeap(), "heap failed integrity check after free");
    return rc;
}

static int TestCpuSet(void)
{
    int rc = 0;
    u16 fill16[4] = {0};
    u32 copy32_src[4] = {0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00};
    u32 copy32_dest[4] = {0};

    CpuFill16(0x1234, fill16, sizeof(fill16));
    rc |= Expect(fill16[0] == 0x1234 && fill16[3] == 0x1234, "CpuFill16 mismatch");

    CpuCopy32(copy32_src, copy32_dest, sizeof(copy32_src));
    rc |= Expect(memcmp(copy32_src, copy32_dest, sizeof(copy32_src)) == 0, "CpuCopy32 mismatch");

    return rc;
}

static int TestLz77(void)
{
    static const u8 compressed[] = {
        0x10, 0x04, 0x00, 0x00,
        0x00,
        'A', 'B', 'C', 'D',
    };
    u8 output[4] = {0};
    int rc = 0;

    rc |= Expect(GetDecompressedDataSize(compressed) == 4, "GetDecompressedDataSize mismatch");
    LZDecompressWram(compressed, output);
    rc |= Expect(memcmp(output, "ABCD", 4) == 0, "LZDecompressWram mismatch");
    return rc;
}

static int TestRl(void)
{
    static const u8 compressed[] = {
        0x30, 0x04, 0x00, 0x00,
        0x03,
        'W', 'X', 'Y', 'Z',
    };
    u8 output[4] = {0};
    int rc = 0;

    RLUnCompWram(compressed, output);
    rc |= Expect(memcmp(output, "WXYZ", 4) == 0, "RLUnCompWram mismatch");
    return rc;
}

static int TestGpuRegs(void)
{
    int rc = 0;

    HostMemoryReset();
    InitGpuRegManager();

    REG_VCOUNT = 100;
    REG_DISPCNT = 0;
    SetGpuReg(REG_OFFSET_BG0HOFS, 0x1234);
    rc |= Expect(GetGpuReg(REG_OFFSET_BG0HOFS) == 0x1234, "GetGpuReg buffer mismatch");
    rc |= Expect(REG_BG0HOFS == 0, "SetGpuReg wrote outside vblank");

    CopyBufferedValuesToGpuRegs();
    rc |= Expect(REG_BG0HOFS == 0x1234, "CopyBufferedValuesToGpuRegs mismatch");

    REG_VCOUNT = 161;
    SetGpuReg(REG_OFFSET_BG0VOFS, 0x5678);
    rc |= Expect(REG_BG0VOFS == 0x5678, "SetGpuReg did not write during vblank");

    REG_IME = 1;
    EnableInterrupts(INTR_FLAG_VBLANK | INTR_FLAG_HBLANK);
    rc |= Expect(REG_IE == (INTR_FLAG_VBLANK | INTR_FLAG_HBLANK), "EnableInterrupts mismatch");
    rc |= Expect((REG_DISPSTAT & (DISPSTAT_VBLANK_INTR | DISPSTAT_HBLANK_INTR))
                 == (DISPSTAT_VBLANK_INTR | DISPSTAT_HBLANK_INTR),
                 "DISPSTAT interrupt sync mismatch");

    DisableInterrupts(INTR_FLAG_HBLANK);
    rc |= Expect(REG_IE == INTR_FLAG_VBLANK, "DisableInterrupts mismatch");
    rc |= Expect((REG_DISPSTAT & (DISPSTAT_VBLANK_INTR | DISPSTAT_HBLANK_INTR))
                 == DISPSTAT_VBLANK_INTR,
                 "DISPSTAT interrupt clear mismatch");

    return rc;
}

static int TestDma3Manager(void)
{
    int rc = 0;
    u32 copy32_src[4] = {0xCAFEBABE, 0x0BADF00D, 0x11223344, 0x55667788};
    u32 copy32_dest[4] = {0};
    u16 fill16_dest[8] = {0};
    s16 request;

    HostMemoryReset();
    ClearDma3Requests();
    REG_VCOUNT = 0;

    request = RequestDma3Copy(copy32_src, copy32_dest, sizeof(copy32_src), DMA3_32BIT);
    rc |= Expect(request >= 0, "RequestDma3Copy returned failure");
    rc |= Expect(WaitDma3Request(request) == -1, "WaitDma3Request should report pending copy");
    ProcessDma3Requests();
    rc |= Expect(memcmp(copy32_src, copy32_dest, sizeof(copy32_src)) == 0, "ProcessDma3Requests copy32 mismatch");
    rc |= Expect(WaitDma3Request(request) == 0, "WaitDma3Request should report completed copy");

    request = RequestDma3Fill(0x1357, fill16_dest, sizeof(fill16_dest), DMA3_16BIT);
    rc |= Expect(request >= 0, "RequestDma3Fill returned failure");
    ProcessDma3Requests();
    rc |= Expect(fill16_dest[0] == 0x1357 && fill16_dest[7] == 0x1357, "ProcessDma3Requests fill16 mismatch");
    rc |= Expect(WaitDma3Request(-1) == 0, "WaitDma3Request(-1) should report no pending DMA");

    return rc;
}

static int TestScanlineEffect(void)
{
    int rc = 0;
    struct ScanlineEffectParams params;
    u8 task_id;

    HostMemoryReset();
    ResetTasks();
    ScanlineEffect_Clear();

    params.dmaDest = &REG_BG0HOFS;
    params.dmaControl = SCANLINE_EFFECT_DMACNT_16BIT;
    params.initState = 1;
    params.unused9 = 0x5A;
    ScanlineEffect_SetParams(params);

    gScanlineEffectRegBuffers[0][0] = 0x0111;
    gScanlineEffectRegBuffers[0][1] = 0x0222;
    gScanlineEffectRegBuffers[0][2] = 0x0333;

    ScanlineEffect_InitHBlankDmaTransfer();
    rc |= Expect(REG_BG0HOFS == 0x0111, "ScanlineEffect first scanline copy mismatch");
    rc |= Expect(gScanlineEffect.srcBuffer == 1, "ScanlineEffect did not swap buffers");
    rc |= Expect((REG_DMA0CNT_H & DMA_ENABLE) != 0, "ScanlineEffect did not arm DMA0");

    HostDmaTriggerHBlank();
    rc |= Expect(REG_BG0HOFS == 0x0222, "ScanlineEffect HBlank copy #1 mismatch");
    HostDmaTriggerHBlank();
    rc |= Expect(REG_BG0HOFS == 0x0333, "ScanlineEffect HBlank copy #2 mismatch");

    gBattle_BG0_X = 7;
    task_id = ScanlineEffect_InitWave(0, 4, 2, 8, 1, SCANLINE_EFFECT_REG_BG0HOFS, TRUE);
    rc |= Expect(task_id < NUM_TASKS, "ScanlineEffect_InitWave returned invalid task id");
    rc |= Expect(gScanlineEffect.waveTaskId == task_id, "ScanlineEffect waveTaskId mismatch");
    rc |= Expect(GetTaskCount() == 1, "ScanlineEffect_InitWave did not create task");

    RunTasks();
    rc |= Expect(gTasks[task_id].data[4] == 0, "ScanlineEffect task delay countdown mismatch");
    rc |= Expect(gScanlineEffectRegBuffers[gScanlineEffect.srcBuffer][0]
                 == gScanlineEffectRegBuffers[0][320] + gBattle_BG0_X,
                 "ScanlineEffect task did not apply BG offset");

    ScanlineEffect_Stop();
    rc |= Expect(gScanlineEffect.state == 0, "ScanlineEffect_Stop state mismatch");
    rc |= Expect(gScanlineEffect.waveTaskId == 0xFF, "ScanlineEffect_Stop wave task mismatch");
    rc |= Expect(GetTaskCount() == 0, "ScanlineEffect_Stop did not destroy task");
    rc |= Expect((REG_DMA0CNT_H & DMA_ENABLE) == 0, "ScanlineEffect_Stop did not stop DMA0");

    return rc;
}

static int TestPalette(void)
{
    static const u16 palette[16] = {
        0x0000, 0x001F, 0x03E0, 0x7C00,
        0x03FF, 0x7C1F, 0x7FE0, 0x4210,
        0x0010, 0x0200, 0x4000, 0x7FFF,
        0x0210, 0x4010, 0x421F, 0x56B5,
    };
    int rc = 0;
    HostMemoryReset();
    ResetPaletteFade();
    LoadPalette(palette, BG_PLTT_ID(0), sizeof(palette));
    rc |= Expect(gPlttBufferUnfaded[1] == palette[1], "LoadPalette unfaded mismatch");
    rc |= Expect(gPlttBufferFaded[1] == palette[1], "LoadPalette faded mismatch");

    TransferPlttBuffer();
    rc |= Expect(((u16 *)PLTT)[1] == palette[1], "TransferPlttBuffer mismatch");

    rc |= Expect(BeginNormalPaletteFade(1, 0, 4, 0, 0) == TRUE, "BeginNormalPaletteFade failed");
    rc |= Expect(gPaletteFade.active == TRUE, "BeginNormalPaletteFade did not activate fade");
    rc |= Expect(gPlttBufferFaded[1] != gPlttBufferUnfaded[1], "palette fade did not change color");
    return rc;
}

static int TestBg(void)
{
    static const struct BgTemplate templates[] = {
        {
            .bg = 0,
            .charBaseIndex = 0,
            .mapBaseIndex = 1,
            .screenSize = 0,
            .paletteMode = 0,
            .priority = 1,
            .baseTile = 0,
        },
    };
    static const u16 tiles[16] = {
        0x0001, 0x0002, 0x0003, 0x0004,
        0x0005, 0x0006, 0x0007, 0x0008,
        0x0009, 0x000A, 0x000B, 0x000C,
        0x000D, 0x000E, 0x000F, 0x0010,
    };
    u16 *tilemap = (u16 *)EWRAM_START;
    int rc = 0;
    u16 expected_bgcnt;
    u16 cursor;

    HostMemoryReset();
    ClearDma3Requests();
    InitGpuRegManager();
    ResetBgsAndClearDma3BusyFlags(TRUE);
    InitBgsFromTemplates(0, templates, ARRAY_COUNT(templates));
    ShowBg(0);
    CopyBufferedValuesToGpuRegs();

    expected_bgcnt = templates[0].priority
                   | (templates[0].charBaseIndex << 2)
                   | (templates[0].paletteMode << 7)
                   | (templates[0].mapBaseIndex << 8)
                   | (templates[0].screenSize << 14);
    rc |= Expect(REG_BG0CNT == expected_bgcnt, "ShowBg BG0CNT mismatch");
    rc |= Expect((REG_DISPCNT & DISPCNT_BG0_ON) != 0, "ShowBg did not enable BG0");

    cursor = LoadBgTiles(0, tiles, sizeof(tiles), 0);
    rc |= Expect(cursor != (u16)-1, "LoadBgTiles returned failure");
    ProcessDma3Requests();
    rc |= Expect(memcmp((void *)BG_VRAM, tiles, sizeof(tiles)) == 0, "LoadBgTiles VRAM mismatch");
    rc |= Expect(IsDma3ManagerBusyWithBgCopy() == FALSE, "BG DMA busy flag did not clear");

    memset(tilemap, 0, BG_SCREEN_SIZE);
    SetBgTilemapBuffer(0, tilemap);
    FillBgTilemapBufferRect(0, 1, 0, 0, 2, 2, 3);
    rc |= Expect(tilemap[0] == (1 | (3 << 12)), "FillBgTilemapBufferRect mismatch");

    CopyBgTilemapBufferToVram(0);
    ProcessDma3Requests();
    rc |= Expect(((u16 *)BG_SCREEN_ADDR(1))[0] == tilemap[0], "CopyBgTilemapBufferToVram mismatch");

    ChangeBgX(0, 0x1200, BG_COORD_SET);
    ChangeBgY(0, 0x3400, BG_COORD_SET);
    CopyBufferedValuesToGpuRegs();
    rc |= Expect(REG_BG0HOFS == 0x12, "ChangeBgX mismatch");
    rc |= Expect(REG_BG0VOFS == 0x34, "ChangeBgY mismatch");

    return rc;
}

static int TestSprite(void)
{
    static const u8 sheet_data[32] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
        0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
        0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F,
    };
    static const u16 palette_data[16] = {
        0x0000, 0x001F, 0x03E0, 0x7C00,
        0x03FF, 0x7C1F, 0x7FE0, 0x4210,
        0x0010, 0x0200, 0x4000, 0x7FFF,
        0x0210, 0x4010, 0x421F, 0x56B5,
    };
    static const struct SpriteSheet sheet = {
        .data = sheet_data,
        .size = sizeof(sheet_data),
        .tag = 0x1234,
    };
    static const struct SpritePalette palette = {
        .data = palette_data,
        .tag = 0x2345,
    };
    static const union AnimCmd anim[] = {
        ANIMCMD_FRAME(0, 1),
        ANIMCMD_END,
    };
    static const union AnimCmd *const anims[] = {
        anim,
    };
    static const union AffineAnimCmd affine_anim[] = {
        AFFINEANIMCMD_END,
    };
    static const union AffineAnimCmd *const affine_anims[] = {
        affine_anim,
    };
    static const struct SpriteTemplate template = {
        .tileTag = 0x1234,
        .paletteTag = 0x2345,
        .oam = &gDummyOamData,
        .anims = anims,
        .images = NULL,
        .affineAnims = affine_anims,
        .callback = SpriteCallbackDummy,
    };
    int rc = 0;
    u16 tile_start;
    u8 palette_slot;
    u8 sprite_id;

    HostMemoryReset();
    ResetSpriteData();
    FreeAllSpritePalettes();

    rc |= Expect(gMain.oamBuffer[0].x == gDummyOamData.x, "ResetSpriteData OAM reset mismatch");
    rc |= Expect(gSpriteCoordOffsetX == 0 && gSpriteCoordOffsetY == 0, "ResetSpriteData coord offset mismatch");

    tile_start = LoadSpriteSheet(&sheet);
    rc |= Expect(GetSpriteTileStartByTag(sheet.tag) != TAG_NONE, "LoadSpriteSheet did not register tile tag");
    rc |= Expect(memcmp(OBJ_VRAM0, sheet_data, sizeof(sheet_data)) == 0, "LoadSpriteSheet VRAM mismatch");

    palette_slot = LoadSpritePalette(&palette);
    rc |= Expect(palette_slot != 0xFF, "LoadSpritePalette returned failure");
    rc |= Expect(GetSpritePaletteTagByPaletteNum(palette_slot) == palette.tag, "LoadSpritePalette tag mismatch");
    rc |= Expect(gPlttBufferUnfaded[OBJ_PLTT_OFFSET] == palette_data[0], "LoadSpritePalette buffer mismatch");

    sprite_id = CreateSprite(&template, 10, 20, 1);
    rc |= Expect(sprite_id != MAX_SPRITES, "CreateSprite returned failure");
    rc |= Expect(gSprites[sprite_id].inUse == TRUE, "CreateSprite did not mark sprite in use");
    rc |= Expect(gSprites[sprite_id].x == 10 && gSprites[sprite_id].y == 20, "CreateSprite position mismatch");
    rc |= Expect(gSprites[sprite_id].oam.tileNum == tile_start, "CreateSprite tile start mismatch");
    rc |= Expect(gSprites[sprite_id].oam.paletteNum == palette_slot, "CreateSprite palette slot mismatch");

    DestroySpriteAndFreeResources(&gSprites[sprite_id]);
    rc |= Expect(gSprites[sprite_id].inUse == FALSE, "DestroySpriteAndFreeResources did not free sprite");
    rc |= Expect(GetSpriteTileStartByTag(sheet.tag) == TAG_NONE, "DestroySpriteAndFreeResources did not free tiles");
    rc |= Expect(GetSpritePaletteTagByPaletteNum(palette_slot) == TAG_NONE, "DestroySpriteAndFreeResources did not free palette");

    return rc;
}

static int TestMainRuntime(void)
{
    int rc = 0;
    u16 actual_rng;
    u16 expected_rng;
    u16 dma_src = 0x5A5A;
    u16 dma_dest = 0;
    u32 vblank_counter1 = 0;

    HostMemoryReset();
    HostStubReset();
    ClearDma3Requests();
    InitGpuRegManager();
    HostCrt0Init();
    memset(&gMain, 0, sizeof(gMain));

    rc |= Expect(INTR_VECTOR == (u32)(uintptr_t)intr_main, "HostCrt0Init vector mismatch");

    InitKeys();
    rc |= Expect(gKeyRepeatContinueDelay == 5, "InitKeys repeat continue mismatch");
    rc |= Expect(gKeyRepeatStartDelay == 40, "InitKeys repeat start mismatch");
    rc |= Expect(gMain.heldKeys == 0, "InitKeys heldKeys mismatch");
    rc |= Expect(gMain.newKeys == 0, "InitKeys newKeys mismatch");
    rc |= Expect(gMain.newAndRepeatedKeys == 0, "InitKeys newAndRepeatedKeys mismatch");
    rc |= Expect(gMain.heldKeysRaw == 0, "InitKeys heldKeysRaw mismatch");
    rc |= Expect(gMain.newKeysRaw == 0, "InitKeys newKeysRaw mismatch");

    REG_TM1CNT_L = 0xBEEF;
    REG_TM1CNT_H = 0x1234;
    SeedRngAndSetTrainerId();
    actual_rng = Random();
    SeedRng(0xBEEF);
    expected_rng = Random();
    rc |= Expect(GetGeneratedTrainerIdLower() == 0xBEEF, "SeedRngAndSetTrainerId trainer id mismatch");
    rc |= Expect(REG_TM1CNT_H == 0, "SeedRngAndSetTrainerId timer stop mismatch");
    rc |= Expect(actual_rng == expected_rng, "SeedRngAndSetTrainerId RNG seed mismatch");

    StartTimer1();
    rc |= Expect(REG_TM1CNT_H == 0x80, "StartTimer1 mismatch");

    SetMainCallback2(TestMainCallback);
    rc |= Expect(gMain.callback2 == TestMainCallback, "SetMainCallback2 callback mismatch");
    rc |= Expect(gMain.state == 0, "SetMainCallback2 state mismatch");

    REG_DISPSTAT = 0;
    EnableVCountIntrAtLine150();
    rc |= Expect((REG_IE & INTR_FLAG_VCOUNT) != 0, "EnableVCountIntrAtLine150 IE mismatch");

    InitIntrHandlers();
    rc |= Expect(memcmp(IntrMain_Buffer, intr_main, sizeof(IntrMain_Buffer)) == 0,
                 "InitIntrHandlers intr_main copy mismatch");
    rc |= Expect(INTR_VECTOR == (u32)(uintptr_t)IntrMain_Buffer, "InitIntrHandlers vector mismatch");
    rc |= Expect(gMain.vblankCallback == NULL, "InitIntrHandlers vblank callback reset mismatch");
    rc |= Expect(gMain.hblankCallback == NULL, "InitIntrHandlers hblank callback reset mismatch");
    rc |= Expect(gMain.serialCallback == NULL, "InitIntrHandlers serial callback reset mismatch");
    rc |= Expect(REG_IME == 1, "InitIntrHandlers IME mismatch");
    rc |= Expect((REG_IE & INTR_FLAG_VBLANK) != 0, "InitIntrHandlers VBlank enable mismatch");

    gIntrTable[1] = NULL;
    gIntrTable[2] = NULL;
    RestoreSerialTimer3IntrHandlers();
    rc |= Expect(gIntrTable[1] == gIntrTableTemplate[1], "RestoreSerialTimer3IntrHandlers serial mismatch");
    rc |= Expect(gIntrTable[2] == gIntrTableTemplate[2], "RestoreSerialTimer3IntrHandlers timer3 mismatch");

    SetVCountCallback(TestMainCallback);
    rc |= Expect(gMain.vcountCallback == TestMainCallback, "SetVCountCallback mismatch");

    InitFlashTimer();
    /* gHostStubLastFlashTimerNum/Intr removed — InitFlashTimer now from upstream */

    memset(gPokemonCrySongs, 0xA5, MAX_POKEMON_CRIES * sizeof(struct PokemonCrySong));
    ClearPokemonCrySongs();
    rc |= Expect(((u8 *)gPokemonCrySongs)[0] == 0, "ClearPokemonCrySongs first byte mismatch");
    rc |= Expect(((u8 *)gPokemonCrySongs)[MAX_POKEMON_CRIES * sizeof(struct PokemonCrySong) - 1] == 0,
                 "ClearPokemonCrySongs last byte mismatch");

    sHBlankCallbackCalls = 0;
    sSerialCallbackCalls = 0;
    sVBlankCallbackCalls = 0;
    SetHBlankCallback(TestHBlankCallback);
    SetSerialCallback(TestSerialCallback);
    SetVBlankCallback(TestVBlankCallback);
    SetVBlankCounter1Ptr(&vblank_counter1);
    gMain.vblankCounter2 = 0;
    EnableInterrupts(INTR_FLAG_HBLANK | INTR_FLAG_SERIAL);

    gMain.intrCheck = 0;
    INTR_CHECK = 0;
    HostInterruptRaise(INTR_FLAG_HBLANK);
    rc |= Expect(HostInterruptDispatch() == TRUE, "HBlank dispatch mismatch");
    rc |= Expect(sHBlankCallbackCalls == 1, "HBlank callback mismatch");
    rc |= Expect((REG_IF & INTR_FLAG_HBLANK) == 0, "HBlank IF ack mismatch");
    rc |= Expect((INTR_CHECK & INTR_FLAG_HBLANK) != 0, "HBlank INTR_CHECK mismatch");
    rc |= Expect((gMain.intrCheck & INTR_FLAG_HBLANK) != 0, "HBlank gMain.intrCheck mismatch");

    gMain.intrCheck = 0;
    INTR_CHECK = 0;
    HostInterruptRaise(INTR_FLAG_SERIAL);
    rc |= Expect(HostInterruptDispatch() == TRUE, "Serial dispatch mismatch");
    rc |= Expect(sSerialCallbackCalls == 1, "Serial callback mismatch");
    rc |= Expect((REG_IF & INTR_FLAG_SERIAL) == 0, "Serial IF ack mismatch");
    rc |= Expect((INTR_CHECK & INTR_FLAG_SERIAL) != 0, "Serial INTR_CHECK mismatch");
    rc |= Expect((gMain.intrCheck & INTR_FLAG_SERIAL) != 0, "Serial gMain.intrCheck mismatch");

    /* Initialize sound engine so m4aSoundVSync (called by VCountIntr) has a valid SOUND_INFO_PTR */
    HostNativeSoundInit();
    InitRFUAPI(); /* Safe on native — uses separate static storage for RFU structs */
    gLinkVSyncDisabled = FALSE;
    gWirelessCommType = 0;
    gSoundInfo.pcmDmaCounter = 7;
    SetGpuReg(REG_OFFSET_BG0HOFS, 0x1357);
    RequestDma3Copy(&dma_src, &dma_dest, sizeof(dma_src), DMA3_16BIT);
    gMain.intrCheck = 0;
    INTR_CHECK = 0;
    HostInterruptRaise(INTR_FLAG_VBLANK | INTR_FLAG_VCOUNT);
    rc |= Expect(HostInterruptDispatch() == TRUE, "VCount/VBlank first dispatch mismatch");
    /* gHostStubM4aSoundVSyncCalls removed — m4aSoundVSync now from upstream */
    rc |= Expect(sVBlankCallbackCalls == 0, "VBlank ran before VCount");
    rc |= Expect((INTR_CHECK & INTR_FLAG_VCOUNT) != 0, "VCount INTR_CHECK mismatch");
    rc |= Expect((gMain.intrCheck & INTR_FLAG_VCOUNT) != 0, "VCount gMain.intrCheck mismatch");
    rc |= Expect(gMain.vblankCounter2 == 0, "VBlank ran during VCount dispatch");

    gMain.intrCheck = 0;
    INTR_CHECK = 0;
    rc |= Expect(HostInterruptDispatch() == TRUE, "VBlank second dispatch mismatch");
    rc |= Expect(sVBlankCallbackCalls == 1, "VBlank callback mismatch");
    rc |= Expect(vblank_counter1 == 1, "VBlank counter1 mismatch");
    rc |= Expect(gMain.vblankCounter2 == 1, "VBlank counter2 mismatch");
    rc |= Expect(REG_BG0HOFS == 0x1357, "VBlank did not flush GPU regs");
    rc |= Expect(dma_dest == dma_src, "VBlank did not process DMA3 queue");
    rc |= Expect(gPcmDmaCounter == 6, "VBlank pcmDmaCounter mismatch (after m4aSoundVSync decrements 7->6)");
    /* gHostStubLinkVSyncCalls removed — LinkVSync now from upstream */
    /* gHostStubM4aSoundMainCalls removed — m4aSoundMain now from upstream */
    /* gHostStubTryReceiveLinkBattleDataCalls removed — function now from upstream */
    /* gHostStubUpdateWirelessStatusIndicatorSpriteCalls removed — function now from upstream */
    rc |= Expect((REG_IF & INTR_FLAG_VBLANK) == 0, "VBlank IF ack mismatch");
    rc |= Expect((INTR_CHECK & INTR_FLAG_VBLANK) != 0, "VBlank INTR_CHECK mismatch");
    rc |= Expect((gMain.intrCheck & INTR_FLAG_VBLANK) != 0, "VBlank gMain.intrCheck mismatch");
    rc |= Expect(HostInterruptDispatch() == FALSE, "interrupt queue should be empty");

    DisableVBlankCounter1();
    gWirelessCommType = 2;
    gSoundInfo.pcmDmaCounter = 9;
    HostInterruptRaise(INTR_FLAG_VBLANK);
    rc |= Expect(HostInterruptDispatch() == TRUE, "Wireless VBlank dispatch mismatch");
    rc |= Expect(vblank_counter1 == 1, "DisableVBlankCounter1 mismatch");
    /* gHostStubRfuVSyncCalls removed — RfuVSync now from upstream */
    /* gHostStubLinkVSyncCalls removed — LinkVSync now from upstream */
    /* gHostStubM4aSoundMainCalls removed — m4aSoundMain now from upstream */
    rc |= Expect(gPcmDmaCounter == 9, "Second VBlank pcmDmaCounter mismatch (no VCount this time, should be 9)");

    gLinkVSyncDisabled = TRUE;
    gWirelessCommType = 0;
    HostInterruptRaise(INTR_FLAG_VBLANK);
    rc |= Expect(HostInterruptDispatch() == TRUE, "Link-disabled VBlank dispatch mismatch");
    /* gHostStubLinkVSyncCalls removed — LinkVSync now from upstream */

    return rc;
}

static int TestAgbMainBootSlice(void)
{
    enum HostAgbMainExitReason exit_reason;
    int rc = 0;

    HostMemoryReset();
    HostStubReset();
    HostIntroStubReset();
    ClearDma3Requests();
    memset(&gMain, 0, sizeof(gMain));
    /* Initialize sound engine so m4aSoundVSync (called by VCountIntr) has a valid SOUND_INFO_PTR */
    HostNativeSoundInit();
    InitRFUAPI(); /* Safe on native — uses separate static storage for RFU structs */
    gLinkVSyncDisabled = FALSE;
    gLinkTransferringData = FALSE;

    exit_reason = HostRunAgbMainUntilSoftReset(1);

    rc |= Expect(exit_reason == HOST_AGBMAIN_EXIT_SOFT_RESET, "AgbMain did not exit via SoftReset");
    rc |= Expect(gMain.callback2 == CB2_InitCopyrightScreenAfterBootup, "AgbMain callback2 target mismatch");
    rc |= Expect(gMain.state == 1, "AgbMain intro state mismatch");
    rc |= Expect(gSaveBlock1Ptr == &gSaveBlock1, "AgbMain gSaveBlock1Ptr mismatch");
    rc |= Expect(gSaveBlock2Ptr == &gSaveBlock2, "AgbMain gSaveBlock2Ptr mismatch");
    rc |= Expect(gSaveBlock2.encryptionKey == 0, "AgbMain encryption key mismatch");
    rc |= Expect(gQuestLogPlaybackState == QL_PLAYBACK_STATE_STOPPED, "AgbMain quest log state mismatch");
    rc |= Expect(gHelpSystemEnabled == FALSE, "AgbMain help system flag mismatch");
    rc |= Expect(gSoftResetDisabled == FALSE, "AgbMain soft reset flag mismatch");
    rc |= Expect(gLinkTransferringData == FALSE, "AgbMain link transferring flag mismatch");
    rc |= Expect(((u16 *)BG_PLTT)[0] == RGB_WHITE, "AgbMain BG palette init mismatch");
    rc |= Expect(REG_WAITCNT == (WAITCNT_PREFETCH_ENABLE | WAITCNT_WS0_S_1 | WAITCNT_WS0_N_3),
                 "AgbMain WAITCNT mismatch");
    rc |= Expect(INTR_VECTOR == (u32)(uintptr_t)IntrMain_Buffer, "AgbMain INTR_VECTOR mismatch");
    rc |= Expect((REG_IE & INTR_FLAG_VBLANK) != 0, "AgbMain did not enable VBlank interrupt");
    rc |= Expect((REG_IE & INTR_FLAG_VCOUNT) != 0, "AgbMain did not enable VCount interrupt");
    /* gHostStubM4aSoundInitCalls removed — m4aSoundInit now from upstream */
    /* gHostStubCheckForFlashMemoryCalls removed — CheckForFlashMemory now from upstream */
    /* gHostStubInitRFUCalls removed — InitRFU now from upstream */
    /* gHostStubSetNotInSaveFailedScreenCalls removed — function now from upstream */
    rc |= Expect(gHostIntroStubGameCubeMultiBootInitCalls == 1, "AgbMain intro did not init multiboot");
    rc |= Expect(gHostIntroStubGameCubeMultiBootMainCalls == 1, "AgbMain intro did not run copyright main once");
    rc |= Expect(gMain.vblankCallback != NULL, "AgbMain intro did not install VBlank callback");
    rc |= Expect(gMain.serialCallback != NULL, "AgbMain intro did not install serial callback");
    /* gHostIntroStubLoadGameSaveCalls removed — LoadGameSave now from upstream */
    /* gHostStubPlayTimeCounterUpdateCalls removed — function now from upstream */
    /* gHostStubMapMusicMainCalls removed — MapMusicMain now from upstream sound.c */
    /* gHostStubM4aSoundVSyncCalls removed — m4aSoundVSync now from upstream */
    /* gHostStubM4aSoundMainCalls removed — m4aSoundMain now from upstream */
    /* gHostStubLinkVSyncCalls removed — LinkVSync now from upstream */
    /* gHostStubTryReceiveLinkBattleDataCalls removed — function now from upstream */
    /* gHostStubUpdateWirelessStatusIndicatorSpriteCalls removed — function now from upstream */
    rc |= Expect(gHostStubSoftResetCalls == 1, "AgbMain SoftReset count mismatch");
    /* gHostStubM4aSoundVSyncOffCalls removed — m4aSoundVSyncOff now from upstream */
    rc |= Expect(gMain.vblankCounter2 >= 1, "AgbMain did not observe VBlank");
    rc |= Expect(CheckHeap(), "AgbMain heap init integrity mismatch");

    return rc;
}

static void RunMainCallbackFrame(void)
{
    gMain.callback2();
    ProcessDma3Requests();
    if (gMain.vblankCallback != NULL)
        gMain.vblankCallback();
}

static void SetKeys(u16 newKeys, u16 heldKeys)
{
    gMain.newKeys = newKeys;
    gMain.newKeysRaw = newKeys;
    gMain.heldKeys = heldKeys;
    gMain.heldKeysRaw = heldKeys;
}

static void ClearKeys(void)
{
    SetKeys(0, 0);
}

static void RunFrames(int count)
{
    while (count-- > 0)
        RunMainCallbackFrame();
}

static int PulseButtonUntilCounterIncrements(u16 button, const u32 *counter, u32 initialCount, int maxFrames, const char *message)
{
    int i;

    for (i = 0; i < maxFrames && *counter == initialCount; i++)
    {
        if (i % 6 == 0)
            SetKeys(button, button);
        else
            ClearKeys();
        RunMainCallbackFrame();
    }

    ClearKeys();
    return Expect(*counter > initialCount, message);
}

static int RunUntilCounterIncrements(const u32 *counter, u32 initialCount, int maxFrames, const char *message)
{
    int i;

    for (i = 0; i < maxFrames && *counter == initialCount; i++)
        RunMainCallbackFrame();

    return Expect(*counter > initialCount, message);
}

static int RunUntilPlaceholderSourceEquals(const u8 *expected, int maxFrames, const char *message)
{
    int i;
    for (i = 0; i < maxFrames; i++)
    {
        if (gHostLastOakSpeechSource == expected)
            break;
        /* Press A every 16 frames to advance text */
        if ((i % 16) == 0)
            SetKeys(A_BUTTON, A_BUTTON);
        else if ((i % 16) == 1)
            ClearKeys();
        RunMainCallbackFrame();
    }
    ClearKeys();
    return Expect(gHostLastOakSpeechSource == expected, message);
}

static int RunUntilCallback2(MainCallback expected, int maxFrames, const char *message)
{
    int i;

    for (i = 0; i < maxFrames && gMain.callback2 != expected; i++)
        RunMainCallbackFrame();

    return Expect(gMain.callback2 == expected, message);
}

static int RunUntilWindowIdMatchingWithAPulses(u8 bg, u8 left, u8 top, u8 width, u8 height, int maxFrames, const char *message)
{
    int i;

    for (i = 0; i < maxFrames && FindWindowIdMatching(bg, left, top, width, height) < 0; i++)
    {
        if ((i % 16) == 0)
            SetKeys(A_BUTTON, A_BUTTON);
        else if ((i % 16) == 1)
            ClearKeys();
        RunMainCallbackFrame();
    }

    ClearKeys();
    return Expect(FindWindowIdMatching(bg, left, top, width, height) >= 0, message);
}

static int RunUntilCallback2WithAPulses(MainCallback expected, int maxFrames, const char *message)
{
    int i;

    for (i = 0; i < maxFrames && gMain.callback2 != expected; i++)
    {
        if ((i % 16) == 0)
            SetKeys(A_BUTTON, A_BUTTON);
        else if ((i % 16) == 1)
            ClearKeys();
        RunMainCallbackFrame();
    }

    ClearKeys();
    return Expect(gMain.callback2 == expected, message);
}

static void ResetBootCallbackHarness(void)
{
    HostMemoryReset();
    HostFlashInit(NULL); /* Reset flash to erased state for clean boot */
    HostStubReset();
    HostIntroStubReset();
    HostOakSpeechStubReset();
    HostTitleScreenStubReset();
    HostNewGameStubReset();
    ClearDma3Requests();
    InitGpuRegManager();
    HostCrt0Init();
    memset(&gMain, 0, sizeof(gMain));
    ResetBgs();
    InitIntrHandlers();
    InitHeap(gHeap, HEAP_SIZE);
    {
        extern void SetDefaultFontsPointer(void);
        SetDefaultFontsPointer();
    }
    HostNativeSoundInit();
    InitRFUAPI();
    CheckForFlashMemory();
    gSaveBlock1Ptr = &gSaveBlock1;
    gSaveBlock2Ptr = &gSaveBlock2;
}

static int AdvanceToFirstIntroFrame(void)
{
    MainCallback copyrightCallback;
    MainCallback waitFadeCallback;
    MainCallback setupIntroCallback;
    MainCallback introCallback;
    int rc = 0;
    int i;

    ResetBootCallbackHarness();

    SetMainCallback2(CB2_InitCopyrightScreenAfterBootup);
    copyrightCallback = gMain.callback2;
    for (i = 0; i < 256 && gMain.callback2 == copyrightCallback; i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 != copyrightCallback, "copyright callback did not advance");
    rc |= Expect(gHostIntroStubGameCubeMultiBootInitCalls == 1, "copyright callback did not init multiboot");
    rc |= Expect(gHostIntroStubGameCubeMultiBootMainCalls == 141, "copyright callback main-loop count mismatch");
    rc |= Expect(gHostIntroStubGameCubeMultiBootQuitCalls == 1, "copyright callback did not quit multiboot");
    /* gHostIntroStubResetMenuAndMonGlobalsCalls removed — function now from upstream */
    /* gHostIntroStubSaveResetSaveCountersCalls removed — function now from upstream */
    /* gHostIntroStubLoadGameSaveCalls removed — function now from upstream */
    /* gHostIntroStubSav2ClearSetDefaultCalls removed — function now from upstream */
    /* gHostIntroStubSetPokemonCryStereoCalls removed — function now from upstream */
    /* gHostIntroStubResetSerialCalls removed — function now from upstream */
    rc |= Expect(gMain.serialCallback == SerialCB, "copyright callback did not restore SerialCB");

    waitFadeCallback = gMain.callback2;
    for (i = 0; i < 8 && gMain.callback2 == waitFadeCallback; i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 != waitFadeCallback, "wait-fade callback did not advance");

    setupIntroCallback = gMain.callback2;
    for (i = 0; i < 8 && gMain.callback2 == setupIntroCallback; i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 != setupIntroCallback, "setup-intro callback did not advance");
    /* Now handled by real implementations: ResetTempTileDataBuffers, DecompressAndCopyTileDataToVram, FreeTempTileDataBuffersIfPossible */
    rc |= Expect(gMain.vblankCallback != NULL, "setup-intro did not install VBlank callback");
    rc |= Expect(GetTaskCount() == 1, "setup-intro did not create intro task");

    introCallback = gMain.callback2;
    RunMainCallbackFrame();
    rc |= Expect(gMain.callback2 == introCallback, "CB2_Intro changed unexpectedly");
    rc |= Expect(WindowMatches(0, 2, 6, 4, 18, 9),
                 "CB2_Intro did not allocate the Game Freak logo window");
    rc |= Expect(gWindowBgTilemapBuffers[2] != NULL,
                 "CB2_Intro did not attach to the Game Freak BG tilemap buffer");
    rc |= Expect(WindowTileDataHasNonZero(0),
                 "CB2_Intro did not draw Game Freak logo pixels into the window buffer");
    rc |= Expect(WindowTilemapHasNonZero(0),
                 "CB2_Intro did not populate the Game Freak logo tilemap");
    rc |= Expect(GetTaskCount() == 1, "CB2_Intro task count mismatch");

    return rc;
}

static int TestIntroBootCallbacks(void)
{
    int rc = 0;
    int i;

    rc |= AdvanceToFirstIntroFrame();

    /* gHostIntroStubPlaySECalls removed — PlaySE now from upstream.
       Run 16 frames to advance to the Game Freak star phase. */
    RunFrames(16);

    /* gHostIntroStubPlaySECalls removed — cannot verify PlaySE call count */
    rc |= Expect(GetTaskCount() == 1, "intro task count changed before Game Freak star");
    rc |= Expect((GetGpuReg(REG_OFFSET_DISPCNT) & DISPCNT_WIN1_ON) != 0,
                 "intro did not enable WIN1 before Game Freak star");
    rc |= Expect(GetGpuReg(REG_OFFSET_WIN1H) == DISPLAY_WIDTH, "intro WIN1H mismatch before Game Freak star");
    rc |= Expect(GetGpuReg(REG_OFFSET_WIN1V) == WIN_RANGE(DISPLAY_HEIGHT / 2 - 48, DISPLAY_HEIGHT / 2 + 48),
                 "intro WIN1V mismatch before Game Freak star");

    return rc;
}

static int AdvanceToGameFreakStar(void)
{
    int rc = 0;

    rc |= AdvanceToFirstIntroFrame();

    /* gHostIntroStubPlaySECalls removed — PlaySE now from upstream.
       Run 16 frames to advance to the Game Freak star phase. */
    RunFrames(16);

    /* gHostIntroStubPlaySECalls removed — cannot verify PlaySE call count */
    rc |= Expect(GetTaskCount() == 1, "intro task count changed before Game Freak star");
    rc |= Expect((GetGpuReg(REG_OFFSET_DISPCNT) & DISPCNT_WIN1_ON) != 0,
                 "intro did not enable WIN1 before Game Freak star");
    rc |= Expect(GetGpuReg(REG_OFFSET_WIN1H) == DISPLAY_WIDTH, "intro WIN1H mismatch before Game Freak star");
    rc |= Expect(GetGpuReg(REG_OFFSET_WIN1V) == WIN_RANGE(DISPLAY_HEIGHT / 2 - 48, DISPLAY_HEIGHT / 2 + 48),
                 "intro WIN1V mismatch before Game Freak star");

    return rc;
}

static int TestIntroScene1BootSlice(void)
{
    u32 base_window_hash;
    int rc = 0;
    int i;

    rc |= AdvanceToGameFreakStar();

    base_window_hash = WindowTileDataHash(0);

    /* Run until Scene 1 settles: two tasks, fade done, window updated.
       gHostIntroStubM4aSongNumStartCalls removed — m4aSongNumStart now from upstream. */
    for (i = 0; i < 2048; i++)
    {
        if (WindowTileDataHash(0) != base_window_hash
         && GetTaskCount() == 2
         && !gPaletteFade.active)
            break;

        RunMainCallbackFrame();
    }

    /* Now handled by real implementations: StartBlendTask, IsBlendTaskActive,
       DecompressAndCopyTileDataToVram, ResetBgPositions */
    rc |= Expect(WindowTileDataHash(0) != base_window_hash,
                 "intro did not update the Game Freak window contents during reveal phases");
    /* gHostIntroStubM4aSongNumStartCalls removed — m4aSongNumStart now from upstream */
    rc |= Expect(GetTaskCount() == 2, "intro did not reach Scene 1 grass-animation state");
    rc |= Expect(!gPaletteFade.active, "intro remained mid-fade instead of settling into Scene 1");

    return rc;
}

static int TestIntroNaturalTitleHandoff(void)
{
    u32 base_playcry_calls;
    int rc = 0;
    int i;

    rc |= AdvanceToGameFreakStar();

    (void)base_playcry_calls;

    for (i = 0; i < 2048 && gMain.callback2 != CB2_InitTitleScreen; i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 == CB2_InitTitleScreen,
                 "intro did not naturally hand off to title init after Scene 3");
    /* Now handled by real implementations: DecompressAndCopyTileDataToVram, ResetBgPositions */
    /* gHostIntroStubPlayCryByModeCalls removed — PlayCryInternal now from upstream */
    rc |= Expect(GetGpuReg(REG_OFFSET_WIN0V) == WIN_RANGE(32, DISPLAY_HEIGHT - 32),
                 "intro Scene 3 WIN0V mismatch before natural title handoff");
    rc |= Expect(GetGpuReg(REG_OFFSET_WIN0H) == WIN_RANGE(0, DISPLAY_WIDTH / 2),
                 "intro Scene 3 WIN0H mismatch before natural title handoff");

    return rc;
}

static int AdvanceToTitleFirstFrame(void)
{
    MainCallback titleInitCallback;
    int rc = 0;
    int i;

    rc |= AdvanceToGameFreakStar();

    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();
    RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 == CB2_InitTitleScreen, "intro did not hand off to title init");
    rc |= Expect(GetTaskCount() == 0, "intro exit did not destroy the intro task");

    HostIntroStubReset();
    HostTitleScreenStubReset();

    titleInitCallback = gMain.callback2;
    for (i = 0; i < 8 && gMain.callback2 == titleInitCallback; i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 != titleInitCallback, "title init did not hand off to title run");
    /* Now handled by real implementations: DecompressAndCopyTileDataToVram, FreeTempTileDataBuffersIfPossible */
    /* gHostIntroStubM4aSongNumStartCalls removed — m4aSongNumStart now from upstream */
    rc |= Expect(gMain.vblankCallback != NULL, "title init did not install VBlank callback");
    rc |= Expect(GetTaskCount() == 2, "title init task count mismatch");
    rc |= Expect((GetGpuReg(REG_OFFSET_DISPCNT) & (DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP))
                    == (DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP),
                 "title init DISPCNT mismatch");

    RunMainCallbackFrame();

    rc |= Expect(gScanlineEffect.state == 1, "title run did not initialize scanline effect");
    /* gHostTitleStubSetHelpContextCalls removed — SetHelpContext now from upstream */
    rc |= Expect(GetTaskCount() == 2, "title run task count changed unexpectedly");

    return rc;
}

static int AdvanceToTitleRunState(void)
{
    int rc = 0;

    rc |= AdvanceToTitleFirstFrame();

    /* gHostTitleStubSetHelpContextCalls removed — SetHelpContext now from upstream.
       Run 512 frames to advance to the title run state. */
    RunFrames(512);

    /* gHostTitleStubSetHelpContextCalls removed — function now from upstream */
    /* gHostTitleStubLastHelpContext removed — function now from upstream */
    /* gHostTitleStubHelpSystemEnableCalls removed — function now from upstream */
    rc |= Expect((GetGpuReg(REG_OFFSET_DISPCNT) & DISPCNT_OBJWIN_ON) != 0,
                 "title run did not enable OBJWIN");
    rc |= Expect(GetGpuReg(REG_OFFSET_WINOUT) == (WINOUT_WIN01_BG_ALL | WINOUT_WIN01_OBJ | WINOUT_WINOBJ_ALL),
                 "title run WINOUT mismatch");
    rc |= Expect(GetGpuReg(REG_OFFSET_BLDCNT) == (BLDCNT_TGT1_BG0 | BLDCNT_EFFECT_LIGHTEN),
                 "title run BLDCNT mismatch");
    rc |= Expect(GetGpuReg(REG_OFFSET_BLDY) == 13, "title run BLDY mismatch");

    return rc;
}

static int TestTitleScreenBootSlice(void)
{
    return AdvanceToTitleFirstFrame();
}

static int TestTitleScreenRunSlice(void)
{
    return AdvanceToTitleRunState();
}

static int TestTitleScreenRestartHandoff(void)
{
    int rc = 0;
    int i;

    rc |= AdvanceToTitleRunState();

    for (i = 0; i < 9600 && gMain.callback2 != CB2_InitCopyrightScreenAfterTitleScreen; i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 == CB2_InitCopyrightScreenAfterTitleScreen,
                 "title run did not hand off back to copyright after timeout");
    /* gHostTitleStubFadeOutMapMusicCalls removed — FadeOutMapMusic now from upstream */
    /* gHostTitleStubLastFadeOutMapMusicSpeed removed — FadeOutMapMusic now from upstream */
    /* gHostTitleStubHelpSystemDisableCalls removed — function now from upstream */

    return rc;
}

static int AdvanceToOakSpeechControlsGuide(void)
{
    int rc = 0;
    int i;
    MainCallback mainMenuRunCallback;

    rc |= AdvanceToTitleRunState();

    gHostIntroStubLoadGameSaveResult = SAVE_STATUS_OK;
    gSaveBlock2.playerGender = MALE;
    memcpy(gSaveBlock2.playerName, "ASH", 4);
    gSaveBlock2.playTimeHours = 12;
    gSaveBlock2.playTimeMinutes = 34;
    gSaveBlock2.optionsWindowFrameType = 0;
    gSaveBlock2.optionsTextSpeed = 2; /* OPTIONS_TEXT_SPEED_FAST */
    gHostTitleStubKantoPokedexCount = 12;
    gHostTitleStubFlagGetBadgeMask = 0x03;

    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();

    for (i = 0; i < 256 && gMain.callback2 != CB2_InitMainMenu; i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 == CB2_InitMainMenu, "title cry path did not hand off to main menu init");
    /* gHostTitleStubPlayCryNormalCalls removed — PlayCry_Normal now from upstream */
    /* gHostTitleStubFadeOutBGMCalls removed — FadeOutBGM now from upstream */
    /* gHostTitleStubLastFadeOutBGMSpeed removed — FadeOutBGM now from upstream */
    /* gHostTitleStubSetSaveBlocksPointersCalls removed — function now from upstream */
    /* gHostIntroStubResetMenuAndMonGlobalsCalls removed — function now from upstream */
    /* gHostIntroStubSaveResetSaveCountersCalls removed — function now from upstream */
    /* gHostIntroStubLoadGameSaveCalls removed — function now from upstream */
    /* gHostIntroStubSav2ClearSetDefaultCalls removed — function now from upstream */
    /* gHostIntroStubSetPokemonCryStereoCalls removed — function now from upstream */

    /* Run until main menu has finished init (vblank installed and fade done).
     * Real main menu initialization can take many frames on native due to
     * text rendering, window setup, and palette fades. */
    for (i = 0; i < 512 && (gMain.callback2 == CB2_InitMainMenu || gMain.vblankCallback == NULL || gPaletteFade.active); i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 != CB2_InitMainMenu, "main menu init did not switch to its run callback");

    /* With SAVE_STATUS_EMPTY (flash reset to 0xFF), the real main menu
     * auto-selects New Game and calls StartNewGameScene immediately
     * (no menu navigation needed). Wait for StartNewGameScene to fire. */
    for (i = 0; i < 512 && gHostTitleStubStartNewGameSceneCalls == 0; i++)
        RunMainCallbackFrame();

    rc |= Expect(gHostTitleStubStartNewGameSceneCalls >= 1,
                 "main menu did not auto-select New Game via StartNewGameScene");

    /* Run frames for the oak speech scene to initialize (controls guide setup). */
    for (i = 0; i < 512 && gPaletteFade.active; i++)
        RunMainCallbackFrame();
    RunFrames(128);
    /* gHostOakSpeechCreateMonSpritesGfxManagerCalls removed — function now from upstream */
    /* Now handled by real implementations: InitStandardTextBoxWindows, CreateTopBarWindowLoadPalette */
    /* gHostOakSpeechPlayBGMCalls removed — PlayBGM now from upstream */
    rc |= Expect(CountWindowsWithTileData() >= 2,
                 "Oak Speech setup did not allocate both the standard textbox and controls-guide windows");
    rc |= Expect(gWindows[1].tileData != NULL && WindowTilemapHasNonZero(1),
                 "Oak Speech controls guide did not populate its page window tilemap");
    rc |= Expect(!gPaletteFade.active,
                 "Oak Speech controls guide was still mid-fade when input-ready state was expected");

    return rc;
}

static int TestTitleScreenMainMenuHandoff(void)
{
    return AdvanceToOakSpeechControlsGuide();
}

static void WaitForFadeThenPressA(int maxFrames)
{
    int i;

    /* Wait for any active palette fade to complete */
    for (i = 0; i < maxFrames && gPaletteFade.active; i++)
        RunMainCallbackFrame();

    /* Send A press, then clear */
    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();
    RunMainCallbackFrame();
}

static int TestOakSpeechControlsGuideToPikachuIntro(void)
{
    int rc = 0;

    rc |= AdvanceToOakSpeechControlsGuide();

    /* Page load counters (gHostOakSpeechControlsGuidePage*Loads etc.) are no longer
       incremented because the AddTextPrinterParameterized stubs that called
       HostOakSpeechStubRecordPrintedText are now real implementations.
       Top bar text tracking (gHostOakSpeechLastTopBarLeftText/RightText) is also
       dead because TopBarWindowPrintTwoStrings/TopBarWindowPrintString are now real. */
    /* gHostOakSpeechLastPlayedBGM removed — PlayBGM now from upstream */

    /* Advance through controls-guide pages 1->2->3, then into Pikachu intro.
       Each page transition involves a palette fade, so we wait for fade to
       finish before pressing A. */
    WaitForFadeThenPressA(64);  /* page 1 -> page 2 */
    WaitForFadeThenPressA(64);  /* page 2 -> page 3 */
    WaitForFadeThenPressA(64);  /* page 3 -> Pikachu intro transition */

    /* Counter no longer incremented — run frames to advance past this point */
    RunFrames(256);
    RunFrames(8);

    /* gHostOakSpeechPlayBGMCalls removed — PlayBGM now from upstream */
    /* gHostOakSpeechLastPlayedBGM removed — PlayBGM now from upstream */

    return rc;
}

static void WaitForInputReady(int maxFrames)
{
    int i;

    /* Wait for palette fade + some extra frames for blend transitions.
       The Pikachu intro uses GPU blend registers (not palette fade) for
       page transitions, taking ~16 frames. We wait for the blend by
       simply running enough frames. */
    for (i = 0; i < maxFrames && gPaletteFade.active; i++)
        RunMainCallbackFrame();
    RunFrames(20);
}

static int TestOakSpeechPikachuIntroPages(void)
{
    int rc = 0;

    rc |= TestOakSpeechControlsGuideToPikachuIntro();

    /* Page load counters and top bar text tracking are no longer incremented/set
       by real implementations. Advance through Pikachu intro pages 1->2->3.
       Each page transition uses GPU blend registers (not palette fade), taking
       ~16 frames for the blend-out and blend-in animation. */
    WaitForInputReady(64);     /* wait for initial fade-in + blend settle */
    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();
    WaitForInputReady(8);      /* wait for page 1->2 blend transition */
    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();
    WaitForInputReady(8);      /* wait for page 2->3 blend transition */

    return rc;
}

static int TestOakSpeechPikachuIntroExitToOakSpeechInit(void)
{
    int rc = 0;

    rc |= TestOakSpeechPikachuIntroPages();

    /* gHostOakSpeechPlayBGMCalls removed — PlayBGM now from upstream */

    /* Press A on Pikachu intro page 3 to trigger the exit sequence.
       The page 2->3 blend transition is already handled by WaitForInputReady
       at the end of TestOakSpeechPikachuIntroPages. */
    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();
    RunMainCallbackFrame();

    /* gHostOakSpeechPlayBGMCalls/LastPlayedBGM removed — run frames to advance
       through the exit sequence (MUS_NEW_GAME_EXIT) and into Oak Speech init (MUS_ROUTE24). */
    RunFrames(128);
    RunFrames(512);
    /* Now handled by real implementations: TopBarWindowPrintTwoStrings/TopBarWindowPrintString
       (gHostOakSpeechLastTopBarLeftText/RightText no longer tracked) */
    /* gHostOakSpeechDoNamingScreenCalls guard removed — naming screen uses real implementation */

    return rc;
}

static int TestOakSpeechWelcomeMessages(void)
{
    int rc = 0;
    u32 welcomePrints;
    u32 thisWorldPrints;

    rc |= TestOakSpeechPikachuIntroExitToOakSpeechInit();

    (void)welcomePrints;
    (void)thisWorldPrints;
    /* Run frames while pressing A periodically to advance through the
     * oak speech text dialogs (welcome, this world, etc.) */
    {
        int j;
        for (j = 0; j < 1024; j++)
        {
            if ((j % 16) == 0)
                SetKeys(A_BUTTON, A_BUTTON);
            else if ((j % 16) == 1)
                ClearKeys();
            RunMainCallbackFrame();
        }
        ClearKeys();
    }
    /* gHostOakSpeechDoNamingScreenCalls guard removed — naming screen uses real implementation */

    return rc;
}

static int TestOakSpeechNidoranReleaseLine(void)
{
    int rc = 0;
    u32 cryCalls;

    rc |= TestOakSpeechWelcomeMessages();

    (void)cryCalls;
    rc |= RunUntilPlaceholderSourceEquals(gOakSpeech_Text_IsInhabitedFarAndWide, 1024,
                                          "Oak Speech did not reach the Nidoran release line");
    /* gHostTitleStubPlayCryNormalCalls removed — PlayCry_Normal now from upstream */
    /* gHostTitleStubLastPlayCrySpecies removed — PlayCry_Normal now from upstream */
    /* gHostOakSpeechDoNamingScreenCalls guard removed — naming screen uses real implementation */

    return rc;
}

static int TestOakSpeechTellMeALittleAboutYourself(void)
{
    int rc = 0;

    rc |= TestOakSpeechNidoranReleaseLine();

    rc |= RunUntilPlaceholderSourceEquals(gOakSpeech_Text_IStudyPokemon, 1024,
                                          "Oak Speech did not reach the I study Pokemon line");
    rc |= RunUntilPlaceholderSourceEquals(gOakSpeech_Text_TellMeALittleAboutYourself, 1024,
                                          "Oak Speech did not reach the tell-me-about-yourself prompt");
    /* gHostOakSpeechDoNamingScreenCalls guard removed — naming screen uses real implementation */

    return rc;
}

static int TestOakSpeechGenderSelectionFlow(void)
{
    int rc = 0;
    int i;

    rc |= TestOakSpeechTellMeALittleAboutYourself();
    fprintf(stderr, "  gender: past TellMeALittleAboutYourself\n");

    fprintf(stderr, "  gender: entering loop, cb2=%p\n", (void *)gMain.callback2);
    fprintf(stderr, "  gender: FindWindowIdMatching test = %d\n", FindWindowIdMatching(0, 18, 9, 9, 4));

    /* Advance through remaining text dialogs toward the gender question.
     * Press A to advance text dialogs, but stop pressing once the palette
     * fade settles and we're near the menu. Then wait for the menu without
     * pressing A. */
    for (i = 0; i < 1024 && FindWindowIdMatching(0, 18, 9, 9, 4) < 0; i++)
    {
        /* Only press A during the text advancement phase (first 768 frames).
         * After that, stop pressing to avoid accidentally navigating the menu. */
        if (i < 768)
        {
            if ((i % 16) == 0)
                SetKeys(A_BUTTON, A_BUTTON);
            else if ((i % 16) == 1)
                ClearKeys();
        }
        /* Debug: check all tasks every frame */
        {
            u8 tid;
            for (tid = 0; tid < NUM_TASKS; tid++)
            {
                if (gTasks[tid].isActive == TRUE && gTasks[tid].prev == HEAD_SENTINEL)
                {
                    u8 walk = tid;
                    int chain_len = 0;
                    do {
                        if (gTasks[walk].func == NULL)
                        {
                            fprintf(stderr, "  BUG: task %u in chain has NULL func (active=%d) at gender frame %d, chain pos %d\n",
                                    walk, gTasks[walk].isActive, i, chain_len);
                            gTasks[walk].func = TaskDummy;
                        }
                        chain_len++;
                        if (chain_len > NUM_TASKS) {
                            fprintf(stderr, "  BUG: infinite task chain at gender frame %d\n", i);
                            break;
                        }
                        walk = gTasks[walk].next;
                    } while (walk != TAIL_SENTINEL && walk < NUM_TASKS);
                    if (walk >= NUM_TASKS && walk != TAIL_SENTINEL)
                        fprintf(stderr, "  BUG: task chain has OOB next=%u at gender frame %d\n", walk, i);
                    break;
                }
            }
        }
        if (gMain.callback2 == NULL)
        {
            fprintf(stderr, "  BUG: callback2 is NULL at gender frame %d!\n", i);
            break;
        }
        if (i >= 85 && i <= 95)
        {
            u8 tid;
            fprintf(stderr, "  gender: frame %d pre-run, tasks=%u, cb2=%p\n", i, GetTaskCount(), (void *)gMain.callback2);
            for (tid = 0; tid < NUM_TASKS; tid++)
            {
                if (gTasks[tid].isActive)
                    fprintf(stderr, "    task[%u]: func=%p prev=%u next=%u\n", tid, (void *)gTasks[tid].func, gTasks[tid].prev, gTasks[tid].next);
            }
        }
        sLastGoodFrame = i;
        RunMainCallbackFrame();
    }
    ClearKeys();
    fprintf(stderr, "  gender: past A-pressing loop (i=%d)\n", i);

    rc |= Expect(FindWindowIdMatching(0, 18, 9, 9, 4) >= 0,
                 "Oak Speech did not create the boy/girl menu window");
    rc |= Expect(WindowTileDataHasNonZero((u8)FindWindowIdMatching(0, 18, 9, 9, 4)),
                 "Oak Speech boy/girl menu did not draw visible menu pixels");

    fprintf(stderr, "  gender: selecting girl\n");
    SetKeys(DPAD_DOWN, DPAD_DOWN);
    RunMainCallbackFrame();
    ClearKeys();
    RunMainCallbackFrame();

    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();

    fprintf(stderr, "  gender: waiting for menu to close\n");
    for (i = 0; i < 128 && FindWindowIdMatching(0, 18, 9, 9, 4) >= 0; i++)
        RunMainCallbackFrame();

    fprintf(stderr, "  gender: done\n");
    rc |= Expect(gSaveBlock2.playerGender == FEMALE,
                 "Oak Speech gender menu did not apply the girl selection");
    rc |= Expect(FindWindowIdMatching(0, 18, 9, 9, 4) < 0,
                 "Oak Speech gender menu window was not cleared after selection");

    return rc;
}

static int NavigateNamingScreen(int maxSetupFrames)
{
    int rc = 0;
    int i;
    MainCallback preCb2 = gMain.callback2;

    /* Wait for the naming screen to take over callback2 (fade out + DoNamingScreen).
     * Press A periodically to advance through any remaining text/menus. */
    for (i = 0; i < maxSetupFrames && gMain.callback2 == preCb2; i++)
    {
        if ((i % 16) == 0)
            SetKeys(A_BUTTON, A_BUTTON);
        else if ((i % 16) == 1)
            ClearKeys();
        RunMainCallbackFrame();
    }
    ClearKeys();
    rc |= Expect(gMain.callback2 != preCb2,
                 "Naming screen did not take over callback2");

    /* Let the naming screen finish its fade-in and become input-ready */
    for (i = 0; i < 128 && gPaletteFade.active; i++)
        RunMainCallbackFrame();
    RunFrames(32);

    /* Press START to move cursor to OK button */
    SetKeys(START_BUTTON, START_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();
    RunFrames(8);

    /* Press A on OK button to accept the default name */
    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();

    /* Wait for naming screen to fade out and return to oak speech */
    for (i = 0; i < 256 && gPaletteFade.active; i++)
        RunMainCallbackFrame();
    RunFrames(64);

    /* Wait for the CB2_ReturnFromNamingScreen state machine to finish and
     * settle back to the oak speech callback */
    for (i = 0; i < 512 && gPaletteFade.active; i++)
        RunMainCallbackFrame();
    RunFrames(64);

    return rc;
}

static int TestOakSpeechPlayerNaming(void)
{
    int rc = 0;
    int i;

    rc |= TestOakSpeechGenderSelectionFlow();
    fprintf(stderr, "  naming: past gender, advancing to name question\n");

    /* Run frames to advance past the "What is your name?" text and into
     * the naming screen fade-out. Press A periodically to advance text. */
    rc |= RunUntilPlaceholderSourceEquals(gOakSpeech_Text_YourNameWhatIsIt, 1024,
                                          "Oak Speech did not reach the player name question");
    fprintf(stderr, "  naming: past name question, navigating naming screen\n");

    /* Navigate through the real naming screen: press START (→ OK) then A (accept) */
    rc |= NavigateNamingScreen(1024);
    fprintf(stderr, "  naming: past naming screen\n");

    /* The player name should now be non-empty (a random default from the name choices) */
    rc |= Expect(gSaveBlock2.playerName[0] != 0xFF && gSaveBlock2.playerName[0] != 0,
                 "Oak Speech player naming did not set a player name");

    /* Wait for the yes/no confirmation window to appear (real CreateYesNoMenu).
     * Press A to dismiss text if needed. */
    for (i = 0; i < 1024 && FindWindowIdMatching(0, 2, 2, 6, 4) < 0; i++)
    {
        if ((i % 16) == 0)
            SetKeys(A_BUTTON, A_BUTTON);
        else if ((i % 16) == 1)
            ClearKeys();
        RunMainCallbackFrame();
    }
    ClearKeys();
    fprintf(stderr, "  naming: yes/no wait done (i=%d, found=%d)\n", i,
            FindWindowIdMatching(0, 2, 2, 6, 4));

    rc |= Expect(FindWindowIdMatching(0, 2, 2, 6, 4) >= 0,
                 "Oak Speech player confirmation did not create a yes/no window");

    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();
    RunMainCallbackFrame();

    return rc;
}

static int TestOakSpeechToCB2NewGameHandoff(void)
{
    int rc = 0;

    rc |= TestOakSpeechPlayerNaming();
    fprintf(stderr, "  rival: past player naming, waiting for rival intro\n");

    rc |= RunUntilPlaceholderSourceEquals(gOakSpeech_Text_WhatWasHisName, 1024,
                                          "Oak Speech did not reach the rival intro line");
    rc |= RunUntilWindowIdMatchingWithAPulses(0, 2, 2, 12, 10, 1024,
                                              "Oak Speech did not create the rival naming options window");
    fprintf(stderr, "  rival: options ready\n");

    /* Navigate to GARY: Down (→ GREEN), Down (→ GARY), A (select) */
    SetKeys(DPAD_DOWN, DPAD_DOWN);
    RunMainCallbackFrame();
    ClearKeys();
    RunMainCallbackFrame();
    SetKeys(DPAD_DOWN, DPAD_DOWN);
    RunMainCallbackFrame();
    ClearKeys();
    RunMainCallbackFrame();
    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();
    RunMainCallbackFrame();

    rc |= RunUntilWindowIdMatchingWithAPulses(0, 2, 2, 6, 4, 512,
                                              "Oak Speech rival confirmation did not create a yes/no window");
    fprintf(stderr, "  rival: confirmation ready\n");

    /* Confirm the rival name */
    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();
    RunMainCallbackFrame();

    rc |= RunUntilCallback2WithAPulses(CB2_NewGame, 1536,
                                       "Oak Speech did not hand off to CB2_NewGame after the rival flow");
    fprintf(stderr, "  rival: handed off to CB2_NewGame\n");

    /* CB2_NewGame runs DoMapLoadLoop synchronously, so by the time it
     * finishes one frame, CB1/CB2 should be set to overworld. */
    RunMainCallbackFrame();

    /* Verify core new-game state using real game state (not stub counters) */
    rc |= Expect(gDifferentSaveFile == TRUE,
                 "CB2_NewGame did not mark the session as a different save file");
    rc |= Expect(gSaveBlock2Ptr->playerName[0] != 0xFF && gSaveBlock2Ptr->playerName[0] != 0,
                 "CB2_NewGame did not preserve the player name through NewGameInitData");
    rc |= Expect(NameBufferEquals(gSaveBlock1Ptr->rivalName, "GARY"),
                 "CB2_NewGame did not restore the rival name after clearing SaveBlock1");
    rc |= Expect(GetMoney(&gSaveBlock1Ptr->money) == 3000,
                 "CB2_NewGame did not seed the starting money");
    rc |= Expect(gPlayerPartyCount == 0,
                 "CB2_NewGame did not clear the player party count");
    rc |= Expect(gSaveBlock1Ptr->location.mapGroup == MAP_GROUP(MAP_PALLET_TOWN_PLAYERS_HOUSE_2F)
                 && gSaveBlock1Ptr->location.mapNum == MAP_NUM(MAP_PALLET_TOWN_PLAYERS_HOUSE_2F),
                 "CB2_NewGame did not warp into the player's room");
    rc |= Expect(gSaveBlock1Ptr->pos.x == 6 && gSaveBlock1Ptr->pos.y == 6,
                 "CB2_NewGame did not update the player coordinates from the warp");
    rc |= Expect(VarGet(VAR_0x403C) == 0x0302,
                 "CB2_NewGame did not seed the RSE national dex var");
    rc |= Expect(FlagGet(FLAG_0x838) == TRUE,
                 "CB2_NewGame did not seed the RSE national dex flag");
    rc |= Expect(gSaveBlock1Ptr->pcItems[0].itemId == ITEM_POTION && gSaveBlock1Ptr->pcItems[0].quantity == 1,
                 "CB2_NewGame did not seed the starting PC potion");
    rc |= Expect((gSaveBlock1Ptr->trainerTower[0].bestTime ^ gSaveBlock2Ptr->encryptionKey) == TRAINER_TOWER_MAX_TIME,
                 "CB2_NewGame did not reset the trainer tower records");
    rc |= Expect(gMain.callback1 == CB1_Overworld,
                 "CB2_NewGame did not install CB1_Overworld");
    rc |= Expect(gMain.callback2 != NULL,
                 "CB2_NewGame did not set callback2 (expected CB2_Overworld)");

    return rc;
}

static int TestTitleScreenSaveClearHandoff(void)
{
    int rc = 0;
    int i;

    rc |= AdvanceToTitleRunState();

    SetKeys(0, B_BUTTON | SELECT_BUTTON | DPAD_UP);
    RunMainCallbackFrame();
    ClearKeys();
    RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 == CB2_SaveClearScreen_Init,
                 "title delete-save chord did not hand off to save-clear init");
    RunMainCallbackFrame();

    /* Run through the save-clear setup, waiting for all fades to complete.
       The setup involves: fade-to-black, GPU init, window/menu setup, fade-in. */
    RunFrames(128);

    rc |= Expect(gMain.callback2 != CB2_SaveClearScreen_Init,
                 "save-clear init did not switch to its run callback");
    /* Now handled by real implementations: LoadStdWindowGfx, DrawStdFrameWithCustomTileAndPalette,
       AddTextPrinterParameterized4, CreateYesNoMenu, DeactivateAllTextPrinters */
    rc |= Expect(CountWindowsWithTileData() >= 1,
                 "save-clear screen did not allocate any real window buffers");

    /* The real CreateYesNoMenu starts the cursor on "No" (pos 1).
       Press UP to move to "Yes" (pos 0), then A to select. */
    SetKeys(DPAD_UP, DPAD_UP);
    RunMainCallbackFrame();
    ClearKeys();
    RunMainCallbackFrame();

    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();

    /* gHostTitleStubClearSaveDataCalls removed — ClearSaveData now from upstream (save.c).
       Just run frames to let the clear proceed. */
    RunFrames(16);

    return rc;
}

static int TestTitleScreenBerryFixHandoff(void)
{
    int rc = 0;
    int i;

    rc |= AdvanceToTitleRunState();

    SetKeys(0, B_BUTTON | SELECT_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();
    RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 == CB2_InitBerryFixProgram,
                 "title berry-fix chord did not hand off to berry-fix init");
    rc |= Expect(gHostTitleStubM4aMPlayAllStopCalls == 1,
                 "berry-fix transition did not stop all music players");
    RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 != CB2_InitBerryFixProgram,
                 "berry-fix init did not switch to its run callback");
    /* gHostStubM4aSoundVSyncOffCalls removed — m4aSoundVSyncOff now from upstream */
    rc |= Expect(GetTaskCount() == 1, "berry-fix init did not create its task");

    RunMainCallbackFrame();

    rc |= Expect(((u16 *)BG_PLTT)[0] == 0x0066,
                 "berry-fix init did not load the begin scene palette");
    rc |= Expect(REG_DISPCNT == DISPCNT_BG0_ON,
                 "berry-fix init did not enable BG0 display");

    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();
    rc |= Expect(((u16 *)BG_PLTT)[0] == 0x0011,
                 "berry-fix did not advance to the ensure-connect scene");

    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();
    rc |= Expect(((u16 *)BG_PLTT)[0] == 0x0022,
                 "berry-fix did not advance to the turn-off-power scene");

    for (i = 0; i < 190 && gHostTitleStubMultiBootStartMasterCalls == 0; i++)
        RunMainCallbackFrame();

    rc |= Expect(gHostTitleStubMultiBootInitCalls == 1,
                 "berry-fix did not initialize multiboot");
    rc |= Expect(gHostTitleStubMultiBootStartMasterCalls == 1,
                 "berry-fix did not begin multiboot transmission");
    rc |= Expect(gHostTitleStubLastMultiBootLength > 0,
                 "berry-fix multiboot length was not set");

    for (i = 0; i < 4 && gHostTitleStubMultiBootCheckCompleteCalls == 0; i++)
        RunMainCallbackFrame();

    rc |= Expect(gHostTitleStubMultiBootCheckCompleteCalls >= 1,
                 "berry-fix did not check for multiboot completion");
    rc |= Expect(((u16 *)BG_PLTT)[0] == 0x0044,
                 "berry-fix did not advance to the follow-instructions scene");

    return rc;
}

static void crash_handler(int sig)
{
    const char msg[] = "\n*** SIGSEGV at frame ";
    char buf[64];
    int n;
    write(2, msg, sizeof(msg)-1);
    n = snprintf(buf, sizeof(buf), "%d ***\n", sLastGoodFrame);
    write(2, buf, n);
    _exit(139);
}

int main(void)
{
    int rc = 0;

    signal(SIGSEGV, crash_handler);
    HostMemoryInit();
    RUN_FILTERED_TEST("TestRandom", TestRandom());
    RUN_FILTERED_TEST("TestHeap", TestHeap());
    RUN_FILTERED_TEST("TestCpuSet", TestCpuSet());
    RUN_FILTERED_TEST("TestLz77", TestLz77());
    RUN_FILTERED_TEST("TestRl", TestRl());
    RUN_FILTERED_TEST("TestGpuRegs", TestGpuRegs());
    RUN_FILTERED_TEST("TestDma3Manager", TestDma3Manager());
    RUN_FILTERED_TEST("TestScanlineEffect", TestScanlineEffect());
    RUN_FILTERED_TEST("TestPalette", TestPalette());
    RUN_FILTERED_TEST("TestBg", TestBg());
    RUN_FILTERED_TEST("TestSprite", TestSprite());
    RUN_FILTERED_TEST("TestMainRuntime", TestMainRuntime());
    RUN_FILTERED_TEST("TestAgbMainBootSlice", TestAgbMainBootSlice());
    RUN_FILTERED_TEST("TestIntroBootCallbacks", TestIntroBootCallbacks());
    RUN_FILTERED_TEST("TestIntroScene1BootSlice", TestIntroScene1BootSlice());
    RUN_FILTERED_TEST("TestIntroNaturalTitleHandoff", TestIntroNaturalTitleHandoff());
    RUN_FILTERED_TEST("TestTitleScreenBootSlice", TestTitleScreenBootSlice());
    RUN_FILTERED_TEST("TestTitleScreenRunSlice", TestTitleScreenRunSlice());
    RUN_FILTERED_TEST("TestTitleScreenRestartHandoff", TestTitleScreenRestartHandoff());
    RUN_FILTERED_TEST("TestTitleScreenMainMenuHandoff", TestTitleScreenMainMenuHandoff());
    RUN_FILTERED_TEST("TestOakSpeechControlsGuideToPikachuIntro", TestOakSpeechControlsGuideToPikachuIntro());
    RUN_FILTERED_TEST("TestOakSpeechPikachuIntroPages", TestOakSpeechPikachuIntroPages());
    RUN_FILTERED_TEST("TestOakSpeechPikachuIntroExitToOakSpeechInit", TestOakSpeechPikachuIntroExitToOakSpeechInit());
    RUN_FILTERED_TEST("TestOakSpeechWelcomeMessages", TestOakSpeechWelcomeMessages());
    RUN_FILTERED_TEST("TestOakSpeechNidoranReleaseLine", TestOakSpeechNidoranReleaseLine());
    RUN_FILTERED_TEST("TestOakSpeechTellMeALittleAboutYourself", TestOakSpeechTellMeALittleAboutYourself());
    RUN_FILTERED_TEST("TestOakSpeechGenderSelectionFlow", TestOakSpeechGenderSelectionFlow());
    RUN_FILTERED_TEST("TestOakSpeechPlayerNaming", TestOakSpeechPlayerNaming());
    RUN_FILTERED_TEST("TestOakSpeechToCB2NewGameHandoff", TestOakSpeechToCB2NewGameHandoff());
    RUN_FILTERED_TEST("TestTitleScreenSaveClearHandoff", TestTitleScreenSaveClearHandoff());
    RUN_FILTERED_TEST("TestTitleScreenBerryFixHandoff", TestTitleScreenBerryFixHandoff());

    if (rc == 0)
        puts("pfr_smoke: ok");
    return rc;
}
