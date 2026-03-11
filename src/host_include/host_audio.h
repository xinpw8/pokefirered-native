#ifndef HOST_AUDIO_H
#define HOST_AUDIO_H

#include "global.h"

void HostAudioInit(void);
void HostAudioShutdown(void);
void HostAudioMixAndPush(void);

#endif /* HOST_AUDIO_H */
