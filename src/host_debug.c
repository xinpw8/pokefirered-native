#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "global.h"

/* Undo isagbprint.h empty macros so we can define real implementations */
#ifdef MgbaOpen
#undef MgbaOpen
#endif
#ifdef MgbaClose
#undef MgbaClose
#endif
#ifdef AGBPrintInit
#undef AGBPrintInit
#endif

static void HostVPrint(const char *prefix, const char *fmt, va_list args)
{
    fprintf(stderr, "%s", prefix);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
}

bool32 MgbaOpen(void)
{
    return TRUE;
}

void MgbaClose(void)
{
}

void MgbaPrintf(s32 level, const char *pBuf, ...)
{
    va_list args;
    char prefix[32];

    snprintf(prefix, sizeof(prefix), "[mgba:%d] ", level);
    va_start(args, pBuf);
    HostVPrint(prefix, pBuf, args);
    va_end(args);
}

void MgbaAssert(const char *pFile, s32 nLine, const char *pExpression, bool32 nStopProgram)
{
    fprintf(stderr, "[mgba assert] %s:%d: %s\n", pFile, nLine, pExpression);
    if (nStopProgram)
        abort();
}

void NoCashGBAPrintf(const char *pBuf, ...)
{
    va_list args;
    va_start(args, pBuf);
    HostVPrint("[nocash] ", pBuf, args);
    va_end(args);
}

void NoCashGBAAssert(const char *pFile, s32 nLine, const char *pExpression, bool32 nStopProgram)
{
    fprintf(stderr, "[nocash assert] %s:%d: %s\n", pFile, nLine, pExpression);
    if (nStopProgram)
        abort();
}

void AGBPrintf(const char *pBuf, ...)
{
    va_list args;
    va_start(args, pBuf);
    HostVPrint("[agb] ", pBuf, args);
    va_end(args);
}

void AGBAssert(const char *pFile, int nLine, const char *pExpression, int nStopProgram)
{
    fprintf(stderr, "[agb assert] %s:%d: %s\n", pFile, nLine, pExpression);
    if (nStopProgram)
        abort();
}

void AGBPrintInit(void)
{
}
