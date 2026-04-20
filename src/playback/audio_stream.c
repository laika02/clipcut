#include "playback/audio_stream.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void set_error(char *dst, int dst_size, const char *message) {
    if (dst != NULL && dst_size > 0) {
        snprintf(dst, (size_t)dst_size, "%s", message);
    }
}

static int audio_stream_thread_main(void *userdata) {
    AudioStreamState *state = userdata;

    while (true) {
        SDL_LockMutex(state->mutex);
        const bool running = state->running;
        const bool active = state->active;
        AudioOutputState *output = state->output;

        if (!running) {
            SDL_UnlockMutex(state->mutex);
            break;
        }

        if (!active || output == NULL || state->source.ctx == NULL) {
            SDL_UnlockMutex(state->mutex);
            SDL_Delay(5);
            continue;
        }

        const size_t writable = audio_output_writable_frames(output);
        size_t remaining = 0;
        if (audio_source_remaining_frames(&state->source, &remaining) != 0) {
            snprintf(state->last_error, sizeof(state->last_error), "%s", "Audio source remaining query failed");
            state->active = false;
            SDL_UnlockMutex(state->mutex);
            SDL_Delay(5);
            continue;
        }
        size_t frames_to_write = writable;
        if (frames_to_write > remaining) {
            frames_to_write = remaining;
        }
        if (frames_to_write > 4096) {
            frames_to_write = 4096;
        }

        if (frames_to_write > 0) {
            const size_t channels = (size_t)state->source.channels;
            if (frames_to_write > state->scratch_capacity_frames) {
                float *resized = realloc(
                    state->scratch_buffer,
                    frames_to_write * channels * sizeof(float)
                );
                if (resized == NULL) {
                    snprintf(state->last_error, sizeof(state->last_error), "%s", "Failed to grow audio stream scratch buffer");
                    state->active = false;
                    SDL_UnlockMutex(state->mutex);
                    SDL_Delay(5);
                    continue;
                }
                state->scratch_buffer = resized;
                state->scratch_capacity_frames = frames_to_write;
            }

            frames_to_write = audio_source_read_frames(&state->source, state->scratch_buffer, frames_to_write);
            char write_error[256] = {0};
            if (frames_to_write > 0 && audio_output_write_frames(
                    output,
                    state->scratch_buffer,
                    frames_to_write,
                    state->source.channels,
                    write_error,
                    (int)sizeof(write_error)
                ) != 0) {
                snprintf(state->last_error, sizeof(state->last_error), "%s", write_error);
                state->active = false;
                SDL_UnlockMutex(state->mutex);
                SDL_Delay(5);
                continue;
            }
        }

        remaining = 0;
        (void)audio_source_remaining_frames(&state->source, &remaining);
        audio_output_set_draining(output, remaining == 0);
        SDL_UnlockMutex(state->mutex);
        SDL_Delay(5);
    }

    return 0;
}

int audio_stream_init(AudioStreamState *state, char *error_message, int error_message_size) {
    if (state == NULL) {
        set_error(error_message, error_message_size, "Missing audio stream state");
        return -1;
    }

    memset(state, 0, sizeof(*state));
    state->mutex = SDL_CreateMutex();
    if (state->mutex == NULL) {
        set_error(error_message, error_message_size, "SDL_CreateMutex failed");
        return -1;
    }

    state->running = true;
    state->thread = SDL_CreateThread(audio_stream_thread_main, "clipcut-audio-stream", state);
    if (state->thread == NULL) {
        set_error(error_message, error_message_size, "SDL_CreateThread failed");
        SDL_DestroyMutex(state->mutex);
        memset(state, 0, sizeof(*state));
        return -1;
    }

    return 0;
}

void audio_stream_stop(AudioStreamState *state) {
    if (state == NULL || state->mutex == NULL) {
        return;
    }

    SDL_LockMutex(state->mutex);
    AudioOutputState *output = state->output;
    state->active = false;
    audio_source_destroy(&state->source);
    state->output = NULL;
    state->last_error[0] = '\0';
    SDL_UnlockMutex(state->mutex);

    if (output != NULL) {
        audio_output_stop(output);
    }
}

void audio_stream_shutdown(AudioStreamState *state) {
    if (state == NULL) {
        return;
    }

    audio_stream_stop(state);
    if (state->mutex != NULL) {
        SDL_LockMutex(state->mutex);
        state->running = false;
        SDL_UnlockMutex(state->mutex);
    }
    if (state->thread != NULL) {
        SDL_WaitThread(state->thread, NULL);
    }
    if (state->mutex != NULL) {
        SDL_DestroyMutex(state->mutex);
    }
    free(state->scratch_buffer);

    memset(state, 0, sizeof(*state));
}

int audio_stream_start_mixed_buffer(
    AudioStreamState *state,
    AudioOutputState *output,
    AudioSource *source,
    char *error_message,
    int error_message_size
) {
    if (state == NULL || output == NULL || source == NULL || source->ctx == NULL) {
        set_error(error_message, error_message_size, "Missing audio stream inputs");
        return -1;
    }

    if (audio_output_start(output, error_message, error_message_size) != 0) {
        return -1;
    }

    SDL_LockMutex(state->mutex);
    state->output = output;
    state->source = *source;
    state->active = true;
    state->last_error[0] = '\0';
    SDL_UnlockMutex(state->mutex);
    memset(source, 0, sizeof(*source));
    return 0;
}

bool audio_stream_take_error(AudioStreamState *state, char *error_message, int error_message_size) {
    if (state == NULL || state->mutex == NULL) {
        return false;
    }

    SDL_LockMutex(state->mutex);
    const bool has_error = state->last_error[0] != '\0';
    if (has_error && error_message != NULL && error_message_size > 0) {
        snprintf(error_message, (size_t)error_message_size, "%s", state->last_error);
    }
    state->last_error[0] = '\0';
    SDL_UnlockMutex(state->mutex);
    return has_error;
}
