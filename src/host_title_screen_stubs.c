#include <stdio.h>

#include "global.h"

#include "berry_fix_program.h"
#include "clear_save_data_screen.h"
#include "constants/flags.h"
#include "constants/vars.h"
#include "event_data.h"
#include "graphics.h"
#include "help_system.h"
#include "item.h"
#include "load_save.h"
#include "main.h"
#include "main_menu.h"
#include "menu.h"
#include "multiboot.h"
#include "oak_speech.h"
#include "pokedex.h"
#include "quest_log.h"
#include "save.h"
#include "sound.h"
#include "string_util.h"
#include "window.h"

#include "host_oak_speech_stubs.h"
#include "host_title_screen_stubs.h"

void UpstreamStartNewGameScene(void);

u32 gHostTitleStubM4aMPlayAllStopCalls = 0;
/* SetSaveBlocksPointers now from load_save.c */
u32 gHostTitleStubCB2InitMainMenuCalls = 0;
u32 gHostTitleStubCB2InitBerryFixProgramCalls = 0;
u32 gHostTitleStubLoadStdWindowGfxCalls = 0;
u32 gHostTitleStubDrawStdFrameCalls = 0;
u32 gHostTitleStubAddTextPrinterParameterized4Calls = 0;
u32 gHostTitleStubCreateYesNoMenuCalls = 0;
u32 gHostTitleStubMenuProcessInputCalls = 0;
u32 gHostTitleStubDestroyYesNoMenuCalls = 0;
u32 gHostTitleStubFreeAllWindowBuffersCalls = 0;
u32 gHostTitleStubDeactivateAllTextPrintersCalls = 0;
/* ClearSaveData now from save.c */
u32 gHostTitleStubMultiBootInitCalls = 0;
u32 gHostTitleStubMultiBootMainCalls = 0;
u32 gHostTitleStubMultiBootStartMasterCalls = 0;
u32 gHostTitleStubMultiBootCheckCompleteCalls = 0;
u32 gHostTitleStubAddTextPrinterParameterized3Calls = 0;
u32 gHostTitleStubLoadBgTilesCalls = 0;
u32 gHostTitleStubFillBgTilemapBufferRectCalls = 0;
u32 gHostTitleStubClearWindowTilemapCalls = 0;
u32 gHostTitleStubRunTextPrintersCalls = 0;
u32 gHostTitleStubIsTextPrinterActiveCalls = 0;
u32 gHostTitleStubFreeAllSpritePalettesCalls = 0;
u32 gHostTitleStubStartNewGameSceneCalls = 0;
u32 gHostTitleStubCB2InitMysteryGiftCalls = 0;
u32 gHostTitleStubGetNationalPokedexCountCalls = 0;
u32 gHostTitleStubGetKantoPokedexCountCalls = 0;
int gHostTitleStubLastMultiBootLength = 0;
const u8 *gHostTitleStubLastPrintedText = NULL;
const u8 *gHostTitleStubLastPrintedText3 = NULL;
s8 gHostTitleStubMenuProcessInputResult = MENU_NOTHING_CHOSEN;
bool8 gHostTitleStubWirelessAdapterConnected = FALSE;
bool32 gHostTitleStubMysteryGiftEnabled = FALSE;
bool8 gHostTitleStubNationalPokedexEnabled = FALSE;
u16 gHostTitleStubNationalPokedexCount = 0;
u16 gHostTitleStubKantoPokedexCount = 0;
u8 gHostTitleStubFlagGetBadgeMask = 0;
bool8 gHostTitleStubTextPrinterActive = FALSE;
static bool8 sHostTitleStubMultiBootComplete = FALSE;
struct HostTitleStubMenuState
{
    bool8 active;
    u8 windowId;
    u8 left;
    u8 top;
    u8 cursorHeight;
    u8 numChoices;
    u8 cursorPos;
};

#define HOST_TITLE_NO_MENU_WINDOW 0xFF

static struct HostTitleStubMenuState sHostTitleStubMenu = {
    .active = FALSE,
    .windowId = HOST_TITLE_NO_MENU_WINDOW,
};
static u8 sHostTitleStubYesNoWindowId = HOST_TITLE_NO_MENU_WINDOW;

static void HostTitleScreenStubResetMenuState(void);
static void HostTitleScreenStubRedrawMenuCursor(void);
static void HostTitleScreenStubMoveMenuCursor(s8 delta, bool8 wrapAround);

/* Title screen graphics and berry fix data now from graphics.c */
__asm__(
    ".pushsection .rodata.host_multiboot_payload,\"a\",@progbits\n"
    ".global gMultiBootProgram_BerryGlitchFix_Start\n"
    "gMultiBootProgram_BerryGlitchFix_Start:\n"
    ".zero 0xD0\n"
    ".global gMultiBootProgram_BerryGlitchFix_End\n"
    "gMultiBootProgram_BerryGlitchFix_End:\n"
    ".byte 0\n"
    ".popsection\n");

