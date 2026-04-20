#ifndef CLIPCUT_PLAYBACK_AUDIO_SOURCE_H
#define CLIPCUT_PLAYBACK_AUDIO_SOURCE_H

#include <stddef.h>

typedef struct AudioSourceVTable AudioSourceVTable;

typedef struct AudioSource {
    void *ctx;
    int sample_rate;
    int channels;
    AudioSourceVTable const *vtable;
} AudioSource;

struct AudioSourceVTable {
    size_t (*read_frames)(void *ctx, float *dst, size_t max_frames, int channels);
    int (*remaining_frames)(void *ctx, size_t *remaining);
    void (*destroy)(void *ctx);
};

size_t audio_source_read_frames(AudioSource *source, float *dst, size_t max_frames);
int audio_source_remaining_frames(AudioSource *source, size_t *remaining);
void audio_source_destroy(AudioSource *source);

#endif
