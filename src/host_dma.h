#ifndef POKEFIRERED_NATIVE_HOST_DMA_H
#define POKEFIRERED_NATIVE_HOST_DMA_H

#include "global.h"

void HostDmaReset(void);
void HostDmaSet(u8 dma_num, const void *src, void *dest, u32 control);
void HostDmaStop(u8 dma_num);
void HostDmaTriggerHBlank(void);
void HostDmaTriggerVBlank(void);

#endif
