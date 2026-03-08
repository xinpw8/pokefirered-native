#include <stdio.h>
#include <string.h>

#include "global.h"
#include "bg.h"
#include "decompress.h"
#include "dma3.h"
#include "berry_fix_program.h"
#include "gba/macro.h"
#include "gpu_regs.h"
#include "clear_save_data_screen.h"
#include "help_system.h"
#include "host_agbmain.h"
#include "host_crt0.h"
#include "host_dma.h"
#include "host_intro_stubs.h"
#include "host_oak_speech_stubs.h"
#include "host_memory.h"
#include "host_runtime_stubs.h"
#include "host_title_screen_stubs.h"
#include "intro.h"
#include "link.h"
#include "load_save.h"
#include "main.h"
#include "main_menu.h"
#include "malloc.h"
#include "m4a.h"
#include "palette.h"
#include "quest_log.h"
#include "random.h"
#include "scanline_effect.h"
#include "save.h"
#include "sprite.h"
#include "strings.h"
#include "task.h"
#include "title_screen.h"

bool32 CheckHeap(void);
extern u16 gBattle_BG0_X;
extern const IntrFunc gIntrTableTemplate[];
extern u32 intr_main[0x200];
extern u32 IntrMain_Buffer[0x200];
extern u8 gPcmDmaCounter;
extern u8 gWirelessCommType;
extern u16 gKeyRepeatContinueDelay;

void EnableVCountIntrAtLine150(void);
void InitIntrHandlers(void);

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
    rc |= Expect(gHostStubLastFlashTimerNum == 2, "InitFlashTimer timer mismatch");
    rc |= Expect(gHostStubLastFlashTimerIntr == &gIntrTable[7], "InitFlashTimer intr slot mismatch");

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

    gLinkVSyncDisabled = FALSE;
    gWirelessCommType = 0;
    gSoundInfo.pcmDmaCounter = 7;
    SetGpuReg(REG_OFFSET_BG0HOFS, 0x1357);
    RequestDma3Copy(&dma_src, &dma_dest, sizeof(dma_src), DMA3_16BIT);
    gMain.intrCheck = 0;
    INTR_CHECK = 0;
    HostInterruptRaise(INTR_FLAG_VBLANK | INTR_FLAG_VCOUNT);
    rc |= Expect(HostInterruptDispatch() == TRUE, "VCount/VBlank first dispatch mismatch");
    rc |= Expect(gHostStubM4aSoundVSyncCalls == 1, "VCount did not call m4aSoundVSync");
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
    rc |= Expect(gPcmDmaCounter == 7, "VBlank pcmDmaCounter mismatch");
    rc |= Expect(gHostStubLinkVSyncCalls == 1, "VBlank did not call LinkVSync");
    rc |= Expect(gHostStubM4aSoundMainCalls == 1, "VBlank did not call m4aSoundMain");
    rc |= Expect(gHostStubTryReceiveLinkBattleDataCalls == 1, "VBlank link receive mismatch");
    rc |= Expect(gHostStubUpdateWirelessStatusIndicatorSpriteCalls == 1,
                 "VBlank wireless indicator mismatch");
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
    rc |= Expect(gHostStubRfuVSyncCalls == 1, "Wireless VBlank did not call RfuVSync");
    rc |= Expect(gHostStubLinkVSyncCalls == 1, "Wireless VBlank should not call LinkVSync");
    rc |= Expect(gHostStubM4aSoundMainCalls == 2, "Second VBlank did not call m4aSoundMain");
    rc |= Expect(gPcmDmaCounter == 9, "Second VBlank pcmDmaCounter mismatch");

    gLinkVSyncDisabled = TRUE;
    gWirelessCommType = 0;
    HostInterruptRaise(INTR_FLAG_VBLANK);
    rc |= Expect(HostInterruptDispatch() == TRUE, "Link-disabled VBlank dispatch mismatch");
    rc |= Expect(gHostStubLinkVSyncCalls == 1, "gLinkVSyncDisabled did not suppress LinkVSync");

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
    rc |= Expect(gHostStubM4aSoundInitCalls == 1, "AgbMain m4aSoundInit count mismatch");
    rc |= Expect(gHostStubCheckForFlashMemoryCalls == 1, "AgbMain CheckForFlashMemory count mismatch");
    rc |= Expect(gHostStubInitRFUCalls == 1, "AgbMain InitRFU count mismatch");
    rc |= Expect(gHostStubSetDefaultFontsPointerCalls == 1, "AgbMain SetDefaultFontsPointer count mismatch");
    rc |= Expect(gHostStubSetNotInSaveFailedScreenCalls == 1, "AgbMain SetNotInSaveFailedScreen count mismatch");
    rc |= Expect(gHostIntroStubGameCubeMultiBootInitCalls == 1, "AgbMain intro did not init multiboot");
    rc |= Expect(gHostIntroStubGameCubeMultiBootMainCalls == 1, "AgbMain intro did not run copyright main once");
    rc |= Expect(gMain.vblankCallback != NULL, "AgbMain intro did not install VBlank callback");
    rc |= Expect(gMain.serialCallback != NULL, "AgbMain intro did not install serial callback");
    rc |= Expect(gHostIntroStubLoadGameSaveCalls == 0, "AgbMain intro should not load save during first copyright frame");
    rc |= Expect(gHostStubPlayTimeCounterUpdateCalls >= 1, "AgbMain did not run PlayTimeCounter_Update");
    rc |= Expect(gHostStubMapMusicMainCalls >= 1, "AgbMain did not run MapMusicMain");
    rc |= Expect(gHostStubM4aSoundVSyncCalls >= 1, "AgbMain did not run VCount interrupt");
    rc |= Expect(gHostStubM4aSoundMainCalls >= 1, "AgbMain did not run VBlank sound");
    rc |= Expect(gHostStubLinkVSyncCalls >= 1, "AgbMain did not run LinkVSync");
    rc |= Expect(gHostStubTryReceiveLinkBattleDataCalls >= 1, "AgbMain did not run TryReceiveLinkBattleData");
    rc |= Expect(gHostStubUpdateWirelessStatusIndicatorSpriteCalls >= 1,
                 "AgbMain did not run wireless indicator update");
    rc |= Expect(gHostStubSoftResetCalls == 1, "AgbMain SoftReset count mismatch");
    rc |= Expect(gHostStubM4aSoundVSyncOffCalls == 1, "AgbMain m4aSoundVSyncOff count mismatch");
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

