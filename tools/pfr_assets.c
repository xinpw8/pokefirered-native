/*
 * pfr_assets.c — Asset compiler for pokefirered-native
 *
 * Converts upstream PNG/PAL/BIN source assets into the intermediate
 * formats that the INCBIN macros reference (.4bpp, .8bpp, .gbapal,
 * .4bpp.lz, .bin.lz, etc.).
 *
 * Also generates C initializer .inc files and preprocesses upstream
 * source files to replace INCBIN macros with real data includes.
 *
 * For canonical batch builds, the tool now copies exact upstream INCBIN
 * binaries and asks upstream make to materialize any missing outputs.
 * The stb_image-backed conversion helpers remain available for ad hoc
 * developer use, but they are no longer the source of truth for the build.
 *
 * Usage:
 *   pfr_assets png2gba <input.png> <output.4bpp|.8bpp> [--num-tiles N]
 *   pfr_assets pal2gba <input.pal> <output.gbapal>
 *   pfr_assets png2pal <input.png> <output.gbapal>
 *   pfr_assets lz77    <input>     <output.lz>
 *   pfr_assets bin2inc <input.bin> <output.inc> [--u8|--u16|--u32]
 *   pfr_assets preproc <input.c> <inc_dir> <output.c>
 *   pfr_assets batch   <upstream_dir> <output_dir> <manifest...>
 *   pfr_assets geninc  <assets_dir> <inc_dir>
 */

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

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

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int run_process(char *const argv[])
{
    pid_t pid = fork();
    int status;

    if (pid < 0)
    {
        perror("fork");
        return 1;
    }

    if (pid == 0)
    {
        execv(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0)
    {
        perror("waitpid");
        return 1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return 1;

    return 0;
}

static int run_process_in_dir(const char *cwd, char *const argv[])
{
    pid_t pid = fork();
    int status;

    if (pid < 0)
    {
        perror("fork");
        return 1;
    }

    if (pid == 0)
    {
        if (cwd != NULL && chdir(cwd) != 0)
        {
            perror(cwd);
            _exit(127);
        }
        execvp(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0)
    {
        perror("waitpid");
        return 1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return 1;

    return 0;
}

static int cmd_gbagfx_png2gba(const char *gbagfx_path, const char *input,
                              const char *output, int num_tiles)
{
    char num_tiles_buf[32];
    char *argv[7];
    int argc = 0;

    argv[argc++] = (char *)gbagfx_path;
    argv[argc++] = (char *)input;
    argv[argc++] = (char *)output;
    if (num_tiles > 0)
    {
        snprintf(num_tiles_buf, sizeof(num_tiles_buf), "%d", num_tiles);
        argv[argc++] = "-num_tiles";
        argv[argc++] = num_tiles_buf;
        argv[argc++] = "-Wnum_tiles";
    }
    argv[argc] = NULL;

    if (run_process(argv) != 0)
    {
        fprintf(stderr, "gbagfx failed: %s -> %s\n", input, output);
        return 1;
    }

    return 0;
}

static int cmd_gbagfx_png2pal(const char *gbagfx_path, const char *input,
                              const char *output)
{
    char *argv[4];

    argv[0] = (char *)gbagfx_path;
    argv[1] = (char *)input;
    argv[2] = (char *)output;
    argv[3] = NULL;

    if (run_process(argv) != 0)
    {
        fprintf(stderr, "gbagfx failed: %s -> %s\n", input, output);
        return 1;
    }

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

/*
 * Load a JASC-PAL file and return the palette as BGR555 values.
 * Returns palette count, or 0 on failure.
 */
static int load_jasc_pal(const char *path, uint16_t *out_pal, int max_colors)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int count, i;
    if (!f) return 0;

    /* Skip JASC-PAL header (3 lines) */
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; }
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; }
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; }
    count = atoi(line);
    if (count <= 0 || count > max_colors) count = max_colors;

    for (i = 0; i < count; i++) {
        int r, g, b;
        if (!fgets(line, sizeof(line), f)) break;
        if (sscanf(line, "%d %d %d", &r, &g, &b) == 3)
            out_pal[i] = rgb_to_bgr555(r, g, b);
    }
    fclose(f);
    return i;
}

static int cmd_png2gba(const char *input, const char *output, int num_tiles,
                       int bpp, const char *pal_path)
{
    size_t png_size;
    uint8_t *png_data = read_file(input, &png_size);
    if (!png_data) { fprintf(stderr, "Cannot open %s\n", input); return 1; }

    int w, h, channels;
    uint8_t *pixels = stbi_load_from_memory(png_data, png_size, &w, &h, &channels, 0);
    free(png_data);
    if (!pixels) { fprintf(stderr, "PNG decode failed: %s\n", input); return 1; }

    /*
     * We need indexed pixel values. Three paths:
     *   1. Grayscale (1 channel): treat raw values as palette indices
     *   2. RGB/RGBA with companion .pal file: reverse-map each pixel's
     *      color to the authoritative palette index (fixes the stb_image
     *      limitation where indexed PNGs are expanded to RGB)
     *   3. RGB/RGBA without .pal: build palette from first-seen colors
     */
    int total_pixels = w * h;
    uint8_t *indices = NULL;

    if (channels == 1) {
        /* Grayscale — treat raw values as palette indices */
        indices = pixels; /* steal the buffer */
    } else if (pal_path != NULL) {
        /* Palette-guided indexing: load the authoritative .pal file and
         * reverse-map each pixel's BGR555 color to its palette index. */
        uint16_t ref_pal[256];
        int ref_count = load_jasc_pal(pal_path, ref_pal, bpp == 4 ? 16 : 256);
        if (ref_count <= 0) {
            fprintf(stderr, "  Warning: could not load companion palette %s, "
                    "falling back to first-seen ordering\n", pal_path);
            pal_path = NULL;
            goto fallback_palette;
        }
        indices = malloc(total_pixels);
        for (int i = 0; i < total_pixels; i++) {
            uint8_t r = pixels[i * channels + 0];
            uint8_t g = pixels[i * channels + 1];
            uint8_t b = pixels[i * channels + 2];
            uint16_t bgr = rgb_to_bgr555(r, g, b);

            int found = 0; /* default to index 0 (transparent) */
            for (int j = 0; j < ref_count; j++) {
                if (ref_pal[j] == bgr) { found = j; break; }
            }
            indices[i] = (uint8_t)found;
        }
        stbi_image_free(pixels);
        pixels = NULL;
    } else {
    fallback_palette:
        /* RGB or RGBA — build a palette from unique colors, then index */
        ;
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
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 1; }
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 1; }
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 1; }
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

