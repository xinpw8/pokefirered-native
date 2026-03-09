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

u32 gHostTitleStubSetHelpContextCalls = 0;
u32 gHostTitleStubHelpSystemEnableCalls = 0;
u32 gHostTitleStubHelpSystemDisableCalls = 0;
u32 gHostTitleStubFadeOutMapMusicCalls = 0;
u32 gHostTitleStubFadeOutBGMCalls = 0;
u32 gHostTitleStubIsNotWaitingForBGMStopCalls = 0;
u32 gHostTitleStubPlayCryNormalCalls = 0;
u32 gHostTitleStubM4aMPlayAllStopCalls = 0;
u32 gHostTitleStubSetSaveBlocksPointersCalls = 0;
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
u32 gHostTitleStubClearSaveDataCalls = 0;
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
u32 gHostTitleStubTryStartQuestLogPlaybackCalls = 0;
u32 gHostTitleStubCB2InitMysteryGiftCalls = 0;
u32 gHostTitleStubIsWirelessAdapterConnectedCalls = 0;
u32 gHostTitleStubIsMysteryGiftEnabledCalls = 0;
u32 gHostTitleStubFlagGetCalls = 0;
u32 gHostTitleStubGetNationalPokedexCountCalls = 0;
u32 gHostTitleStubGetKantoPokedexCountCalls = 0;
int gHostTitleStubLastMultiBootLength = 0;
u8 gHostTitleStubLastHelpContext = 0;
u8 gHostTitleStubLastFadeOutMapMusicSpeed = 0;
u8 gHostTitleStubLastFadeOutBGMSpeed = 0;
u16 gHostTitleStubLastPlayCrySpecies = 0;
s8 gHostTitleStubLastPlayCryPan = 0;
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

