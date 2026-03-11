#ifndef HOST_SOUND_INIT_H
#define HOST_SOUND_INIT_H

/*
 * HostNativeSoundInit — full m4aSoundInit replacement for native.
 *
 * Sets up SOUND_INFO_PTR, SoundInfo fields, music players, and cry players
 * without writing to DMA/timer hardware registers (which hang on native).
 */
void HostNativeSoundInit(void);

#endif /* HOST_SOUND_INIT_H */
