/*
 * pfr_frame_diff.c — Pixel-level PPM frame comparator
 *
 * Loads two 240x160 P6 PPM files and compares pixel-by-pixel.
 * Reports match/mismatch count, max per-channel delta, first differing
 * pixel, and RMSE. Optionally writes a diff highlight image.
 *
 * Usage:
 *   pfr_frame_diff <expected.ppm> <actual.ppm> [--diff-image output.ppm]
 *
 * Exit codes:
 *   0 = frames match exactly
 *   1 = frames differ
 *   2 = usage/file error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define GBA_W 240
#define GBA_H 160
#define NUM_PIXELS (GBA_W * GBA_H)

static int load_ppm(const char *path, unsigned char rgb[NUM_PIXELS * 3],
                    int *out_w, int *out_h)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "pfr_frame_diff: cannot open '%s'\n", path);
        return -1;
    }

    char magic[3];
    if (fscanf(f, "%2s", magic) != 1 || strcmp(magic, "P6") != 0) {
        fprintf(stderr, "pfr_frame_diff: '%s' is not a P6 PPM\n", path);
        fclose(f);
        return -1;
    }

    /* Skip comments */
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c == '#') {
            while ((c = fgetc(f)) != EOF && c != '\n')
                ;
        } else if (c > ' ') {
            ungetc(c, f);
            break;
        }
    }

    int w, h, maxval;
    if (fscanf(f, "%d %d %d", &w, &h, &maxval) != 3) {
        fprintf(stderr, "pfr_frame_diff: '%s' has invalid PPM header\n", path);
        fclose(f);
        return -1;
    }
    /* consume the single whitespace byte after maxval */
    fgetc(f);

    if (maxval != 255) {
        fprintf(stderr, "pfr_frame_diff: '%s' maxval=%d, expected 255\n",
                path, maxval);
        fclose(f);
        return -1;
    }

    *out_w = w;
    *out_h = h;

    size_t expected = (size_t)w * h * 3;
    size_t got = fread(rgb, 1, expected, f);
    fclose(f);

    if (got != expected) {
        fprintf(stderr, "pfr_frame_diff: '%s' truncated (%zu/%zu bytes)\n",
                path, got, expected);
        return -1;
    }
    return 0;
}

static void write_ppm(const char *path, const unsigned char *rgb, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "pfr_frame_diff: cannot write '%s'\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(rgb, 1, (size_t)w * h * 3, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *diff_path = NULL;

    if (argc < 3 || argc > 5) {
        fprintf(stderr,
                "Usage: pfr_frame_diff <expected.ppm> <actual.ppm> "
                "[--diff-image output.ppm]\n");
        return 2;
    }

    const char *expected_path = argv[1];
    const char *actual_path = argv[2];

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--diff-image") == 0 && i + 1 < argc) {
            diff_path = argv[++i];
        } else {
            fprintf(stderr, "pfr_frame_diff: unknown option '%s'\n", argv[i]);
            return 2;
        }
    }

    unsigned char *expected_rgb = malloc(NUM_PIXELS * 3);
    unsigned char *actual_rgb = malloc(NUM_PIXELS * 3);
    if (!expected_rgb || !actual_rgb) {
        fprintf(stderr, "pfr_frame_diff: out of memory\n");
        return 2;
    }

    int ew, eh, aw, ah;
    if (load_ppm(expected_path, expected_rgb, &ew, &eh) != 0)
        return 2;
    if (load_ppm(actual_path, actual_rgb, &aw, &ah) != 0)
        return 2;

    if (ew != aw || eh != ah) {
        fprintf(stderr,
                "pfr_frame_diff: dimension mismatch: %dx%d vs %dx%d\n",
                ew, eh, aw, ah);
        free(expected_rgb);
        free(actual_rgb);
        return 1;
    }

    int total_pixels = ew * eh;
    int mismatch_count = 0;
    int max_delta = 0;
    int first_x = -1, first_y = -1;
    double sum_sq = 0.0;

    unsigned char *diff_rgb = NULL;
    if (diff_path) {
        diff_rgb = malloc(total_pixels * 3);
        if (!diff_rgb) {
            fprintf(stderr, "pfr_frame_diff: out of memory for diff image\n");
            diff_path = NULL;
        }
    }

    for (int i = 0; i < total_pixels; i++) {
        int off = i * 3;
        int dr = abs((int)expected_rgb[off + 0] - (int)actual_rgb[off + 0]);
        int dg = abs((int)expected_rgb[off + 1] - (int)actual_rgb[off + 1]);
        int db = abs((int)expected_rgb[off + 2] - (int)actual_rgb[off + 2]);

        int pixel_max = dr;
        if (dg > pixel_max) pixel_max = dg;
        if (db > pixel_max) pixel_max = db;

        if (pixel_max > 0) {
            mismatch_count++;
            if (first_x < 0) {
                first_x = i % ew;
                first_y = i / ew;
            }
            if (pixel_max > max_delta)
                max_delta = pixel_max;
            sum_sq += (double)(dr * dr + dg * dg + db * db);

            if (diff_rgb) {
                diff_rgb[off + 0] = 255;
                diff_rgb[off + 1] = 0;
                diff_rgb[off + 2] = 0;
            }
        } else {
            if (diff_rgb) {
                /* dim copy of the matching pixel */
                diff_rgb[off + 0] = expected_rgb[off + 0] / 4;
                diff_rgb[off + 1] = expected_rgb[off + 1] / 4;
                diff_rgb[off + 2] = expected_rgb[off + 2] / 4;
            }
        }
    }

    if (diff_rgb && diff_path) {
        write_ppm(diff_path, diff_rgb, ew, eh);
        free(diff_rgb);
    }

    if (mismatch_count == 0) {
        printf("MATCH: %s vs %s (%dx%d, %d pixels)\n",
               expected_path, actual_path, ew, eh, total_pixels);
        free(expected_rgb);
        free(actual_rgb);
        return 0;
    }

    double rmse = sqrt(sum_sq / (total_pixels * 3.0));
    printf("MISMATCH: %s vs %s\n", expected_path, actual_path);
    printf("  dimensions:    %dx%d (%d pixels)\n", ew, eh, total_pixels);
    printf("  mismatched:    %d pixels (%.1f%%)\n",
           mismatch_count, 100.0 * mismatch_count / total_pixels);
    printf("  max delta:     %d\n", max_delta);
    printf("  RMSE:          %.3f\n", rmse);
    printf("  first diff at: (%d, %d)\n", first_x, first_y);
    if (diff_path)
        printf("  diff image:    %s\n", diff_path);

    free(expected_rgb);
    free(actual_rgb);
    return 1;
}
