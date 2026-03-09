/*
 * host_sound_mixer.c
 *
 * Native C reimplementation of the GBA m4a sound engine assembly routines
 * from m4a_1.s.  Provides every function that the upstream m4a.c expects
 * from the assembly:
 *
 *   - SoundMain (PCM mixer, called from VBlank)
 *   - MPlayMain (music player tick processor)
 *   - m4aSoundVSync (DMA counter management)
 *   - TrackStop, ChnVolSetAsm, RealClearChain, SoundMainBTM
 *   - MPlayJumpTableCopy
 *   - umul3232H32
 *   - All ply_* track command handlers defined in assembly
 *   - ply_note (note playback)
 *
 * On the native host, DMA to audio hardware is a no-op.
 * REG_VCOUNT scanline-budget checks are skipped (always complete).
 */

/* Rename the header's SoundMainBTM declaration to avoid conflict
 * with our definition that takes a void* argument. */
#define SoundMainBTM SoundMainBTM_HeaderDecl
#include <string.h>
#include "global.h"
#include "gba/m4a_internal.h"
#undef SoundMainBTM

/* ---- External references (defined in m4a.c) ---- */

extern void FadeOutBody(struct MusicPlayerInfo *mplayInfo);
extern void TrkVolPitSet(struct MusicPlayerInfo *mplayInfo,
                         struct MusicPlayerTrack *track);
extern void ClearChain(void *x);
extern void Clear64byte(void *x);
extern u32  MidiKeyToFreq(struct WaveData *wav, u8 key, u8 fineAdjust);
extern void ClearModM(struct MusicPlayerTrack *track);

/* External references (defined in m4a_tables.c) */
extern const u8  gClockTable[];
extern const s8  gDeltaEncodingTable[];
extern void *const gMPlayJumpTableTemplate[];

/* Runtime jump table and other globals (defined in m4a.c) */
extern MPlayFunc gMPlayJumpTable[];

/* ---- Constants not in the C headers ---- */

#define TONEDATA_TYPE_REV        0x10
#define TONEDATA_TYPE_CMP        0x20
#define SOUND_CHANNEL_SF_SPECIAL 0x20
#define WAVE_DATA_FLAG_LOOP      0xC0

/* ---- SoundMainRAM dummy (m4a.c copies SoundMainRAM code here) ---- */

char SoundMainRAM[0x800];

/* ---- Static decoding buffer for compressed DPCM audio ---- */

static s8 sDecodingBuffer[64];

/* ================================================================
 * umul3232H32 -- upper 32 bits of a 64-bit unsigned multiply
 * ================================================================ */
u32 umul3232H32(u32 a, u32 b)
{
    return (u32)(((u64)a * (u64)b) >> 32);
}

/* ================================================================
 * SoundMainBTM -- zero 64 bytes at the address passed via the
 * jump table (Clear64byte -> gMPlayJumpTable[35]).
 *
 * The header declares this as void SoundMainBTM(void), but it is
 * only ever called through a function-pointer cast to void(*)(void*).
 * We define it with the real signature to make the zeroing work.
 * The conflicting header prototype is harmless since no code calls
 * SoundMainBTM() directly.
 * ================================================================ */
void SoundMainBTM(void *dest)
{
    memset(dest, 0, 64);
}

/* ================================================================
 * RealClearChain -- unlink a SoundChannel from its track list
 * ================================================================ */
void RealClearChain(void *x)
{
    struct SoundChannel *chan = (struct SoundChannel *)x;
    struct MusicPlayerTrack *track = chan->track;

    if (track == NULL)
        return;

    struct SoundChannel *next = (struct SoundChannel *)chan->nextChannelPointer;
    struct SoundChannel *prev = (struct SoundChannel *)chan->prevChannelPointer;

    if (prev != NULL)
        prev->nextChannelPointer = next;
    else
        track->chan = next;

    if (next != NULL)
        next->prevChannelPointer = prev;

    chan->track = NULL;
}

/* ================================================================
 * DPCM decoder (SoundMainRAM_Unk2 in assembly)
 *
 * Decodes a 64-sample block from 4-bit delta pairs using
 * gDeltaEncodingTable into sDecodingBuffer.
 * Returns the signed sample at the given index.
 * ================================================================ */
static s8 DecodeDPCMSample(struct SoundChannel *chan, s32 sampleIndex)
{
    struct WaveData *wav = chan->wav;
    u32 blockIndex = (u32)sampleIndex >> 6;

    /* xpi/xpc store the previously decoded block index as combined u32.
     * Assembly stores it via str at o_SoundChannel_xpi (which spans xpi+xpc). */
    u32 prevBlock = (u32)chan->xpi | ((u32)chan->xpc << 16);

    if (blockIndex != prevBlock) {
        chan->xpi = (u16)(blockIndex & 0xFFFF);
        chan->xpc = (u16)(blockIndex >> 16);

        /* Each compressed block: 0x21 (33) bytes.
         * 1 initial sample + 32 packed bytes = 64 nybble deltas.
         * Blocks start at wav->data (offset 0x10 from WaveData base). */
        const u8 *src = (const u8 *)wav->data + blockIndex * 0x21;
        s8 *dst = sDecodingBuffer;
        s32 remaining = 0x40;

        s8 prev = (s8)*src++;
        *dst++ = prev;
        remaining--;

        /* First packed byte: process low nybble only.
         * (Assembly: ldrb r1,[r2],1 then branches to the low-nybble path.) */
        u8 byte = *src++;
        prev = prev + gDeltaEncodingTable[byte & 0xF];
        *dst++ = prev;
        remaining--;

        /* Remaining pairs: high nybble then low nybble */
        while (remaining > 0) {
            byte = *src++;
            prev = prev + gDeltaEncodingTable[byte >> 4];
            *dst++ = prev;
            remaining--;
            prev = prev + gDeltaEncodingTable[byte & 0xF];
            *dst++ = prev;
            remaining--;
        }
    }

    return sDecodingBuffer[sampleIndex & 0x3F];
}

