#include "playback/audio_source.h"

#include <string.h>

size_t audio_source_read_frames(AudioSource *source, float *dst, size_t max_frames) {
    if (source == NULL || source->ctx == NULL || source->vtable == NULL || source->vtable->read_frames == NULL) {
        return 0;
    }

    return source->vtable->read_frames(source->ctx, dst, max_frames, source->channels);
}

int audio_source_remaining_frames(AudioSource *source, size_t *remaining) {
    if (remaining != NULL) {
        *remaining = 0;
    }
    if (source == NULL || source->ctx == NULL || source->vtable == NULL || source->vtable->remaining_frames == NULL) {
        return -1;
    }

    return source->vtable->remaining_frames(source->ctx, remaining);
}

void audio_source_destroy(AudioSource *source) {
    if (source == NULL) {
        return;
    }

    if (source->ctx != NULL && source->vtable != NULL && source->vtable->destroy != NULL) {
        source->vtable->destroy(source->ctx);
    }

    memset(source, 0, sizeof(*source));
}
