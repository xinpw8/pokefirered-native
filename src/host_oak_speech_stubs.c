#include <string.h>

#include "global.h"

#include "decompress.h"
#include "main.h"
#include "malloc.h"
#include "menu.h"
#include "naming_screen.h"
#include "new_game.h"
#include "new_menu_helpers.h"
#include "palette.h"
#include "play_time.h"
#include "pokeball.h"
#include "pokemon.h"
#include "data.h"
#include "math_util.h"
#include "overworld.h"
#include "sound.h"
#include "sprite.h"
#include "string_util.h"
#include "task.h"
#include "text.h"
#include "text_window.h"
#include "trainer_pokemon_sprites.h"
#include "window.h"

#include "host_oak_speech_stubs.h"
#include "host_new_game_stubs.h"
#include "host_title_screen_stubs.h"

TextFlags gTextFlags = {0};
u8 gStringVar1[256] = {0};
u8 gStringVar2[256] = {0};
u8 gStringVar3[256] = {0};
u8 gStringVar4[1024] = {0};

u32 gHostOakSpeechCreateMonSpritesGfxManagerCalls = 0;
u32 gHostOakSpeechInitStandardTextBoxWindowsCalls = 0;
u32 gHostOakSpeechCreateTopBarWindowLoadPaletteCalls = 0;
u32 gHostOakSpeechPlayBGMCalls = 0;
u32 gHostOakSpeechDoNamingScreenCalls = 0;
u32 gHostOakSpeechCB2NewGameCalls = 0;
u32 gHostOakSpeechControlsGuidePage1Loads = 0;
u32 gHostOakSpeechControlsGuidePage2Loads = 0;
u32 gHostOakSpeechControlsGuidePage3Loads = 0;
u32 gHostOakSpeechPikachuIntroPage1Loads = 0;
u32 gHostOakSpeechPikachuIntroPage2Loads = 0;
u32 gHostOakSpeechPikachuIntroPage3Loads = 0;
u32 gHostOakSpeechWelcomeToTheWorldPrints = 0;
u32 gHostOakSpeechThisWorldPrints = 0;
const u8 *gHostOakSpeechLastExpandedPlaceholderSource = NULL;
u16 gHostOakSpeechLastPlayedBGM = 0;
const u8 *gHostOakSpeechLastTopBarLeftText = NULL;
const u8 *gHostOakSpeechLastTopBarRightText = NULL;

static u8 sOakSpeechMonSpriteBuffer[0x8000] = {0};
static const u16 sOakSpeechTextWindowPalette[16] = {
    RGB_WHITE, RGB_WHITE, RGB_WHITE, RGB_WHITE,
    RGB_WHITE, RGB_WHITE, RGB_WHITE, RGB_WHITE,
    RGB_WHITE, RGB_WHITE, RGB_WHITE, RGB_WHITE,
    RGB_WHITE, RGB_WHITE, RGB_WHITE, RGB_BLACK,
};
static const struct WindowTemplate sHostStandardTextBoxWindowTemplates[] = {
    {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 15,
        .width = 26,
        .height = 4,
        .paletteNum = 15,
        .baseBlock = 0x198,
    },
    DUMMY_WIN_TEMPLATE
};
static const u32 sHostBlankMonPaletteLz77[] = {0x00002000};

#define DEFINE_TEXT(name, value) const u8 name[] = value

DEFINE_TEXT(gText_Controls, "Controls");
DEFINE_TEXT(gText_ABUTTONNext, "A Next");
DEFINE_TEXT(gText_ABUTTONNext_BBUTTONBack, "A Next B Back");
DEFINE_TEXT(gText_Boy, "Boy");
DEFINE_TEXT(gText_Girl, "Girl");