/* ================================================================
 * SoundMain -- VBlank entry point for the m4a sound engine
 * ================================================================ */
void SoundMain(void)
{
    struct SoundInfo *si = SOUND_INFO_PTR;

    if (si->ident != ID_NUMBER)
        return;

    si->ident = ID_NUMBER + 1;  /* lock */

    /* Call MPlayMainHead chain */
    if (si->MPlayMainHead != NULL)
        si->MPlayMainHead(si->musicPlayerHead);

    /* Call CgbSound */
    if (si->CgbSound != NULL)
        si->CgbSound();

    /* ---- PCM Mixer ---- */
    {
        s32 samplesPerVBlank = si->pcmSamplesPerVBlank;
        s32 dmaCounter = si->pcmDmaCounter;

        /* Compute current sub-buffer pointer */
        s8 *pcmBuf = si->pcmBuffer;
        if (dmaCounter > 1) {
            s32 periodsIn = si->pcmDmaPeriod - (dmaCounter - 1);
            pcmBuf += periodsIn * samplesPerVBlank;
        }

        s8 *bufL = pcmBuf;             /* left  channel sub-buffer */
        s8 *bufR = pcmBuf + PCM_DMA_BUF_SIZE; /* right channel sub-buffer */

        /* ---- Reverb or zero-fill ---- */
        if (si->reverb) {
            u32 revCoeff = si->reverb;
            s8 *prevBuf;

            if (dmaCounter == 2)
                prevBuf = si->pcmBuffer;   /* wrap to start */
            else
                prevBuf = pcmBuf + samplesPerVBlank;

            s32 n;
            for (n = 0; n < samplesPerVBlank; n++) {
                /* Assembly order: [r5,r6]=current_right, [r5]=current_left,
                 * [r7,r6]=prev_right, [r7]=prev_left.
                 * r6 = PCM_DMA_BUF_SIZE, r5 = bufL, r7 = prevBuf. */
                s32 sum = (s32)(s8)bufL[n + PCM_DMA_BUF_SIZE]  /* current right */
                        + (s32)(s8)bufL[n]                       /* current left */
                        + (s32)(s8)prevBuf[n + PCM_DMA_BUF_SIZE] /* prev right */
                        + (s32)(s8)prevBuf[n];                   /* prev left */

                s32 result = (sum * (s32)revCoeff) >> 9;
                if (result & 0x80)
                    result++;
                bufL[n + PCM_DMA_BUF_SIZE] = (s8)result;  /* right */
                bufL[n] = (s8)result;                       /* left */
            }
        } else {
            memset(bufL, 0, (size_t)samplesPerVBlank);
            memset(bufR, 0, (size_t)samplesPerVBlank);
        }

        /* ---- Per-channel mixing ---- */
        s32 divFreq = si->divFreq;
        struct SoundChannel *chan = si->chans;
        s32 ch;

        for (ch = si->maxChans; ch > 0; ch--, chan++) {
            struct WaveData *wav;
            u8 sf;
            u8 envVol;

            sf = chan->statusFlags;
            if (!(sf & SOUND_CHANNEL_SF_ON))
                continue;

            wav = chan->wav;

            /* ---- Envelope state machine ---- */
            if (sf & SOUND_CHANNEL_SF_START) {
                if (sf & SOUND_CHANNEL_SF_STOP) {
                    chan->statusFlags = 0;
                    continue;
                }
                sf = SOUND_CHANNEL_SF_ENV_ATTACK;
                chan->statusFlags = sf;
                {
                    u32 initOffset = chan->count;
                    chan->currentPointer = (s8 *)wav->data + initOffset;
                    chan->count = wav->size - initOffset;
                }
                envVol = 0;
                chan->envelopeVolume = 0;
                chan->fw = 0;

                if ((wav->status >> 8) & 0x40) {
                    sf |= SOUND_CHANNEL_SF_LOOP;
                    chan->statusFlags = sf;
                }
                goto attack_phase;
            }

            envVol = chan->envelopeVolume;

            if (sf & SOUND_CHANNEL_SF_IEC) {
                u8 echoLen = chan->pseudoEchoLength;
                echoLen--;
                chan->pseudoEchoLength = echoLen;
                if (echoLen > 0)
                    goto calc_volume;
                chan->statusFlags = 0;
                continue;
            }

            if (sf & SOUND_CHANNEL_SF_STOP) {
                u32 nv = (u32)envVol * chan->release >> 8;
                if (nv > chan->pseudoEchoVolume) {
                    envVol = (u8)nv;
                    goto calc_volume;
                }
                goto enter_echo;
            }

            {
                u8 envState = sf & SOUND_CHANNEL_SF_ENV;
                if (envState == SOUND_CHANNEL_SF_ENV_DECAY) {
                    u32 nv = (u32)envVol * chan->decay >> 8;
                    if (nv > chan->sustain) {
                        envVol = (u8)nv;
                        goto calc_volume;
                    }
                    envVol = chan->sustain;
                    if (envVol == 0)
                        goto enter_echo;
                    sf--;
                    chan->statusFlags = sf;
                    goto calc_volume;
                }
                if (envState == SOUND_CHANNEL_SF_ENV_ATTACK)
                    goto attack_phase;
                goto calc_volume;
            }

        attack_phase:
            {
                u32 nv = (u32)envVol + chan->attack;
                if (nv < 0xFF) {
                    envVol = (u8)nv;
                    goto calc_volume;
                }
                envVol = 0xFF;
                sf--;
                chan->statusFlags = sf;
                goto calc_volume;
            }

        enter_echo:
            envVol = chan->pseudoEchoVolume;
            if (envVol == 0) {
                chan->statusFlags = 0;
                continue;
            }
            sf |= SOUND_CHANNEL_SF_IEC;
            chan->statusFlags = sf;

        calc_volume:
            chan->envelopeVolume = envVol;
            {
                u32 scaledEnv = ((u32)si->masterVolume + 1) * envVol >> 4;
                chan->envelopeVolumeRight = (u8)(scaledEnv * chan->rightVolume >> 8);
                chan->envelopeVolumeLeft  = (u8)(scaledEnv * chan->leftVolume >> 8);
            }

            /* Loop info */
            s32 loopLen = 0;
            s8 *loopPtr = NULL;
            if (sf & SOUND_CHANNEL_SF_LOOP) {
                loopPtr = (s8 *)wav->data + wav->loopStart;
                loopLen = (s32)(wav->size - wav->loopStart);
            }

            /* ---- Sample mixing ---- */
            {
                s32 count   = (s32)chan->count;
                s8  *cp     = chan->currentPointer;
                u8  type    = chan->type;
                s32 envR    = chan->envelopeVolumeRight;
                s32 envL    = chan->envelopeVolumeLeft;
                s32 outIdx  = 0;
                s32 remain  = samplesPerVBlank;

                if (type & (TONEDATA_TYPE_CMP | TONEDATA_TYPE_REV)) {
                    /* ---- Compressed / reverse (SoundMainRAM_Unk1) ---- */

                    /* First-time setup */
                    if (!(sf & SOUND_CHANNEL_SF_SPECIAL)) {
                        sf |= SOUND_CHANNEL_SF_SPECIAL;
                        chan->statusFlags = sf;

                        if (type & TONEDATA_TYPE_REV) {
                            /* Reverse pointer:
                             * asm: r1 = wav->size + wav*2 + 0x20 - cp
                             * wav->data = (u8*)wav + 0x10, so wav*2+0x20 = 2*wav+0x20
                             * On native with virtual addresses this won't produce
                             * a valid pointer, so we convert to sample-index form
                             * instead. */
                            s32 offset = (s32)(cp - (s8 *)wav->data);
                            s32 revOffset = (s32)wav->size - offset;
                            cp = (s8 *)wav->data + revOffset;
                            chan->currentPointer = cp;
                        }

                        if (wav->type != 0) {
                            /* Convert to sample index for compressed data */
                            s32 idx = (s32)(cp - (s8 *)wav->data);
                            cp = (s8 *)(uintptr_t)(u32)idx;
                            chan->currentPointer = cp;
                        }
                    }

                    s32 fw = (s32)chan->fw;
                    s32 freq;
                    if (type & TONEDATA_TYPE_FIX)
                        freq = 0x800000;
                    else
                        freq = (s32)((u32)divFreq * chan->frequency);

                    if (wav->type != 0) {
                        /* ---- Compressed DPCM playback ---- */

                        /* Force decode on first access */
                        chan->xpi = 0xFFFF;
                        chan->xpc = 0xFF00;

                        s32 sIdx = (s32)(u32)(uintptr_t)cp;

                        if (type & TONEDATA_TYPE_REV) {
                            /* Reverse compressed */
                            sIdx--;
                            s8 cur = DecodeDPCMSample(chan, sIdx);
                            sIdx--;
                            s8 nxt = DecodeDPCMSample(chan, sIdx);
                            s32 diff = nxt - cur;

                            while (remain > 0) {
                                s32 interp = cur + ((fw * diff) >> 23);
                                bufL[outIdx] = (s8)(bufL[outIdx] + ((envR * interp) >> 8));
                                bufR[outIdx] = (s8)(bufR[outIdx] + ((envL * interp) >> 8));
                                outIdx++;
                                remain--;

                                fw += freq;
                                s32 adv = (u32)fw >> 23;
                                if (adv) {
                                    fw &= 0x7FFFFF;
                                    count -= adv;
                                    if (count <= 0) {
                                        chan->statusFlags = 0;
                                        goto mix_done_cmp;
                                    }
                                    if (adv == 1) {
                                        cur += diff;
                                    } else {
                                        sIdx -= (adv - 1);
                                        cur = DecodeDPCMSample(chan, sIdx);
                                    }
                                    sIdx--;
                                    nxt = DecodeDPCMSample(chan, sIdx);
                                    diff = nxt - cur;
                                }
                            }
                            sIdx += 2; /* restore: assembly does add r3, r3, 2 */
                        } else {
                            /* Forward compressed */
                            s8 cur = DecodeDPCMSample(chan, sIdx);
                            sIdx++;
                            s8 nxt = DecodeDPCMSample(chan, sIdx);
                            s32 diff = nxt - cur;

                            while (remain > 0) {
                                s32 interp = cur + ((fw * diff) >> 23);
                                bufL[outIdx] = (s8)(bufL[outIdx] + ((envR * interp) >> 8));
                                bufR[outIdx] = (s8)(bufR[outIdx] + ((envL * interp) >> 8));
                                outIdx++;
                                remain--;

                                fw += freq;
                                s32 adv = (u32)fw >> 23;
                                if (adv) {
                                    fw &= 0x7FFFFF;
                                    count -= adv;
                                    if (count <= 0) {
                                        if (loopLen > 0) {
                                            s32 loopSt = (s32)wav->loopStart;
                                            while (count <= 0)
                                                count += loopLen;
                                            sIdx = loopSt + (loopLen - count);
                                        } else {
                                            chan->statusFlags = 0;
                                            goto mix_done_cmp;
                                        }
                                    } else {
                                        sIdx += adv;
                                    }
                                    if (adv == 1) {
                                        cur += diff;
                                    } else {
                                        sIdx--;
                                        cur = DecodeDPCMSample(chan, sIdx);
                                        sIdx++;
                                    }
                                    nxt = DecodeDPCMSample(chan, sIdx);
                                    diff = nxt - cur;
                                }
                            }
                            sIdx--; /* assembly: sub r3, r3, 1 */
                        }

                    mix_done_cmp:
                        chan->fw = (u32)fw;
                        chan->count = (u32)count;
                        chan->currentPointer = (s8 *)(uintptr_t)(u32)sIdx;
                        continue;

                    } else if (type & TONEDATA_TYPE_REV) {
                        /* ---- Uncompressed reverse playback ---- */
                        cp--;
                        s8 cur = *cp;
                        s8 prv = *(cp - 1);
                        s32 diff = prv - cur;

                        while (remain > 0) {
                            s32 interp = cur + ((fw * diff) >> 23);
                            bufL[outIdx] = (s8)(bufL[outIdx] + ((envR * interp) >> 8));
                            bufR[outIdx] = (s8)(bufR[outIdx] + ((envL * interp) >> 8));
                            outIdx++;
                            remain--;

                            fw += freq;
                            s32 adv = (u32)fw >> 23;
                            if (adv) {
                                fw &= 0x7FFFFF;
                                count -= adv;
                                if (count <= 0) {
                                    chan->statusFlags = 0;
                                    break;
                                }
                                cp -= adv;
                                cur = *cp;
                                prv = *(cp - 1);
                                diff = prv - cur;
                            }
                        }
                        cp++;  /* assembly: add r3, r3, 1 */

                        chan->fw = (u32)fw;
                        chan->count = (u32)count;
                        chan->currentPointer = cp;
                        continue;
                    }
                    /* CMP set but not compressed and not reverse -- fall through */
                    chan->fw = (u32)fw;
                    chan->count = (u32)count;
                    chan->currentPointer = cp;
                    continue;
                }

                if (type & TONEDATA_TYPE_FIX) {
                    /* ---- Fixed-rate playback (type & 0x08) ---- */
                    /* Assembly: reads samples sequentially, one per output.
                     * Handles count underflow with loop or end. */

                    while (remain > 0) {
                        s32 batch;

                        if (count <= 4) {
                            /* Few samples left -- handle one at a time */
                            while (count > 0 && remain > 0) {
                                s32 sample = *cp++;
                                bufL[outIdx] = (s8)(bufL[outIdx] + ((envR * sample) >> 8));
                                bufR[outIdx] = (s8)(bufR[outIdx] + ((envL * sample) >> 8));
                                outIdx++;
                                remain--;
                                count--;
                            }
                            if (count <= 0) {
                                if (loopLen > 0) {
                                    cp = loopPtr;
                                    count = loopLen;
                                    continue;
                                }
                                chan->statusFlags = 0;
                                break;
                            }
                            continue;
                        }

                        /* Process bulk: min(count, remain) samples */
                        batch = count;
                        if (batch > remain) batch = remain;

                        s32 n;
                        for (n = 0; n < batch; n++) {
                            s32 sample = *cp++;
                            bufL[outIdx] = (s8)(bufL[outIdx] + ((envR * sample) >> 8));
                            bufR[outIdx] = (s8)(bufR[outIdx] + ((envL * sample) >> 8));
                            outIdx++;
                        }
                        count -= batch;
                        remain -= batch;

                        if (count <= 0) {
                            if (loopLen > 0) {
                                cp = loopPtr;
                                count = loopLen;
                            } else {
                                chan->statusFlags = 0;
                                break;
                            }
                        }
                    }

                    chan->count = (u32)count;
                    chan->currentPointer = cp;
                } else {
                    /* ---- Normal playback with linear interpolation ---- */
                    s32 fw = (s32)chan->fw;
                    s32 freq = (s32)((u32)divFreq * chan->frequency);

                    s8 cur = cp[0];
                    cp++;
                    s8 nxt = cp[0];
                    s32 diff = nxt - cur;

                    while (remain > 0) {
                        s32 interp = cur + ((fw * diff) >> 23);
                        bufL[outIdx] = (s8)(bufL[outIdx] + ((envR * interp) >> 8));
                        bufR[outIdx] = (s8)(bufR[outIdx] + ((envL * interp) >> 8));
                        outIdx++;
                        remain--;

                        fw += freq;
                        s32 adv = (u32)fw >> 23;
                        if (adv) {
                            fw &= 0x7FFFFF;
                            count -= adv;
                            if (count <= 0) {
                                if (loopLen > 0) {
                                    while (count <= 0)
                                        count += loopLen;
                                    cp = loopPtr + (loopLen - count);
                                } else {
                                    chan->statusFlags = 0;
                                    break;
                                }
                            } else if (adv == 1) {
                                cur += diff;
                                cp++;
                            } else {
                                cp += (adv - 1);
                                cur = cp[0];
                                cp++;
                            }
                            nxt = cp[0];
                            diff = nxt - cur;
                        }
                    }
                    cp--;
                    chan->fw = (u32)fw;
                    chan->count = (u32)count;
                    chan->currentPointer = cp;
                }
            }
        }
    }

    si->ident = ID_NUMBER;  /* unlock */
}

