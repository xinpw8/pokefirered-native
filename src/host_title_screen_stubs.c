#include <stdio.h>

#include "global.h"

#include "berry_fix_program.h"
#include "clear_save_data_screen.h"
#include "event_data.h"
#include "graphics.h"
#include "help_system.h"
#include "load_save.h"
#include "main_menu.h"
#include "menu.h"
#include "multiboot.h"
#include "oak_speech.h"
#include "pokedex.h"
#include "quest_log.h"
#include "save.h"
#include "sound.h"
#include "text_window_graphics.h"

#include "host_title_screen_stubs.h"

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
static const struct TextWindowGraphics sHostTitleStubUserWindowGraphics = {
    .tiles = NULL,
    .palette = NULL,
};

const u16 gGraphics_TitleScreen_GameTitleLogoPals[13 * 16] = {0};
const u8 gGraphics_TitleScreen_GameTitleLogoTiles[1] = {0};
const u8 gGraphics_TitleScreen_GameTitleLogoMap[1] = {0};
const u16 gGraphics_TitleScreen_BoxArtMonPals[16] = {0};
const u8 gGraphics_TitleScreen_BoxArtMonTiles[1] = {0};
const u8 gGraphics_TitleScreen_BoxArtMonMap[1] = {0};
u16 gGraphics_TitleScreen_BackgroundPals[16] = {0};
const u8 gGraphics_TitleScreen_CopyrightPressStartTiles[1] = {0};
const u8 gGraphics_TitleScreen_CopyrightPressStartMap[1] = {0};
const u16 gTitleScreen_Slash_Pal[16] = {0};
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
const u16 gMenuMessageWindow_Gfx[1] = {0};
const u8 gMultiBootProgram_BerryGlitchFix_Start[0xD0] = {0};
const u8 gMultiBootProgram_BerryGlitchFix_End[] = {0};

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
}

void HostTitleScreenStubSetMenuProcessInputResult(s8 result)
{
    gHostTitleStubMenuProcessInputResult = result;
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
}

void LoadStdWindowGfx(u8 windowId, u16 destOffset, u8 palOffset)
{
    gHostTitleStubLoadStdWindowGfxCalls++;
    (void)windowId;
    (void)destOffset;
    (void)palOffset;
}

void DrawStdFrameWithCustomTileAndPalette(u8 windowId, bool8 copyToVram, u16 baseTileNum, u8 paletteNum)
{
    gHostTitleStubDrawStdFrameCalls++;
    (void)windowId;
    (void)copyToVram;
    (void)baseTileNum;
    (void)paletteNum;
}

void AddTextPrinterParameterized4(u8 windowId, u8 fontId, u8 x, u8 y, u8 letterSpacing, u8 lineSpacing, const u8 *color, s8 speed, const u8 *str)
{
    gHostTitleStubAddTextPrinterParameterized4Calls++;
    gHostTitleStubLastPrintedText = str;
    gHostTitleStubTextPrinterActive = FALSE;
    (void)windowId;
    (void)fontId;
    (void)x;
    (void)y;
    (void)letterSpacing;
    (void)lineSpacing;
    (void)color;
    (void)speed;
}

void AddTextPrinterParameterized3(u8 windowId, u8 fontId, u8 x, u8 y, const u8 *color, s8 speed, const u8 *str)
{
    gHostTitleStubAddTextPrinterParameterized3Calls++;
    gHostTitleStubLastPrintedText3 = str;
    gHostTitleStubTextPrinterActive = FALSE;
    (void)windowId;
    (void)fontId;
    (void)x;
    (void)y;
    (void)color;
    (void)speed;
}

void CreateYesNoMenu(const struct WindowTemplate *window, u8 fontId, u8 left, u8 top, u16 baseTileNum, u8 paletteNum, u8 initialCursorPos)
{
    gHostTitleStubCreateYesNoMenuCalls++;
    (void)window;
    (void)fontId;
    (void)left;
    (void)top;
    (void)baseTileNum;
    (void)paletteNum;
    (void)initialCursorPos;
}

s8 Menu_ProcessInputNoWrapClearOnChoose(void)
{
    s8 result = gHostTitleStubMenuProcessInputResult;

    gHostTitleStubMenuProcessInputCalls++;
    gHostTitleStubMenuProcessInputResult = MENU_NOTHING_CHOSEN;
    return result;
}

void DestroyYesNoMenu(void)
{
    gHostTitleStubDestroyYesNoMenuCalls++;
}

void FreeAllWindowBuffers(void)
{
    gHostTitleStubFreeAllWindowBuffersCalls++;
}

void DeactivateAllTextPrinters(void)
{
    gHostTitleStubDeactivateAllTextPrintersCalls++;
    gHostTitleStubTextPrinterActive = FALSE;
}

void RunTextPrinters(void)
{
    gHostTitleStubRunTextPrintersCalls++;
    gHostTitleStubTextPrinterActive = FALSE;
}

bool16 IsTextPrinterActive(u8 windowId)
{
    gHostTitleStubIsTextPrinterActiveCalls++;
    (void)windowId;
    return gHostTitleStubTextPrinterActive;
}

void ClearWindowTilemap(u8 windowId)
{
    gHostTitleStubClearWindowTilemapCalls++;
    (void)windowId;
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
    gHostTitleStubFlagGetCalls++;
    if (idx >= FLAG_BADGE01_GET && idx < FLAG_BADGE01_GET + 8)
        return (gHostTitleStubFlagGetBadgeMask & (1u << (idx - FLAG_BADGE01_GET))) != 0;
    if (idx == FLAG_SYS_POKEDEX_GET)
        return gHostTitleStubNationalPokedexEnabled || gHostTitleStubKantoPokedexCount != 0 || gHostTitleStubNationalPokedexCount != 0;
    return FALSE;
}

bool32 IsNationalPokedexEnabled(void)
{
    return gHostTitleStubNationalPokedexEnabled;
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

const struct TextWindowGraphics *GetUserWindowGraphics(u8 idx)
{
    (void)idx;
    return &sHostTitleStubUserWindowGraphics;
}

u8 *ConvertIntToDecimalStringN(u8 *str, u32 value, u8 mode, u8 n)
{
    char format[8];
    char buffer[32];
    size_t length;

    (void)mode;
    snprintf(format, sizeof(format), "%%0%uu", (unsigned)n);
    snprintf(buffer, sizeof(buffer), format, (unsigned)value);
    length = strlen(buffer);
    memcpy(str, buffer, length + 1);
    return str + length;
}

u8 *StringAppend(u8 *dest, const u8 *src)
{
    while (*dest != EOS)
        dest++;
    while (*src != EOS)
        *dest++ = *src++;
    *dest = EOS;
    return dest;
}

void StartNewGameScene(void)
{
    gHostTitleStubStartNewGameSceneCalls++;
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
