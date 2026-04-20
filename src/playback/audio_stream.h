#ifndef CLIPCUT_PLAYBACK_AUDIO_STREAM_H
#define CLIPCUT_PLAYBACK_AUDIO_STREAM_H

#include <stdbool.h>
#include <stddef.h>

#include "playback/audio_output.h"
#include "playback/audio_source.h"

typedef struct AudioStreamState {
    SDL_Thread *thread;
    SDL_mutex *mutex;
    AudioOutputState *output;
    AudioSource source;
    float *scratch_buffer;
    size_t scratch_capacity_frames;
    bool running;
    bool active;
    char last_error[256];
} AudioStreamState;

int audio_stream_init(AudioStreamState *state, char *error_message, int error_message_size);
void audio_stream_shutdown(AudioStreamState *state);
void audio_stream_stop(AudioStreamState *state);
int audio_stream_start_mixed_buffer(
    AudioStreamState *state,
    AudioOutputState *output,
    AudioSource *source,
    char *error_message,
    int error_message_size
);
bool audio_stream_take_error(AudioStreamState *state, char *error_message, int error_message_size);

#endif
