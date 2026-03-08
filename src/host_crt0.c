#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "gba/gba.h"
#include "host_crt0.h"
#include "host_dma.h"
#include "librfu.h"
#include "main.h"

extern u32 intr_main[];

static const u16 sInterruptFlags[] = {
    INTR_FLAG_VCOUNT,
    INTR_FLAG_SERIAL,
    INTR_FLAG_TIMER3,
    INTR_FLAG_HBLANK,
    INTR_FLAG_VBLANK,
    INTR_FLAG_TIMER0,
    INTR_FLAG_TIMER1,
    INTR_FLAG_TIMER2,
    INTR_FLAG_DMA0,
    INTR_FLAG_DMA1,
    INTR_FLAG_DMA2,
    INTR_FLAG_DMA3,
    INTR_FLAG_KEYPAD,
    INTR_FLAG_GAMEPAK,
};

static int GetInterruptIndex(u16 pendingFlags, u16 *selectedFlag)
{
    int i;

    for (i = 0; i < ARRAY_COUNT(sInterruptFlags); i++)
    {
        if ((pendingFlags & sInterruptFlags[i]) == 0)
            continue;

        *selectedFlag = sInterruptFlags[i];
        return i;
    }

    *selectedFlag = 0;
    return -1;
}

static u16 GetNestedInterruptMask(void)
{
    u16 mask = INTR_FLAG_GAMEPAK
             | INTR_FLAG_SERIAL
             | INTR_FLAG_TIMER3
             | INTR_FLAG_VCOUNT
             | INTR_FLAG_HBLANK;

    if (gSTWIStatus != NULL && gSTWIStatus->timerSelect < 4)
        mask |= (INTR_FLAG_TIMER0 << gSTWIStatus->timerSelect);

    return mask;
}

static void HandleSelectedInterrupt(u16 selectedFlag)
{
    switch (selectedFlag)
    {
    case INTR_FLAG_HBLANK:
        HostDmaTriggerHBlank();
        break;
    case INTR_FLAG_VBLANK:
        HostDmaTriggerVBlank();
        break;
    case INTR_FLAG_GAMEPAK:
        *(vu8 *)REG_ADDR_SOUNDCNT_X = (u8)selectedFlag;
        fprintf(stderr, "host crt0 dispatcher does not support GAMEPAK IRQ spin semantics\n");
        abort();
    }
}

void HostCrt0Init(void)
{
    REG_VCOUNT = DISPLAY_HEIGHT + 1;
    REG_IME = 0;
    REG_IE = 0;
    REG_IF = 0;
    INTR_VECTOR = (u32)(uintptr_t)intr_main;
}

void HostInterruptRaise(u16 flags)
{
    REG_IF |= flags;
}

bool8 HostInterruptDispatch(void)
{
    u16 originalIe;
    u16 originalIme;
    u16 pendingFlags;
    u16 selectedFlag;
    u16 nestedIe;
    int interruptIndex;
    IntrFunc callback;

    if (INTR_VECTOR == 0 || REG_IME == 0)
        return FALSE;

    originalIe = REG_IE;
    originalIme = REG_IME;
    pendingFlags = originalIe & REG_IF;
    interruptIndex = GetInterruptIndex(pendingFlags, &selectedFlag);
    if (interruptIndex < 0)
        return FALSE;

    REG_IME = 0;
    REG_IF &= ~selectedFlag;
    nestedIe = (originalIe & ~selectedFlag) & GetNestedInterruptMask();
    REG_IE = nestedIe;

    HandleSelectedInterrupt(selectedFlag);

    callback = gIntrTable[interruptIndex];
    if (callback != NULL)
        callback();

    REG_IE = originalIe;
    REG_IME = originalIme;
    return TRUE;
}

void HostInterruptDispatchAll(void)
{
    while (HostInterruptDispatch())
    {
    }
}
