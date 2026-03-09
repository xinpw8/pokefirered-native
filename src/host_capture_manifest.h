#ifndef HOST_CAPTURE_MANIFEST_H
#define HOST_CAPTURE_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HOST_CAPTURE_MAX_INPUT_RANGES 256
#define HOST_CAPTURE_MAX_MILESTONES 64

enum
{
    HOST_CAPTURE_BUTTON_A      = 1u << 0,
    HOST_CAPTURE_BUTTON_B      = 1u << 1,
    HOST_CAPTURE_BUTTON_SELECT = 1u << 2,
    HOST_CAPTURE_BUTTON_START  = 1u << 3,
    HOST_CAPTURE_BUTTON_RIGHT  = 1u << 4,
    HOST_CAPTURE_BUTTON_LEFT   = 1u << 5,
    HOST_CAPTURE_BUTTON_UP     = 1u << 6,
    HOST_CAPTURE_BUTTON_DOWN   = 1u << 7,
    HOST_CAPTURE_BUTTON_R      = 1u << 8,
    HOST_CAPTURE_BUTTON_L      = 1u << 9,
};

struct HostCaptureInputRange
{
    uint32_t start_frame;
    uint32_t end_frame;
    uint16_t pressed_mask;
};

struct HostCaptureMilestone
{
    char name[128];
    uint32_t frame;
};

struct HostCaptureInputScript
{
    struct HostCaptureInputRange ranges[HOST_CAPTURE_MAX_INPUT_RANGES];
    uint32_t range_count;
};

struct HostCaptureManifest
{
    struct HostCaptureMilestone milestones[HOST_CAPTURE_MAX_MILESTONES];
    uint32_t milestone_count;
    char input_path[256];
};

bool HostCaptureParseInputMask(const char *mask_text, uint16_t *out_mask);
bool HostCaptureLoadInputScript(const char *path, struct HostCaptureInputScript *out_script);
bool HostCaptureLoadManifest(const char *path, struct HostCaptureManifest *out_manifest);
uint16_t HostCaptureButtonsForFrame(const struct HostCaptureInputScript *script, uint32_t frame);

#endif