static void ResetBootCallbackHarness(void)
{
    HostMemoryReset();
    HostStubReset();
    HostIntroStubReset();
    HostOakSpeechStubReset();
    HostTitleScreenStubReset();
    ClearDma3Requests();
    InitGpuRegManager();
    HostCrt0Init();
    memset(&gMain, 0, sizeof(gMain));
    ResetBgs();
    InitIntrHandlers();
    InitHeap(gHeap, HEAP_SIZE);
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
    rc |= Expect(gHostIntroStubResetMenuAndMonGlobalsCalls == 1, "copyright callback did not reset menu globals");
    rc |= Expect(gHostIntroStubSaveResetSaveCountersCalls == 1, "copyright callback did not reset save counters");
    rc |= Expect(gHostIntroStubLoadGameSaveCalls == 1, "copyright callback did not load save");
    rc |= Expect(gHostIntroStubSav2ClearSetDefaultCalls == 1, "copyright callback did not clear default save");
    rc |= Expect(gHostIntroStubSetPokemonCryStereoCalls == 1, "copyright callback did not set cry stereo");
    rc |= Expect(gHostIntroStubResetSerialCalls == 1, "copyright callback did not reset serial");
    rc |= Expect(gMain.serialCallback == SerialCB, "copyright callback did not restore SerialCB");

    waitFadeCallback = gMain.callback2;
    for (i = 0; i < 8 && gMain.callback2 == waitFadeCallback; i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 != waitFadeCallback, "wait-fade callback did not advance");

    setupIntroCallback = gMain.callback2;
    for (i = 0; i < 8 && gMain.callback2 == setupIntroCallback; i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 != setupIntroCallback, "setup-intro callback did not advance");
    rc |= Expect(gHostIntroStubResetTempTileDataBuffersCalls == 1, "setup-intro did not reset temp tile buffers");
    rc |= Expect(gHostIntroStubDecompressAndCopyTileDataToVramCalls == 2, "setup-intro tile decompress count mismatch");
    rc |= Expect(gHostIntroStubFreeTempTileDataBuffersCalls == 1, "setup-intro did not release temp tile buffers");
    rc |= Expect(gMain.vblankCallback != NULL, "setup-intro did not install VBlank callback");
    rc |= Expect(GetTaskCount() == 1, "setup-intro did not create intro task");

    introCallback = gMain.callback2;
    RunMainCallbackFrame();
    rc |= Expect(gMain.callback2 == introCallback, "CB2_Intro changed unexpectedly");
    rc |= Expect(gHostIntroStubInitWindowsCalls == 1, "CB2_Intro did not init windows");
    rc |= Expect(gHostIntroStubFillWindowPixelBufferCalls == 1, "CB2_Intro window clear count mismatch");
    rc |= Expect(gHostIntroStubBlitBitmapToWindowCalls == 1, "CB2_Intro bitmap blit count mismatch");
    rc |= Expect(gHostIntroStubPutWindowTilemapCalls == 1, "CB2_Intro did not put window tilemap");
    rc |= Expect(gHostIntroStubCopyWindowToVramCalls == 1, "CB2_Intro did not copy window to VRAM");
    rc |= Expect(GetTaskCount() == 1, "CB2_Intro task count mismatch");

    return rc;
}

