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
#include "upstream_event_scripts.h"

u32 gHostOakSpeechInitStandardTextBoxWindowsCalls = 0;
u32 gHostOakSpeechCreateTopBarWindowLoadPaletteCalls = 0;
u32 gHostOakSpeechDoNamingScreenCalls = 0;
u32 gHostOakSpeechControlsGuidePage1Loads = 0;
u32 gHostOakSpeechControlsGuidePage2Loads = 0;
u32 gHostOakSpeechControlsGuidePage3Loads = 0;
u32 gHostOakSpeechPikachuIntroPage1Loads = 0;
u32 gHostOakSpeechPikachuIntroPage2Loads = 0;
u32 gHostOakSpeechPikachuIntroPage3Loads = 0;
u32 gHostOakSpeechWelcomeToTheWorldPrints = 0;
u32 gHostOakSpeechThisWorldPrints = 0;
u32 gHostOakSpeechIStudyPokemonPrints = 0;
u32 gHostOakSpeechAskPlayerGenderPrints = 0;
const u8 *gHostOakSpeechLastTopBarLeftText = NULL;
const u8 *gHostOakSpeechLastTopBarRightText = NULL;

static u8 sOakSpeechMonSpriteBuffer[0x8000] = {0};
static bool8 sHostOakSpeechNewGameInitialized = FALSE;
static const u32 sHostBlankMonPaletteLz77[] = {0x00002000};

/*
 * Text strings (gControlsGuide_Text_*, gOakSpeech_Text_*, gPikachuIntro_Text_*,
 * gNameChoice_*, gOtherText_NewName) are now defined in upstream_event_scripts.c
 * via asm aliases. #include "upstream_event_scripts.h" above provides the externs.
 */

/* gOamData_* now from battle_anim.h (included via battle_anim.c) */
/* gMonPaletteTable now from data.c */

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
    gHostOakSpeechInitStandardTextBoxWindowsCalls = 0;
    gHostOakSpeechCreateTopBarWindowLoadPaletteCalls = 0;
    gHostOakSpeechDoNamingScreenCalls = 0;
    gHostOakSpeechControlsGuidePage1Loads = 0;
    gHostOakSpeechControlsGuidePage2Loads = 0;
    gHostOakSpeechControlsGuidePage3Loads = 0;
    gHostOakSpeechPikachuIntroPage1Loads = 0;
    gHostOakSpeechPikachuIntroPage2Loads = 0;
    gHostOakSpeechPikachuIntroPage3Loads = 0;
    gHostOakSpeechWelcomeToTheWorldPrints = 0;
    gHostOakSpeechThisWorldPrints = 0;
    gHostOakSpeechIStudyPokemonPrints = 0;
    gHostOakSpeechAskPlayerGenderPrints = 0;
    gHostOakSpeechLastTopBarLeftText = NULL;
    gHostOakSpeechLastTopBarRightText = NULL;
    sHostOakSpeechNewGameInitialized = FALSE;
    memset(sOakSpeechMonSpriteBuffer, 0, sizeof(sOakSpeechMonSpriteBuffer));
    memset(&gTextFlags, 0, sizeof(gTextFlags));
}

/* CreatePokeballSpriteToReleaseMon, CreateTradePokeballSprite now from pokeball.c */

void DoNamingScreen(u8 templateNum, u8 *destBuffer, u16 monSpecies, u16 monGender, u32 monPersonality, MainCallback returnCallback)
{
    gHostOakSpeechDoNamingScreenCalls++;
    (void)monSpecies;
    (void)monGender;
    (void)monPersonality;
    if (templateNum == NAMING_SCREEN_PLAYER)
        CopyHostString(destBuffer, gNameChoice_Ash);
    else
        CopyHostString(destBuffer, gNameChoice_Gary);
    ResetTasks();
    SetMainCallback2(returnCallback);
}

/* Q_8_8_inv now from math_util.c */
