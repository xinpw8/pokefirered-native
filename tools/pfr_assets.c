/*
 * pfr_assets.c — Asset compiler for pokefirered-native
 *
 * Converts upstream PNG/PAL/BIN source assets into the intermediate
 * formats that the INCBIN macros reference (.4bpp, .8bpp, .gbapal,
 * .4bpp.lz, .bin.lz, etc.).
 *
 * This replaces the upstream gbagfx tool without requiring libpng-dev
 * by using stb_image.h for PNG decoding.
 *
 * Usage:
 *   pfr_assets png2gba <input.png> <output.4bpp|.8bpp> [--num-tiles N]
 *   pfr_assets pal2gba <input.pal> <output.gbapal>
 *   pfr_assets lz77    <input>     <output.lz>
 *   pfr_assets batch   <upstream_dir> <output_dir>
 *       Converts all title_screen/ and intro/ assets in one shot.
 */

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

/* ---------- GBA palette color ---------- */

static uint16_t rgb_to_bgr555(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r >> 3) & 0x1F)
         | (((g >> 3) & 0x1F) << 5)
         | (((b >> 3) & 0x1F) << 10);
}

/* ---------- File I/O helpers ---------- */

static uint8_t *read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(sz);
    if (buf) *out_size = fread(buf, 1, sz, f);
    fclose(f);
    return buf;
}

static int write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return -1; }
    fwrite(data, 1, size, f);
    fclose(f);
    return 0;
}

/* ---------- PNG to 4bpp/8bpp tile data ---------- */

/*
 * GBA tiles are 8x8 pixels. In 4bpp mode each pixel is 4 bits (2 per byte),
 * so a tile is 32 bytes. In 8bpp mode each pixel is 1 byte, so 64 bytes.
 *
 * The PNG must be a paletted/indexed image. We extract the palette index
 * for each pixel and pack into GBA tile format.
 *
 * If the PNG is RGB/RGBA, we quantize by finding the nearest palette
 * entry (first 16 or 256 colors, derived from the image).
 */

static int cmd_png2gba(const char *input, const char *output, int num_tiles, int bpp)
{
    size_t png_size;
    uint8_t *png_data = read_file(input, &png_size);
    if (!png_data) { fprintf(stderr, "Cannot open %s\n", input); return 1; }

    int w, h, channels;
    uint8_t *pixels = stbi_load_from_memory(png_data, png_size, &w, &h, &channels, 0);
    free(png_data);
    if (!pixels) { fprintf(stderr, "PNG decode failed: %s\n", input); return 1; }

    /* We need indexed pixel values. If the image has 1 channel, treat as
     * grayscale indices. If RGB/RGBA, we build a simple palette and quantize. */
    int total_pixels = w * h;
    uint8_t *indices = NULL;

    if (channels == 1) {
        /* Grayscale — treat raw values as palette indices */
        indices = pixels; /* steal the buffer */
    } else {
        /* RGB or RGBA — build a palette from unique colors, then index */
        uint16_t palette[256];
        int pal_count = 0;
        indices = malloc(total_pixels);

        for (int i = 0; i < total_pixels; i++) {
            uint8_t r = pixels[i * channels + 0];
            uint8_t g = pixels[i * channels + 1];
            uint8_t b = pixels[i * channels + 2];
            uint16_t bgr = rgb_to_bgr555(r, g, b);

            /* Find or add to palette */
            int found = -1;
            for (int j = 0; j < pal_count; j++) {
                if (palette[j] == bgr) { found = j; break; }
            }
            if (found < 0) {
                if (pal_count >= (bpp == 4 ? 16 : 256)) {
                    /* Overflow — clamp to last entry */
                    found = pal_count - 1;
                } else {
                    found = pal_count;
                    palette[pal_count++] = bgr;
                }
            }
            indices[i] = (uint8_t)found;
        }

        stbi_image_free(pixels);
        pixels = NULL;
    }

    /* Pack into GBA tile format */
    int tiles_x = w / 8;
    int tiles_y = h / 8;
    int total_tiles_available = tiles_x * tiles_y;
    int tiles_to_write = num_tiles > 0 ? num_tiles : total_tiles_available;
    if (tiles_to_write > total_tiles_available)
        tiles_to_write = total_tiles_available;

    int bytes_per_tile = (bpp == 4) ? 32 : 64;
    size_t out_size = tiles_to_write * bytes_per_tile;
    uint8_t *tile_data = calloc(1, out_size);

    for (int t = 0; t < tiles_to_write; t++) {
        int tile_x = t % tiles_x;
        int tile_y = t / tiles_x;
        uint8_t *dst = tile_data + t * bytes_per_tile;

        for (int py = 0; py < 8; py++) {
            int src_y = tile_y * 8 + py;
            for (int px = 0; px < 8; px++) {
                int src_x = tile_x * 8 + px;
                uint8_t idx = 0;
                if (src_x < w && src_y < h)
                    idx = indices[src_y * w + src_x];

                if (bpp == 4) {
                    /* 4bpp: 2 pixels per byte, low nibble first */
                    int byte_off = py * 4 + px / 2;
                    if (px & 1)
                        dst[byte_off] |= (idx & 0x0F) << 4;
                    else
                        dst[byte_off] |= (idx & 0x0F);
                } else {
                    /* 8bpp: 1 pixel per byte */
                    dst[py * 8 + px] = idx;
                }
            }
        }
    }

    if (indices != pixels) free(indices);
    else stbi_image_free(pixels);

    int ret = write_file(output, tile_data, out_size);
    free(tile_data);

    if (ret == 0)
        printf("  %s -> %s (%d tiles, %dbpp, %zu bytes)\n",
               input, output, tiles_to_write, bpp, out_size);
    return ret;
}

