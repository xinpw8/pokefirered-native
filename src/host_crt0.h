#ifndef POKEFIRERED_NATIVE_HOST_CRT0_H
#define POKEFIRERED_NATIVE_HOST_CRT0_H

#include "global.h"

void HostCrt0Init(void);
void HostInterruptRaise(u16 flags);
bool8 HostInterruptDispatch(void);
void HostInterruptDispatchAll(void);

#endif