const u16 gGraphics_TitleScreen_GameTitleLogoPals[] = {
#include "title_screen/firered/game_title_logo.gbapal.u16.inc"
};
const u8 gGraphics_TitleScreen_GameTitleLogoTiles[] = {
#include "title_screen/firered/game_title_logo.8bpp.lz.u8.inc"
};
const u8 gGraphics_TitleScreen_GameTitleLogoMap[] = {
#include "title_screen/firered/game_title_logo.bin.lz.u8.inc"
};
const u16 gGraphics_TitleScreen_BoxArtMonPals[] = {
#include "title_screen/firered/box_art_mon.gbapal.u16.inc"
};
const u8 gGraphics_TitleScreen_BoxArtMonTiles[] = {
#include "title_screen/firered/box_art_mon.4bpp.lz.u8.inc"
};
const u8 gGraphics_TitleScreen_BoxArtMonMap[] = {
#include "title_screen/firered/box_art_mon.bin.lz.u8.inc"
};
u16 gGraphics_TitleScreen_BackgroundPals[] = {
#include "title_screen/firered/background.gbapal.u16.inc"
};
const u8 gGraphics_TitleScreen_CopyrightPressStartTiles[] = {
#include "title_screen/copyright_press_start.4bpp.lz.u8.inc"
};
const u8 gGraphics_TitleScreen_CopyrightPressStartMap[] = {
#include "title_screen/copyright_press_start.bin.lz.u8.inc"
};
const u16 gTitleScreen_Slash_Pal[] = {
#include "title_screen/firered/slash.gbapal.u16.inc"
};
const u32 gTitleScreen_BlankSprite_Tiles[1] = {0};
const u8 gBerryFixGameboy_Gfx[] = {0x10, 0x04, 0x00, 0x00, 0x00, 'G', 'B', '0', '0'};
const u8 gBerryFixGameboy_Tilemap[] = {0x10, 0x04, 0x00, 0x00, 0x00, 'T', 'M', '0', '0'};
const u8 gBerryFixGameboy_Pal[0x200] = {0x11, 0x00};
const u8 gBerryFixGameboyLogo_Gfx[] = {0x10, 0x04, 0x00, 0x00, 0x00, 'G', 'L', '0', '0'};
const u8 gBerryFixGameboyLogo_Tilemap[] = {0x10, 0x04, 0x00, 0x00, 0x00, 'L', 'M', '0', '0'};
const u8 gBerryFixGameboyLogo_Pal[0x200] = {0x22, 0x00};
const u8 gBerryFixGbaTransfer_Gfx[] = {0x10, 0x04, 0x00, 0x00, 0x00, 'T', 'R', '0', '0'};
const u8 gBerryFixGbaTransfer_Tilemap[] = {0x10, 0x04, 0x00, 0x00, 0x00, 'T', 'M', '1', '0'};
const u8 gBerryFixGbaTransfer_Pal[0x200] = {0x33, 0x00};
const u8 gBerryFixGbaTransferHighlight_Gfx[] = {0x10, 0x04, 0x00, 0x00, 0x00, 'H', 'I', '0', '0'};
const u8 gBerryFixGbaTransferHighlight_Tilemap[] = {0x10, 0x04, 0x00, 0x00, 0x00, 'H', 'M', '0', '0'};
const u8 gBerryFixGbaTransferHighlight_Pal[0x200] = {0x44, 0x00};
const u8 gBerryFixGbaTransferError_Gfx[] = {0x10, 0x04, 0x00, 0x00, 0x00, 'E', 'R', '0', '0'};
const u8 gBerryFixGbaTransferError_Tilemap[] = {0x10, 0x04, 0x00, 0x00, 0x00, 'E', 'M', '0', '0'};
const u8 gBerryFixGbaTransferError_Pal[0x200] = {0x55, 0x00};
const u8 gBerryFixWindow_Gfx[] = {0x10, 0x04, 0x00, 0x00, 0x00, 'W', 'N', '0', '0'};
const u8 gBerryFixWindow_Tilemap[] = {0x10, 0x04, 0x00, 0x00, 0x00, 'W', 'M', '0', '0'};
const u8 gBerryFixWindow_Pal[0x200] = {0x66, 0x00};
const u8 gText_ClearAllSaveData[] = "Clear all save data areas?";
const u8 gText_ClearingData[] = "Clearing data..\nPlease wait.";
const u8 gText_NewGame[] = "New Game";
const u8 gText_Continue[] = "Continue";
const u8 gText_Player[] = "Player";
const u8 gText_Time[] = "Time";
const u8 gText_Pokedex[] = "Pokedex";
const u8 gText_Badges[] = "Badges";
const u8 gText_MysteryGift[] = "Mystery Gift";
const u8 gText_SaveFileHasBeenDeleted[] = "Save file deleted";
const u8 gText_SaveFileCorrupted[] = "Save file corrupted";
const u8 gText_1MSubCircuitBoardNotInstalled[] = "1M sub-circuit board not installed";
const u8 gText_WirelessNotConnected[] = "Wireless adapter not connected";
const u8 gText_MysteryGiftCantUse[] = "Mystery Gift can't be used";
const u8 gTextJPDummy_Hiki[] = " caught";
const u8 gTextJPDummy_Ko[] = " badges";
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
    gHostTitleStubSetHelpContextCalls = 0;
    gHostTitleStubHelpSystemEnableCalls = 0;
    gHostTitleStubHelpSystemDisableCalls = 0;
    gHostTitleStubFadeOutMapMusicCalls = 0;
    gHostTitleStubFadeOutBGMCalls = 0;
    gHostTitleStubIsNotWaitingForBGMStopCalls = 0;
    gHostTitleStubPlayCryNormalCalls = 0;
    gHostTitleStubM4aMPlayAllStopCalls = 0;
    gHostTitleStubSetSaveBlocksPointersCalls = 0;
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
    gHostTitleStubClearSaveDataCalls = 0;
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
    gHostTitleStubTryStartQuestLogPlaybackCalls = 0;
    gHostTitleStubCB2InitMysteryGiftCalls = 0;
    gHostTitleStubIsWirelessAdapterConnectedCalls = 0;
    gHostTitleStubIsMysteryGiftEnabledCalls = 0;
    gHostTitleStubFlagGetCalls = 0;
    gHostTitleStubGetNationalPokedexCountCalls = 0;
    gHostTitleStubGetKantoPokedexCountCalls = 0;
    gHostTitleStubLastMultiBootLength = 0;
    gHostTitleStubLastHelpContext = 0;
    gHostTitleStubLastFadeOutMapMusicSpeed = 0;
    gHostTitleStubLastFadeOutBGMSpeed = 0;
    gHostTitleStubLastPlayCrySpecies = 0;
    gHostTitleStubLastPlayCryPan = 0;
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

void SetHelpContext(u8 contextId)
{
    gHostTitleStubSetHelpContextCalls++;
    gHostTitleStubLastHelpContext = contextId;
}

void HelpSystem_Enable(void)
{
    gHostTitleStubHelpSystemEnableCalls++;
}

void HelpSystem_Disable(void)
{
    gHostTitleStubHelpSystemDisableCalls++;
}

void FadeOutMapMusic(u8 speed)
{
    gHostTitleStubFadeOutMapMusicCalls++;
    gHostTitleStubLastFadeOutMapMusicSpeed = speed;
}

bool8 IsNotWaitingForBGMStop(void)
{
    gHostTitleStubIsNotWaitingForBGMStopCalls++;
    return TRUE;
}

