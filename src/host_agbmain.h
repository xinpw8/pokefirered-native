#ifndef POKEFIRERED_NATIVE_HOST_AGBMAIN_H
#define POKEFIRERED_NATIVE_HOST_AGBMAIN_H

#include "global.h"

enum HostAgbMainExitReason
{
    HOST_AGBMAIN_EXIT_NONE = 0,
    HOST_AGBMAIN_EXIT_SOFT_RESET,
    HOST_AGBMAIN_EXIT_RETURNED,
};

enum HostAgbMainExitReason HostRunAgbMainUntilSoftReset(u32 exit_after_cb2_calls);
void HostAgbMainOnCb2InitCopyrightScreenAfterBootup(void);
void HostAgbMainOnSoftReset(u32 resetFlags);

#endif