/* ================================================================
 * m4aSoundVSync -- VBlank DMA counter management
 * ================================================================ */
void m4aSoundVSync(void)
{
    struct SoundInfo *si = SOUND_INFO_PTR;
    u32 ident = si->ident;

    if (ident - ID_NUMBER > 1)
        return;

    s32 counter = (s32)si->pcmDmaCounter - 1;
    si->pcmDmaCounter = (u8)counter;

    if (counter > 0)
        return;

    si->pcmDmaCounter = si->pcmDmaPeriod;

    /* On native host: skip DMA1/DMA2 register manipulation. */
}

/* ================================================================
 * ChnVolSetAsm -- update a channel's left/right volume
 *
 * In the assembly, r4 = chan, r5 = track (set by calling context).
 * This is placed in the jump table at index [32] but is also
 * called directly from MPlayMain and ply_note inner loops.
 * We provide both the internal implementation and the symbol.
 * ================================================================ */
static void ChnVolSetAsm_Internal(struct SoundChannel *chan,
                                   struct MusicPlayerTrack *track)
{
    s32 velocity  = (s32)chan->velocity;
    s32 rhythmPan = (s8)chan->rhythmPan;

    s32 rpRight = (0x80 + rhythmPan) * velocity;
    s32 volR = ((s32)track->volMR * rpRight) >> 14;
    if (volR > 0xFF) volR = 0xFF;
    chan->rightVolume = (u8)volR;

