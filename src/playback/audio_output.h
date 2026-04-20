#ifndef CLIPCUT_PLAYBACK_AUDIO_OUTPUT_H
#define CLIPCUT_PLAYBACK_AUDIO_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL.h>

typedef struct AudioOutputState {
    SDL_AudioDeviceID device_id;
    SDL_AudioSpec obtained_spec;
    float *ring_buffer;
    size_t ring_capacity_frames;
    size_t read_frame_index;
    size_t write_frame_index;
    size_t buffered_frames;
    uint64_t played_frames;
    bool draining;
    bool initialized;
    bool playing;
} AudioOutputState;

void audio_output_init(AudioOutputState *state);
int audio_output_open(AudioOutputState *state, char *error_message, int error_message_size);
void audio_output_close(AudioOutputState *state);
void audio_output_stop(AudioOutputState *state);
int audio_output_start(
    AudioOutputState *state,
    char *error_message,
    int error_message_size
);
size_t audio_output_writable_frames(AudioOutputState *state);
int audio_output_write_frames(
    AudioOutputState *state,
    const float *samples,
    size_t frame_count,
    int channels,
    char *error_message,
    int error_message_size
);
void audio_output_set_draining(AudioOutputState *state, bool draining);
bool audio_output_is_playing(AudioOutputState *state);
double audio_output_played_seconds(const AudioOutputState *state);

#endif
