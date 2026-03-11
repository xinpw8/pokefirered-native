/*
 * host_audio.c — SDL2 audio backend for pokefirered-native
 *
 * Opens an SDL2 audio device at the GBA's native sample rate (13379 Hz,
 * signed 8-bit mono) and feeds it PCM samples from the m4a sound engine
 * via a lock-free SPSC ring buffer.
 *
 * HostAudioMixAndPush() is called once per VBlank from the game loop:
 *   1. m4aSoundVSync()  — manages pcmDmaCounter
 *   2. SoundMain()      — mixes PCM into SoundInfo.pcmBuffer
 *   3. Push freshly mixed samples into the ring buffer
 *
 * The SDL audio callback pulls samples from the ring buffer.
 */

/*
 * Include SDL2 headers BEFORE project headers to avoid the upstream
 * abs() macro (defined in global.h) colliding with stdlib's abs().
 */
#include <SDL2/SDL.h>
/* undo upstream abs macro that global.h may redefine */
#ifdef abs
#undef abs
#endif

#include <stdio.h>
#include <string.h>
#include "global.h"
#include "gba/gba.h"
#include "gba/m4a_internal.h"
#include "host_log.h"
#include "m4a.h"
#include "host_audio.h"

/* ---- Ring buffer (SPSC, lock-free) ---- */

#define RING_SIZE 8192u  /* must be power of 2 */
#define RING_MASK (RING_SIZE - 1u)

static s8  *sRingBuf;
static u32  sRingHead;   /* written by producer (game thread) */
static u32  sRingTail;   /* written by consumer (audio thread) */

static inline u32 RingAvailable(void)
{
    u32 head = __atomic_load_n(&sRingHead, __ATOMIC_ACQUIRE);
    u32 tail = __atomic_load_n(&sRingTail, __ATOMIC_ACQUIRE);
    return (head - tail) & (RING_SIZE - 1u);
}

static void RingPush(const s8 *data, u32 count)
{
    u32 head = __atomic_load_n(&sRingHead, __ATOMIC_RELAXED);
    u32 tail = __atomic_load_n(&sRingTail, __ATOMIC_ACQUIRE);
    u32 free = (RING_SIZE - 1u) - ((head - tail) & RING_MASK);

    if (count > free)
        count = free;  /* drop oldest-unsent samples silently */

    for (u32 i = 0; i < count; i++)
    {
        sRingBuf[(head + i) & RING_MASK] = data[i];
    }

    __atomic_store_n(&sRingHead, (head + count) & RING_MASK, __ATOMIC_RELEASE);
}

static void RingPull(s8 *out, u32 count)
{
    u32 tail = __atomic_load_n(&sRingTail, __ATOMIC_RELAXED);
    u32 head = __atomic_load_n(&sRingHead, __ATOMIC_ACQUIRE);
    u32 avail = (head - tail) & RING_MASK;

    u32 copy = (count <= avail) ? count : avail;

    for (u32 i = 0; i < copy; i++)
    {
        out[i] = sRingBuf[(tail + i) & RING_MASK];
    }

    /* fill remainder with silence */
    if (copy < count)
        memset(out + copy, 0, count - copy);

    __atomic_store_n(&sRingTail, (tail + copy) & RING_MASK, __ATOMIC_RELEASE);
}

/* ---- SDL2 audio callback ---- */

static SDL_AudioDeviceID sAudioDevice;

static void AudioCallback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    RingPull((s8 *)stream, (u32)len);
}

/* ---- Public API ---- */

void HostAudioInit(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
    {
        HostLogPrintf("host_audio: SDL_InitSubSystem(AUDIO) failed: %s\n",
                      SDL_GetError());
        return;
    }

    sRingBuf = (s8 *)calloc(RING_SIZE, 1);
    if (!sRingBuf)
    {
        HostLogPrintf("host_audio: ring buffer allocation failed\n");
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }

    sRingHead = 0;
    sRingTail = 0;

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq     = 13379;
    want.format   = AUDIO_S8;
    want.channels = 1;
    want.samples  = 1024;
    want.callback = AudioCallback;

    sAudioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (sAudioDevice == 0)
    {
        HostLogPrintf("host_audio: SDL_OpenAudioDevice failed: %s\n",
                      SDL_GetError());
        free(sRingBuf);
        sRingBuf = NULL;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }

    /* Unpause — audio callback starts running */
    SDL_PauseAudioDevice(sAudioDevice, 0);

    HostLogPrintf("host_audio: opened device (freq=%d fmt=0x%04x ch=%d buf=%d)\n",
                  have.freq, have.format, have.channels, have.samples);
}