    s32 rpLeft  = (0x7F - rhythmPan) * velocity;
    s32 volL = ((s32)track->volML * rpLeft) >> 14;
    if (volL > 0xFF) volL = 0xFF;
    chan->leftVolume = (u8)volL;
}

/* ================================================================
 * MPlayMain -- process one music player per tick
 * ================================================================ */
void MPlayMain(struct MusicPlayerInfo *mplayInfo)
{
    if (mplayInfo->ident != ID_NUMBER)
        return;

    mplayInfo->ident = ID_NUMBER + 1;  /* lock */

    /* Chain to next music player */
    if (mplayInfo->MPlayMainNext != NULL)
        mplayInfo->MPlayMainNext(mplayInfo->musicPlayerNext);

    if ((s32)mplayInfo->status < 0)
        goto done;

    struct SoundInfo *si = SOUND_INFO_PTR;

    FadeOutBody(mplayInfo);

    if ((s32)mplayInfo->status < 0)
        goto done;

    /* Tempo processing */
    u32 tempoC = (u32)mplayInfo->tempoC + (u32)mplayInfo->tempoI;

    while (tempoC >= 150) {
        /* ---- Process one tick ---- */
        u8  trackCount = mplayInfo->trackCount;
        struct MusicPlayerTrack *trk = mplayInfo->tracks;
        u32 trackBit = 1;
        u32 activeTracks = 0;
        s32 t;

        for (t = (s32)trackCount; t > 0; t--,
             trk = (struct MusicPlayerTrack *)((u8 *)trk + sizeof(*trk)),
             trackBit <<= 1)
        {
            u8 flags = trk->flags;
            if (!(flags & MPT_FLG_EXIST))
                continue;

            activeTracks |= trackBit;

            /* Decrement gateTime on active channels */
            {
                struct SoundChannel *c = trk->chan;
                while (c != NULL) {
                    u8 csf = c->statusFlags;
                    if (csf & SOUND_CHANNEL_SF_ON) {
                        u8 gt = c->gateTime;
                        if (gt != 0) {
                            gt--;
                            c->gateTime = gt;
                            if (gt == 0)
                                c->statusFlags = csf | SOUND_CHANNEL_SF_STOP;
                        }
                    } else {
                        ClearChain(c);
                    }
                    c = (struct SoundChannel *)c->nextChannelPointer;
                }
            }

            flags = trk->flags;

            if (flags & MPT_FLG_START) {
                Clear64byte(trk);
                trk->flags = MPT_FLG_EXIST;
                trk->bendRange = 2;
                trk->volX = 0x40;
                trk->lfoSpeed = 0x16;
                trk->tone.type = 1;
                goto check_wait;
            }

            goto check_wait;

        parse_cmd:
            {
                u8 *cmdPtr = trk->cmdPtr;
                u8 cmd = *cmdPtr;

                if (cmd < 0x80) {
                    cmd = trk->runningStatus;
                } else {
                    cmdPtr++;
                    trk->cmdPtr = cmdPtr;
                    if (cmd >= 0xBD)
                        trk->runningStatus = cmd;
                }

                if (cmd >= 0xCF) {
                    /* Note command */
                    if (si->plynote)
                        si->plynote(cmd - 0xCF, mplayInfo, trk);
                    goto check_wait;
                }

                if (cmd > 0xB0) {
                    u8 cmdIdx = cmd - 0xB1;
                    mplayInfo->cmd = cmdIdx;
                    MPlayFunc func = si->MPlayJumpTable[cmdIdx];
                    ((void (*)(struct MusicPlayerInfo *,
                               struct MusicPlayerTrack *))func)(mplayInfo, trk);
                    if (trk->flags == 0)
                        goto track_done;
                    goto check_wait;
                }

                /* Wait (0x80..0xB0) */
                trk->wait = gClockTable[cmd - 0x80];
            }

        check_wait:
            if (trk->wait == 0)
                goto parse_cmd;

            trk->wait--;

            /* LFO */
            if (trk->lfoSpeed == 0)
                goto track_done;
            if (trk->mod == 0)
                goto track_done;
            if (trk->lfoDelayC != 0) {
                trk->lfoDelayC--;
                goto track_done;
            }

            {
                /* Assembly uses the full (potentially >255) addition result
                 * for the triangle wave, but stores only the low byte. */
                u32 spdC_full = (u32)trk->lfoSpeedC + (u32)trk->lfoSpeed;
                trk->lfoSpeedC = (u8)spdC_full;
                s32 lfoVal;

                /* Triangle wave: check bit 7 of (spdC_full - 0x40) */
                if (((spdC_full - 0x40) & 0xFF) >= 0x80)
                    lfoVal = (s8)(u8)spdC_full;
                else
                    lfoVal = (s32)0x80 - (s32)spdC_full;

                s32 modResult = ((s32)trk->mod * lfoVal) >> 6;

                /* Only update if changed (compare low bytes) */
                if ((u8)modResult == (u8)trk->modM)
                    goto track_done;

                trk->modM = (s8)modResult;

                if (trk->modT == 0)
                    trk->flags |= MPT_FLG_PITCHG;
                else
                    trk->flags |= MPT_FLG_VOLCHG;
            }

        track_done:
            (void)0;
        }

        /* Update clock */
        mplayInfo->clock++;

        if (activeTracks == 0) {
            mplayInfo->status = MUSICPLAYER_STATUS_PAUSE;
            goto done;
        }
        mplayInfo->status = activeTracks;
        tempoC -= 150;
    }

    mplayInfo->tempoC = (u16)tempoC;

    /* ---- Post-tick: update dirty track volumes/pitches ---- */
    {
        u8  trackCount = mplayInfo->trackCount;
        struct MusicPlayerTrack *trk = mplayInfo->tracks;
        s32 t;

        for (t = (s32)trackCount; t > 0; t--,
             trk = (struct MusicPlayerTrack *)((u8 *)trk + sizeof(*trk)))
        {
            u8 flags = trk->flags;
            if (!(flags & MPT_FLG_EXIST))
                continue;
            if (!(flags & (MPT_FLG_VOLCHG | MPT_FLG_PITCHG)))
                continue;

            TrkVolPitSet(mplayInfo, trk);

            struct SoundChannel *c = trk->chan;
            while (c != NULL) {
                u8 csf = c->statusFlags;
                if (!(csf & SOUND_CHANNEL_SF_ON)) {
                    ClearChain(c);
                    goto next_ch;
                }

                u8 cgbType = c->type & TONEDATA_TYPE_CGB;

                if (flags & MPT_FLG_VOLCHG) {
                    ChnVolSetAsm_Internal(c, trk);
                    if (cgbType != 0) {
                        struct CgbChannel *cgb = (struct CgbChannel *)c;
                        cgb->modify |= CGB_CHANNEL_MO_VOL;
                    }
                }

                if (flags & MPT_FLG_PITCHG) {
                    u8  key = c->key;
                    s32 finalKey = (s32)key + (s32)(s8)trk->keyM;
                    if (finalKey < 0) finalKey = 0;

                    if (cgbType != 0) {
                        struct CgbChannel *cgb = (struct CgbChannel *)c;
                        cgb->frequency = si->MidiKeyToCgbFreq(
                            cgbType, (u8)finalKey, trk->pitM);
                        cgb->modify |= CGB_CHANNEL_MO_PIT;
                    } else {
                        c->frequency = MidiKeyToFreq(
                            c->wav, (u8)finalKey, trk->pitM);
                    }
                }

            next_ch:
                c = (struct SoundChannel *)c->nextChannelPointer;
            }

            trk->flags = flags & 0xF0;
        }
    }

done:
    mplayInfo->ident = ID_NUMBER;  /* unlock */
}