/* ---------- JASC PAL to GBA palette ---------- */

static int cmd_pal2gba(const char *input, const char *output)
{
    FILE *f = fopen(input, "r");
    if (!f) { fprintf(stderr, "Cannot open %s\n", input); return 1; }

    char line[256];
    /* JASC-PAL header */
    fgets(line, sizeof(line), f); /* "JASC-PAL" */
    fgets(line, sizeof(line), f); /* "0100" */
    fgets(line, sizeof(line), f); /* color count */
    int count = atoi(line);
    if (count <= 0 || count > 256) { fclose(f); return 1; }

    uint16_t *pal = calloc(count, sizeof(uint16_t));
    for (int i = 0; i < count; i++) {
        if (!fgets(line, sizeof(line), f)) break;
        int r, g, b;
        if (sscanf(line, "%d %d %d", &r, &g, &b) == 3)
            pal[i] = rgb_to_bgr555(r, g, b);
    }
    fclose(f);

    int ret = write_file(output, pal, count * 2);
    if (ret == 0)
        printf("  %s -> %s (%d colors, %d bytes)\n",
               input, output, count, count * 2);
    free(pal);
    return ret;
}

/* ---------- LZ77 compression (GBA format) ---------- */

static int cmd_lz77(const char *input, const char *output)
{
    size_t in_size;
    uint8_t *in_data = read_file(input, &in_size);
    if (!in_data) { fprintf(stderr, "Cannot open %s\n", input); return 1; }

    /* Worst case: header + each byte as literal (9/8 expansion) */
    size_t max_out = 4 + in_size + in_size / 8 + 16;
    uint8_t *out_data = calloc(1, max_out);

    /* GBA LZ77 header: type=0x10, followed by 24-bit decompressed size (LE) */
    out_data[0] = 0x10;
    out_data[1] = in_size & 0xFF;
    out_data[2] = (in_size >> 8) & 0xFF;
    out_data[3] = (in_size >> 16) & 0xFF;

    size_t out_pos = 4;
    size_t in_pos = 0;

    while (in_pos < in_size) {
        size_t flag_pos = out_pos++;
        out_data[flag_pos] = 0;

        for (int bit = 7; bit >= 0 && in_pos < in_size; bit--) {
            /* Try to find a match in the sliding window */
            int best_len = 0;
            int best_off = 0;

            int window_start = (int)in_pos - 4096;
            if (window_start < 0) window_start = 0;

            for (int j = window_start; j < (int)in_pos; j++) {
                int len = 0;
                while (len < 18 && in_pos + len < in_size
                       && in_data[j + len] == in_data[in_pos + len])
                    len++;
                if (len > best_len) {
                    best_len = len;
                    best_off = (int)in_pos - j;
                }
            }

            if (best_len >= 3) {
                /* Compressed: set flag bit, write (len-3, offset-1) */
                out_data[flag_pos] |= (1 << bit);
                int encoded_len = best_len - 3;
                int encoded_off = best_off - 1;
                out_data[out_pos++] = (encoded_len << 4) | (encoded_off >> 8);
                out_data[out_pos++] = encoded_off & 0xFF;
                in_pos += best_len;
            } else {
                /* Literal byte */
                out_data[out_pos++] = in_data[in_pos++];
            }
        }
    }

    free(in_data);
    int ret = write_file(output, out_data, out_pos);
    if (ret == 0)
        printf("  %s -> %s (%zu -> %zu bytes, %.1f%%)\n",
               input, output, in_size, out_pos,
               in_size ? (100.0 * out_pos / in_size) : 0.0);
    free(out_data);
    return ret;
}

