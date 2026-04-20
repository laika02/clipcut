#include "playback/audio_source_mixed_buffer.h"

#include <stdlib.h>
#include <string.h>

typedef struct MixedBufferSourceContext {
    const MixedAudioBuffer *buffer;
    size_t cursor;
} MixedBufferSourceContext;

static size_t mixed_buffer_read_frames(void *ctx, float *dst, size_t max_frames, int channels) {
    MixedBufferSourceContext *source = ctx;
    if (source == NULL || source->buffer == NULL || source->buffer->samples == NULL || dst == NULL) {
        return 0;
    }

    const size_t remaining = source->buffer->frame_count - source->cursor;
    size_t frames_to_copy = max_frames;
    if (frames_to_copy > remaining) {
        frames_to_copy = remaining;
    }

    memcpy(
        dst,
        source->buffer->samples + (source->cursor * (size_t)channels),
        frames_to_copy * (size_t)channels * sizeof(float)
    );
    source->cursor += frames_to_copy;
    return frames_to_copy;
}

static int mixed_buffer_remaining_frames(void *ctx, size_t *remaining) {
    MixedBufferSourceContext *source = ctx;
    if (source == NULL || source->buffer == NULL) {
        return -1;
    }

    if (remaining != NULL) {
        *remaining = source->buffer->frame_count - source->cursor;
    }
    return 0;
}

static void mixed_buffer_destroy(void *ctx) {
    free(ctx);
}

int audio_source_from_mixed_buffer(
    const MixedAudioBuffer *buffer,
    size_t start_frame,
    AudioSource *source
) {
    static const AudioSourceVTable vtable = {
        .read_frames = mixed_buffer_read_frames,
        .remaining_frames = mixed_buffer_remaining_frames,
        .destroy = mixed_buffer_destroy,
    };

    if (buffer == NULL || buffer->samples == NULL || source == NULL) {
        return -1;
    }

    MixedBufferSourceContext *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return -1;
    }

    ctx->buffer = buffer;
    ctx->cursor = start_frame > buffer->frame_count ? buffer->frame_count : start_frame;

    source->ctx = ctx;
    source->sample_rate = buffer->sample_rate;
    source->channels = buffer->channels;
    source->vtable = &vtable;
    return 0;
}
