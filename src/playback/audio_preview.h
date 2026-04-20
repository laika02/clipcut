#ifndef CLIPCUT_PLAYBACK_AUDIO_PREVIEW_H
#define CLIPCUT_PLAYBACK_AUDIO_PREVIEW_H

#include <stddef.h>

#include "model/project.h"

typedef struct MixedAudioBuffer {
    float *samples;
    int sample_rate;
    int channels;
    size_t frame_count;
} MixedAudioBuffer;

typedef struct AudioPreviewError {
    char message[256];
} AudioPreviewError;

void mixed_audio_buffer_reset(MixedAudioBuffer *buffer);
int render_preview_audio_buffer(
    const ProjectState *project,
    double start_sec,
    double end_sec,
    MixedAudioBuffer *buffer,
    AudioPreviewError *error
);

#endif