/* ---------- Batch mode: convert all title/intro assets ---------- */

struct AssetRule {
    const char *src;     /* relative to upstream graphics/ dir */
    const char *dst;     /* relative to output dir */
    enum { PNG4, PNG8, PAL, BIN_COPY, LZ77_WRAP } type;
    int num_tiles;       /* for PNG conversion, 0 = all */
};

static const struct AssetRule sTitleScreenAssets[] = {
    /* FireRed title screen */
    { "title_screen/firered/game_title_logo.png", "title_screen/firered/game_title_logo.8bpp", PNG8, 0 },
    { "title_screen/firered/game_title_logo.pal", "title_screen/firered/game_title_logo.gbapal", PAL, 0 },
    { "title_screen/firered/game_title_logo.bin", "title_screen/firered/game_title_logo.bin", BIN_COPY, 0 },
    { "title_screen/firered/box_art_mon.png",     "title_screen/firered/box_art_mon.4bpp", PNG4, 135 },
    { "title_screen/firered/box_art_mon.pal",     "title_screen/firered/box_art_mon.gbapal", PAL, 0 },
    { "title_screen/firered/box_art_mon.bin",     "title_screen/firered/box_art_mon.bin", BIN_COPY, 0 },
    { "title_screen/firered/background.pal",      "title_screen/firered/background.gbapal", PAL, 0 },
    { "title_screen/firered/slash.pal",           "title_screen/firered/slash.gbapal", PAL, 0 },
    { "title_screen/copyright_press_start.png",   "title_screen/copyright_press_start.4bpp", PNG4, 0 },
    { "title_screen/copyright_press_start.bin",   "title_screen/copyright_press_start.bin", BIN_COPY, 0 },
};