void HostAudioShutdown(void)
{
    if (sAudioDevice != 0)
    {
        SDL_CloseAudioDevice(sAudioDevice);
        sAudioDevice = 0;
    }
    if (sRingBuf)
    {
        free(sRingBuf);
        sRingBuf = NULL;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void HostAudioLock(void)
{
    if (sAudioDevice != 0)
        SDL_LockAudioDevice(sAudioDevice);
}

void HostAudioUnlock(void)
{
    if (sAudioDevice != 0)
        SDL_UnlockAudioDevice(sAudioDevice);
}

void HostAudioResetBufferedSamples(void)
{
    sRingHead = 0;
    sRingTail = 0;
    if (sRingBuf != NULL)
        memset(sRingBuf, 0, RING_SIZE);
}

void HostAudioMixAndPush(void)
{
    static u32 sFrameCount;
    static u32 sNonZeroFrames;
    static u32 sActiveChannelFrames;
    struct SoundInfo *si = SOUND_INFO_PTR;
    if (si == NULL)
        return;

    /* If audio device failed to open, still run the sound engine so that
     * game-logic queries (IsPokemonCryPlaying, etc.) stay correct. */
    m4aSoundVSync();
    SoundMain();

    sFrameCount++;

    if (sRingBuf == NULL)
        return;

    /* ---- Determine where SoundMain just wrote ----
     *
     * SoundMain reads pcmDmaCounter and computes:
     *   if (dmaCounter <= 1): offset = 0
     *   if (dmaCounter >  1): offset = (pcmDmaPeriod - (dmaCounter-1)) * pcmSamplesPerVBlank
     *
     * pcmBuffer layout: [left channel: PCM_DMA_BUF_SIZE bytes][right: PCM_DMA_BUF_SIZE bytes]
     * Each channel is divided into pcmDmaPeriod sub-buffers of pcmSamplesPerVBlank samples.
     *
     * We replicate the same offset formula to find the freshly mixed data.
     */
    s32 dmaCounter       = (s32)si->pcmDmaCounter;
    s32 samplesPerVBlank = si->pcmSamplesPerVBlank;
    s32 offset;

    if (dmaCounter > 1)
        offset = (si->pcmDmaPeriod - (dmaCounter - 1)) * samplesPerVBlank;
    else
        offset = 0;

    const s8 *bufL = si->pcmBuffer + offset;
    const s8 *bufR = si->pcmBuffer + PCM_DMA_BUF_SIZE + offset;

    /* Mix left + right to mono and push into ring buffer */
    s8 mono[PCM_DMA_BUF_SIZE];  /* large enough for any pcmSamplesPerVBlank */
    s32 nonZero = 0;
    for (s32 i = 0; i < samplesPerVBlank; i++)
    {
        s32 mix = ((s32)bufL[i] + (s32)bufR[i]) >> 1;
        mono[i] = (s8)mix;
        if (mix != 0) nonZero++;
    }
    if (nonZero > 0)
        sNonZeroFrames++;

    /* Count active channels (SF_ON = 0xC7 = SF_START|SF_STOP|SF_IEC|SF_ENV) */
    {
        s32 active = 0;
        for (s32 ch = 0; ch < si->maxChans; ch++)
        {
            if (si->chans[ch].statusFlags & 0xC7) /* SOUND_CHANNEL_SF_ON */
                active++;
        }
        if (active > 0)
            sActiveChannelFrames++;
    }

    /* Periodic diagnostic every 300 frames (~5 seconds) */
    if (sFrameCount % 300 == 0)
    {
        HostLogPrintf("host_audio: frame=%u nonZeroFrames=%u activeChannelFrames=%u ident=0x%08X "
                      "maxChans=%u pcmDmaCounter=%u pcmDmaPeriod=%u samplesPerVBlank=%u "
                      "MPlayMainHead=%p musicPlayerHead=%p\n",
                      sFrameCount, sNonZeroFrames, sActiveChannelFrames,
                      si->ident, si->maxChans, si->pcmDmaCounter, si->pcmDmaPeriod,
                      si->pcmSamplesPerVBlank,
                      (void *)si->MPlayMainHead, (void *)si->musicPlayerHead);
        sNonZeroFrames = 0;
        sActiveChannelFrames = 0;
    }

    RingPush(mono, (u32)samplesPerVBlank);
}