/* ================================================================
 * TrackStop -- stop a track and all its channels
 * ================================================================ */
void TrackStop(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;

    if (!(track->flags & MPT_FLG_EXIST))
        return;

    struct SoundChannel *c = track->chan;
    while (c != NULL) {
        if (c->statusFlags != 0) {
            u8 cgbType = c->type & TONEDATA_TYPE_CGB;
            if (cgbType != 0) {
                struct SoundInfo *si = SOUND_INFO_PTR;
                if (si->CgbOscOff)
                    si->CgbOscOff(cgbType);
            }
            c->statusFlags = 0;
        }
        c->track = NULL;
        c = (struct SoundChannel *)c->nextChannelPointer;
    }

    track->chan = NULL;
}

/* ================================================================
 * MPlayJumpTableCopy -- copy gMPlayJumpTableTemplate[36] to dest
 * ================================================================ */
void MPlayJumpTableCopy(MPlayFunc *dest)
{
    s32 i;
    for (i = 0; i < 36; i++)
        dest[i] = (MPlayFunc)gMPlayJumpTableTemplate[i];
}

/* ================================================================
 * Helper: read bytes from track command stream
 * ================================================================ */
static inline u8 TrackReadByte(struct MusicPlayerTrack *track)
{
    u8 *p = track->cmdPtr;
    u8 val = *p;
    track->cmdPtr = p + 1;
    return val;
}