static const struct AssetRule sIntroAssets[] = {
    /* Copyright screen */
    { "intro/copyright.png",           "intro/copyright.4bpp", PNG4, 0 },
    { "intro/copyright.pal",           "intro/copyright.gbapal", PAL, 0 },
    { "intro/copyright.bin",           "intro/copyright.bin", BIN_COPY, 0 },
    /* Game Freak logo */
    { "intro/game_freak/bg.png",       "intro/game_freak/bg.4bpp", PNG4, 0 },
    { "intro/game_freak/bg.pal",       "intro/game_freak/bg.gbapal", PAL, 0 },
    { "intro/game_freak/bg.bin",       "intro/game_freak/bg.bin", BIN_COPY, 0 },
    { "intro/game_freak/logo.png",     "intro/game_freak/logo.4bpp", PNG4, 0 },
    { "intro/game_freak/logo.pal",     "intro/game_freak/logo.gbapal", PAL, 0 },
    { "intro/game_freak/game_freak.png","intro/game_freak/game_freak.4bpp", PNG4, 0 },
    { "intro/game_freak/star.png",     "intro/game_freak/star.4bpp", PNG4, 0 },
    { "intro/game_freak/star.pal",     "intro/game_freak/star.gbapal", PAL, 0 },
    { "intro/game_freak/sparkles_small.png", "intro/game_freak/sparkles_small.4bpp", PNG4, 0 },
    { "intro/game_freak/sparkles_big.png",   "intro/game_freak/sparkles_big.4bpp", PNG4, 0 },
    { "intro/game_freak/sparkles.pal", "intro/game_freak/sparkles.gbapal", PAL, 0 },
    { "intro/game_freak/presents.png", "intro/game_freak/presents.4bpp", PNG4, 0 },
    /* Scene 1 */
    { "intro/scene_1/bg.png",         "intro/scene_1/bg.4bpp", PNG4, 0 },
    { "intro/scene_1/bg.pal",         "intro/scene_1/bg.gbapal", PAL, 0 },
    { "intro/scene_1/bg.bin",         "intro/scene_1/bg.bin", BIN_COPY, 0 },
    { "intro/scene_1/grass.png",      "intro/scene_1/grass.4bpp", PNG4, 0 },
    { "intro/scene_1/grass.pal",      "intro/scene_1/grass.gbapal", PAL, 0 },
    { "intro/scene_1/grass.bin",      "intro/scene_1/grass.bin", BIN_COPY, 0 },
    /* Scene 2 */
    { "intro/scene_2/bg.png",         "intro/scene_2/bg.4bpp", PNG4, 0 },
    { "intro/scene_2/bg.pal",         "intro/scene_2/bg.gbapal", PAL, 0 },
    { "intro/scene_2/bg.bin",         "intro/scene_2/bg.bin", BIN_COPY, 0 },
    { "intro/scene_2/plants.png",     "intro/scene_2/plants.4bpp", PNG4, 0 },
    { "intro/scene_2/plants.pal",     "intro/scene_2/plants.gbapal", PAL, 0 },
    { "intro/scene_2/plants.bin",     "intro/scene_2/plants.bin", BIN_COPY, 0 },
    { "intro/gengar.pal",             "intro/gengar.gbapal", PAL, 0 },
    { "intro/scene_2/gengar_close.png","intro/scene_2/gengar_close.4bpp", PNG4, 0 },
    { "intro/scene_2/gengar_close.bin","intro/scene_2/gengar_close.bin", BIN_COPY, 0 },
    { "intro/scene_2/gengar.png",     "intro/scene_2/gengar.4bpp", PNG4, 0 },
    { "intro/nidorino.pal",           "intro/nidorino.gbapal", PAL, 0 },
    { "intro/scene_2/nidorino_close.png","intro/scene_2/nidorino_close.4bpp", PNG4, 0 },
    { "intro/scene_2/nidorino_close.pal","intro/scene_2/nidorino_close.gbapal", PAL, 0 },
    { "intro/scene_2/nidorino_close.bin","intro/scene_2/nidorino_close.bin", BIN_COPY, 0 },
    { "intro/scene_2/nidorino.png",   "intro/scene_2/nidorino.4bpp", PNG4, 0 },
    /* Scene 3 */
    { "intro/scene_3/bg.png",         "intro/scene_3/bg.4bpp", PNG4, 0 },
    { "intro/scene_3/bg.pal",         "intro/scene_3/bg.gbapal", PAL, 0 },
    { "intro/scene_3/bg.bin",         "intro/scene_3/bg.bin", BIN_COPY, 0 },
    { "intro/scene_3/gengar_anim.png","intro/scene_3/gengar_anim.4bpp", PNG4, 0 },
    { "intro/scene_3/gengar_anim.bin","intro/scene_3/gengar_anim.bin", BIN_COPY, 0 },
    { "intro/scene_3/gengar_static.png","intro/scene_3/gengar_static.4bpp", PNG4, 0 },
    { "intro/scene_3/grass.png",      "intro/scene_3/grass.4bpp", PNG4, 0 },
    { "intro/scene_3/nidorino.png",   "intro/scene_3/nidorino.4bpp", PNG4, 0 },
    { "intro/scene_3/recoil_dust.png","intro/scene_3/recoil_dust.4bpp", PNG4, 0 },
    { "intro/scene_3/swipe.png",      "intro/scene_3/swipe.4bpp", PNG4, 0 },
};

