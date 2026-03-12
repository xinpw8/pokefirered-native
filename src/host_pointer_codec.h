#ifndef POKEFIRERED_NATIVE_HOST_POINTER_CODEC_H
#define POKEFIRERED_NATIVE_HOST_POINTER_CODEC_H

#include <stddef.h>

#include "global.h"

void HostPointerCodecReset(void);
u32 HostEncodePointerToken(const void *target);
const void *HostDecodePointerToken(u32 encoded);
void HostStorePointerHalfwords(u16 *dst, const void *value);
void *HostLoadPointerHalfwords(const u16 *src);
const void *HostDecodeScriptPointer(u32 encoded);
void HostWritePatchedPointer(const void *blobBase, size_t blobSize, u8 *dst, const void *target);

#endif