/* ---------- PNG palette extraction to GBA palette ---------- */

/*
 * Extracts the unique color palette from a PNG and writes it as a GBA
 * BGR555 .gbapal file. This handles the case where the upstream build
 * generates .gbapal directly from .png (no separate .pal file).
 */

static int cmd_png2pal(const char *input, const char *output)
{
    size_t png_size;
    uint8_t *png_data = read_file(input, &png_size);
    if (!png_data) { fprintf(stderr, "Cannot open %s\n", input); return 1; }

    int w, h, channels;
    uint8_t *pixels = stbi_load_from_memory(png_data, png_size, &w, &h, &channels, 0);
    free(png_data);
    if (!pixels) { fprintf(stderr, "PNG decode failed: %s\n", input); return 1; }

    uint16_t palette[256];
    int pal_count = 0;

    if (channels == 1) {
        /* Grayscale — build a palette from unique grayscale values */
        int total_pixels = w * h;
        for (int i = 0; i < total_pixels && pal_count < 256; i++) {
            uint8_t v = pixels[i];
            uint16_t bgr = rgb_to_bgr555(v, v, v);
            int found = 0;
            for (int j = 0; j < pal_count; j++) {
                if (palette[j] == bgr) { found = 1; break; }
            }
            if (!found)
                palette[pal_count++] = bgr;
        }
    } else {
        /* RGB or RGBA — extract unique BGR555 colors in order of first appearance */
        int total_pixels = w * h;
        for (int i = 0; i < total_pixels && pal_count < 256; i++) {
            uint8_t r = pixels[i * channels + 0];
            uint8_t g = pixels[i * channels + 1];
            uint8_t b = pixels[i * channels + 2];
            uint16_t bgr = rgb_to_bgr555(r, g, b);

            int found = 0;
            for (int j = 0; j < pal_count; j++) {
                if (palette[j] == bgr) { found = 1; break; }
            }
            if (!found)
                palette[pal_count++] = bgr;
        }
    }

    stbi_image_free(pixels);

    /* Clamp to 16 colors — PAL_FROM_PNG is only used with 4bpp assets,
     * and on GBA 4bpp tiles index into a single 16-color palette slot.
     * The original gbagfx always outputs exactly 16 colors (32 bytes). */
    if (pal_count > 16) {
        fprintf(stderr, "  Warning: %s has %d unique colors, clamping to 16 for 4bpp\n",
                input, pal_count);
        pal_count = 16;
    }
    int padded = 16;
    for (int i = pal_count; i < padded; i++)
        palette[i] = 0;

    int ret = write_file(output, palette, padded * 2);
    if (ret == 0)
        printf("  %s -> %s (%d colors, %d bytes)\n",
               input, output, pal_count, padded * 2);
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

/* ---------- Binary to C initializer (.inc file) ---------- */

/*
 * Converts a binary file into a C initializer list suitable for
 * #include inside an array declaration:
 *
 *   static const u32 foo[] = {
 *   #include "foo.u32.inc"
 *   };
 *
 * elem_size: 1 for u8, 2 for u16, 4 for u32
 * Output: comma-separated hex values, 12 per line.
 */

static int cmd_bin2inc(const char *input, const char *output, int elem_size)
{
    size_t in_size;
    uint8_t *data = read_file(input, &in_size);
    if (!data) { fprintf(stderr, "Cannot open %s\n", input); return 1; }

    FILE *f = fopen(output, "w");
    if (!f) { perror(output); free(data); return 1; }

    /* Pad to element alignment */
    size_t padded = in_size;
    if (elem_size > 1 && (padded % elem_size))
        padded += elem_size - (padded % elem_size);

    size_t num_elems = padded / elem_size;
    int col = 0;

    for (size_t i = 0; i < num_elems; i++) {
        size_t off = i * elem_size;

        if (elem_size == 1) {
            uint8_t v = (off < in_size) ? data[off] : 0;
            fprintf(f, "0x%02X", v);
        } else if (elem_size == 2) {
            uint8_t lo = (off < in_size) ? data[off] : 0;
            uint8_t hi = (off + 1 < in_size) ? data[off + 1] : 0;
            fprintf(f, "0x%04X", lo | (hi << 8));
        } else { /* 4 */
            uint32_t v = 0;
            for (int b = 0; b < 4; b++) {
                if (off + b < in_size)
                    v |= (uint32_t)data[off + b] << (b * 8);
            }
            fprintf(f, "0x%08X", v);
        }

        if (i + 1 < num_elems)
            fprintf(f, ",");

        col++;
        if (col >= 12) {
            fprintf(f, "\n");
            col = 0;
        }
    }

    if (col > 0)
        fprintf(f, "\n");

    fclose(f);
    free(data);
    return 0;
}

/* ---------- Generate .inc files for all assets in a directory ---------- */

static void geninc_recursive(const char *assets_dir, const char *inc_dir,
                              const char *rel_path, int *count)
{
    char full_path[512];
    if (rel_path[0])
        snprintf(full_path, sizeof(full_path), "%s/%s", assets_dir, rel_path);
    else
        snprintf(full_path, sizeof(full_path), "%s", assets_dir);

    DIR *d = opendir(full_path);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char child_rel[512];
        if (rel_path[0])
            snprintf(child_rel, sizeof(child_rel), "%s/%s", rel_path, ent->d_name);
        else
            snprintf(child_rel, sizeof(child_rel), "%s", ent->d_name);

        char child_full[512];
        snprintf(child_full, sizeof(child_full), "%s/%s", assets_dir, child_rel);

        struct stat st;
        if (stat(child_full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            geninc_recursive(assets_dir, inc_dir, child_rel, count);
        } else if (S_ISREG(st.st_mode)) {
            /* Generate .u8.inc, .u16.inc, .u32.inc for each binary file */
            static const struct { const char *suffix; int elem; } sizes[] = {
                { ".u8.inc",  1 },
                { ".u16.inc", 2 },
                { ".u32.inc", 4 },
            };

            for (int s = 0; s < 3; s++) {
                char inc_path[600];
                snprintf(inc_path, sizeof(inc_path), "%s/%s%s",
                         inc_dir, child_rel, sizes[s].suffix);

                /* Ensure output directory exists */
                char dir_buf[600];
                snprintf(dir_buf, sizeof(dir_buf), "%s", inc_path);
                for (char *p = dir_buf + 1; *p; p++) {
                    if (*p == '/') {
                        *p = '\0';
                        mkdir(dir_buf, 0755);
                        *p = '/';
                    }
                }

                if (cmd_bin2inc(child_full, inc_path, sizes[s].elem) == 0)
                    (*count)++;
            }
        }
    }
    closedir(d);
}

static int cmd_geninc(const char *assets_dir, const char *inc_dir)
{
    int count = 0;
    mkdir(inc_dir, 0755);
    geninc_recursive(assets_dir, inc_dir, "", &count);
    printf("Generated %d .inc files in %s\n", count, inc_dir);
    return 0;
}

/* ---------- INCBIN preprocessor ---------- */

/*
 * Preprocesses an upstream .c file, replacing INCBIN_XX("path") with
 * a C array initializer that #include's the corresponding .inc file.
 *
 * Input:  static const u32 foo[] = INCBIN_U32("graphics/intro/foo.4bpp.lz");
 * Output: static const u32 foo[] = {
 *         #include "/abs/path/inc/intro/foo.4bpp.lz.u32.inc"
 *         };
 *
 * The "graphics/" prefix is stripped from the INCBIN path to match the
 * asset directory structure.
 */

static int cmd_preproc(const char *src_path, const char *inc_dir, const char *out_path)
{
    size_t src_size;
    uint8_t *src_data = read_file(src_path, &src_size);
    if (!src_data) { fprintf(stderr, "Cannot open %s\n", src_path); return 1; }

    FILE *f = fopen(out_path, "w");
    if (!f) { perror(out_path); free(src_data); return 1; }

    const char *p = (const char *)src_data;
    const char *end = p + src_size;

    /* INCBIN variant table */
    static const struct {
        const char *prefix;
        size_t prefix_len;
        const char *inc_suffix;
    } variants[] = {
        { "INCBIN_U8(",  10, ".u8.inc"  },
        { "INCBIN_U16(", 11, ".u16.inc" },
        { "INCBIN_U32(", 11, ".u32.inc" },
        { "INCBIN_S8(",  10, ".u8.inc"  },
        { "INCBIN_S16(", 11, ".u16.inc" },
        { "INCBIN_S32(", 11, ".u32.inc" },
    };

    int replacements = 0;

    while (p < end) {
        /* Try to match any INCBIN variant */
        int matched = 0;

        for (int v = 0; v < 6; v++) {
            size_t plen = variants[v].prefix_len;
            if ((size_t)(end - p) >= plen &&
                strncmp(p, variants[v].prefix, plen) == 0)
            {
                /* Found INCBIN_XX( — extract the path string */
                const char *q = p + plen;

                /* Skip whitespace */
                while (q < end && (*q == ' ' || *q == '\t')) q++;

                if (q < end && (*q == '"' || *q == ' ')) {
                    if (*q == ' ') {
                        while (q < end && *q == ' ') q++;
                    }
                    if (q < end && *q == '"') {
                        q++; /* skip opening quote */
                        const char *path_start = q;
                        while (q < end && *q != '"') q++;

                        if (q < end) {
                            size_t path_len = q - path_start;
                            char asset_path[512];
                            snprintf(asset_path, sizeof(asset_path),
                                     "%.*s", (int)path_len, path_start);

                            q++; /* skip closing quote */
                            /* Skip whitespace and closing paren */
                            while (q < end && (*q == ' ' || *q == '\t')) q++;
                            if (q < end && *q == ')') q++;

                            /* Strip "graphics/" prefix if present */
                            const char *rel = asset_path;
                            if (strncmp(rel, "graphics/", 9) == 0)
                                rel += 9;

                            /* Build the .inc file path */
                            char inc_path[700];
                            snprintf(inc_path, sizeof(inc_path),
                                     "%s/%s%s", inc_dir, rel, variants[v].inc_suffix);

                            /* Check if the .inc file exists */
                            if (file_exists(inc_path)) {
                                fprintf(f, "{\n#include \"%s\"\n}", inc_path);
                                replacements++;
                            } else {
                                /* Fallback: keep as {0} */
                                fprintf(f, "{0} /* MISSING: %s */", inc_path);
                            }

                            p = q;
                            matched = 1;
                            break;
                        }
                    }
                }
            }
        }

        if (!matched) {
            fputc(*p, f);
            p++;
        }
    }

    fclose(f);
    free(src_data);
    printf("  %s -> %s (%d INCBIN replacements)\n", src_path, out_path, replacements);
    return 0;
}

/* ---------- Batch mode: materialize exact upstream INCBIN assets ---------- */

struct AssetPathList
{
    char **items;
    size_t count;
    size_t capacity;
};

static void mkdirs(const char *path);

static char *dup_string(const char *src)
{
    size_t len = strlen(src) + 1;
    char *dst = malloc(len);
    if (dst != NULL)
        memcpy(dst, src, len);
    return dst;
}

static void asset_path_list_free(struct AssetPathList *list)
{
    size_t i;
    for (i = 0; i < list->count; i++)
        free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int asset_path_list_add_unique(struct AssetPathList *list, const char *path)
{
    size_t i;

    for (i = 0; i < list->count; i++)
    {
        if (strcmp(list->items[i], path) == 0)
            return 0;
    }

    if (list->count == list->capacity)
    {
        size_t new_capacity = list->capacity == 0 ? 64 : list->capacity * 2;
        char **new_items = realloc(list->items, new_capacity * sizeof(*new_items));
        if (new_items == NULL)
        {
            fprintf(stderr, "Out of memory while growing asset list\n");
            return 1;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->count] = dup_string(path);
    if (list->items[list->count] == NULL)
    {
        fprintf(stderr, "Out of memory while duplicating asset path\n");
        return 1;
    }
    list->count++;
    return 0;
}

static char *trim_left(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s))
        s++;
    return s;
}

static void trim_right(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
}

static int load_asset_manifest(const char *manifest_path, struct AssetPathList *list)
{
    FILE *f = fopen(manifest_path, "r");
    char line[1024];
    int line_no = 0;

    if (f == NULL)
    {
        perror(manifest_path);
        return 1;
    }

    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *comment;
        char *path;
        char *types;

        line_no++;
        comment = strchr(line, '#');
        if (comment != NULL)
            *comment = '\0';

        trim_right(line);
        path = trim_left(line);
        if (*path == '\0')
            continue;

        types = path;
        while (*types != '\0' && !isspace((unsigned char)*types))
            types++;
        if (*types == '\0')
        {
            fprintf(stderr, "%s:%d: expected '<asset_path> <types>'\n",
                    manifest_path, line_no);
            fclose(f);
            return 1;
        }
        *types++ = '\0';
        types = trim_left(types);
        if (*types == '\0')
        {
            fprintf(stderr, "%s:%d: missing type list for %s\n",
                    manifest_path, line_no, path);
            fclose(f);
            return 1;
        }

        if (asset_path_list_add_unique(list, path) != 0)
        {
            fclose(f);
            return 1;
        }
    }

    fclose(f);
    return 0;
}