static int TestIntroBootCallbacks(void)
{
    int rc = 0;
    int i;

    rc |= AdvanceToFirstIntroFrame();

    for (i = 0; i < 16 && gHostIntroStubPlaySECalls == 0; i++)
        RunMainCallbackFrame();

    rc |= Expect(gHostIntroStubPlaySECalls == 1, "intro did not advance to Game Freak star phase");
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
    int i;

    rc |= AdvanceToFirstIntroFrame();

    for (i = 0; i < 16 && gHostIntroStubPlaySECalls == 0; i++)
        RunMainCallbackFrame();

    rc |= Expect(gHostIntroStubPlaySECalls == 1, "intro did not advance to Game Freak star phase");
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
    u32 base_start_blend_calls;
    u32 base_blend_active_calls;
    u32 base_blit_calls;
    u32 base_copy_window_calls;
    u32 base_decompress_calls;
    int rc = 0;
    int i;

    rc |= AdvanceToGameFreakStar();

    base_start_blend_calls = gHostIntroStubStartBlendTaskCalls;
    base_blend_active_calls = gHostIntroStubIsBlendTaskActiveCalls;
    base_blit_calls = gHostIntroStubBlitBitmapToWindowCalls;
    base_copy_window_calls = gHostIntroStubCopyWindowToVramCalls;
    base_decompress_calls = gHostIntroStubDecompressAndCopyTileDataToVramCalls;

    for (i = 0; i < 512; i++)
    {
        if (gHostIntroStubStartBlendTaskCalls >= base_start_blend_calls + 2
         && gHostIntroStubBlitBitmapToWindowCalls >= base_blit_calls + 2
         && gHostIntroStubCopyWindowToVramCalls >= base_copy_window_calls + 1
         && gHostIntroStubDecompressAndCopyTileDataToVramCalls >= base_decompress_calls + 4
         && gHostIntroStubResetBgPositionsCalls >= 1
         && gHostIntroStubM4aSongNumStartCalls >= 1
         && GetTaskCount() == 2
         && !gPaletteFade.active)
            break;

        RunMainCallbackFrame();
    }

    rc |= Expect(gHostIntroStubStartBlendTaskCalls >= base_start_blend_calls + 2,
                 "intro did not reach reveal-name and reveal-logo blend setup");
    rc |= Expect(gHostIntroStubIsBlendTaskActiveCalls > base_blend_active_calls,
                 "intro did not poll blend completion during reveal phases");
    rc |= Expect(gHostIntroStubBlitBitmapToWindowCalls >= base_blit_calls + 2,
                 "intro did not blit reveal-logo assets into the window");
    rc |= Expect(gHostIntroStubCopyWindowToVramCalls >= base_copy_window_calls + 1,
                 "intro did not copy reveal-logo window contents to VRAM");
    rc |= Expect(gHostIntroStubDecompressAndCopyTileDataToVramCalls >= base_decompress_calls + 4,
                 "intro did not stream Scene 1 tile data into VRAM");
    rc |= Expect(gHostIntroStubResetBgPositionsCalls >= 1, "intro did not reset bg positions for Scene 1");
    rc |= Expect(gHostIntroStubM4aSongNumStartCalls >= 1, "intro did not start Scene 1 music");
    rc |= Expect(GetTaskCount() == 2, "intro did not reach Scene 1 grass-animation state");
    rc |= Expect(!gPaletteFade.active, "intro remained mid-fade instead of settling into Scene 1");

    return rc;
}

