#include "playback/audio_output.h"
#include "playback/audio_preview.h"
#include "playback/audio_source.h"
#include "playback/audio_source_mixed_buffer.h"
#include "playback/audio_stream.h"

#include <SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void free_synthetic_buffer(MixedAudioBuffer *buffer) {
    free(buffer->samples);
    memset(buffer, 0, sizeof(*buffer));
}

int main(void) {
    if (SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) != 0) {
        fprintf(stderr, "failed to set SDL_AUDIODRIVER: %s\n", SDL_GetError());
        return 1;
    }
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    MixedAudioBuffer buffer = {0};
    buffer.sample_rate = 48000;
    buffer.channels = 2;
    buffer.frame_count = 48000;
    buffer.samples = calloc(buffer.frame_count * (size_t)buffer.channels, sizeof(float));
    if (buffer.samples == NULL) {
        fprintf(stderr, "failed to allocate synthetic audio buffer\n");
        SDL_Quit();
        return 1;
    }

    for (size_t i = 0; i < buffer.frame_count; ++i) {
        const float sample = (float)sin((2.0 * 3.14159265358979323846 * 440.0 * (double)i) / (double)buffer.sample_rate) * 0.1f;
        buffer.samples[i * 2] = sample;
        buffer.samples[(i * 2) + 1] = sample;
    }

    AudioOutputState output;
    AudioStreamState stream;
    AudioSource source = {0};
    char error[256] = {0};

    audio_output_init(&output);
    if (audio_output_open(&output, error, (int)sizeof(error)) != 0) {
        fprintf(stderr, "audio_output_open failed: %s\n", error);
        free_synthetic_buffer(&buffer);
        SDL_Quit();
        return 1;
    }

    if (audio_stream_init(&stream, error, (int)sizeof(error)) != 0) {
        fprintf(stderr, "audio_stream_init failed: %s\n", error);
        audio_output_close(&output);
        free_synthetic_buffer(&buffer);
        SDL_Quit();
        return 1;
    }

    if (audio_source_from_mixed_buffer(&buffer, 0, &source) != 0) {
        fprintf(stderr, "audio_source_from_mixed_buffer failed\n");
        audio_stream_shutdown(&stream);
        audio_output_close(&output);
        free_synthetic_buffer(&buffer);
        SDL_Quit();
        return 1;
    }

    if (audio_stream_start_mixed_buffer(&stream, &output, &source, error, (int)sizeof(error)) != 0) {
        fprintf(stderr, "audio_stream_start_mixed_buffer failed: %s\n", error);
        audio_source_destroy(&source);
        audio_stream_shutdown(&stream);
        audio_output_close(&output);
        free_synthetic_buffer(&buffer);
        SDL_Quit();
        return 1;
    }

    Uint32 started = SDL_GetTicks();
    while (audio_output_is_playing(&output)) {
        char stream_error[256] = {0};
        if (audio_stream_take_error(&stream, stream_error, (int)sizeof(stream_error))) {
            fprintf(stderr, "audio_stream error: %s\n", stream_error);
            audio_stream_shutdown(&stream);
            audio_output_close(&output);
            free_synthetic_buffer(&buffer);
            SDL_Quit();
            return 1;
        }
        if ((SDL_GetTicks() - started) > 3000) {
            fprintf(stderr, "audio stream smoke timed out\n");
            audio_stream_shutdown(&stream);
            audio_output_close(&output);
            free_synthetic_buffer(&buffer);
            SDL_Quit();
            return 1;
        }
        SDL_Delay(10);
    }

    printf("audio_stream_smoke=ok frames:%zu rate:%d channels:%d\n",
        buffer.frame_count,
        buffer.sample_rate,
        buffer.channels);

    audio_stream_shutdown(&stream);
    audio_output_close(&output);
    free_synthetic_buffer(&buffer);
    SDL_Quit();
    return 0;
}