DEFINE_TEXT(gControlsGuide_Text_Intro, "Use the controls to play.");
DEFINE_TEXT(gControlsGuide_Text_DPad, "Move with the D-Pad.");
DEFINE_TEXT(gControlsGuide_Text_AButton, "A confirms.");
DEFINE_TEXT(gControlsGuide_Text_BButton, "B cancels.");
DEFINE_TEXT(gControlsGuide_Text_StartButton, "Start opens the menu.");
DEFINE_TEXT(gControlsGuide_Text_SelectButton, "Select uses shortcuts.");
DEFINE_TEXT(gControlsGuide_Text_LRButtons, "L and R cycle pages.");

DEFINE_TEXT(gPikachuIntro_Text_Page1, "Welcome to the world of Pokemon.");
DEFINE_TEXT(gPikachuIntro_Text_Page2, "Pikachu travels with you.");
DEFINE_TEXT(gPikachuIntro_Text_Page3, "Your adventure is about to begin.");

DEFINE_TEXT(gOakSpeech_Text_WelcomeToTheWorld, "Welcome to the world of Pokemon.");
DEFINE_TEXT(gOakSpeech_Text_ThisWorld, "This world is inhabited far and wide.");
DEFINE_TEXT(gOakSpeech_Text_IsInhabitedFarAndWide, "Pokemon live everywhere.");
DEFINE_TEXT(gOakSpeech_Text_IStudyPokemon, "I study Pokemon as a profession.");
DEFINE_TEXT(gOakSpeech_Text_TellMeALittleAboutYourself, "First, tell me a little about yourself.");
DEFINE_TEXT(gOakSpeech_Text_AskPlayerGender, "Are you a boy? Or are you a girl?");
DEFINE_TEXT(gOakSpeech_Text_YourNameWhatIsIt, "Let's begin with your name. What is it?");
DEFINE_TEXT(gOakSpeech_Text_SoYourNameIsPlayer, "Right. So your name is {PLAYER}.");
DEFINE_TEXT(gOakSpeech_Text_YourRivalsNameWhatWasIt, "This is my grandson. What was his name now?");
DEFINE_TEXT(gOakSpeech_Text_WhatWasHisName, "Erm, what was his name?");
DEFINE_TEXT(gOakSpeech_Text_ConfirmRivalName, "Was it {RIVAL}? ");
DEFINE_TEXT(gOakSpeech_Text_RememberRivalsName, "That's right! I remember now! His name is {RIVAL}! ");
DEFINE_TEXT(gOakSpeech_Text_LetsGo, "Your very own Pokemon legend is about to unfold!");
DEFINE_TEXT(gOtherText_NewName, "NEW NAME");

