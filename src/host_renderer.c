/*
 * host_renderer.c — GBA Mode 0 software PPU for pokefirered-native
 *
 * Reads VRAM, OAM, PLTT, and I/O registers from the memory-mapped GBA
 * address space and composites a 240x160 ARGB8888 framebuffer.
 *
 * Supports:
 *   - Mode 0 text backgrounds (4bpp and 8bpp tiles)
 *   - Sprites/OBJ (4bpp and 8bpp, with h/v flip, priority)
 *   - BG/OBJ palette lookup (BGR555 → ARGB8888)
 *   - BG scroll (HOFS/VOFS)
 *   - BG and OBJ priority sorting
 *   - DISPCNT BG/OBJ enable flags
 *   - Forced blank
 *   - Basic alpha blend (BLD) for fade-to-black/white
 *
 * Does NOT yet support:
 *   - Affine BGs (Mode 1/2)
 *   - Bitmap modes (Mode 3/4/5)
 *   - Mosaic
 *   - Windows
 *   - Per-pixel OBJ blending
 *   - Affine sprites (rotation/scaling)
 */

#include <stdio.h>
#include <string.h>

#include "host_renderer.h"
#include "gba/gba.h"

/*
 * GBA hardware access.
 *
 * Use local names prefixed with PPU_ to avoid colliding with the
 * upstream defines in gba/defines.h and gba/io_reg.h (those are
 * for *setting* register fields; ours are for *reading/extracting*).
 */

#define PPU_VRAM_BASE       ((const u8 *)VRAM)
#define PPU_OAM_BASE        ((const u8 *)OAM)
#define PPU_PLTT_BASE       ((const u16 *)PLTT)
#define PPU_IO_BASE         ((volatile u16 *)REG_BASE)

#define PPU_BG_PLTT         (PPU_PLTT_BASE)
#define PPU_OBJ_PLTT        (PPU_PLTT_BASE + 256)

#define PPU_BG_VRAM         (PPU_VRAM_BASE)
#define PPU_OBJ_VRAM        (PPU_VRAM_BASE + 0x10000)

/* I/O register read helper (offset in bytes) */
#define IOREG(off)      (PPU_IO_BASE[(off) >> 1])
#define IO_DISPCNT      0x00
#define IO_BG0CNT       0x08
#define IO_BG0HOFS      0x10
#define IO_BG0VOFS      0x12
#define IO_BLDCNT       0x50
#define IO_BLDALPHA     0x52
#define IO_BLDY         0x54

/* DISPCNT bits (local names to avoid upstream collision) */
#define DCNT_MODE_MASK  0x0007
#define DCNT_OBJ_1D     0x0040
#define DCNT_FORCED_BLK 0x0080
#define DCNT_BG0_ON     0x0100
#define DCNT_BG1_ON     0x0200
#define DCNT_BG2_ON     0x0400
#define DCNT_BG3_ON     0x0800
#define DCNT_OBJ_ON     0x1000

/* BLDCNT bits */
#define BLD_MODE_MASK   0x00C0
#define BLD_MODE_OFF    0x0000
#define BLD_MODE_ALPHA  0x0040
#define BLD_MODE_LIGHT  0x0080
#define BLD_MODE_DARK   0x00C0

/* BGCNT field extraction (read direction — upstream macros are write direction) */
#define PPU_BGCNT_PRIORITY(v)       ((v) & 3)
#define PPU_BGCNT_CHARBASE(v)       (((v) >> 2) & 3)
#define PPU_BGCNT_PALMODE(v)        (((v) >> 7) & 1)   /* 0=4bpp, 1=8bpp */
#define PPU_BGCNT_SCREENBASE(v)     (((v) >> 8) & 0x1F)
#define PPU_BGCNT_SCREENSIZE(v)     (((v) >> 14) & 3)

/* OAM attribute access */
#define OAM_ATTR0(entry)  (((const u16 *)(PPU_OAM_BASE))[(entry) * 4 + 0])
#define OAM_ATTR1(entry)  (((const u16 *)(PPU_OAM_BASE))[(entry) * 4 + 1])
#define OAM_ATTR2(entry)  (((const u16 *)(PPU_OAM_BASE))[(entry) * 4 + 2])