static void mkdirs(const char *path)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
    }
}

static int process_rule(const struct AssetRule *rule,
                        const char *gfx_dir, const char *out_dir)
{
    char src_path[512], dst_path[512], lz_path[512];
    snprintf(src_path, sizeof(src_path), "%s/%s", gfx_dir, rule->src);
    snprintf(dst_path, sizeof(dst_path), "%s/%s", out_dir, rule->dst);

    /* Ensure output subdirectory exists */
    mkdirs(dst_path);

    int ret = 0;
    switch (rule->type) {
    case PNG4:
        ret = cmd_png2gba(src_path, dst_path, rule->num_tiles, 4);
        break;
    case PNG8:
        ret = cmd_png2gba(src_path, dst_path, rule->num_tiles, 8);
        break;
    case PAL:
        ret = cmd_pal2gba(src_path, dst_path);
        break;
    case BIN_COPY: {
        size_t sz;
        uint8_t *data = read_file(src_path, &sz);
        if (!data) { fprintf(stderr, "Cannot open %s\n", src_path); return 1; }
        ret = write_file(dst_path, data, sz);
        if (ret == 0)
            printf("  %s -> %s (%zu bytes)\n", src_path, dst_path, sz);
        free(data);
        break;
    }
    default:
        break;
    }

    /* Also produce .lz compressed version */
    if (ret == 0) {
        snprintf(lz_path, sizeof(lz_path), "%s.lz", dst_path);
        ret = cmd_lz77(dst_path, lz_path);
    }

    return ret;
}

static int cmd_batch(const char *upstream_dir, const char *out_dir)
{
    char gfx_dir[512];
    snprintf(gfx_dir, sizeof(gfx_dir), "%s/graphics", upstream_dir);

    int errors = 0;
    size_t i;

    printf("=== Title Screen Assets ===\n");
    for (i = 0; i < sizeof(sTitleScreenAssets) / sizeof(sTitleScreenAssets[0]); i++)
        errors += process_rule(&sTitleScreenAssets[i], gfx_dir, out_dir);

    printf("\n=== Intro Assets ===\n");
    for (i = 0; i < sizeof(sIntroAssets) / sizeof(sIntroAssets[0]); i++)
        errors += process_rule(&sIntroAssets[i], gfx_dir, out_dir);

    printf("\nDone. %d errors.\n", errors);
    return errors ? 1 : 0;
}

/* ---------- Main ---------- */

static void usage(void)
{
    fprintf(stderr,
        "Usage:\n"
        "  pfr_assets png2gba <input.png> <output.4bpp|.8bpp> [--num-tiles N] [--8bpp]\n"
        "  pfr_assets pal2gba <input.pal> <output.gbapal>\n"
        "  pfr_assets lz77    <input>     <output.lz>\n"
        "  pfr_assets batch   <upstream_pokefirered_dir> <output_dir>\n"
    );
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(); return 1; }

    if (strcmp(argv[1], "png2gba") == 0) {
        if (argc < 4) { usage(); return 1; }
        int num_tiles = 0, bpp = 4;
        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--num-tiles") == 0 && i + 1 < argc)
                num_tiles = atoi(argv[++i]);
            else if (strcmp(argv[i], "--8bpp") == 0)
                bpp = 8;
        }
        return cmd_png2gba(argv[2], argv[3], num_tiles, bpp);
    }
    else if (strcmp(argv[1], "pal2gba") == 0) {
        if (argc < 4) { usage(); return 1; }
        return cmd_pal2gba(argv[2], argv[3]);
    }
    else if (strcmp(argv[1], "lz77") == 0) {
        if (argc < 4) { usage(); return 1; }
        return cmd_lz77(argv[2], argv[3]);
    }
    else if (strcmp(argv[1], "batch") == 0) {
        if (argc < 4) { usage(); return 1; }
        return cmd_batch(argv[2], argv[3]);
    }
    else {
        usage();
        return 1;
    }
}