DEFINE_TEXT(gNameChoice_Red, "RED");
DEFINE_TEXT(gNameChoice_Fire, "FIRE");
DEFINE_TEXT(gNameChoice_Ash, "ASH");
DEFINE_TEXT(gNameChoice_Kene, "KENE");
DEFINE_TEXT(gNameChoice_Geki, "GEKI");
DEFINE_TEXT(gNameChoice_Jak, "JAK");
DEFINE_TEXT(gNameChoice_Janne, "JANNE");
DEFINE_TEXT(gNameChoice_Jonn, "JONN");
DEFINE_TEXT(gNameChoice_Kamon, "KAMON");
DEFINE_TEXT(gNameChoice_Karl, "KARL");
DEFINE_TEXT(gNameChoice_Taylor, "TAYLOR");
DEFINE_TEXT(gNameChoice_Oscar, "OSCAR");
DEFINE_TEXT(gNameChoice_Hiro, "HIRO");
DEFINE_TEXT(gNameChoice_Max, "MAX");
DEFINE_TEXT(gNameChoice_Jon, "JON");
DEFINE_TEXT(gNameChoice_Ralph, "RALPH");
DEFINE_TEXT(gNameChoice_Kay, "KAY");
DEFINE_TEXT(gNameChoice_Tosh, "TOSH");
DEFINE_TEXT(gNameChoice_Roak, "ROAK");
DEFINE_TEXT(gNameChoice_Omi, "OMI");
DEFINE_TEXT(gNameChoice_Jodi, "JODI");
DEFINE_TEXT(gNameChoice_Amanda, "AMANDA");
DEFINE_TEXT(gNameChoice_Hillary, "HILLARY");
DEFINE_TEXT(gNameChoice_Makey, "MAKEY");
DEFINE_TEXT(gNameChoice_Michi, "MICHI");
DEFINE_TEXT(gNameChoice_Paula, "PAULA");
DEFINE_TEXT(gNameChoice_June, "JUNE");
DEFINE_TEXT(gNameChoice_Cassie, "CASSIE");
DEFINE_TEXT(gNameChoice_Rey, "REY");
DEFINE_TEXT(gNameChoice_Seda, "SEDA");
DEFINE_TEXT(gNameChoice_Kiko, "KIKO");
DEFINE_TEXT(gNameChoice_Mina, "MINA");
DEFINE_TEXT(gNameChoice_Norie, "NORIE");
DEFINE_TEXT(gNameChoice_Sai, "SAI");
DEFINE_TEXT(gNameChoice_Momo, "MOMO");
DEFINE_TEXT(gNameChoice_Suzi, "SUZI");
DEFINE_TEXT(gNameChoice_Green, "GREEN");
DEFINE_TEXT(gNameChoice_Gary, "GARY");
DEFINE_TEXT(gNameChoice_Kaz, "KAZ");
DEFINE_TEXT(gNameChoice_Toru, "TORU");
DEFINE_TEXT(gNameChoice_Leaf, "LEAF");

const struct OamData gOamData_AffineOff_ObjBlend_32x32 = {0};
const struct OamData gOamData_AffineOff_ObjNormal_32x32 = {0};
const struct OamData gOamData_AffineOff_ObjNormal_32x16 = {0};
const struct OamData gOamData_AffineOff_ObjNormal_16x8 = {0};
const struct CompressedSpritePalette gMonPaletteTable[NUM_SPECIES] = {
    [0 ... NUM_SPECIES - 1] = {
        .data = sHostBlankMonPaletteLz77,
        .tag = 0,
    },
};
struct SpriteTemplate gMultiuseSpriteTemplate = {0};

static u8 *CopyHostString(u8 *dest, const u8 *src)
{
    while (*src != EOS && *src != 0)
        *dest++ = *src++;
    *dest = EOS;
    return dest;
}

void HostOakSpeechStubRecordPrintedText(const u8 *text)
{
    if (text == gControlsGuide_Text_Intro)
        gHostOakSpeechControlsGuidePage1Loads++;
    else if (text == gControlsGuide_Text_DPad)
        gHostOakSpeechControlsGuidePage2Loads++;
    else if (text == gControlsGuide_Text_StartButton)
        gHostOakSpeechControlsGuidePage3Loads++;
    else if (text == gPikachuIntro_Text_Page1)
        gHostOakSpeechPikachuIntroPage1Loads++;
    else if (text == gPikachuIntro_Text_Page2)
        gHostOakSpeechPikachuIntroPage2Loads++;
    else if (text == gPikachuIntro_Text_Page3)
        gHostOakSpeechPikachuIntroPage3Loads++;
}