#define ATTR0_Y(a0)         ((a0) & 0xFF)
#define ATTR0_AFFINE(a0)    (((a0) >> 8) & 3)
#define ATTR0_OBJMODE(a0)   (((a0) >> 10) & 3)
#define ATTR0_BPP(a0)       (((a0) >> 13) & 1)    /* 0=4bpp, 1=8bpp */
#define ATTR0_SHAPE(a0)     (((a0) >> 14) & 3)

#define ATTR1_X(a1)         ((a1) & 0x1FF)
#define ATTR1_HFLIP(a1)     (((a1) >> 12) & 1)
#define ATTR1_VFLIP(a1)     (((a1) >> 13) & 1)
#define ATTR1_SIZE(a1)      (((a1) >> 14) & 3)

#define ATTR2_TILE(a2)      ((a2) & 0x3FF)
#define ATTR2_PRIORITY(a2)  (((a2) >> 10) & 3)
#define ATTR2_PALETTE(a2)   (((a2) >> 12) & 0xF)

/* Sprite size lookup: [shape][size] → {width, height} */
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

/* ---------- Internal state ---------- */

static u32 sFramebuffer[GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT];
static bool8 sInitialized;

/* Per-pixel priority tracking for BG/OBJ compositing.
 * Lower value = higher priority (drawn on top). */
static u8 sPriorityMap[GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT];

/* ---------- Color conversion ---------- */

static inline u32 Bgr555ToArgb8888(u16 bgr)
{
    u32 r = (bgr & 0x1F);
    u32 g = ((bgr >> 5) & 0x1F);
    u32 b = ((bgr >> 10) & 0x1F);

    /* Expand 5-bit to 8-bit: (c << 3) | (c >> 2) */
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);

    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

/* ---------- BG rendering (Mode 0, text) ---------- */

static void RenderTextBg(int bgNum, u16 dispcnt)
{
    u16 bgcnt = IOREG(IO_BG0CNT + bgNum * 2);
    u16 hofs = IOREG(IO_BG0HOFS + bgNum * 4);
    u16 vofs = IOREG(IO_BG0VOFS + bgNum * 4);
    u8 priority = PPU_BGCNT_PRIORITY(bgcnt);
    int charBase = PPU_BGCNT_CHARBASE(bgcnt) * 0x4000;
    int screenBase = PPU_BGCNT_SCREENBASE(bgcnt) * 0x800;
    int is8bpp = PPU_BGCNT_PALMODE(bgcnt);
    int screenSize = PPU_BGCNT_SCREENSIZE(bgcnt);

    /* Text BG map dimensions in tiles */
    int mapW = (screenSize & 1) ? 64 : 32;
    int mapH = (screenSize & 2) ? 64 : 32;

    int x, y;
    for (y = 0; y < GBA_SCREEN_HEIGHT; y++)
    {
        for (x = 0; x < GBA_SCREEN_WIDTH; x++)
        {
            int px = (x + hofs) & ((mapW * 8) - 1);
            int py = (y + vofs) & ((mapH * 8) - 1);

            /* Which tile in the map? */
            int tileX = px >> 3;
            int tileY = py >> 3;

            /* For maps > 32 tiles wide/tall, select the right 32x32 sub-screen */
            int screenBlock = 0;
            if (mapW == 64 && tileX >= 32)
            {
                screenBlock += 1;
                tileX -= 32;
            }
            if (mapH == 64 && tileY >= 32)
            {
                screenBlock += (mapW == 64) ? 2 : 1;
                tileY -= 32;
            }

            int mapOffset = screenBase + screenBlock * 0x800 + (tileY * 32 + tileX) * 2;
            u16 mapEntry = *(const u16 *)(PPU_BG_VRAM + mapOffset);

            int tileNum = mapEntry & 0x3FF;
            int hFlip = (mapEntry >> 10) & 1;
            int vFlip = (mapEntry >> 11) & 1;
            int palNum = (mapEntry >> 12) & 0xF;

            /* Pixel within tile */
            int tpx = px & 7;
            int tpy = py & 7;
            if (hFlip) tpx = 7 - tpx;
            if (vFlip) tpy = 7 - tpy;

            u8 colorIdx;
            if (is8bpp)
            {
                int tileAddr = charBase + tileNum * 64 + tpy * 8 + tpx;
                colorIdx = PPU_BG_VRAM[tileAddr];
            }
            else
            {
                int tileAddr = charBase + tileNum * 32 + tpy * 4 + (tpx >> 1);
                u8 byte = PPU_BG_VRAM[tileAddr];
                colorIdx = (tpx & 1) ? (byte >> 4) : (byte & 0xF);
            }

            /* Color index 0 is transparent */
            if (colorIdx == 0)
                continue;

            int fbIdx = y * GBA_SCREEN_WIDTH + x;

            /* Priority check: only draw if this BG has equal or higher priority */
            if (priority < sPriorityMap[fbIdx])
            {
                u16 color;
                if (is8bpp)
                    color = PPU_BG_PLTT[colorIdx];
                else
                    color = PPU_BG_PLTT[palNum * 16 + colorIdx];

                sFramebuffer[fbIdx] = Bgr555ToArgb8888(color);
                sPriorityMap[fbIdx] = priority;
            }
        }
    }
}

