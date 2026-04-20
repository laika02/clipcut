#include "playback/audio_output.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void audio_output_callback(void *userdata, Uint8 *stream, int len) {
    AudioOutputState *state = userdata;
    SDL_memset(stream, 0, (size_t)len);

    if (state == NULL || !state->playing || state->ring_buffer == NULL) {
        return;
    }

    const size_t bytes_per_frame =
        (size_t)state->obtained_spec.channels * (sizeof(float));
    if (bytes_per_frame == 0) {
        state->playing = false;
        return;
    }

    const size_t requested_frames = (size_t)len / bytes_per_frame;
    float *dst = (float *)stream;
    size_t frames_written = 0;
    while (frames_written < requested_frames && state->buffered_frames > 0) {
        const size_t contiguous_frames =
            state->write_frame_index > state->read_frame_index
                ? (state->write_frame_index - state->read_frame_index)
                : (state->ring_capacity_frames - state->read_frame_index);
        size_t frames_to_copy = contiguous_frames;
        const size_t frames_remaining = requested_frames - frames_written;
        if (frames_to_copy > frames_remaining) {
            frames_to_copy = frames_remaining;
        }
        if (frames_to_copy > state->buffered_frames) {
            frames_to_copy = state->buffered_frames;
        }

        const size_t samples_to_copy = frames_to_copy * (size_t)state->obtained_spec.channels;
        SDL_memcpy(
            dst + (frames_written * (size_t)state->obtained_spec.channels),
            state->ring_buffer + (state->read_frame_index * (size_t)state->obtained_spec.channels),
            samples_to_copy * sizeof(float)
        );

        state->read_frame_index = (state->read_frame_index + frames_to_copy) % state->ring_capacity_frames;
        state->buffered_frames -= frames_to_copy;
        frames_written += frames_to_copy;
    }
    state->played_frames += (uint64_t)frames_written;

    if (state->buffered_frames == 0 && state->draining) {
        state->playing = false;
    }
}

void audio_output_init(AudioOutputState *state) {
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
}

int audio_output_open(AudioOutputState *state, char *error_message, int error_message_size) {
    if (state == NULL) {
        return -1;
    }
    if (state->initialized) {
        return 0;
    }

    SDL_AudioSpec desired;
    SDL_zero(desired);
    desired.freq = 48000;
    desired.format = AUDIO_F32SYS;
    desired.channels = 2;
    desired.samples = 2048;
    desired.callback = audio_output_callback;
    desired.userdata = state;

    state->device_id = SDL_OpenAudioDevice(NULL, 0, &desired, &state->obtained_spec, 0);
    if (state->device_id == 0) {
        if (error_message != NULL && error_message_size > 0) {
            snprintf(error_message, (size_t)error_message_size, "SDL_OpenAudioDevice failed: %s", SDL_GetError());
        }
        return -1;
    }

    state->ring_capacity_frames = (size_t)state->obtained_spec.samples * 8u;
    state->ring_buffer = calloc(
        state->ring_capacity_frames * (size_t)state->obtained_spec.channels,
        sizeof(float)
    );
    if (state->ring_buffer == NULL) {
        if (error_message != NULL && error_message_size > 0) {
            snprintf(error_message, (size_t)error_message_size, "Failed to allocate audio ring buffer");
        }
        SDL_CloseAudioDevice(state->device_id);
        memset(&state->obtained_spec, 0, sizeof(state->obtained_spec));
        state->device_id = 0;
        return -1;
    }

    state->initialized = true;
    state->playing = false;
    SDL_PauseAudioDevice(state->device_id, 1);
    return 0;
}

void audio_output_stop(AudioOutputState *state) {
    if (state == NULL || !state->initialized) {
        return;
    }

    SDL_LockAudioDevice(state->device_id);
    state->read_frame_index = 0;
    state->write_frame_index = 0;
    state->buffered_frames = 0;
    state->played_frames = 0;
    state->draining = false;
    state->playing = false;
    SDL_UnlockAudioDevice(state->device_id);
    SDL_PauseAudioDevice(state->device_id, 1);
}

void audio_output_close(AudioOutputState *state) {
    if (state == NULL) {
        return;
    }

    if (state->initialized) {
        audio_output_stop(state);
        SDL_CloseAudioDevice(state->device_id);
    }

    free(state->ring_buffer);

    memset(state, 0, sizeof(*state));
}

