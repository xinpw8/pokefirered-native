#include "global.h"

#include "berry_fix_program.h"
#include "clear_save_data_screen.h"
#include "graphics.h"
#include "help_system.h"
#include "load_save.h"
#include "main_menu.h"
#include "menu.h"
#include "save.h"
#include "sound.h"

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
u8 gHostTitleStubLastHelpContext = 0;
u8 gHostTitleStubLastFadeOutMapMusicSpeed = 0;
u8 gHostTitleStubLastFadeOutBGMSpeed = 0;
u16 gHostTitleStubLastPlayCrySpecies = 0;
s8 gHostTitleStubLastPlayCryPan = 0;
const u8 *gHostTitleStubLastPrintedText = NULL;
s8 gHostTitleStubMenuProcessInputResult = MENU_NOTHING_CHOSEN;

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
const u8 gText_ClearAllSaveData[] = "Clear all save data areas?";
const u8 gText_ClearingData[] = "Clearing data..\nPlease wait.";

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
    gHostTitleStubLastHelpContext = 0;
    gHostTitleStubLastFadeOutMapMusicSpeed = 0;
    gHostTitleStubLastFadeOutBGMSpeed = 0;
    gHostTitleStubLastPlayCrySpecies = 0;
    gHostTitleStubLastPlayCryPan = 0;
    gHostTitleStubLastPrintedText = NULL;
    gHostTitleStubMenuProcessInputResult = MENU_NOTHING_CHOSEN;
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

void CB2_InitMainMenu(void)
{
    gHostTitleStubCB2InitMainMenuCalls++;
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
    (void)windowId;
    (void)fontId;
    (void)x;
    (void)y;
    (void)letterSpacing;
    (void)lineSpacing;
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
}

void ClearSaveData(void)
{
    gHostTitleStubClearSaveDataCalls++;
}

void CB2_InitBerryFixProgram(void)
{
    gHostTitleStubCB2InitBerryFixProgramCalls++;
}
