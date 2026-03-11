#ifndef POKEFIRERED_NATIVE_HOST_LOG_H
#define POKEFIRERED_NATIVE_HOST_LOG_H

#include <stdio.h>

void HostLogAttachFile(FILE *file);
int HostLogGetFd(void);
void HostLogPrintf(const char *fmt, ...);

#endif