static inline u32 TrackReadPointer(struct MusicPlayerTrack *track)
{
    u8 *p = track->cmdPtr;
    u32 val = (u32)p[0]
            | ((u32)p[1] << 8)
            | ((u32)p[2] << 16)
            | ((u32)p[3] << 24);
    track->cmdPtr = p + 4;
    return val;
}

/* ================================================================
 * Track command handlers
 * ================================================================ */

void ply_fine(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    struct SoundChannel *c = track->chan;
    while (c != NULL) {
        u8 sf = c->statusFlags;
        if (sf & SOUND_CHANNEL_SF_ON)
            c->statusFlags = sf | SOUND_CHANNEL_SF_STOP;
        RealClearChain(c);
        c = (struct SoundChannel *)c->nextChannelPointer;
    }
    track->flags = 0;
}

void ply_goto(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    u32 dest = TrackReadPointer(track);
    track->cmdPtr = (u8 *)(uintptr_t)dest;
}

void ply_patt(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 level = track->patternLevel;
    if (level >= 3) {
        ply_fine(mplayInfo, track);
        return;
    }
    track->patternStack[level] = track->cmdPtr + 4;
    track->patternLevel = level + 1;
    ply_goto(mplayInfo, track);
}

void ply_pend(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    u8 level = track->patternLevel;
    if (level == 0)
        return;
    level--;
    track->patternLevel = level;
    track->cmdPtr = track->patternStack[level];
}