void HostOakSpeechStubReset(void)
{
    gHostOakSpeechCreateMonSpritesGfxManagerCalls = 0;
    gHostOakSpeechInitStandardTextBoxWindowsCalls = 0;
    gHostOakSpeechCreateTopBarWindowLoadPaletteCalls = 0;
    gHostOakSpeechPlayBGMCalls = 0;
    gHostOakSpeechDoNamingScreenCalls = 0;
    gHostOakSpeechCB2NewGameCalls = 0;
    gHostOakSpeechControlsGuidePage1Loads = 0;
    gHostOakSpeechControlsGuidePage2Loads = 0;
    gHostOakSpeechControlsGuidePage3Loads = 0;
    gHostOakSpeechPikachuIntroPage1Loads = 0;
    gHostOakSpeechPikachuIntroPage2Loads = 0;
    gHostOakSpeechPikachuIntroPage3Loads = 0;
    gHostOakSpeechWelcomeToTheWorldPrints = 0;
    gHostOakSpeechThisWorldPrints = 0;
    gHostOakSpeechLastExpandedPlaceholderSource = NULL;
    gHostOakSpeechLastPlayedBGM = 0;
    gHostOakSpeechLastTopBarLeftText = NULL;
    gHostOakSpeechLastTopBarRightText = NULL;
    memset(gStringVar1, 0, sizeof(gStringVar1));
    memset(gStringVar2, 0, sizeof(gStringVar2));
    memset(gStringVar3, 0, sizeof(gStringVar3));
    memset(gStringVar4, 0, sizeof(gStringVar4));
    memset(sOakSpeechMonSpriteBuffer, 0, sizeof(sOakSpeechMonSpriteBuffer));
    memset(&gTextFlags, 0, sizeof(gTextFlags));
}

struct MonSpritesGfxManager *CreateMonSpritesGfxManager(u8 battlePosition, u8 mode)
{
    gHostOakSpeechCreateMonSpritesGfxManagerCalls++;
    (void)battlePosition;
    (void)mode;
    return (struct MonSpritesGfxManager *)sOakSpeechMonSpriteBuffer;
}

void DestroyMonSpritesGfxManager(void)
{
}

u8 *MonSpritesGfxManager_GetSpritePtr(u8 bufferId)
{
    (void)bufferId;
    return sOakSpeechMonSpriteBuffer;
}

void InitStandardTextBoxWindows(void)
{
    gHostOakSpeechInitStandardTextBoxWindowsCalls++;
    InitWindows(sHostStandardTextBoxWindowTemplates);
    FillWindowPixelBuffer(0, PIXEL_FILL(0));
    PutWindowTilemap(0);
    CopyWindowToVram(0, COPYWIN_FULL);
}

void InitTextBoxGfxAndPrinters(void)
{
}

void Menu_LoadStdPalAt(u16 offset)
{
    (void)offset;
}

const u16 *GetTextWindowPalette(u8 id)
{
    (void)id;
    return sOakSpeechTextWindowPalette;
}

u8 GetTextSpeedSetting(void)
{
    return 0;
}

void ClearDialogWindowAndFrame(u8 windowId, bool8 copyToVram)
{
    gHostTitleStubTextPrinterActive = FALSE;
    if (windowId < WINDOWS_MAX && gWindows[windowId].tileData != NULL)
    {
        FillWindowPixelBuffer(windowId, PIXEL_FILL(0));
        PutWindowTilemap(windowId);
        if (copyToVram)
            CopyWindowToVram(windowId, COPYWIN_FULL);
    }
}

u8 CreateTopBarWindowLoadPalette(u8 bg, u8 width, u8 yPos, u8 palette, u16 baseTile)
{
    gHostOakSpeechCreateTopBarWindowLoadPaletteCalls++;
    (void)bg;
    (void)width;
    (void)yPos;
    (void)palette;
    (void)baseTile;
    return 0;
}

void TopBarWindowPrintTwoStrings(const u8 *string, const u8 *string2, bool8 fgColorChooser, u8 notUsed, bool8 copyToVram)
{
    gHostOakSpeechLastTopBarLeftText = string;
    gHostOakSpeechLastTopBarRightText = string2;
    gHostTitleStubLastPrintedText = string;
    gHostTitleStubLastPrintedText3 = string2;
    (void)fgColorChooser;
    (void)notUsed;
    (void)copyToVram;
}

void TopBarWindowPrintString(const u8 *string, u8 unUsed, bool8 copyToVram)
{
    gHostOakSpeechLastTopBarLeftText = NULL;
    gHostOakSpeechLastTopBarRightText = string;
    gHostTitleStubLastPrintedText3 = string;
    (void)unUsed;
    (void)copyToVram;
}