static int copy_exact_asset(const char *upstream_dir, const char *asset_path, const char *out_dir)
{
    char src_path[768];
    char dst_path[768];
    size_t sz;
    uint8_t *data;
    const char *rel = asset_path;

    snprintf(src_path, sizeof(src_path), "%s/%s", upstream_dir, asset_path);
    if (strncmp(rel, "graphics/", 9) == 0)
        rel += 9;
    snprintf(dst_path, sizeof(dst_path), "%s/%s", out_dir, rel);

    mkdirs(dst_path);
    data = read_file(src_path, &sz);
    if (data == NULL)
    {
        fprintf(stderr, "Cannot open %s\n", src_path);
        return 1;
    }

    if (write_file(dst_path, data, sz) != 0)
    {
        free(data);
        return 1;
    }

    printf("  %s -> %s (%zu bytes)\n", src_path, dst_path, sz);
    free(data);
    return 0;
}

static int materialize_missing_assets(const char *upstream_dir, const struct AssetPathList *assets)
{
    char **argv;
    size_t i;
    size_t missing = 0;

    for (i = 0; i < assets->count; i++)
    {
        char path[768];
        snprintf(path, sizeof(path), "%s/%s", upstream_dir, assets->items[i]);
        if (!file_exists(path))
            missing++;
    }

    if (missing == 0)
        return 0;

    argv = calloc(missing + 6, sizeof(*argv));
    if (argv == NULL)
    {
        fprintf(stderr, "Out of memory while preparing upstream make command\n");
        return 1;
    }

    argv[0] = "make";
    argv[1] = "-C";
    argv[2] = (char *)upstream_dir;
    argv[3] = "SETUP_PREREQS=0";
    argv[4] = "NODEP=1";

    missing = 0;
    for (i = 0; i < assets->count; i++)
    {
        char path[768];
        snprintf(path, sizeof(path), "%s/%s", upstream_dir, assets->items[i]);
        if (!file_exists(path))
            argv[5 + missing++] = assets->items[i];
    }
    argv[5 + missing] = NULL;

    printf("Materializing %zu upstream asset(s) via make\n", missing);
    if (run_process_in_dir(NULL, argv) != 0)
    {
        fprintf(stderr, "upstream make failed while materializing INCBIN assets\n");
        free(argv);
        return 1;
    }

    free(argv);
    return 0;
}

