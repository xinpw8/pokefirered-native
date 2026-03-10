#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba/gba.h"
#include "host_agbmain.h"
#include "host_memory.h"
#include "host_runtime_stubs.h"

static void HostUnimplemented(const char *name)
{
    fprintf(stderr, "host BIOS function not implemented: %s\n", name);
    abort();
}

void SoftReset(u32 resetFlags)
{
    gHostStubSoftResetCalls++;
    HostAgbMainOnSoftReset(resetFlags);
}

void RegisterRamReset(u32 resetFlags)
{
    HostMemoryResetByFlags(resetFlags);
}

void VBlankIntrWait(void)
{
}

u16 Sqrt(u32 num)
{
    return (u16)floor(sqrt((double)num));
}

u16 ArcTan2(s16 x, s16 y)
{
    double angle = atan2((double)y, (double)x);
    long gba_angle = lround(angle * (65536.0 / (2.0 * M_PI)));

    return (u16)gba_angle;
}

static void CpuTransfer(const void *src, void *dest, u32 control, bool32 fast_mode)
{
    size_t unit_size = (control & CPU_SET_32BIT) ? sizeof(u32) : sizeof(u16);
    size_t unit_count = control & 0x1FFFFF;
    const u8 *src_bytes = src;
    u8 *dest_bytes = dest;

    if (fast_mode)
        unit_size = sizeof(u32);

    if (control & CPU_SET_SRC_FIXED)
    {
        for (size_t i = 0; i < unit_count; ++i)
            memcpy(dest_bytes + (i * unit_size), src_bytes, unit_size);
        return;
    }

    memcpy(dest_bytes, src_bytes, unit_count * unit_size);
}

void CpuSet(const void *src, void *dest, u32 control)
{
    CpuTransfer(src, dest, control, FALSE);
}

void CpuFastSet(const void *src, void *dest, u32 control)
{
    CpuTransfer(src, dest, control | CPU_SET_32BIT, TRUE);
}

static double AffineAngleToRadians(u16 angle)
{
    return (double)angle * (2.0 * M_PI / 65536.0);
}

static s16 ToFixed8_8(double value)
{
    long scaled = lround(value * 256.0);

    if (scaled > INT16_MAX)
        scaled = INT16_MAX;
    else if (scaled < INT16_MIN)
        scaled = INT16_MIN;

    return (s16)scaled;
}

void BgAffineSet(struct BgAffineSrcData *src, struct BgAffineDstData *dest, s32 count)
{
    s32 i;

    for (i = 0; i < count; i++)
    {
        double theta = AffineAngleToRadians(src[i].alpha);
        double sine = sin(theta);
        double cosine = cos(theta);
        double scale_x = src[i].sx / 256.0;
        double scale_y = src[i].sy / 256.0;
        s16 pa = ToFixed8_8(cosine * scale_x);
        s16 pb = ToFixed8_8(-sine * scale_x);
        s16 pc = ToFixed8_8(sine * scale_y);
        s16 pd = ToFixed8_8(cosine * scale_y);

        dest[i].pa = pa;
        dest[i].pb = pb;
        dest[i].pc = pc;
        dest[i].pd = pd;
        dest[i].dx = src[i].texX - pa * src[i].scrX - pb * src[i].scrY;
        dest[i].dy = src[i].texY - pc * src[i].scrX - pd * src[i].scrY;
    }
}

void ObjAffineSet(struct ObjAffineSrcData *src, void *dest, s32 count, s32 offset)
{
    u8 *dest_bytes = dest;
    s32 i;

    for (i = 0; i < count; i++)
    {
        double theta = AffineAngleToRadians(src[i].rotation);
        double sine = sin(theta);
        double cosine = cos(theta);
        double scale_x = src[i].xScale / 256.0;
        double scale_y = src[i].yScale / 256.0;
        u8 *matrix = dest_bytes + (i * offset * 4);

        *(s16 *)(matrix + offset * 0) = ToFixed8_8(cosine * scale_x);
        *(s16 *)(matrix + offset * 1) = ToFixed8_8(-sine * scale_x);
        *(s16 *)(matrix + offset * 2) = ToFixed8_8(sine * scale_y);
        *(s16 *)(matrix + offset * 3) = ToFixed8_8(cosine * scale_y);
    }
}

static void Lz77UnComp(const u8 *src, u8 *dest)
{
    if (src[0] == 0)
        return;

    if (src[0] != 0x10)
    {
        fprintf(stderr, "invalid LZ77 header: 0x%02X\n", src[0]);
        abort();
    }

    size_t out_size = src[1] | ((size_t)src[2] << 8) | ((size_t)src[3] << 16);
    src += 4;

    size_t written = 0;
    while (written < out_size)
    {
        u8 flags = *src++;
        for (int bit = 7; bit >= 0 && written < out_size; --bit)
        {
            if ((flags & (1u << bit)) == 0)
            {
                dest[written++] = *src++;
                continue;
            }

            u8 hi = *src++;
            u8 lo = *src++;
            size_t disp = ((size_t)(hi & 0x0F) << 8) | lo;
            size_t length = (hi >> 4) + 3;
            size_t copy_from = written - (disp + 1);

            for (size_t i = 0; i < length && written < out_size; ++i)
                dest[written++] = dest[copy_from + i];
        }
    }
}

void LZ77UnCompWram(const void *src, void *dest)
{
    Lz77UnComp(src, dest);
}

void LZ77UnCompVram(const void *src, void *dest)
{
    Lz77UnComp(src, dest);
}

static void RLUnComp(const u8 *src, u8 *dest)
{
    if (src[0] != 0x30)
    {
        fprintf(stderr, "invalid RL header: 0x%02X\n", src[0]);
        abort();
    }

    size_t out_size = src[1] | ((size_t)src[2] << 8) | ((size_t)src[3] << 16);
    src += 4;

    size_t written = 0;
    while (written < out_size)
    {
        u8 control = *src++;
        size_t count = (control & 0x7F) + 1;

        if (control & 0x80)
        {
            u8 value = *src++;
            count += 2;
            while (count-- != 0 && written < out_size)
                dest[written++] = value;
        }
        else
        {
            while (count-- != 0 && written < out_size)
                dest[written++] = *src++;
        }
    }
}

void RLUnCompWram(const void *src, void *dest)
{
    RLUnComp(src, dest);
}

void RLUnCompVram(const void *src, void *dest)
{
    RLUnComp(src, dest);
}

int MultiBoot(struct MultiBootParam *mp)
{
    (void)mp;
    HostUnimplemented("MultiBoot");
    return -1;
}

s32 Div(s32 num, s32 denom)
{
    if (denom == 0)
        HostUnimplemented("Div by zero");
    return num / denom;
}