void HostTitleScreenStubReset(void)
{
    gHostTitleStubM4aMPlayAllStopCalls = 0;
    gHostTitleStubCB2InitMainMenuCalls = 0;
    gHostTitleStubCB2InitBerryFixProgramCalls = 0;
    gHostTitleStubLoadStdWindowGfxCalls = 0;
    gHostTitleStubDrawStdFrameCalls = 0;
    gHostTitleStubAddTextPrinterParameterized4Calls = 0;
    gHostTitleStubCreateYesNoMenuCalls = 0;
    gHostTitleStubMenuProcessInputCalls = 0;
    gHostTitleStubDestroyYesNoMenuCalls = 0;
    gHostTitleStubFreeAllWindowBuffersCalls = 0;
    gHostTitleStubDeactivateAllTextPrintersCalls = 0;
    gHostTitleStubMultiBootInitCalls = 0;
    gHostTitleStubMultiBootMainCalls = 0;
    gHostTitleStubMultiBootStartMasterCalls = 0;
    gHostTitleStubMultiBootCheckCompleteCalls = 0;
    gHostTitleStubAddTextPrinterParameterized3Calls = 0;
    gHostTitleStubLoadBgTilesCalls = 0;
    gHostTitleStubFillBgTilemapBufferRectCalls = 0;
    gHostTitleStubClearWindowTilemapCalls = 0;
    gHostTitleStubRunTextPrintersCalls = 0;
    gHostTitleStubIsTextPrinterActiveCalls = 0;
    gHostTitleStubFreeAllSpritePalettesCalls = 0;
    gHostTitleStubStartNewGameSceneCalls = 0;
    gHostTitleStubCB2InitMysteryGiftCalls = 0;
    gHostTitleStubGetNationalPokedexCountCalls = 0;
    gHostTitleStubGetKantoPokedexCountCalls = 0;
    gHostTitleStubLastMultiBootLength = 0;
    gHostTitleStubLastPrintedText = NULL;
    gHostTitleStubLastPrintedText3 = NULL;
    gHostTitleStubMenuProcessInputResult = MENU_NOTHING_CHOSEN;
    gHostTitleStubWirelessAdapterConnected = FALSE;
    gHostTitleStubMysteryGiftEnabled = FALSE;
    gHostTitleStubNationalPokedexEnabled = FALSE;
    gHostTitleStubNationalPokedexCount = 0;
    gHostTitleStubKantoPokedexCount = 0;
    gHostTitleStubFlagGetBadgeMask = 0;
    gHostTitleStubTextPrinterActive = FALSE;
    sHostTitleStubMultiBootComplete = FALSE;
    HostTitleScreenStubResetMenuState();
}

void HostTitleScreenStubSetMenuProcessInputResult(s8 result)
{
    gHostTitleStubMenuProcessInputResult = result;
}

static void HostTitleScreenStubResetMenuState(void)
{
    sHostTitleStubMenu.active = FALSE;
    sHostTitleStubMenu.windowId = HOST_TITLE_NO_MENU_WINDOW;
    sHostTitleStubMenu.left = 0;
    sHostTitleStubMenu.top = 0;
    sHostTitleStubMenu.cursorHeight = 0;
    sHostTitleStubMenu.numChoices = 0;
    sHostTitleStubMenu.cursorPos = 0;
    sHostTitleStubYesNoWindowId = HOST_TITLE_NO_MENU_WINDOW;
}

static void HostTitleScreenStubRedrawMenuCursor(void)
{
    u8 i;
    u8 windowId = sHostTitleStubMenu.windowId;
    u16 windowWidthPixels;
    u16 windowHeightPixels;

    if (!sHostTitleStubMenu.active || windowId >= WINDOWS_MAX || gWindows[windowId].tileData == NULL)
        return;

    windowWidthPixels = gWindows[windowId].window.width * 8;
    windowHeightPixels = gWindows[windowId].window.height * 8;

    FillWindowPixelBuffer(windowId, PIXEL_FILL(1));
    for (i = 0; i < sHostTitleStubMenu.numChoices; i++)
    {
        u16 y = sHostTitleStubMenu.top + (u16)i * sHostTitleStubMenu.cursorHeight;
        u16 height = sHostTitleStubMenu.cursorHeight;
        u8 fillValue = (i == sHostTitleStubMenu.cursorPos) ? PIXEL_FILL(3) : PIXEL_FILL(1);

        if (y >= windowHeightPixels)
            break;
        if (y + height > windowHeightPixels)
            height = windowHeightPixels - y;
        FillWindowPixelRect(windowId, fillValue, 0, y, windowWidthPixels, height);
    }

    PutWindowTilemap(windowId);
    CopyWindowToVram(windowId, COPYWIN_FULL);
}