static int cmd_batch(const char *upstream_dir, const char *out_dir, int manifest_count, const char *const *manifest_paths)
{
    struct AssetPathList assets = {0};
    int errors = 0;
    int i;

    if (manifest_count <= 0)
    {
        fprintf(stderr, "batch requires at least one manifest file\n");
        return 1;
    }

    for (i = 0; i < manifest_count; i++)
    {
        printf("=== Asset Manifest: %s ===\n", manifest_paths[i]);
        if (load_asset_manifest(manifest_paths[i], &assets) != 0)
        {
            asset_path_list_free(&assets);
            return 1;
        }
    }

    printf("Loaded %zu unique upstream INCBIN asset(s)\n", assets.count);
    if (materialize_missing_assets(upstream_dir, &assets) != 0)
    {
        asset_path_list_free(&assets);
        return 1;
    }

    for (i = 0; i < (int)assets.count; i++)
    {
        char src_path[768];
        snprintf(src_path, sizeof(src_path), "%s/%s", upstream_dir, assets.items[i]);
        if (!file_exists(src_path))
        {
            fprintf(stderr, "Missing upstream asset after materialization: %s\n", src_path);
            errors++;
            continue;
        }
        errors += copy_exact_asset(upstream_dir, assets.items[i], out_dir);
    }

    asset_path_list_free(&assets);
    printf("\nDone. %d errors.\n", errors);
    return errors ? 1 : 0;
}

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