u8 CreateTextCursorSprite(u8 sheetId, u16 x, u16 y, u8 priority, u8 subpriority)
{
    (void)sheetId;
    (void)x;
    (void)y;
    (void)priority;
    (void)subpriority;
    memset(&gSprites[0], 0, sizeof(gSprites[0]));
    return 0;
}

void DestroyTextCursorSprite(u8 spriteId)
{
    (void)spriteId;
}

void PlayBGM(u16 songNum)
{
    gHostOakSpeechPlayBGMCalls++;
    gHostOakSpeechLastPlayedBGM = songNum;
    (void)songNum;
}

void ClearTopBarWindow(void)
{
    gHostOakSpeechLastTopBarLeftText = NULL;
    gHostOakSpeechLastTopBarRightText = NULL;
}

void DestroyTopBarWindow(void)
{
    gHostOakSpeechLastTopBarLeftText = NULL;
    gHostOakSpeechLastTopBarRightText = NULL;
}

void *MallocAndDecompress(const void *src, u32 *size)
{
    void *ptr;
    u8 *sizeAsBytes = (u8 *)size;
    const u8 *srcAsBytes = src;

    sizeAsBytes[0] = srcAsBytes[1];
    sizeAsBytes[1] = srcAsBytes[2];
    sizeAsBytes[2] = srcAsBytes[3];
    sizeAsBytes[3] = 0;

    ptr = Alloc(*size);
    if (ptr)
        LZ77UnCompWram(src, ptr);
    return ptr;
}

void DrawDialogueFrame(u8 windowId, bool8 transfer)
{
    if (windowId < WINDOWS_MAX && gWindows[windowId].tileData != NULL)
    {
        FillWindowPixelBuffer(windowId, PIXEL_FILL(1));
        PutWindowTilemap(windowId);
        if (transfer)
            CopyWindowToVram(windowId, COPYWIN_FULL);
    }
}

void ClearStdWindowAndFrameToTransparent(u8 windowId, bool8 copyToVram)
{
    HostTitleScreenStubForgetMenuWindow(windowId);
    if (windowId < WINDOWS_MAX && gWindows[windowId].tileData != NULL)
    {
        FillWindowPixelBuffer(windowId, PIXEL_FILL(0));
        ClearWindowTilemap(windowId);
        if (copyToVram)
            CopyWindowToVram(windowId, COPYWIN_FULL);
    }
}

u16 AddTextPrinterParameterized(u8 windowId, u8 fontId, const u8 *str, u8 x, u8 y, u8 speed, void (*callback)(struct TextPrinterTemplate *, u16))
{
    HostOakSpeechStubRecordPrintedText(str);
    gHostTitleStubLastPrintedText = str;
    gHostTitleStubTextPrinterActive = FALSE;
    (void)windowId;
    (void)fontId;
    (void)x;
    (void)y;
    (void)speed;
    (void)callback;
    return 0;
}

u16 AddTextPrinterParameterized2(u8 windowId, u8 fontId, const u8 *str, u8 speed, void (*callback)(struct TextPrinterTemplate *, u16), u8 fgColor, u8 bgColor, u8 shadowColor)
{
    HostOakSpeechStubRecordPrintedText(str);
    gHostTitleStubLastPrintedText = str;
    gHostTitleStubTextPrinterActive = FALSE;
    (void)windowId;
    (void)fontId;
    (void)speed;
    (void)callback;
    (void)fgColor;
    (void)bgColor;
    (void)shadowColor;
    return 0;
}

u8 *StringExpandPlaceholders(u8 *dest, const u8 *src)
{
    gHostOakSpeechLastExpandedPlaceholderSource = src;
    if (src == gOakSpeech_Text_WelcomeToTheWorld)
        gHostOakSpeechWelcomeToTheWorldPrints++;
    else if (src == gOakSpeech_Text_ThisWorld)
        gHostOakSpeechThisWorldPrints++;
    return CopyHostString(dest, src);
}