u8 HostTitleScreenStubInitMenuCursor(u8 windowId, u8 left, u8 top, u8 cursorHeight, u8 numChoices, u8 initialCursorPos)
{
    if (cursorHeight < 8)
        cursorHeight = 8;

    sHostTitleStubMenu.active = TRUE;
    sHostTitleStubMenu.windowId = windowId;
    sHostTitleStubMenu.left = left;
    sHostTitleStubMenu.top = top;
    sHostTitleStubMenu.cursorHeight = cursorHeight;
    sHostTitleStubMenu.numChoices = numChoices;
    sHostTitleStubMenu.cursorPos = (numChoices != 0 && initialCursorPos < numChoices) ? initialCursorPos : 0;
    HostTitleScreenStubRedrawMenuCursor();
    return sHostTitleStubMenu.cursorPos;
}

static void HostTitleScreenStubMoveMenuCursor(s8 delta, bool8 wrapAround)
{
    s16 newPos;

    if (!sHostTitleStubMenu.active || sHostTitleStubMenu.numChoices == 0)
        return;

    newPos = (s16)sHostTitleStubMenu.cursorPos + delta;
    if (wrapAround)
    {
        if (newPos < 0)
            newPos = sHostTitleStubMenu.numChoices - 1;
        else if (newPos >= sHostTitleStubMenu.numChoices)
            newPos = 0;
    }
    else
    {
        if (newPos < 0)
            newPos = 0;
        else if (newPos >= sHostTitleStubMenu.numChoices)
            newPos = sHostTitleStubMenu.numChoices - 1;
    }

    if (newPos != sHostTitleStubMenu.cursorPos)
    {
        sHostTitleStubMenu.cursorPos = (u8)newPos;
        HostTitleScreenStubRedrawMenuCursor();
    }
}

s8 HostTitleScreenStubProcessMenuInput(bool8 wrapAround)
{
    s8 result = gHostTitleStubMenuProcessInputResult;

    if (result != MENU_NOTHING_CHOSEN)
    {
        gHostTitleStubMenuProcessInputResult = MENU_NOTHING_CHOSEN;
        return result;
    }
    if (!sHostTitleStubMenu.active)
        return MENU_NOTHING_CHOSEN;
    if (gMain.newKeys & A_BUTTON)
        return sHostTitleStubMenu.cursorPos;
    if (gMain.newKeys & B_BUTTON)
        return MENU_B_PRESSED;
    if (gMain.newKeys & DPAD_UP)
    {
        HostTitleScreenStubMoveMenuCursor(-1, wrapAround);
        return MENU_NOTHING_CHOSEN;
    }
    if (gMain.newKeys & DPAD_DOWN)
    {
        HostTitleScreenStubMoveMenuCursor(1, wrapAround);
        return MENU_NOTHING_CHOSEN;
    }

    return MENU_NOTHING_CHOSEN;
}

void HostTitleScreenStubForgetMenuWindow(u8 windowId)
{
    if (sHostTitleStubMenu.windowId == windowId)
    {
        sHostTitleStubMenu.active = FALSE;
        sHostTitleStubMenu.windowId = HOST_TITLE_NO_MENU_WINDOW;
    }
    if (sHostTitleStubYesNoWindowId == windowId)
        sHostTitleStubYesNoWindowId = HOST_TITLE_NO_MENU_WINDOW;
}

/* SetHelpContext, HelpSystem_Enable, HelpSystem_Disable now from help_system.c */

/* m4aMPlayAllStop now from m4a.c */
/* SetSaveBlocksPointers now from load_save.c */
/* IsMysteryGiftEnabled now from event_data.c */

/* IsWirelessAdapterConnected now from link.c */
/* FlagGet and IsNationalPokedexEnabled now from event_data.c */

/* GetNationalPokedexCount, GetKantoPokedexCount now from pokedex.c */

void StartNewGameScene(void)
{
    gHostTitleStubStartNewGameSceneCalls++;
    UpstreamStartNewGameScene();
}

/* CB2_InitMysteryGift now from mystery_gift_menu.c */
/* ClearSaveData now from save.c */

void MultiBootInit(struct MultiBootParam *mp)
{
    gHostTitleStubMultiBootInitCalls++;
    sHostTitleStubMultiBootComplete = FALSE;
    mp->probe_count = 0;
    mp->response_bit = 0x2;
    mp->client_bit = 0x2;
    mp->check_wait = 0;
    mp->sendflag = 0;
}

int MultiBootMain(struct MultiBootParam *mp)
{
    gHostTitleStubMultiBootMainCalls++;
    (void)mp;
    return 0;
}

void MultiBootStartMaster(struct MultiBootParam *mp, const u8 *srcp, int length, u8 palette_color, s8 palette_speed)
{
    gHostTitleStubMultiBootStartMasterCalls++;
    gHostTitleStubLastMultiBootLength = length;
    sHostTitleStubMultiBootComplete = TRUE;
    mp->boot_srcp = srcp;
    mp->boot_endp = srcp + length;
    mp->palette_data = palette_color;
    mp->probe_count = 0xe9;
    (void)palette_speed;
}

bool32 MultiBootCheckComplete(struct MultiBootParam *mp)
{
    gHostTitleStubMultiBootCheckCompleteCalls++;
    (void)mp;
    return sHostTitleStubMultiBootComplete;
}