void ply_rept(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 *cmdPtr = track->cmdPtr;
    u8 count = *cmdPtr;

    if (count == 0) {
        /* Infinite repeat: skip the count byte, then goto */
        track->cmdPtr = cmdPtr + 1;
        ply_goto(mplayInfo, track);
        return;
    }

    /* Increment repeat counter */
    track->repN++;
    u8 repN = track->repN;

    /* Read the threshold byte (advances cmdPtr past it) */
    u8 threshold = TrackReadByte(track);

    if (repN < threshold) {
        /* Keep looping: read the goto target */
        ply_goto(mplayInfo, track);
    } else {
        /* Done: reset counter, skip 4-byte pointer */
        track->repN = 0;
        track->cmdPtr += 4;
    }
}

void ply_prio(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->priority = TrackReadByte(track);
}

void ply_tempo(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 val = TrackReadByte(track);
    u16 tempoD = (u16)val << 1;
    mplayInfo->tempoD = tempoD;
    mplayInfo->tempoI = (u16)((u32)tempoD * mplayInfo->tempoU >> 8);
}

void ply_keysh(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->keyShift = (s8)TrackReadByte(track);
    track->flags |= MPT_FLG_PITCHG;
}

void ply_voice(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 voiceNum = TrackReadByte(track);
    struct ToneData *tone = &mplayInfo->tone[voiceNum];

    /* Copy the 12-byte ToneData into the track's embedded copy.
     * Assembly reads three 32-bit words with address validation. */
    *(u32 *)&track->tone.type = *(const u32 *)&tone->type;
    track->tone.wav = tone->wav;
    *(u32 *)&track->tone.attack = *(const u32 *)&tone->attack;
}

void ply_vol(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->vol = TrackReadByte(track);
    track->flags |= MPT_FLG_VOLCHG;
}

void ply_pan(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->pan = (s8)(TrackReadByte(track) - C_V);
    track->flags |= MPT_FLG_VOLCHG;
}

void ply_bend(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->bend = (s8)(TrackReadByte(track) - C_V);
    track->flags |= MPT_FLG_PITCHG;
}

void ply_bendr(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->bendRange = TrackReadByte(track);
    track->flags |= MPT_FLG_PITCHG;
}

void ply_lfos(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    u8 val = TrackReadByte(track);
    track->lfoSpeed = val;
    if (val == 0)
        ClearModM(track);
}

void ply_lfodl(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->lfoDelay = TrackReadByte(track);
}

void ply_mod(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    u8 val = TrackReadByte(track);
    track->mod = val;
    if (val == 0)
        ClearModM(track);
}

void ply_modt(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    u8 val = TrackReadByte(track);
    if (track->modT != val) {
        track->modT = val;
        track->flags |= MPT_FLG_VOLCHG | MPT_FLG_PITCHG;
    }
}

void ply_tune(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->tune = (s8)(TrackReadByte(track) - C_V);
    track->flags |= MPT_FLG_PITCHG;
}

void ply_port(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    /* Assembly: reads reg offset, then value, writes to GBA sound regs.
     * On native host, we just consume the bytes. */
    TrackReadByte(track); /* register offset */
    TrackReadByte(track); /* value */
}

void ply_endtie(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;

    u8 key;
    u8 *cmdPtr = track->cmdPtr;

    if (*cmdPtr < 0x80) {
        key = *cmdPtr;
        track->key = key;
        track->cmdPtr = cmdPtr + 1;
    } else {
        key = track->key;
    }

    struct SoundChannel *c = track->chan;
    while (c != NULL) {
        u8 csf = c->statusFlags;
        if ((csf & (SOUND_CHANNEL_SF_START | SOUND_CHANNEL_SF_ENV))
            && !(csf & SOUND_CHANNEL_SF_STOP)
            && c->midiKey == key)
        {
            c->statusFlags = csf | SOUND_CHANNEL_SF_STOP;
            return;
        }
        c = (struct SoundChannel *)c->nextChannelPointer;
    }
}

/* ================================================================
 * ply_note -- note-on handler
 *
 * Called as si->plynote(note_cmd, mplayInfo, track).
 * note_cmd = original byte - 0xCF (0 = TIE, 1..N = note lengths).
 *
 * This is a large function (assembly lines 1538-1816) that handles:
 *   - Reading key, velocity, gate time from the command stream
 *   - Key-split / rhythm voice lookup
 *   - Channel allocation (steal lowest-priority or stopped channel)
 *   - Setting up the new channel (frequency, volume, envelope, etc.)
 * ================================================================ */
void ply_note(u32 note_cmd, struct MusicPlayerInfo *mplayInfo,
              struct MusicPlayerTrack *track)
{
    struct SoundInfo *si = SOUND_INFO_PTR;

    /* Set gate time from clock table */
    track->gateTime = gClockTable[note_cmd];

    /* Read optional key, velocity, and extra gate time from stream */
    u8 *cmdPtr = track->cmdPtr;
    if (*cmdPtr < 0x80) {
        track->key = *cmdPtr++;
        if (*cmdPtr < 0x80) {
            track->velocity = *cmdPtr++;
            if (*cmdPtr < 0x80) {
                track->gateTime += *cmdPtr++;
            }
        }
        track->cmdPtr = cmdPtr;
    }

    /* Determine the ToneData to use */
    struct ToneData *toneData;
    u8 key = track->key;
    s32 rhythmPanOverride = 0;

    u8 trackToneType = track->tone.type;
    if (trackToneType & (TONEDATA_TYPE_RHY | TONEDATA_TYPE_SPL)) {
        /* Key-split or rhythm: look up sub-voice */
        u8 lookupKey = key;
        struct ToneData *subVoice;

        if (trackToneType & TONEDATA_TYPE_SPL) {
            /* Key-split: use the keySplitTable to map key -> voice index */
            const u8 *keySplitTable = (const u8 *)(uintptr_t)
                *(u32 *)&track->tone.attack; /* ToneData_keySplitTable alias */
            lookupKey = keySplitTable[key];
        }

        subVoice = &((struct ToneData *)track->tone.wav)[lookupKey];

        /* Check if the sub-voice is itself a split/rhythm (shouldn't be) */
        if (subVoice->type & (TONEDATA_TYPE_SPL | TONEDATA_TYPE_RHY))
            return;

        /* For rhythm: check rhythmPan override */
        if (trackToneType & TONEDATA_TYPE_RHY) {
            u8 ps = subVoice->pan_sweep;
            if (ps & 0x80) {
                rhythmPanOverride = (s32)(ps - TONEDATA_P_S_PAN) * 2;
            }
            key = subVoice->key;
        }

        toneData = subVoice;
    } else {
        toneData = &track->tone;
        key = track->key;
    }

