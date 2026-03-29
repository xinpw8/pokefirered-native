/*
 * host_bg_regs.c — Override of upstream bg_regs.c for dynamic GBA addresses.
 *
 * The upstream version uses &REG_* as static initializers for pointer arrays.
 * With dynamic addresses (extern globals), these can't be compile-time constants.
 * Solution: make the arrays non-const and initialize them at runtime via constructor.
 */

#include "global.h"

/* Non-const versions — initialized at runtime */
vu16 *gBGControlRegs[4];
vu16 *gBGHOffsetRegs[4];
vu16 *gBGVOffsetRegs[4];

/* These use REG_OFFSET_* (plain integers) — still const-safe */
const u16 gDISPCNTBGFlags[] = { DISPCNT_BG0_ON, DISPCNT_BG1_ON, DISPCNT_BG2_ON, DISPCNT_BG3_ON };
const u16 gOverworldBackgroundLayerFlags[] = { BLDCNT_TGT2_BG0, BLDCNT_TGT2_BG1, BLDCNT_TGT2_BG2, BLDCNT_TGT2_BG3 };
const u16 gBLDCNTTarget1BGFlags[] = { BLDCNT_TGT1_BG0, BLDCNT_TGT1_BG1, BLDCNT_TGT1_BG2, BLDCNT_TGT1_BG3 };

const u8 gBGControlRegOffsets[] = {
    REG_OFFSET_BG0CNT, REG_OFFSET_BG1CNT, REG_OFFSET_BG2CNT, REG_OFFSET_BG3CNT,
};

const u8 gBGHOffsetRegOffsets[] = {
    REG_OFFSET_BG0HOFS, REG_OFFSET_BG1HOFS, REG_OFFSET_BG2HOFS, REG_OFFSET_BG3HOFS,
};

const u8 gBGVOffsetRegOffsets[] = {
    REG_OFFSET_BG0VOFS, REG_OFFSET_BG1VOFS, REG_OFFSET_BG2VOFS, REG_OFFSET_BG3VOFS,
};

/* Called after HostMemoryInit() sets the dynamic GBA addresses */
void HostBgRegsInit(void)
{
    gBGControlRegs[0] = &REG_BG0CNT;
    gBGControlRegs[1] = &REG_BG1CNT;
    gBGControlRegs[2] = &REG_BG2CNT;
    gBGControlRegs[3] = &REG_BG3CNT;

    gBGHOffsetRegs[0] = &REG_BG0HOFS;
    gBGHOffsetRegs[1] = &REG_BG1HOFS;
    gBGHOffsetRegs[2] = &REG_BG2HOFS;
    gBGHOffsetRegs[3] = &REG_BG3HOFS;

    gBGVOffsetRegs[0] = &REG_BG0VOFS;
    gBGVOffsetRegs[1] = &REG_BG1VOFS;
    gBGVOffsetRegs[2] = &REG_BG2VOFS;
    gBGVOffsetRegs[3] = &REG_BG3VOFS;
}
