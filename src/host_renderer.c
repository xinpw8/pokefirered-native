/*
 * host_renderer.c — GBA per-scanline software PPU for pokefirered-native
 *
 * Renders one scanline at a time, reading VRAM, OAM, PLTT, and I/O
 * registers from the memory-mapped GBA address space and compositing
 * a 240x160 ARGB8888 framebuffer.
 *
 * Supports:
 *   - Mode 0 text backgrounds (4bpp and 8bpp tiles)
 *   - Mode 1 text BG0/BG1 and affine BG2
 *   - Sprites/OBJ (4bpp and 8bpp, normal + affine, with flip/priority)
 *   - Semi-transparent OBJ (forced alpha blend)
 *   - OBJ Window sprites
 *   - BG/OBJ palette lookup (BGR555 -> ARGB8888)
 *   - BG scroll (HOFS/VOFS) with per-scanline visibility
 *   - BG and OBJ priority sorting with two-layer compositing
 *   - Windows (WIN0, WIN1, OBJWIN, WINOUT)
 *   - Alpha blending (EVA/EVB coefficients)
 *   - Brightness fade (lighten/darken) with per-layer first-target check
 *   - Affine BG reference point latching (latched at VBlank, PB/PD per scanline)
 *   - Mosaic (BG and OBJ)
 *   - DISPCNT BG/OBJ/WIN enable flags
 *   - Forced blank
 */

#include <stdio.h>
#include <string.h>

#include "host_renderer.h"
#include "gba/gba.h"

/* ================================================================
 *  GBA hardware access
 * ================================================================ */

#define PPU_VRAM_BASE       ((const u8 *)VRAM)
#define PPU_OAM_BASE        ((const u8 *)OAM)
#define PPU_PLTT_BASE       ((const u16 *)PLTT)
#define PPU_IO_BASE         ((volatile u16 *)REG_BASE)

#define PPU_BG_PLTT         (PPU_PLTT_BASE)
#define PPU_OBJ_PLTT        (PPU_PLTT_BASE + 256)
#define PPU_BG_VRAM         (PPU_VRAM_BASE)
#define PPU_OBJ_VRAM        (PPU_VRAM_BASE + 0x10000)

/* I/O register read helper (offset in bytes from REG_BASE) */
#define IOREG(off)      (PPU_IO_BASE[(off) >> 1])

#define IO_DISPCNT      0x00
#define IO_BG0CNT       0x08
#define IO_BG0HOFS      0x10
#define IO_BG0VOFS      0x12
#define IO_BG2PA        0x20
#define IO_BG2PB        0x22
#define IO_BG2PC        0x24
#define IO_BG2PD        0x26
#define IO_BG2X_L       0x28
#define IO_BG2X_H       0x2A
#define IO_BG2Y_L       0x2C
#define IO_BG2Y_H       0x2E
#define IO_WIN0H        0x40
#define IO_WIN1H        0x42
#define IO_WIN0V        0x44
#define IO_WIN1V        0x46
#define IO_WININ        0x48
#define IO_WINOUT       0x4A
#define IO_MOSAIC       0x4C
#define IO_BLDCNT       0x50
#define IO_BLDALPHA     0x52
#define IO_BLDY         0x54

/* DISPCNT bits */
#define DCNT_MODE_MASK  0x0007
#define DCNT_OBJ_1D     0x0040
#define DCNT_FORCED_BLK 0x0080
#define DCNT_BG0_ON     0x0100
#define DCNT_BG1_ON     0x0200
#define DCNT_BG2_ON     0x0400
#define DCNT_BG3_ON     0x0800
#define DCNT_OBJ_ON     0x1000
#define DCNT_WIN0_ON    0x2000
#define DCNT_WIN1_ON    0x4000
#define DCNT_OBJWIN_ON  0x8000

/* BLDCNT bits */
#define BLD_MODE_MASK   0x00C0
#define BLD_MODE_OFF    0x0000
#define BLD_MODE_ALPHA  0x0040
#define BLD_MODE_LIGHT  0x0080
#define BLD_MODE_DARK   0x00C0

/* Layer IDs (match BLDCNT first/second target bit positions) */
#define LAYER_BG0   0
#define LAYER_BG1   1
#define LAYER_BG2   2
#define LAYER_BG3   3
#define LAYER_OBJ   4
#define LAYER_BD    5

/* Window enable bits */
#define WIN_BG0     (1 << 0)
#define WIN_BG1     (1 << 1)
#define WIN_BG2     (1 << 2)
#define WIN_BG3     (1 << 3)
#define WIN_OBJ     (1 << 4)
#define WIN_SFX     (1 << 5)
#define WIN_ALL     0x3F