    /* Priority = mplayInfo->priority + track->priority, capped at 255 */
    u32 priority = (u32)mplayInfo->priority + (u32)track->priority;
    if (priority > 0xFF) priority = 0xFF;

    /* Determine CGB type */
    u8 cgbType = toneData->type & TONEDATA_TYPE_CGB;

    struct SoundChannel *allocChan = NULL;

    if (cgbType != 0) {
        /* CGB channel allocation */
        struct CgbChannel *cgbChans = si->cgbChans;
        if (cgbChans == NULL)
            return;

        struct CgbChannel *cgb = &cgbChans[cgbType - 1];
        u8 csf = cgb->statusFlags;

        if ((csf & SOUND_CHANNEL_SF_ON)
            && !(csf & SOUND_CHANNEL_SF_STOP))
        {
            u8 cpri = cgb->priority;
            if (cpri > priority)
                return;
            if (cpri == priority && (uintptr_t)cgb->track >= (uintptr_t)track)
                return;
        }

        allocChan = (struct SoundChannel *)cgb;
    } else {
        /* Direct sound channel allocation */
        u8 maxChans = si->maxChans;
        struct SoundChannel *best = NULL;
        u8 bestPri = (u8)priority;
        struct MusicPlayerTrack *bestTrack = track;
        s32 foundStopped = 0;
        struct SoundChannel *c = si->chans;
        s32 n;

        for (n = maxChans; n > 0; n--, c++) {
            u8 csf = c->statusFlags;
            if (!(csf & SOUND_CHANNEL_SF_ON)) {
                /* Free channel */
                best = c;
                goto alloc_ok;
            }

            if (csf & SOUND_CHANNEL_SF_STOP) {
                if (!foundStopped) {
                    foundStopped = 1;
                    bestPri = c->priority;
                    bestTrack = c->track;
                    best = c;
                } else {
                    /* Among stopped: pick lowest priority, then oldest track */
                    if (c->priority < bestPri) {
                        bestPri = c->priority;
                        bestTrack = c->track;
                        best = c;
                    } else if (c->priority == bestPri) {
                        if ((uintptr_t)c->track >= (uintptr_t)bestTrack) {
                            bestTrack = c->track;
                            best = c;
                        }
                    }
                }
                continue;
            }

            if (!foundStopped) {
                /* Among active: same priority comparison */
                if (c->priority < bestPri) {
                    bestPri = c->priority;
                    bestTrack = c->track;
                    best = c;
                } else if (c->priority == bestPri) {
                    if ((uintptr_t)c->track >= (uintptr_t)bestTrack) {
                        bestTrack = c->track;
                        best = c;
                    }
                }
            }
        }

        if (best == NULL)
            return;

        allocChan = best;

    alloc_ok:
        (void)0;
    }

    /* Set up the allocated channel */
    ClearChain(allocChan);

    allocChan->prevChannelPointer = NULL;
    struct SoundChannel *oldHead = track->chan;
    allocChan->nextChannelPointer = oldHead;
    if (oldHead != NULL)
        oldHead->prevChannelPointer = allocChan;
    track->chan = allocChan;
    allocChan->track = track;

    /* Reset LFO delay */
    track->lfoDelayC = track->lfoDelay;
    if (track->lfoDelay != 0)
        ClearModM(track);

    TrkVolPitSet(mplayInfo, track);

    /* Set channel fields.
     * Assembly copies 4 bytes at once: gateTime/key/velocity/runningStatus
     * -> gateTime/midiKey/velocity/priority, then overwrites priority and key. */
    allocChan->gateTime = track->gateTime;
    allocChan->midiKey = track->key;
    allocChan->velocity = track->velocity;
    allocChan->priority = (u8)priority;
    allocChan->key = key;
    allocChan->rhythmPan = (u8)rhythmPanOverride;
    allocChan->type = toneData->type;

    struct WaveData *wav = toneData->wav;
    allocChan->wav = wav;
    *(u32 *)&allocChan->attack = *(const u32 *)&toneData->attack;

    /* Copy pseudo-echo from track */
    allocChan->pseudoEchoVolume = track->pseudoEchoVolume;
    allocChan->pseudoEchoLength = track->pseudoEchoLength;

    /* Volume */
    ChnVolSetAsm_Internal(allocChan, track);

    /* Pitch: compute final key with keyM adjustment */
    s32 finalKey = (s32)key + (s32)(s8)track->keyM;
    if (finalKey < 0) finalKey = 0;

    if (cgbType != 0) {
        /* CGB channel */
        struct CgbChannel *cgb = (struct CgbChannel *)allocChan;
        cgb->length = toneData->length;

        u8 panSweep = toneData->pan_sweep;
        if ((panSweep & 0x80) || !(panSweep & 0x70))
            panSweep = 0x08;
        cgb->sweep = panSweep;

        allocChan->frequency = si->MidiKeyToCgbFreq(
            cgbType, (u8)finalKey, track->pitM);
    } else {
        /* Direct sound channel */
        allocChan->count = track->unk_3C;
        allocChan->frequency = MidiKeyToFreq(wav, (u8)finalKey, track->pitM);
    }

    allocChan->statusFlags = SOUND_CHANNEL_SF_START;
    track->flags &= 0xF0;
}
