/*
 * host_sound_init.c — Native replacement for m4aSoundInit()
 *
 * Mirrors the real m4aSoundInit() from m4a.c but replaces SampleFreqSet()
 * and m4aSoundMode() with inline field assignments that skip:
 *   - VCount spin-wait loops (REG_VCOUNT never updates outside frame loop)
 *   - Timer register writes (REG_TM0CNT_H / REG_TM0CNT_L)
 *   - DMA register writes (m4aSoundVSyncOff / m4aSoundVSyncOn)
 * All of these hang or are meaningless on native hardware.
 */

#include <string.h>
#include "global.h"
#include "gba/gba.h"
#include "gba/m4a_internal.h"
#include "m4a.h"

/*
 * HostSampleFreqSet — inline replacement for SampleFreqSet().
 *
 * Sets the frequency-related fields in SoundInfo without touching
 * timer registers, DMA registers, or spin-waiting on VCOUNT.
 */
static void HostSampleFreqSet(struct SoundInfo *soundInfo, u32 freq)
{
    extern const u16 gPcmSamplesPerVBlankTable[];

    freq = (freq & 0xF0000) >> 16;
    soundInfo->freq = freq;
    soundInfo->pcmSamplesPerVBlank = gPcmSamplesPerVBlankTable[freq - 1];
    soundInfo->pcmDmaPeriod = PCM_DMA_BUF_SIZE / soundInfo->pcmSamplesPerVBlank;

    /* LCD refresh rate 59.7275Hz */
    soundInfo->pcmFreq = (597275 * soundInfo->pcmSamplesPerVBlank + 5000) / 10000;

    /* CPU frequency 16.78MHz */
    soundInfo->divFreq = (16777216 / soundInfo->pcmFreq + 1) >> 1;

    /* Skip: REG_TM0CNT_H = 0                            (timer off) */
    /* Skip: REG_TM0CNT_L = -(280896 / pcmSamplesPerVBlank) */
    /* Skip: m4aSoundVSyncOn()                            (DMA regs)  */
    /* Skip: VCount spin-wait                                          */
    /* Skip: REG_TM0CNT_H = TIMER_ENABLE | TIMER_1CLK    (timer on)  */

    soundInfo->pcmDmaCounter = 0;
}

/*
 * HostSoundMode — inline replacement for m4aSoundMode().
 *
 * Applies the mode bitmask to SoundInfo fields without calling
 * m4aSoundVSyncOff() or SampleFreqSet() (which both hang on native).
 */
static void HostSoundMode(struct SoundInfo *soundInfo, u32 mode)
{
    u32 temp;

    /* reverb */
    temp = mode & (SOUND_MODE_REVERB_SET | SOUND_MODE_REVERB_VAL);
    if (temp)
        soundInfo->reverb = temp & SOUND_MODE_REVERB_VAL;

    /* max channels */
    temp = mode & SOUND_MODE_MAXCHN;
    if (temp)
    {
        struct SoundChannel *chan;
        u32 n;

        soundInfo->maxChans = temp >> SOUND_MODE_MAXCHN_SHIFT;

        n = MAX_DIRECTSOUND_CHANNELS;
        chan = &soundInfo->chans[0];
        while (n != 0)
        {
            chan->statusFlags = 0;
            n--;
            chan++;
        }
    }

    /* master volume */
    temp = mode & SOUND_MODE_MASVOL;
    if (temp)
        soundInfo->masterVolume = temp >> SOUND_MODE_MASVOL_SHIFT;

    /* DA bit — write to REG_SOUNDBIAS_H is harmless on native */
    temp = mode & SOUND_MODE_DA_BIT;
    if (temp)
    {
        temp = (temp & 0x300000) >> 14;
        REG_SOUNDBIAS_H = (REG_SOUNDBIAS_H & 0x3F) | temp;
    }

    /* frequency — apply via our safe inline version */
    temp = mode & SOUND_MODE_FREQ;
    if (temp)
        HostSampleFreqSet(soundInfo, temp);
}

void HostNativeSoundInit(void)
{
    s32 i;
    struct SoundInfo *soundInfo = &gSoundInfo;

    /* ---- Phase 1: Core SoundInfo setup (mirrors SoundInit) ---- */
    SOUND_INFO_PTR = soundInfo;
    CpuFill32(0, soundInfo, sizeof(struct SoundInfo));

    soundInfo->maxChans = 8;
    soundInfo->masterVolume = 15;
    soundInfo->plynote = (PlyNoteFunc)ply_note;
    soundInfo->CgbSound = DummyFunc;
    soundInfo->CgbOscOff = (CgbOscOffFunc)DummyFunc;
    soundInfo->MidiKeyToCgbFreq = (MidiKeyToCgbFreqFunc)DummyFunc;
    soundInfo->ExtVolPit = (ExtVolPitFunc)DummyFunc;

    /* ---- Phase 2: Jump table ---- */
    MPlayJumpTableCopy(gMPlayJumpTable);
    soundInfo->MPlayJumpTable = gMPlayJumpTable;

    /* ---- Phase 3: Frequency setup (inlined, no hardware writes) ---- */
    HostSampleFreqSet(soundInfo, SOUND_MODE_FREQ_13379);

    soundInfo->ident = ID_NUMBER;

    /* ---- Phase 4: CGB channel extension ---- */
    MPlayExtender(gCgbChans);

    /* ---- Phase 5: Sound mode (inlined, no VSync off/on) ---- */
    HostSoundMode(soundInfo, SOUND_MODE_DA_BIT_8
                           | SOUND_MODE_FREQ_13379
                           | (12 << SOUND_MODE_MASVOL_SHIFT)
                           | (5 << SOUND_MODE_MAXCHN_SHIFT));

    /* ---- Phase 6: Open all music players ---- */
    for (i = 0; i < 4; i++) /* 4 music players: BGM, SE1, SE2, SE3 */
    {
        struct MusicPlayerInfo *mplayInfo = gMPlayTable[i].info;
        MPlayOpen(mplayInfo, gMPlayTable[i].track, gMPlayTable[i].unk_8);
        mplayInfo->unk_B = gMPlayTable[i].unk_A;
        mplayInfo->memAccArea = gMPlayMemAccArea;
    }

    /* ---- Phase 7: Pokémon cry players ---- */
    memcpy(&gPokemonCrySong, &gPokemonCrySongTemplate, sizeof(struct PokemonCrySong));

    for (i = 0; i < MAX_POKEMON_CRIES; i++)
    {
        struct MusicPlayerInfo *mplayInfo = &gPokemonCryMusicPlayers[i];
        struct MusicPlayerTrack *track = &gPokemonCryTracks[i * 2];
        MPlayOpen(mplayInfo, track, 2);
        track->chan = 0;
    }
}