/* ---------- Main ---------- */

static void usage(void)
{
    fprintf(stderr,
        "Usage:\n"
        "  pfr_assets png2gba  <input.png> <output.4bpp|.8bpp> [--num-tiles N] [--8bpp] [--pal <file.pal>]\n"
        "  pfr_assets pal2gba  <input.pal> <output.gbapal>\n"
        "  pfr_assets png2pal  <input.png> <output.gbapal>\n"
        "  pfr_assets lz77     <input>     <output.lz>\n"
        "  pfr_assets bin2inc  <input.bin> <output.inc> [--u16|--u32]\n"
        "  pfr_assets preproc  <input.c>   <inc_dir> <output.c>\n"
        "  pfr_assets batch    <upstream_pokefirered_dir> <output_dir> <manifest...>\n"
        "  pfr_assets geninc   <assets_dir> <inc_dir>\n"
    );
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(); return 1; }

    if (strcmp(argv[1], "png2gba") == 0) {
        if (argc < 4) { usage(); return 1; }
        int num_tiles = 0, bpp = 4;
        const char *pal_path = NULL;
        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--num-tiles") == 0 && i + 1 < argc)
                num_tiles = atoi(argv[++i]);
            else if (strcmp(argv[i], "--8bpp") == 0)
                bpp = 8;
            else if (strcmp(argv[i], "--pal") == 0 && i + 1 < argc)
                pal_path = argv[++i];
        }
        return cmd_png2gba(argv[2], argv[3], num_tiles, bpp, pal_path);
    }
    else if (strcmp(argv[1], "pal2gba") == 0) {
        if (argc < 4) { usage(); return 1; }
        return cmd_pal2gba(argv[2], argv[3]);
    }
    else if (strcmp(argv[1], "png2pal") == 0) {
        if (argc < 4) { usage(); return 1; }
        return cmd_png2pal(argv[2], argv[3]);
    }
    else if (strcmp(argv[1], "lz77") == 0) {
        if (argc < 4) { usage(); return 1; }
        return cmd_lz77(argv[2], argv[3]);
    }
    else if (strcmp(argv[1], "bin2inc") == 0) {
        if (argc < 4) { usage(); return 1; }
        int elem = 1;
        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--u16") == 0) elem = 2;
            else if (strcmp(argv[i], "--u32") == 0) elem = 4;
        }
        return cmd_bin2inc(argv[2], argv[3], elem);
    }
    else if (strcmp(argv[1], "preproc") == 0) {
        if (argc < 5) { usage(); return 1; }
        return cmd_preproc(argv[2], argv[3], argv[4]);
    }
    else if (strcmp(argv[1], "batch") == 0) {
        if (argc < 5) { usage(); return 1; }
        return cmd_batch(argv[2], argv[3], argc - 4, (const char *const *)&argv[4]);
    }
    else if (strcmp(argv[1], "geninc") == 0) {
        if (argc < 4) { usage(); return 1; }
        return cmd_geninc(argv[2], argv[3]);
    }
    else {
        usage();
        return 1;
    }
}