static int TestIntroNaturalTitleHandoff(void)
{
    u32 base_decompress_calls;
    u32 base_playcry_calls;
    u32 base_reset_bg_calls;
    int rc = 0;
    int i;

    rc |= AdvanceToGameFreakStar();

    base_decompress_calls = gHostIntroStubDecompressAndCopyTileDataToVramCalls;
    base_playcry_calls = gHostIntroStubPlayCryByModeCalls;
    base_reset_bg_calls = gHostIntroStubResetBgPositionsCalls;

    for (i = 0; i < 2048 && gMain.callback2 != CB2_InitTitleScreen; i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 == CB2_InitTitleScreen,
                 "intro did not naturally hand off to title init after Scene 3");
    rc |= Expect(gHostIntroStubDecompressAndCopyTileDataToVramCalls >= base_decompress_calls + 16,
                 "intro did not stream Scene 1/2/3 tile data before natural title handoff");
    rc |= Expect(gHostIntroStubPlayCryByModeCalls >= base_playcry_calls + 1,
                 "intro did not reach the Scene 3 Nidorino cry before natural title handoff");
    rc |= Expect(gHostIntroStubResetBgPositionsCalls >= base_reset_bg_calls + 2,
                 "intro did not reset bg positions for later fight scenes");
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
    rc |= Expect(gHostIntroStubDecompressAndCopyTileDataToVramCalls == 8,
                 "title init tile decompress count mismatch");
    rc |= Expect(gHostIntroStubFreeTempTileDataBuffersCalls == 1,
                 "title init did not release temp tile buffers");
    rc |= Expect(gHostIntroStubM4aSongNumStartCalls == 1, "title init did not start title music");
    rc |= Expect(gMain.vblankCallback != NULL, "title init did not install VBlank callback");
    rc |= Expect(GetTaskCount() == 2, "title init task count mismatch");
    rc |= Expect((GetGpuReg(REG_OFFSET_DISPCNT) & (DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP))
                    == (DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP),
                 "title init DISPCNT mismatch");

    RunMainCallbackFrame();

    rc |= Expect(gScanlineEffect.state == 1, "title run did not initialize scanline effect");
    rc |= Expect(gHostTitleStubSetHelpContextCalls == 0, "title run advanced too far");
    rc |= Expect(GetTaskCount() == 2, "title run task count changed unexpectedly");

    return rc;
}

static int AdvanceToTitleRunState(void)
{
    int rc = 0;
    int i;

    rc |= AdvanceToTitleFirstFrame();

    for (i = 0; i < 512 && gHostTitleStubSetHelpContextCalls == 0; i++)
        RunMainCallbackFrame();

    rc |= Expect(gHostTitleStubSetHelpContextCalls == 1, "title screen did not reach run-state help context");
    rc |= Expect(gHostTitleStubLastHelpContext == HELPCONTEXT_TITLE_SCREEN,
                 "title screen help context mismatch");
    rc |= Expect(gHostTitleStubHelpSystemEnableCalls == 1, "title screen did not enable help system");
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

    for (i = 0; i < 3200 && gMain.callback2 != CB2_InitCopyrightScreenAfterTitleScreen; i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 == CB2_InitCopyrightScreenAfterTitleScreen,
                 "title run did not hand off back to copyright after timeout");
    rc |= Expect(gHostTitleStubFadeOutMapMusicCalls == 1, "title restart did not fade out map music");
    rc |= Expect(gHostTitleStubLastFadeOutMapMusicSpeed == 10, "title restart fade speed mismatch");
    rc |= Expect(gHostTitleStubHelpSystemDisableCalls == 1, "title restart did not disable help system");

    return rc;
}