/* BGCNT field extraction */
#define PPU_BGCNT_PRIORITY(v)       ((v) & 3)
#define PPU_BGCNT_CHARBASE(v)       (((v) >> 2) & 3)
#define PPU_BGCNT_MOSAIC(v)         (((v) >> 6) & 1)
#define PPU_BGCNT_PALMODE(v)        (((v) >> 7) & 1)   /* 0=4bpp, 1=8bpp */
#define PPU_BGCNT_SCREENBASE(v)     (((v) >> 8) & 0x1F)
#define PPU_BGCNT_AFFINE_WRAP(v)    (((v) >> 13) & 1)
#define PPU_BGCNT_SCREENSIZE(v)     (((v) >> 14) & 3)

/* OAM attribute access */
#define OAM_ATTR0(entry)  (((const u16 *)(PPU_OAM_BASE))[(entry) * 4 + 0])
#define OAM_ATTR1(entry)  (((const u16 *)(PPU_OAM_BASE))[(entry) * 4 + 1])
#define OAM_ATTR2(entry)  (((const u16 *)(PPU_OAM_BASE))[(entry) * 4 + 2])

#define ATTR0_Y(a0)         ((a0) & 0xFF)
#define ATTR0_AFFINE(a0)    (((a0) >> 8) & 3)
#define ATTR0_OBJMODE(a0)   (((a0) >> 10) & 3)
#define ATTR0_MOSAIC(a0)    (((a0) >> 12) & 1)
#define ATTR0_BPP(a0)       (((a0) >> 13) & 1)
#define ATTR0_SHAPE(a0)     (((a0) >> 14) & 3)

#define ATTR1_X(a1)         ((a1) & 0x1FF)
#define ATTR1_AFFINE_IDX(a1)(((a1) >> 9) & 0x1F)
#define ATTR1_HFLIP(a1)     (((a1) >> 12) & 1)
#define ATTR1_VFLIP(a1)     (((a1) >> 13) & 1)
#define ATTR1_SIZE(a1)      (((a1) >> 14) & 3)

#define ATTR2_TILE(a2)      ((a2) & 0x3FF)
#define ATTR2_PRIORITY(a2)  (((a2) >> 10) & 3)
#define ATTR2_PALETTE(a2)   (((a2) >> 12) & 0xF)

/* Affine OAM parameter access (rotation group N, 0-31) */
#define OAM_AFFINE_PA(grp)  ((s16)(((const u16 *)(PPU_OAM_BASE))[(grp) * 16 + 3]))
#define OAM_AFFINE_PB(grp)  ((s16)(((const u16 *)(PPU_OAM_BASE))[(grp) * 16 + 7]))
#define OAM_AFFINE_PC(grp)  ((s16)(((const u16 *)(PPU_OAM_BASE))[(grp) * 16 + 11]))
#define OAM_AFFINE_PD(grp)  ((s16)(((const u16 *)(PPU_OAM_BASE))[(grp) * 16 + 15]))

/* Sprite size lookup: [shape][size] -> {width, height} */
static const u8 sSpriteWidth[3][4] = {
    { 8, 16, 32, 64 },   /* square */
    { 16, 32, 32, 64 },  /* h-rect */
    { 8, 8, 16, 32 },    /* v-rect */
};
static const u8 sSpriteHeight[3][4] = {
    { 8, 16, 32, 64 },   /* square */
    { 8, 8, 16, 32 },    /* h-rect */
    { 16, 32, 32, 64 },  /* v-rect */
};

/* ================================================================
 *  Internal state
 * ================================================================ */

static u32 sFramebuffer[GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT];

/* Per-scanline two-layer compositing buffers */
static u16 sScanTop[GBA_SCREEN_WIDTH];
static u8  sScanTopLayer[GBA_SCREEN_WIDTH];
static u8  sScanTopPrio[GBA_SCREEN_WIDTH];
static u16 sScanBot[GBA_SCREEN_WIDTH];
static u8  sScanBotLayer[GBA_SCREEN_WIDTH];
static u16 sScanBotNonObj[GBA_SCREEN_WIDTH];
static u8  sScanBotNonObjLayer[GBA_SCREEN_WIDTH];
static bool8 sScanObjSemiTrans[GBA_SCREEN_WIDTH];

/* Window state */
static u8 sWindowMask[GBA_SCREEN_WIDTH];
static bool8 sObjWindowPixels[GBA_SCREEN_WIDTH];

/* Affine BG internal reference points (latched at VBlank) */
static s32 sAffineRefX[2];   /* [0]=BG2, [1]=BG3 */
static s32 sAffineRefY[2];

static bool8 sInitialized;

/* ================================================================
 *  Color conversion
 * ================================================================ */