void FadeOutBGM(u8 speed)
{
    gHostTitleStubFadeOutBGMCalls++;
    gHostTitleStubLastFadeOutBGMSpeed = speed;
}

void PlayCry_Normal(u16 species, s8 pan)
{
    gHostTitleStubPlayCryNormalCalls++;
    gHostTitleStubLastPlayCrySpecies = species;
    gHostTitleStubLastPlayCryPan = pan;
}

void m4aMPlayAllStop(void)
{
    gHostTitleStubM4aMPlayAllStopCalls++;
}

void SetSaveBlocksPointers(void)
{
    gHostTitleStubSetSaveBlocksPointersCalls++;
    gSaveBlock1Ptr = &gSaveBlock1;
    gSaveBlock2Ptr = &gSaveBlock2;
    gPokemonStoragePtr = &gPokemonStorage;
    SetBagPocketsPointers();
}

bool32 IsMysteryGiftEnabled(void)
{
    gHostTitleStubIsMysteryGiftEnabledCalls++;
    return gHostTitleStubMysteryGiftEnabled;
}

bool8 IsWirelessAdapterConnected(void)
{
    gHostTitleStubIsWirelessAdapterConnectedCalls++;
    return gHostTitleStubWirelessAdapterConnected;
}

bool8 FlagGet(u16 idx)
{
    u8 *ptr;

    gHostTitleStubFlagGetCalls++;
    if (idx >= FLAG_BADGE01_GET && idx < FLAG_BADGE01_GET + 8)
    {
        if ((gHostTitleStubFlagGetBadgeMask & (1u << (idx - FLAG_BADGE01_GET))) != 0)
            return TRUE;
    }
    ptr = GetFlagPointer(idx);
    if (ptr != NULL && (*ptr & (1 << (idx & 7))) != 0)
        return TRUE;
    if (idx == FLAG_SYS_POKEDEX_GET)
        return gHostTitleStubNationalPokedexEnabled || gHostTitleStubKantoPokedexCount != 0 || gHostTitleStubNationalPokedexCount != 0;
    return FALSE;
}

bool32 IsNationalPokedexEnabled(void)
{
    if (gHostTitleStubNationalPokedexEnabled)
        return TRUE;
    return gSaveBlock2Ptr != NULL
        && gSaveBlock2Ptr->pokedex.unused == 0xDA
        && VarGet(VAR_0x403C) == 0x0302
        && FlagGet(FLAG_0x838);
}

u16 GetNationalPokedexCount(u8 caseId)
{
    gHostTitleStubGetNationalPokedexCountCalls++;
    (void)caseId;
    return gHostTitleStubNationalPokedexCount;
}

u16 GetKantoPokedexCount(u8 caseId)
{
    gHostTitleStubGetKantoPokedexCountCalls++;
    (void)caseId;
    return gHostTitleStubKantoPokedexCount;
}

u8 *ConvertIntToDecimalStringN(u8 *str, s32 value, enum StringConvertMode mode, u8 n)
{
    u8 digits[16];
    u32 magnitude;
    u8 digit_count = 0;
    u8 output_len;
    u8 i;

    if (value < 0)
        magnitude = (u32)(-value);
    else
        magnitude = (u32)value;

    do
    {
        digits[digit_count++] = (u8)('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude != 0 && digit_count < (u8)sizeof(digits));

    output_len = digit_count;
    if (mode != STR_CONV_MODE_LEFT_ALIGN && output_len < n)
        output_len = n;

    for (i = 0; i < output_len; i++)
    {
        if (i < output_len - digit_count)
        {
            str[i] = (mode == STR_CONV_MODE_LEADING_ZEROS) ? '0' : ' ';
        }
        else
        {
            str[i] = digits[digit_count - 1 - (i - (output_len - digit_count))];
        }
    }

    str[output_len] = EOS;
    return str + output_len;
}

u8 *StringAppend(u8 *dest, const u8 *src)
{
    while (*dest != EOS && *dest != 0)
        dest++;
    while (*src != EOS && *src != 0)
        *dest++ = *src++;
    *dest = EOS;
    return dest;
}

void StartNewGameScene(void)
{
    gHostTitleStubStartNewGameSceneCalls++;
    UpstreamStartNewGameScene();
}

void TryStartQuestLogPlayback(u8 taskId)
{
    gHostTitleStubTryStartQuestLogPlaybackCalls++;
    (void)taskId;
}

void CB2_InitMysteryGift(void)
{
    gHostTitleStubCB2InitMysteryGiftCalls++;
}

void ClearSaveData(void)
{
    gHostTitleStubClearSaveDataCalls++;
}

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