void CreatePokeballSpriteToReleaseMon(u8 monSpriteId, u8 monPalNum, u8 x, u8 y, u8 oamPriority, u8 subpriortiy, u8 delay, u32 fadePalettes)
{
    (void)monSpriteId;
    (void)monPalNum;
    (void)x;
    (void)y;
    (void)oamPriority;
    (void)subpriortiy;
    (void)delay;
    (void)fadePalettes;
}

u8 CreateTradePokeballSprite(u8 monSpriteId, u8 monPalNum, u8 x, u8 y, u8 oamPriority, u8 subPriority, u8 delay, u32 fadePalettes)
{
    (void)monSpriteId;
    (void)monPalNum;
    (void)x;
    (void)y;
    (void)oamPriority;
    (void)subPriority;
    (void)delay;
    (void)fadePalettes;
    return 0;
}

bool8 IsCryFinished(void)
{
    return TRUE;
}

u16 GetStdWindowBaseTileNum(void)
{
    return 0;
}

u8 GetFontAttribute(u8 fontId, u8 attributeId)
{
    (void)fontId;
    (void)attributeId;
    return 1;
}

u8 Menu_InitCursor(u8 windowId, u8 fontId, u8 left, u8 top, u8 cursorHeight, u8 numChoices, u8 initialCursorPos)
{
    (void)fontId;
    return HostTitleScreenStubInitMenuCursor(windowId, left, top, cursorHeight, numChoices, initialCursorPos);
}

s8 Menu_ProcessInputNoWrapAround(void)
{
    return HostTitleScreenStubProcessMenuInput(FALSE);
}

s8 Menu_ProcessInput(void)
{
    return HostTitleScreenStubProcessMenuInput(TRUE);
}

void DoNamingScreen(u8 templateNum, u8 *destBuffer, u16 monSpecies, u16 monGender, u32 monPersonality, MainCallback returnCallback)
{
    gHostOakSpeechDoNamingScreenCalls++;
    (void)monSpecies;
    (void)monGender;
    (void)monPersonality;
    if (templateNum == NAMING_SCREEN_PLAYER)
        CopyHostString(destBuffer, (const u8 *)"ASH");
    else
        CopyHostString(destBuffer, (const u8 *)"GARY");
    ResetTasks();
    SetMainCallback2(returnCallback);
}

bool8 IsMonSpriteNotFlipped(u16 species)
{
    (void)species;
    return TRUE;
}

u16 LoadMonPicInWindow(u16 species, u32 otId, u32 personality, bool8 isFrontPic, u8 paletteSlot, u8 windowId)
{
    (void)species;
    (void)otId;
    (void)personality;
    (void)isFrontPic;
    (void)paletteSlot;
    (void)windowId;
    return 0;
}

u16 CreateTrainerPicSprite(u16 species, bool8 isFrontPic, s16 x, s16 y, u8 paletteSlot, u16 paletteTag)
{
    (void)species;
    (void)isFrontPic;
    (void)x;
    (void)y;
    (void)paletteSlot;
    (void)paletteTag;
    return 0;
}

u16 FreeAndDestroyMonPicSprite(u16 spriteId)
{
    return spriteId;
}

u16 FreeAndDestroyTrainerPicSprite(u16 spriteId)
{
    return spriteId;
}

void FreeMonSpritesGfx(void)
{
}

void SetMultiuseSpriteTemplateToPokemon(u16 speciesTag, u8 battlerPosition)
{
    (void)speciesTag;
    (void)battlerPosition;
    gMultiuseSpriteTemplate = gDummySpriteTemplate;
}

s16 Q_8_8_inv(s16 y)
{
    if (y == 0)
        return 0;
    return (s16)((256 * 256) / y);
}

void UpstreamCB2_NewGame(void);

void CB2_NewGame(void)
{
    gHostOakSpeechCB2NewGameCalls++;
    UpstreamCB2_NewGame();
}
