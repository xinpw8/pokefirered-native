#include "host_capture_manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t parse_input_token(const char *token)
{
    if (strcmp(token, "A") == 0)
        return HOST_CAPTURE_BUTTON_A;
    if (strcmp(token, "B") == 0)
        return HOST_CAPTURE_BUTTON_B;
    if (strcmp(token, "SELECT") == 0)
        return HOST_CAPTURE_BUTTON_SELECT;
    if (strcmp(token, "START") == 0)
        return HOST_CAPTURE_BUTTON_START;
    if (strcmp(token, "RIGHT") == 0)
        return HOST_CAPTURE_BUTTON_RIGHT;
    if (strcmp(token, "LEFT") == 0)
        return HOST_CAPTURE_BUTTON_LEFT;
    if (strcmp(token, "UP") == 0)
        return HOST_CAPTURE_BUTTON_UP;
    if (strcmp(token, "DOWN") == 0)
        return HOST_CAPTURE_BUTTON_DOWN;
    if (strcmp(token, "R") == 0)
        return HOST_CAPTURE_BUTTON_R;
    if (strcmp(token, "L") == 0)
        return HOST_CAPTURE_BUTTON_L;
    if (strcmp(token, "NONE") == 0)
        return 0;
    return UINT16_MAX;
}

bool HostCaptureParseInputMask(const char *mask_text, uint16_t *out_mask)
{
    char buffer[128];
    char *token;
    char *save_ptr = NULL;
    uint16_t mask = 0;

    if (strncmp(mask_text, "0x", 2) == 0 || strncmp(mask_text, "0X", 2) == 0)
    {
        char *end_ptr = NULL;
        unsigned long value = strtoul(mask_text, &end_ptr, 16);

        if (end_ptr == NULL || *end_ptr != '\0')
            return false;
        *out_mask = (uint16_t)value;
        return true;
    }

    snprintf(buffer, sizeof(buffer), "%.127s", mask_text);
    token = strtok_r(buffer, "+|,", &save_ptr);
    while (token != NULL)
    {
        uint16_t value = parse_input_token(token);

        if (value == UINT16_MAX)
            return false;
        mask |= value;
        token = strtok_r(NULL, "+|,", &save_ptr);
    }

    *out_mask = mask;
    return true;
}

bool HostCaptureLoadInputScript(const char *path, struct HostCaptureInputScript *out_script)
{
    FILE *file = fopen(path, "r");
    char line[256];
    uint32_t line_number = 0;

    if (file == NULL)
    {
        fprintf(stderr, "capture_manifest: could not open input script %s\n", path);
        return false;
    }

    out_script->range_count = 0;
    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *hash;
        char *start_text;
        char *end_text;
        char *mask_text;
        char *save_ptr = NULL;
        unsigned long start_frame;
        unsigned long end_frame;
        uint16_t mask;

        line_number++;
        hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';

        start_text = strtok_r(line, " \t\r\n", &save_ptr);
        if (start_text == NULL)
            continue;
        end_text = strtok_r(NULL, " \t\r\n", &save_ptr);
        mask_text = strtok_r(NULL, " \t\r\n", &save_ptr);

        if (end_text == NULL || mask_text == NULL)
        {
            fprintf(stderr, "capture_manifest: malformed input script line %u\n", line_number);
            fclose(file);
            return false;
        }

        start_frame = strtoul(start_text, NULL, 10);
        end_frame = strtoul(end_text, NULL, 10);
        if (end_frame < start_frame)
        {
            fprintf(stderr, "capture_manifest: invalid frame range on line %u\n", line_number);
            fclose(file);
            return false;
        }

        if (!HostCaptureParseInputMask(mask_text, &mask))
        {
            fprintf(stderr, "capture_manifest: invalid input mask on line %u: %s\n",
                    line_number, mask_text);
            fclose(file);
            return false;
        }

        if (out_script->range_count >= HOST_CAPTURE_MAX_INPUT_RANGES)
        {
            fprintf(stderr, "capture_manifest: too many scripted input ranges\n");
            fclose(file);
            return false;
        }

        out_script->ranges[out_script->range_count].start_frame = (uint32_t)start_frame;
        out_script->ranges[out_script->range_count].end_frame = (uint32_t)end_frame;
        out_script->ranges[out_script->range_count].pressed_mask = mask;
        out_script->range_count++;
    }

    fclose(file);
    return true;
}

bool HostCaptureLoadManifest(const char *path, struct HostCaptureManifest *out_manifest)
{
    FILE *file = fopen(path, "r");
    char line[512];

    if (file == NULL)
    {
        fprintf(stderr, "capture_manifest: could not open manifest %s\n", path);
        return false;
    }

    out_manifest->milestone_count = 0;
    out_manifest->input_path[0] = '\0';

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *hash = strchr(line, '#');
        char name[128];
        char input_file[256];
        unsigned frame;

        if (hash != NULL)
            *hash = '\0';
        if (sscanf(line, "%127s %u %255s", name, &frame, input_file) != 3)
            continue;

        if (out_manifest->milestone_count >= HOST_CAPTURE_MAX_MILESTONES)
        {
            fprintf(stderr, "capture_manifest: too many milestones in %s\n", path);
            fclose(file);
            return false;
        }

        snprintf(out_manifest->milestones[out_manifest->milestone_count].name,
                 sizeof(out_manifest->milestones[out_manifest->milestone_count].name),
                 "%s", name);
        out_manifest->milestones[out_manifest->milestone_count].frame = frame;
        out_manifest->milestone_count++;

        if (out_manifest->input_path[0] == '\0')
        {
            snprintf(out_manifest->input_path, sizeof(out_manifest->input_path), "%s", input_file);
        }
    }

    fclose(file);
    return out_manifest->milestone_count > 0 && out_manifest->input_path[0] != '\0';
}

uint16_t HostCaptureButtonsForFrame(const struct HostCaptureInputScript *script, uint32_t frame)
{
    uint16_t pressed_mask = 0;
    uint32_t i;

    for (i = 0; i < script->range_count; i++)
    {
        if (frame < script->ranges[i].start_frame || frame > script->ranges[i].end_frame)
            continue;
        pressed_mask |= script->ranges[i].pressed_mask;
    }

    return pressed_mask;
}