/* ---------- OBJ/sprite rendering ---------- */

static void RenderSprites(u16 dispcnt)
{
    int obj1d = (dispcnt & DCNT_OBJ_1D) != 0;
    int i;

    /* Render sprites in reverse order (OAM 127 first, 0 last = highest priority) */
    for (i = 127; i >= 0; i--)
    {
        u16 attr0 = OAM_ATTR0(i);
        u16 attr1 = OAM_ATTR1(i);
        u16 attr2 = OAM_ATTR2(i);

        int affineMode = ATTR0_AFFINE(attr0);

        /* Skip disabled sprites (affine mode 2 = hidden) */
        if (affineMode == 2)
            continue;

        /* Skip affine sprites for now (mode 1 or 3) */
        if (affineMode == 1 || affineMode == 3)
            continue;

        int shape = ATTR0_SHAPE(attr0);
        int size = ATTR1_SIZE(attr1);
        if (shape > 2) continue;

        int sprW = sSpriteWidth[shape][size];
        int sprH = sSpriteHeight[shape][size];

        int sprY = ATTR0_Y(attr0);
        int sprX = ATTR1_X(attr1);

        /* Sign-extend X from 9 bits */
        if (sprX >= 256) sprX -= 512;
        /* Wrap Y */
        if (sprY >= 160) sprY -= 256;

        int is8bpp = ATTR0_BPP(attr0);
        int hFlip = ATTR1_HFLIP(attr1);
        int vFlip = ATTR1_VFLIP(attr1);
        int tileNum = ATTR2_TILE(attr2);
        int priority = ATTR2_PRIORITY(attr2);
        int palNum = ATTR2_PALETTE(attr2);

        int py, px;
        for (py = 0; py < sprH; py++)
        {
            int screenY = sprY + py;
            if (screenY < 0 || screenY >= GBA_SCREEN_HEIGHT)
                continue;

            for (px = 0; px < sprW; px++)
            {
                int screenX = sprX + px;
                if (screenX < 0 || screenX >= GBA_SCREEN_WIDTH)
                    continue;

                int tpx = hFlip ? (sprW - 1 - px) : px;
                int tpy = vFlip ? (sprH - 1 - py) : py;

                /* Which tile within the sprite? */
                int tilePx = tpx >> 3;
                int tilePy = tpy >> 3;

                int tile;
                if (obj1d)
                {
                    /* 1D mapping: tiles are sequential */
                    int tilesPerRow = sprW >> 3;
                    tile = tileNum + tilePy * tilesPerRow + tilePx;
                    if (is8bpp)
                        tile = tileNum + (tilePy * tilesPerRow + tilePx) * 2;
                }
                else
                {
                    /* 2D mapping: 32 tiles per row in VRAM */
                    tile = tileNum + tilePy * 32 + tilePx;
                    if (is8bpp)
                        tile = tileNum + tilePy * 32 + tilePx * 2;
                }

                int subX = tpx & 7;
                int subY = tpy & 7;

                u8 colorIdx;
                if (is8bpp)
                {
                    int addr = tile * 32 + subY * 8 + subX;
                    colorIdx = PPU_OBJ_VRAM[addr];
                }
                else
                {
                    int addr = tile * 32 + subY * 4 + (subX >> 1);
                    u8 byte = PPU_OBJ_VRAM[addr];
                    colorIdx = (subX & 1) ? (byte >> 4) : (byte & 0xF);
                }

                if (colorIdx == 0)
                    continue;

                int fbIdx = screenY * GBA_SCREEN_WIDTH + screenX;

                if (priority <= sPriorityMap[fbIdx])
                {
                    u16 color;
                    if (is8bpp)
                        color = PPU_OBJ_PLTT[colorIdx];
                    else
                        color = PPU_OBJ_PLTT[palNum * 16 + colorIdx];

                    sFramebuffer[fbIdx] = Bgr555ToArgb8888(color);
                    sPriorityMap[fbIdx] = priority;
                }
            }
        }
    }
}