static inline u32 Bgr555ToArgb8888(u16 bgr)
{
    u32 r = (bgr & 0x1F);
    u32 g = ((bgr >> 5) & 0x1F);
    u32 b = ((bgr >> 10) & 0x1F);
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

/* ================================================================
 *  Scanline compositing
 * ================================================================ */

static void ClearScanline(u16 backdrop)
{
    int x;
    for (x = 0; x < GBA_SCREEN_WIDTH; x++)
    {
        sScanTop[x] = backdrop;
        sScanTopLayer[x] = LAYER_BD;
        sScanTopPrio[x] = 4;
        sScanBot[x] = backdrop;
        sScanBotLayer[x] = LAYER_BD;
        sScanBotNonObj[x] = backdrop;
        sScanBotNonObjLayer[x] = LAYER_BD;
        sScanObjSemiTrans[x] = FALSE;
    }
}

/*
 * Insert a pixel into the two-layer compositing stack.
 * Called in back-to-front render order, so each new non-transparent
 * pixel has equal or higher visual priority than the current top.
 */
static inline void CompositPixel(int x, u16 color, u8 layer, u8 priority)
{
    u16 oldTop = sScanTop[x];
    u8 oldTopLayer = sScanTopLayer[x];

    sScanBot[x] = sScanTop[x];
    sScanBotLayer[x] = sScanTopLayer[x];
    if (oldTopLayer != LAYER_OBJ)
    {
        sScanBotNonObj[x] = oldTop;
        sScanBotNonObjLayer[x] = oldTopLayer;
    }
    sScanTop[x] = color;
    sScanTopLayer[x] = layer;
    sScanTopPrio[x] = priority;
    sScanObjSemiTrans[x] = FALSE;
}

static inline void CompositPixelObjSemiTrans(int x, u16 color, u8 priority)
{
    u16 oldTop = sScanTop[x];
    u8 oldTopLayer = sScanTopLayer[x];

    sScanBot[x] = sScanTop[x];
    sScanBotLayer[x] = sScanTopLayer[x];
    if (oldTopLayer != LAYER_OBJ)
    {
        sScanBotNonObj[x] = oldTop;
        sScanBotNonObjLayer[x] = oldTopLayer;
    }
    sScanTop[x] = color;
    sScanTopLayer[x] = LAYER_OBJ;
    sScanTopPrio[x] = priority;
    sScanObjSemiTrans[x] = TRUE;
}

/* ================================================================
 *  Window evaluation
 * ================================================================ */

static void ApplyWindowRect(int y, u16 winh, u16 winv, u8 enable)
{
    int x1 = winh >> 8;
    int x2 = winh & 0xFF;
    int y1 = winv >> 8;
    int y2 = winv & 0xFF;
    bool8 yInside;
    int x;

    if (y1 <= y2)
        yInside = (y >= y1 && y < y2);
    else
        yInside = (y >= y1 || y < y2);

    if (!yInside)
        return;

    if (x1 <= x2)
    {
        for (x = x1; x < x2 && x < GBA_SCREEN_WIDTH; x++)
            sWindowMask[x] = enable;
    }
    else
    {
        for (x = 0; x < x2 && x < GBA_SCREEN_WIDTH; x++)
            sWindowMask[x] = enable;
        for (x = x1; x < GBA_SCREEN_WIDTH; x++)
            sWindowMask[x] = enable;
    }
}

static void EvalWindows(int y, u16 dispcnt)
{
    int x;
    bool8 anyWindow = (dispcnt & (DCNT_WIN0_ON | DCNT_WIN1_ON | DCNT_OBJWIN_ON)) != 0;

    if (!anyWindow)
    {
        for (x = 0; x < GBA_SCREEN_WIDTH; x++)
            sWindowMask[x] = WIN_ALL;
        return;
    }

    /* Start with "outside all windows" mask */
    {
        u8 winout = IOREG(IO_WINOUT) & 0x3F;
        for (x = 0; x < GBA_SCREEN_WIDTH; x++)
            sWindowMask[x] = winout;
    }

    /* OBJ Window (lowest window priority) */
    if (dispcnt & DCNT_OBJWIN_ON)
    {
        u8 objwinEnable = (IOREG(IO_WINOUT) >> 8) & 0x3F;
        for (x = 0; x < GBA_SCREEN_WIDTH; x++)
        {
            if (sObjWindowPixels[x])
                sWindowMask[x] = objwinEnable;
        }
    }

    /* WIN1 (medium window priority, overrides OBJWIN) */
    if (dispcnt & DCNT_WIN1_ON)
        ApplyWindowRect(y, IOREG(IO_WIN1H), IOREG(IO_WIN1V),
                         (IOREG(IO_WININ) >> 8) & 0x3F);

    /* WIN0 (highest window priority, overrides WIN1 and OBJWIN) */
    if (dispcnt & DCNT_WIN0_ON)
        ApplyWindowRect(y, IOREG(IO_WIN0H), IOREG(IO_WIN0V),
                         IOREG(IO_WININ) & 0x3F);
}

/* ================================================================
 *  Text BG rendering (one scanline)
 * ================================================================ */

static void RenderTextBgScanline(int bgNum, int y)
{
    u16 bgcnt = IOREG(IO_BG0CNT + bgNum * 2);
    u16 hofs = IOREG(IO_BG0HOFS + bgNum * 4);
    u16 vofs = IOREG(IO_BG0VOFS + bgNum * 4);
    u8 priority = PPU_BGCNT_PRIORITY(bgcnt);
    int charBase = PPU_BGCNT_CHARBASE(bgcnt) * 0x4000;
    int screenBase = PPU_BGCNT_SCREENBASE(bgcnt) * 0x800;
    int is8bpp = PPU_BGCNT_PALMODE(bgcnt);
    int screenSize = PPU_BGCNT_SCREENSIZE(bgcnt);
    int mapW = (screenSize & 1) ? 64 : 32;
    int mapH = (screenSize & 2) ? 64 : 32;
    u8 layerBit = 1 << bgNum;
    int mosaicH = 1, mosaicV = 1;
    int mosaicY, x;

    if (PPU_BGCNT_MOSAIC(bgcnt))
    {
        u16 mosaic = IOREG(IO_MOSAIC);
        mosaicH = (mosaic & 0xF) + 1;
        mosaicV = ((mosaic >> 4) & 0xF) + 1;
    }
    mosaicY = (mosaicV > 1) ? (y / mosaicV) * mosaicV : y;

    for (x = 0; x < GBA_SCREEN_WIDTH; x++)
    {
        int mx, px, py, tileX, tileY, screenBlock, mapOffset;
        u16 mapEntry;
        int tileNum, hFlip, vFlip, palNum, tpx, tpy;
        u8 colorIdx;
        u16 color;

        if (!(sWindowMask[x] & layerBit))
            continue;

        mx = (mosaicH > 1) ? (x / mosaicH) * mosaicH : x;
        px = (mx + hofs) & ((mapW * 8) - 1);
        py = (mosaicY + vofs) & ((mapH * 8) - 1);

        tileX = px >> 3;
        tileY = py >> 3;

        screenBlock = 0;
        if (mapW == 64 && tileX >= 32) { screenBlock += 1; tileX -= 32; }
        if (mapH == 64 && tileY >= 32) { screenBlock += (mapW == 64) ? 2 : 1; tileY -= 32; }

        mapOffset = screenBase + screenBlock * 0x800 + (tileY * 32 + tileX) * 2;
        mapEntry = *(const u16 *)(PPU_BG_VRAM + mapOffset);

        tileNum = mapEntry & 0x3FF;
        hFlip = (mapEntry >> 10) & 1;
        vFlip = (mapEntry >> 11) & 1;
        palNum = (mapEntry >> 12) & 0xF;

        tpx = px & 7;
        tpy = py & 7;
        if (hFlip) tpx = 7 - tpx;
        if (vFlip) tpy = 7 - tpy;

        if (is8bpp)
        {
            colorIdx = PPU_BG_VRAM[charBase + tileNum * 64 + tpy * 8 + tpx];
        }
        else
        {
            u8 byte = PPU_BG_VRAM[charBase + tileNum * 32 + tpy * 4 + (tpx >> 1)];
            colorIdx = (tpx & 1) ? (byte >> 4) : (byte & 0xF);
        }

        if (colorIdx == 0)
            continue;

        color = is8bpp ? PPU_BG_PLTT[colorIdx]
                       : PPU_BG_PLTT[palNum * 16 + colorIdx];
        CompositPixel(x, color, bgNum, priority);
    }
}

/* ================================================================
 *  Affine BG rendering (one scanline)
 * ================================================================ */

static s32 ReadAffineRefPoint(int offsetLow)
{
    u32 low = IOREG(offsetLow);
    u32 high = IOREG(offsetLow + 2);
    u32 value = low | (high << 16);
    if (value & 0x08000000)
        value |= 0xF0000000;
    return (s32)value;
}

static void RenderAffineBgScanline(int bgNum, int y)
{
    u16 bgcnt = IOREG(IO_BG0CNT + bgNum * 2);
    u8 priority = PPU_BGCNT_PRIORITY(bgcnt);
    int charBase = PPU_BGCNT_CHARBASE(bgcnt) * 0x4000;
    int screenBase = PPU_BGCNT_SCREENBASE(bgcnt) * 0x800;
    int wraparound = PPU_BGCNT_AFFINE_WRAP(bgcnt);
    int screenSize = PPU_BGCNT_SCREENSIZE(bgcnt);
    int bgPixels = 128 << screenSize;
    int mapTiles = bgPixels >> 3;
    int idx = bgNum - 2;
    s16 pa = (s16)IOREG(IO_BG2PA + idx * 0x10);
    s16 pc = (s16)IOREG(IO_BG2PC + idx * 0x10);
    s32 refX = sAffineRefX[idx];
    s32 refY = sAffineRefY[idx];
    u8 layerBit = 1 << bgNum;
    int x;

    (void)y;

    for (x = 0; x < GBA_SCREEN_WIDTH; x++)
    {
        s32 srcX, srcY;
        int tileX, tileY, mapIndex, tileNum, tileAddr;
        u8 colorIdx;

        if (!(sWindowMask[x] & layerBit))
            continue;

        srcX = (refX + pa * x) >> 8;
        srcY = (refY + pc * x) >> 8;

        if (wraparound)
        {
            srcX &= bgPixels - 1;
            srcY &= bgPixels - 1;
        }
        else if (srcX < 0 || srcX >= bgPixels || srcY < 0 || srcY >= bgPixels)
        {
            continue;
        }

        tileX = srcX >> 3;
        tileY = srcY >> 3;
        mapIndex = tileY * mapTiles + tileX;
        tileNum = PPU_BG_VRAM[screenBase + mapIndex];
        tileAddr = charBase + tileNum * 64 + (srcY & 7) * 8 + (srcX & 7);
        colorIdx = PPU_BG_VRAM[tileAddr];
        if (colorIdx == 0)
            continue;

        CompositPixel(x, PPU_BG_PLTT[colorIdx], bgNum, priority);
    }
}

/* ================================================================
 *  Sprite tile pixel lookup (shared by normal and affine paths)
 * ================================================================ */

static u8 LookupSpritePixel(int tileNum, int is8bpp, int obj1d,
                             int sprW, int tpx, int tpy)
{
    int tilePx = tpx >> 3;
    int tilePy = tpy >> 3;
    int tile;
    int subX = tpx & 7;
    int subY = tpy & 7;

    if (obj1d)
    {
        int tilesPerRow = sprW >> 3;
        if (is8bpp)
            tile = tileNum + (tilePy * tilesPerRow + tilePx) * 2;
        else
            tile = tileNum + tilePy * tilesPerRow + tilePx;
    }
    else
    {
        if (is8bpp)
            tile = tileNum + tilePy * 32 + tilePx * 2;
        else
            tile = tileNum + tilePy * 32 + tilePx;
    }

    if (is8bpp)
    {
        return PPU_OBJ_VRAM[tile * 32 + subY * 8 + subX];
    }
    else
    {
        u8 byte = PPU_OBJ_VRAM[tile * 32 + subY * 4 + (subX >> 1)];
        return (subX & 1) ? (byte >> 4) : (byte & 0xF);
    }
}

/* ================================================================
 *  OBJ Window sprite pre-pass
 * ================================================================ */

static void RenderObjWindowScanline(int y, u16 dispcnt)
{
    int obj1d = (dispcnt & DCNT_OBJ_1D) != 0;
    int i;

    for (i = 127; i >= 0; i--)
    {
        u16 attr0 = OAM_ATTR0(i);
        u16 attr1 = OAM_ATTR1(i);
        u16 attr2 = OAM_ATTR2(i);
        int affineMode = ATTR0_AFFINE(attr0);
        int objMode = ATTR0_OBJMODE(attr0);
        int shape, size, sprW, sprH, sprY, sprX;
        int is8bpp, tileNum;
        int boundsW, boundsH;
        int relY, px;
        int useMosaic;
        u16 mosaic;
        int objMosaicH = 1, objMosaicV = 1;

        if (affineMode == 2) continue;
        if (objMode != 2) continue;

        shape = ATTR0_SHAPE(attr0);
        size = ATTR1_SIZE(attr1);
        if (shape > 2) continue;

        sprW = sSpriteWidth[shape][size];
        sprH = sSpriteHeight[shape][size];
        sprY = ATTR0_Y(attr0);
        sprX = ATTR1_X(attr1);
        if (sprX >= 256) sprX -= 512;
        if (sprY >= 160) sprY -= 256;

        is8bpp = ATTR0_BPP(attr0);
        tileNum = ATTR2_TILE(attr2);
        useMosaic = ATTR0_MOSAIC(attr0);

        mosaic = IOREG(IO_MOSAIC);
        objMosaicH = ((mosaic >> 8) & 0xF) + 1;
        objMosaicV = ((mosaic >> 12) & 0xF) + 1;

        boundsW = (affineMode == 3) ? sprW * 2 : sprW;
        boundsH = (affineMode == 3) ? sprH * 2 : sprH;

        relY = y - sprY;
        if (useMosaic && objMosaicV > 1)
            relY = (relY / objMosaicV) * objMosaicV;
        if (relY < 0 || relY >= boundsH) continue;

        if (affineMode == 1 || affineMode == 3)
        {
            int affIdx = ATTR1_AFFINE_IDX(attr1);
            s16 pa = OAM_AFFINE_PA(affIdx);
            s16 pb = OAM_AFFINE_PB(affIdx);
            s16 pc = OAM_AFFINE_PC(affIdx);
            s16 pd = OAM_AFFINE_PD(affIdx);
            int halfW = sprW >> 1;
            int halfH = sprH >> 1;
            int cX = boundsW >> 1;
            int cY = boundsH >> 1;

            for (px = 0; px < boundsW; px++)
            {
                int screenX = sprX + px;
                int mx, texX, texY;
                u8 colorIdx;
                if (screenX < 0 || screenX >= GBA_SCREEN_WIDTH) continue;
                mx = (useMosaic && objMosaicH > 1) ? (px / objMosaicH) * objMosaicH : px;
                texX = ((pa * (mx - cX) + pb * (relY - cY)) >> 8) + halfW;
                texY = ((pc * (mx - cX) + pd * (relY - cY)) >> 8) + halfH;
                if (texX < 0 || texX >= sprW || texY < 0 || texY >= sprH) continue;
                colorIdx = LookupSpritePixel(tileNum, is8bpp, obj1d, sprW, texX, texY);
                if (colorIdx != 0)
                    sObjWindowPixels[screenX] = TRUE;
            }
        }
        else
        {
            int hFlip = ATTR1_HFLIP(attr1);
            int vFlip = ATTR1_VFLIP(attr1);
            int tpy = vFlip ? (sprH - 1 - relY) : relY;

            for (px = 0; px < sprW; px++)
            {
                int screenX = sprX + px;
                int mx, tpx;
                u8 colorIdx;
                if (screenX < 0 || screenX >= GBA_SCREEN_WIDTH) continue;
                mx = (useMosaic && objMosaicH > 1) ? (px / objMosaicH) * objMosaicH : px;
                tpx = hFlip ? (sprW - 1 - mx) : mx;
                colorIdx = LookupSpritePixel(tileNum, is8bpp, obj1d, sprW, tpx, tpy);
                if (colorIdx != 0)
                    sObjWindowPixels[screenX] = TRUE;
            }
        }
    }
}

/* ================================================================
 *  Sprite rendering (one scanline, one priority level)
 * ================================================================ */

static void RenderSpritesScanline(int y, u16 dispcnt, int targetPrio)
{
    int obj1d = (dispcnt & DCNT_OBJ_1D) != 0;
    int objMosaicH = 1, objMosaicV = 1;
    u16 mosaic;
    int i;

    mosaic = IOREG(IO_MOSAIC);
    objMosaicH = ((mosaic >> 8) & 0xF) + 1;
    objMosaicV = ((mosaic >> 12) & 0xF) + 1;

    for (i = 127; i >= 0; i--)
    {
        u16 attr0 = OAM_ATTR0(i);
        u16 attr1 = OAM_ATTR1(i);
        u16 attr2 = OAM_ATTR2(i);
        int affineMode = ATTR0_AFFINE(attr0);
        int objMode = ATTR0_OBJMODE(attr0);
        int shape, size, sprW, sprH, sprY, sprX;
        int is8bpp, tileNum, priority, palNum;
        int boundsW, boundsH;
        int relY, px;
        int useMosaic;

        if (affineMode == 2) continue;
        if (objMode == 2) continue;    /* OBJ window handled separately */
        if (objMode == 3) continue;    /* Prohibited / not rendered on hardware */

        priority = ATTR2_PRIORITY(attr2);
        if (priority != targetPrio) continue;

        shape = ATTR0_SHAPE(attr0);
        size = ATTR1_SIZE(attr1);
        if (shape > 2) continue;

        sprW = sSpriteWidth[shape][size];
        sprH = sSpriteHeight[shape][size];
        sprY = ATTR0_Y(attr0);
        sprX = ATTR1_X(attr1);
        if (sprX >= 256) sprX -= 512;
        if (sprY >= 160) sprY -= 256;

        is8bpp = ATTR0_BPP(attr0);
        tileNum = ATTR2_TILE(attr2);
        palNum = ATTR2_PALETTE(attr2);
        useMosaic = ATTR0_MOSAIC(attr0);

        boundsW = (affineMode == 3) ? sprW * 2 : sprW;
        boundsH = (affineMode == 3) ? sprH * 2 : sprH;

        relY = y - sprY;
        if (useMosaic && objMosaicV > 1)
            relY = (relY / objMosaicV) * objMosaicV;
        if (relY < 0 || relY >= boundsH) continue;

        if (affineMode == 1 || affineMode == 3)
        {
            /* Affine sprite */
            int affIdx = ATTR1_AFFINE_IDX(attr1);
            s16 pa = OAM_AFFINE_PA(affIdx);
            s16 pb = OAM_AFFINE_PB(affIdx);
            s16 pc = OAM_AFFINE_PC(affIdx);
            s16 pd = OAM_AFFINE_PD(affIdx);
            int halfW = sprW >> 1;
            int halfH = sprH >> 1;
            int cX = boundsW >> 1;
            int cY = boundsH >> 1;

            for (px = 0; px < boundsW; px++)
            {
                int screenX = sprX + px;
                int mx, texX, texY;
                u8 colorIdx;
                u16 color;
                if (screenX < 0 || screenX >= GBA_SCREEN_WIDTH) continue;
                if (!(sWindowMask[screenX] & WIN_OBJ)) continue;

                mx = (useMosaic && objMosaicH > 1) ? (px / objMosaicH) * objMosaicH : px;
                texX = ((pa * (mx - cX) + pb * (relY - cY)) >> 8) + halfW;
                texY = ((pc * (mx - cX) + pd * (relY - cY)) >> 8) + halfH;
                if (texX < 0 || texX >= sprW || texY < 0 || texY >= sprH) continue;

                colorIdx = LookupSpritePixel(tileNum, is8bpp, obj1d, sprW, texX, texY);
                if (colorIdx == 0) continue;

                color = is8bpp ? PPU_OBJ_PLTT[colorIdx]
                               : PPU_OBJ_PLTT[palNum * 16 + colorIdx];

                if (objMode == 1)
                    CompositPixelObjSemiTrans(screenX, color, priority);
                else
                    CompositPixel(screenX, color, LAYER_OBJ, priority);
            }
        }
        else
        {
            /* Normal (non-affine) sprite */
            int hFlip = ATTR1_HFLIP(attr1);
            int vFlip = ATTR1_VFLIP(attr1);
            int tpy = vFlip ? (sprH - 1 - relY) : relY;

            for (px = 0; px < sprW; px++)
            {
                int screenX = sprX + px;
                int mx, tpx;
                u8 colorIdx;
                u16 color;
                if (screenX < 0 || screenX >= GBA_SCREEN_WIDTH) continue;
                if (!(sWindowMask[screenX] & WIN_OBJ)) continue;

                mx = (useMosaic && objMosaicH > 1) ? (px / objMosaicH) * objMosaicH : px;
                tpx = hFlip ? (sprW - 1 - mx) : mx;
                colorIdx = LookupSpritePixel(tileNum, is8bpp, obj1d, sprW, tpx, tpy);
                if (colorIdx == 0) continue;

                color = is8bpp ? PPU_OBJ_PLTT[colorIdx]
                               : PPU_OBJ_PLTT[palNum * 16 + colorIdx];

                if (objMode == 1)
                    CompositPixelObjSemiTrans(screenX, color, priority);
                else
                    CompositPixel(screenX, color, LAYER_OBJ, priority);
            }
        }
    }
}

/* ================================================================
 *  Blending
 * ================================================================ */

static inline u16 BlendAlpha(u16 a, u16 b, int eva, int evb)
{
    int r = ((a & 0x1F) * eva + (b & 0x1F) * evb) >> 4;
    int g = (((a >> 5) & 0x1F) * eva + ((b >> 5) & 0x1F) * evb) >> 4;
    int bl = (((a >> 10) & 0x1F) * eva + ((b >> 10) & 0x1F) * evb) >> 4;
    if (r > 31) r = 31;
    if (g > 31) g = 31;
    if (bl > 31) bl = 31;
    return (u16)(r | (g << 5) | (bl << 10));
}

static inline u16 BlendBrighten(u16 c, int evy)
{
    int r = c & 0x1F;
    int g = (c >> 5) & 0x1F;
    int b = (c >> 10) & 0x1F;
    r += ((31 - r) * evy) >> 4;
    g += ((31 - g) * evy) >> 4;
    b += ((31 - b) * evy) >> 4;
    return (u16)(r | (g << 5) | (b << 10));
}

static inline u16 BlendDarken(u16 c, int evy)
{
    int r = c & 0x1F;
    int g = (c >> 5) & 0x1F;
    int b = (c >> 10) & 0x1F;
    r -= (r * evy) >> 4;
    g -= (g * evy) >> 4;
    b -= (b * evy) >> 4;
    return (u16)(r | (g << 5) | (b << 10));
}

static void BlendScanline(void)
{
    u16 bldcnt = IOREG(IO_BLDCNT);
    int mode = (bldcnt & BLD_MODE_MASK);
    u8 firstTarget = bldcnt & 0x3F;
    u8 secondTarget = (bldcnt >> 8) & 0x3F;
    u16 bldalpha = IOREG(IO_BLDALPHA);
    int eva = bldalpha & 0x1F;
    int evb = (bldalpha >> 8) & 0x1F;
    u16 bldy_reg = IOREG(IO_BLDY);
    int evy = bldy_reg & 0x1F;
    int x;

    if (eva > 16) eva = 16;
    if (evb > 16) evb = 16;
    if (evy > 16) evy = 16;

    for (x = 0; x < GBA_SCREEN_WIDTH; x++)
    {
        u8 topLayer = sScanTopLayer[x];
        u8 botLayer = sScanBotLayer[x];
        bool8 sfxEnabled = (sWindowMask[x] & WIN_SFX) != 0;

        /*
         * Semi-transparent OBJ: force alpha blend regardless of BLDCNT
         * first-target bits, but second target must still be set.
         */
        if (sScanObjSemiTrans[x] && sfxEnabled)
        {
            u8 blendLayer = sScanBotNonObjLayer[x];
            if (secondTarget & (1 << blendLayer))
                sScanTop[x] = BlendAlpha(sScanTop[x], sScanBotNonObj[x], eva, evb);
            continue;
        }

        if (!sfxEnabled)
            continue;
        if (!(firstTarget & (1 << topLayer)))
            continue;

        switch (mode)
        {
        case BLD_MODE_ALPHA:
            if (secondTarget & (1 << botLayer))
                sScanTop[x] = BlendAlpha(sScanTop[x], sScanBot[x], eva, evb);
            break;
        case BLD_MODE_LIGHT:
            if (evy > 0)
                sScanTop[x] = BlendBrighten(sScanTop[x], evy);
            break;
        case BLD_MODE_DARK:
            if (evy > 0)
                sScanTop[x] = BlendDarken(sScanTop[x], evy);
            break;
        }
    }
}

/* ================================================================
 *  Scanline commit (BGR555 -> ARGB8888)
 * ================================================================ */

static void CommitScanline(int y)
{
    u32 *row = &sFramebuffer[y * GBA_SCREEN_WIDTH];
    int x;

    for (x = 0; x < GBA_SCREEN_WIDTH; x++)
        row[x] = Bgr555ToArgb8888(sScanTop[x]);
}

/* ================================================================
 *  Public API
 * ================================================================ */

void HostRendererInit(void)
{
    memset(sFramebuffer, 0, sizeof(sFramebuffer));
    sAffineRefX[0] = sAffineRefX[1] = 0;
    sAffineRefY[0] = sAffineRefY[1] = 0;
    sInitialized = TRUE;
}

void HostRendererStartFrame(void)
{
    /* Latch affine BG reference points from written register values.
     * On real GBA hardware, these are latched at the start of VBlank
     * and then advanced by PB/PD each scanline during display. */
    sAffineRefX[0] = ReadAffineRefPoint(IO_BG2X_L);
    sAffineRefY[0] = ReadAffineRefPoint(IO_BG2Y_L);
    sAffineRefX[1] = ReadAffineRefPoint(IO_BG2X_L + 0x10);
    sAffineRefY[1] = ReadAffineRefPoint(IO_BG2Y_L + 0x10);
}

void HostRendererRenderScanline(u16 y)
{
    u16 dispcnt = IOREG(IO_DISPCNT);
    int mode, prio, bg;

    if (y >= GBA_SCREEN_HEIGHT)
        return;

    /* Forced blank: white */
    if (dispcnt & DCNT_FORCED_BLK)
    {
        u32 *row = &sFramebuffer[y * GBA_SCREEN_WIDTH];
        int x;
        for (x = 0; x < GBA_SCREEN_WIDTH; x++)
            row[x] = 0xFFFFFFFF;
        return;
    }

    mode = dispcnt & DCNT_MODE_MASK;
    if (mode > 2)
        return;

    /* Clear scanline to backdrop color */
    ClearScanline(PPU_BG_PLTT[0]);

    /* Clear OBJ window pixels */
    memset(sObjWindowPixels, 0, sizeof(sObjWindowPixels));

    /* Pre-render OBJ window sprites for window evaluation */
    if ((dispcnt & DCNT_OBJ_ON) && (dispcnt & DCNT_OBJWIN_ON))
        RenderObjWindowScanline(y, dispcnt);

    /* Evaluate window masks for this scanline */
    EvalWindows(y, dispcnt);

    /*
     * Render BGs and OBJs in priority order (back to front).
     * Within the same priority: BG3 behind BG2 behind BG1 behind BG0,
     * then OBJ on top (OBJ wins ties with BG at the same priority).
     * Within OBJ: OAM 127 drawn first (lowest precedence),
     * OAM 0 drawn last (highest precedence).
     */
    for (prio = 3; prio >= 0; prio--)
    {
        for (bg = 3; bg >= 0; bg--)
        {
            u16 bgEnable = DCNT_BG0_ON << bg;
            u16 bgcnt;

            if (!(dispcnt & bgEnable))
                continue;
            if (mode == 1 && bg == 3)
                continue;
            if (mode == 2 && bg < 2)
                continue;

            bgcnt = IOREG(IO_BG0CNT + bg * 2);
            if (PPU_BGCNT_PRIORITY(bgcnt) != (u8)prio)
                continue;

            if ((mode == 1 && bg == 2) || mode == 2)
                RenderAffineBgScanline(bg, y);
            else
                RenderTextBgScanline(bg, y);
        }

        if (dispcnt & DCNT_OBJ_ON)
            RenderSpritesScanline(y, dispcnt, prio);
    }

    /* Apply blending effects */
    BlendScanline();

    /* Convert BGR555 to ARGB8888 and write to framebuffer */
    CommitScanline(y);

    /* Advance affine BG internal reference points for next scanline */
    sAffineRefX[0] += (s16)IOREG(IO_BG2PB);
    sAffineRefY[0] += (s16)IOREG(IO_BG2PD);
    sAffineRefX[1] += (s16)IOREG(IO_BG2PB + 0x10);
    sAffineRefY[1] += (s16)IOREG(IO_BG2PD + 0x10);
}

void HostRendererRenderFrame(void)
{
    int y;
    HostRendererStartFrame();
    for (y = 0; y < GBA_SCREEN_HEIGHT; y++)
        HostRendererRenderScanline(y);
}

const u32 *HostRendererGetFramebuffer(void)
{
    return sFramebuffer;
}
