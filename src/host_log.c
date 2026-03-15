#include "host_log.h"
#include "host_savestate.h"

#include <stdarg.h>
#include <unistd.h>

static FILE *sHostLogFile;

void HostLogAttachFile(FILE *file)
{
    sHostLogFile = file;
    HostSavestateProtectRegion(&sHostLogFile, sizeof(sHostLogFile));
}

int HostLogGetFd(void)
{
    if (sHostLogFile == NULL)
        return -1;

    return fileno(sHostLogFile);
}

void HostLogPrintf(const char *fmt, ...)
{
    va_list args;
    va_list copy;

    va_start(args, fmt);
    va_copy(copy, args);
    vfprintf(stderr, fmt, args);
    va_end(args);

    if (sHostLogFile != NULL)
    {
        vfprintf(sHostLogFile, fmt, copy);
        fflush(sHostLogFile);
    }

    va_end(copy);
}