/* ---------- Brightness/fade (BLDY) ---------- */

static void ApplyBrightness(void)
{
    u16 bldcnt = IOREG(IO_BLDCNT);
    u16 bldy_reg = IOREG(IO_BLDY);
    int mode = bldcnt & BLD_MODE_MASK;

    if (mode == BLD_MODE_OFF)
        return;

    int evy = bldy_reg & 0x1F;
    if (evy > 16) evy = 16;
    if (evy == 0) return;

    /* Which layers are first-target? (bits 0-5 of BLDCNT) */
    /* For simplicity, apply to entire framebuffer if any BG/OBJ target is set */
    u8 targets = bldcnt & 0x3F;
    if (targets == 0)
        return;

    int i;
    for (i = 0; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; i++)
    {
        u32 pixel = sFramebuffer[i];
        u32 r = (pixel >> 16) & 0xFF;
        u32 g = (pixel >> 8) & 0xFF;
        u32 b = pixel & 0xFF;

        if (mode == BLD_MODE_LIGHT)
        {
            /* Fade to white */
            r += ((255 - r) * evy) >> 4;
            g += ((255 - g) * evy) >> 4;
            b += ((255 - b) * evy) >> 4;
        }
        else if (mode == BLD_MODE_DARK)
        {
            /* Fade to black */
            r -= (r * evy) >> 4;
            g -= (g * evy) >> 4;
            b -= (b * evy) >> 4;
        }

        sFramebuffer[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
}

/* ---------- Public API ---------- */

void HostRendererInit(void)
{
    memset(sFramebuffer, 0, sizeof(sFramebuffer));
    sInitialized = TRUE;
}

void HostRendererRenderFrame(void)
{
    u16 dispcnt = IOREG(IO_DISPCNT);

    /* Clear to backdrop color (palette entry 0) */
    u32 backdrop = Bgr555ToArgb8888(PPU_BG_PLTT[0]);
    {
        int i;
        for (i = 0; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; i++)
        {
            sFramebuffer[i] = backdrop;
            sPriorityMap[i] = 4; /* lowest priority = 4 (behind everything) */
        }
    }

    /* Forced blank: output white */
    if (dispcnt & DCNT_FORCED_BLK)
    {
        memset(sFramebuffer, 0xFF, sizeof(sFramebuffer));
        return;
    }

    /* Only support Mode 0 for now */
    int mode = dispcnt & DCNT_MODE_MASK;
    if (mode != 0)
        return;

    /*
     * Render BGs in priority order (3 = lowest, 0 = highest).
     * Within the same priority, higher-numbered BGs go behind.
     */
    int prio;
    for (prio = 3; prio >= 0; prio--)
    {
        int bg;
        for (bg = 3; bg >= 0; bg--)
        {
            u16 bgEnable = DCNT_BG0_ON << bg;
            if (!(dispcnt & bgEnable))
                continue;

            u16 bgcnt = IOREG(IO_BG0CNT + bg * 2);
            if (PPU_BGCNT_PRIORITY(bgcnt) != (u8)prio)
                continue;

            RenderTextBg(bg, dispcnt);
        }
    }

    /* Render sprites on top */
    if (dispcnt & DCNT_OBJ_ON)
        RenderSprites(dispcnt);

    /* Apply brightness effects */
    ApplyBrightness();
}

const u32 *HostRendererGetFramebuffer(void)
{
    return sFramebuffer;
}
