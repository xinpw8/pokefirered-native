#ifndef PFR_NATIVE_UI_DATA_H
#define PFR_NATIVE_UI_DATA_H
#include <stdint.h>

#define PFR_FONT_GLYPH_COUNT 512
#define PFR_FONT_GLYPH_SIZE 64
#define PFR_FONT_HEIGHT 14
#define PFR_GBA_SCREEN_W 240
#define PFR_GBA_SCREEN_H 160

extern const uint8_t gPfrFontGlyphs[512][64];
extern const uint8_t gPfrFontGlyphWidths[512];
extern const uint8_t gPfrAsciiToGbaChar[128];

/* Window frame: 9 tiles of 32 bytes each (4bpp) */
extern const uint8_t gPfrWindowFrameTiles[9][32];
extern const uint16_t gPfrWindowFramePalette[16];

/* Standard window (menus) */
extern const uint8_t gPfrStdWindowTiles[9][32];
extern const uint16_t gPfrStdWindowPalette[16];

/* Message box background */
extern const uint8_t gPfrMessageBoxTiles[];
extern const uint32_t gPfrMessageBoxSize;
extern const uint16_t gPfrMessageBoxPalette[16];

/* Battle UI healthbox tiles + palette */
extern const uint8_t gPfrBattlePlayerHBTiles[];
extern const uint32_t gPfrBattlePlayerHBSize;
extern const uint16_t gPfrBattlePlayerHBPalette[16];

extern const uint8_t gPfrBattleOppHBTiles[];
extern const uint32_t gPfrBattleOppHBSize;
extern const uint16_t gPfrBattleOppHBPalette[16];

extern const uint8_t gPfrBattleElementsTiles[];
extern const uint32_t gPfrBattleElementsSize;
extern const uint16_t gPfrBattleElementsPalette[16];

extern const uint8_t gPfrBattleTextboxTiles[];
extern const uint32_t gPfrBattleTextboxSize;
extern const uint16_t gPfrBattleTextboxPalette[16];

/* Text color definitions (RGB555) */
typedef struct {
    uint16_t fg;
    uint16_t shadow;
    uint16_t bg;
} PfrTextColor;

#define PFR_TEXT_COLOR_DARK   0  /* Dark text on light bg */
#define PFR_TEXT_COLOR_WHITE  1  /* White text on dark bg */
#define PFR_TEXT_COLOR_RED    2  /* Red text (low HP) */
#define PFR_TEXT_COLOR_YELLOW 3  /* Yellow text (mid HP) */
#define PFR_TEXT_COLOR_GREEN  4  /* Green text (full HP) */
#define PFR_TEXT_COLOR_COUNT  5

extern const PfrTextColor gPfrTextColors[PFR_TEXT_COLOR_COUNT];

#endif /* PFR_NATIVE_UI_DATA_H */
