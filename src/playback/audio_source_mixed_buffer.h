#ifndef CLIPCUT_PLAYBACK_AUDIO_SOURCE_MIXED_BUFFER_H
#define CLIPCUT_PLAYBACK_AUDIO_SOURCE_MIXED_BUFFER_H

#include <stddef.h>

#include "playback/audio_preview.h"
#include "playback/audio_source.h"

int audio_source_from_mixed_buffer(
    const MixedAudioBuffer *buffer,
    size_t start_frame,
    AudioSource *source
);

#endif