static int TestTitleScreenMainMenuHandoff(void)
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
    gHostTitleStubKantoPokedexCount = 12;
    gHostTitleStubFlagGetBadgeMask = 0x03;

    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();

    for (i = 0; i < 256 && gMain.callback2 != CB2_InitMainMenu; i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 == CB2_InitMainMenu, "title cry path did not hand off to main menu init");
    rc |= Expect(gHostTitleStubPlayCryNormalCalls == 1, "title cry path did not play the title cry");
    rc |= Expect(gHostTitleStubFadeOutBGMCalls == 1, "title cry path did not fade out BGM");
    rc |= Expect(gHostTitleStubLastFadeOutBGMSpeed == 4, "title cry path fade speed mismatch");
    rc |= Expect(gHostTitleStubSetSaveBlocksPointersCalls == 1,
                 "title cry path did not set save block pointers");
    rc |= Expect(gHostIntroStubResetMenuAndMonGlobalsCalls == 1,
                 "title cry path did not reset menu globals");
    rc |= Expect(gHostIntroStubSaveResetSaveCountersCalls == 1,
                 "title cry path did not reset save counters");
    rc |= Expect(gHostIntroStubLoadGameSaveCalls == 1, "title cry path did not load save data");
    rc |= Expect(gHostIntroStubSav2ClearSetDefaultCalls == 0,
                 "title cry path should not clear save defaults when a save is present");
    rc |= Expect(gHostIntroStubSetPokemonCryStereoCalls == 1,
                 "title cry path did not restore cry stereo setting");

    for (i = 0; i < 128 && gHostTitleStubAddTextPrinterParameterized3Calls == 0; i++)
        RunMainCallbackFrame();

    for (i = 0; i < 64 && (gMain.vblankCallback == NULL || gPaletteFade.active); i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 != CB2_InitMainMenu, "main menu init did not switch to its run callback");
    rc |= Expect(gHostTitleStubAddTextPrinterParameterized3Calls >= 6,
                 "main menu did not print its continue/new-game stats and options");
    rc |= Expect(gHostTitleStubGetKantoPokedexCountCalls == 1,
                 "main menu did not execute the continue-stats Pokedex path");
    rc |= Expect(gHostTitleStubFlagGetCalls >= 3,
                 "main menu did not execute the expected badge or Pokedex flag checks");
    rc |= Expect(gHostIntroStubFillWindowPixelBufferCalls >= 2,
                 "main menu did not fill the expected continue/new-game windows");
    rc |= Expect(gHostIntroStubPutWindowTilemapCalls >= 2,
                 "main menu did not put the expected window tilemaps");
    rc |= Expect(gHostIntroStubCopyWindowToVramCalls >= 2,
                 "main menu did not copy the expected window content to VRAM");
    rc |= Expect(gMain.vblankCallback != NULL,
                 "main menu did not install its VBlank callback");
    rc |= Expect((GetGpuReg(REG_OFFSET_DISPCNT) & (DISPCNT_OBJ_ON | DISPCNT_WIN0_ON))
                    == (DISPCNT_OBJ_ON | DISPCNT_WIN0_ON),
                 "main menu DISPCNT mismatch");

    mainMenuRunCallback = gMain.callback2;

    SetKeys(DPAD_DOWN, DPAD_DOWN);
    RunMainCallbackFrame();
    ClearKeys();
    RunMainCallbackFrame();

    SetKeys(A_BUTTON, A_BUTTON);
    RunMainCallbackFrame();
    ClearKeys();

    for (i = 0; i < 64 && gHostTitleStubStartNewGameSceneCalls == 0; i++)
        RunMainCallbackFrame();

    rc |= Expect(gHostTitleStubStartNewGameSceneCalls == 1,
                 "main menu New Game selection did not hand off to StartNewGameScene");
    rc |= Expect(gHostTitleStubFreeAllWindowBuffersCalls >= 1,
                 "main menu New Game selection did not free window buffers");

    for (i = 0; i < 32 && gHostOakSpeechCreateTopBarWindowLoadPaletteCalls == 0; i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 != mainMenuRunCallback,
                 "New Game selection did not switch away from the main menu callback");
    rc |= Expect(gHostOakSpeechCreateMonSpritesGfxManagerCalls == 1,
                 "Oak Speech setup did not create the mon sprite graphics manager");
    rc |= Expect(gHostOakSpeechInitStandardTextBoxWindowsCalls == 1,
                 "Oak Speech setup did not initialize standard text box windows");
    rc |= Expect(gHostOakSpeechCreateTopBarWindowLoadPaletteCalls == 1,
                 "Oak Speech setup did not reach the controls-guide top bar window stage");

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

    for (i = 0; i < 128 && gHostTitleStubCreateYesNoMenuCalls == 0; i++)
        RunMainCallbackFrame();

    rc |= Expect(gMain.callback2 != CB2_SaveClearScreen_Init,
                 "save-clear init did not switch to its run callback");
    rc |= Expect(gHostTitleStubLoadStdWindowGfxCalls == 2,
                 "save-clear screen did not load standard window graphics twice");
    rc |= Expect(gHostTitleStubDrawStdFrameCalls == 1,
                 "save-clear screen did not draw its dialogue frame");
    rc |= Expect(gHostTitleStubAddTextPrinterParameterized4Calls == 1,
                 "save-clear screen did not print its confirmation prompt");
    rc |= Expect(gHostTitleStubLastPrintedText == gText_ClearAllSaveData,
                 "save-clear screen printed the wrong confirmation prompt");
    rc |= Expect(gHostTitleStubCreateYesNoMenuCalls == 1,
                 "save-clear screen did not create its yes-no menu");
    rc |= Expect(gHostTitleStubDeactivateAllTextPrintersCalls == 1,
                 "save-clear screen did not deactivate text printers during GPU init");

    HostTitleScreenStubSetMenuProcessInputResult(0);

    for (i = 0; i < 4 && gHostTitleStubMenuProcessInputCalls == 0; i++)
        RunMainCallbackFrame();

    rc |= Expect(gHostTitleStubMenuProcessInputCalls == 1,
                 "save-clear screen did not read yes-no input");
    rc |= Expect(gHostTitleStubClearSaveDataCalls == 1,
                 "save-clear screen did not clear save data on yes selection");
    rc |= Expect(gHostTitleStubAddTextPrinterParameterized4Calls == 2,
                 "save-clear screen did not print its clearing message");
    rc |= Expect(gHostTitleStubLastPrintedText == gText_ClearingData,
                 "save-clear screen printed the wrong clearing message");
    rc |= Expect(gHostIntroStubFillWindowPixelBufferCalls >= 1,
                 "save-clear screen did not clear the message window before printing");

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
    rc |= Expect(gHostStubM4aSoundVSyncOffCalls == 1,
                 "berry-fix init did not disable m4a VSync");
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

int main(void)
{
    int rc = 0;

    HostMemoryInit();
    rc |= TestRandom();
    rc |= TestHeap();
    rc |= TestCpuSet();
    rc |= TestLz77();
    rc |= TestRl();
    rc |= TestGpuRegs();
    rc |= TestDma3Manager();
    rc |= TestScanlineEffect();
    rc |= TestPalette();
    rc |= TestBg();
    rc |= TestSprite();
    rc |= TestMainRuntime();
    rc |= TestAgbMainBootSlice();
    rc |= TestIntroBootCallbacks();
    rc |= TestIntroScene1BootSlice();
    rc |= TestIntroNaturalTitleHandoff();
    rc |= TestTitleScreenBootSlice();
    rc |= TestTitleScreenRunSlice();
    rc |= TestTitleScreenRestartHandoff();
    rc |= TestTitleScreenMainMenuHandoff();
    rc |= TestTitleScreenSaveClearHandoff();
    rc |= TestTitleScreenBerryFixHandoff();

    if (rc == 0)
        puts("pfr_smoke: ok");
    return rc;
}