int audio_output_start(
    AudioOutputState *state,
    char *error_message,
    int error_message_size
) {
    if (!state->initialized) {
        if (error_message != NULL && error_message_size > 0) {
            snprintf(error_message, (size_t)error_message_size, "Audio device not initialized");
        }
        return -1;
    }

    SDL_LockAudioDevice(state->device_id);
    state->read_frame_index = 0;
    state->write_frame_index = 0;
    state->buffered_frames = 0;
    state->played_frames = 0;
    state->draining = false;
    state->playing = true;
    SDL_UnlockAudioDevice(state->device_id);
    SDL_PauseAudioDevice(state->device_id, 0);
    return 0;
}

size_t audio_output_writable_frames(AudioOutputState *state) {
    if (state == NULL || !state->initialized) {
        return 0;
    }

    SDL_LockAudioDevice(state->device_id);
    const size_t writable = state->ring_capacity_frames - state->buffered_frames;
    SDL_UnlockAudioDevice(state->device_id);
    return writable;
}

int audio_output_write_frames(
    AudioOutputState *state,
    const float *samples,
    size_t frame_count,
    int channels,
    char *error_message,
    int error_message_size
) {
    if (state == NULL || !state->initialized || samples == NULL) {
        if (error_message != NULL && error_message_size > 0) {
            snprintf(error_message, (size_t)error_message_size, "Missing audio output state or samples");
        }
        return -1;
    }
    if (channels != state->obtained_spec.channels || state->obtained_spec.format != AUDIO_F32SYS) {
        if (error_message != NULL && error_message_size > 0) {
            snprintf(error_message, (size_t)error_message_size, "Audio output format mismatch");
        }
        return -1;
    }

    SDL_LockAudioDevice(state->device_id);
    const size_t writable = state->ring_capacity_frames - state->buffered_frames;
    if (frame_count > writable) {
        SDL_UnlockAudioDevice(state->device_id);
        if (error_message != NULL && error_message_size > 0) {
            snprintf(error_message, (size_t)error_message_size, "Audio ring buffer overflow");
        }
        return -1;
    }

    size_t frames_written = 0;
    while (frames_written < frame_count) {
        const size_t contiguous_frames =
            state->write_frame_index >= state->read_frame_index
                ? (state->ring_capacity_frames - state->write_frame_index)
                : (state->read_frame_index - state->write_frame_index);
        size_t frames_to_copy = frame_count - frames_written;
        if (frames_to_copy > contiguous_frames) {
            frames_to_copy = contiguous_frames;
        }

        const size_t samples_to_copy = frames_to_copy * (size_t)channels;
        SDL_memcpy(
            state->ring_buffer + (state->write_frame_index * (size_t)channels),
            samples + (frames_written * (size_t)channels),
            samples_to_copy * sizeof(float)
        );

        state->write_frame_index = (state->write_frame_index + frames_to_copy) % state->ring_capacity_frames;
        state->buffered_frames += frames_to_copy;
        frames_written += frames_to_copy;
    }

    SDL_UnlockAudioDevice(state->device_id);
    return 0;
}

void audio_output_set_draining(AudioOutputState *state, bool draining) {
    if (state == NULL || !state->initialized) {
        return;
    }

    SDL_LockAudioDevice(state->device_id);
    state->draining = draining;
    SDL_UnlockAudioDevice(state->device_id);
}

bool audio_output_is_playing(AudioOutputState *state) {
    if (state == NULL || !state->initialized) {
        return false;
    }

    SDL_LockAudioDevice(state->device_id);
    const bool playing = state->playing;
    SDL_UnlockAudioDevice(state->device_id);
    return playing;
}

double audio_output_played_seconds(const AudioOutputState *state) {
    if (state == NULL || !state->initialized || state->obtained_spec.freq <= 0) {
        return 0.0;
    }

    SDL_LockAudioDevice(state->device_id);
    const uint64_t played_frames = state->played_frames;
    const int sample_rate = state->obtained_spec.freq;
    SDL_UnlockAudioDevice(state->device_id);
    return (double)played_frames / (double)sample_rate;
}
