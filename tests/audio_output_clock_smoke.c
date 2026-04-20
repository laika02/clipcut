#include "playback/audio_output.h"

#include <SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    if (SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) != 0) {
        fprintf(stderr, "failed to set SDL_AUDIODRIVER: %s\n", SDL_GetError());
        return 1;
    }
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    AudioOutputState output;
    char error[256] = {0};
    audio_output_init(&output);

    if (audio_output_open(&output, error, (int)sizeof(error)) != 0) {
        fprintf(stderr, "audio_output_open failed: %s\n", error);
        SDL_Quit();
        return 1;
    }
    if (audio_output_start(&output, error, (int)sizeof(error)) != 0) {
        fprintf(stderr, "audio_output_start failed: %s\n", error);
        audio_output_close(&output);
        SDL_Quit();
        return 1;
    }

    const size_t frame_count = audio_output_writable_frames(&output) / 2;
    if (frame_count == 0) {
        fprintf(stderr, "audio output reported no writable frames\n");
        audio_output_close(&output);
        SDL_Quit();
        return 1;
    }
    float *samples = calloc(frame_count * 2, sizeof(float));
    if (samples == NULL) {
        fprintf(stderr, "failed to allocate samples\n");
        audio_output_close(&output);
        SDL_Quit();
        return 1;
    }

    for (size_t i = 0; i < frame_count; ++i) {
        const float sample = (float)sin((2.0 * 3.14159265358979323846 * 220.0 * (double)i) / 48000.0) * 0.1f;
        samples[i * 2] = sample;
        samples[(i * 2) + 1] = sample;
    }

    if (audio_output_write_frames(&output, samples, frame_count, 2, error, (int)sizeof(error)) != 0) {
        fprintf(stderr, "audio_output_write_frames failed: %s\n", error);
        free(samples);
        audio_output_close(&output);
        SDL_Quit();
        return 1;
    }
    audio_output_set_draining(&output, true);

    const Uint32 started = SDL_GetTicks();
    while (audio_output_is_playing(&output)) {
        if ((SDL_GetTicks() - started) > 3000) {
            fprintf(stderr, "audio output clock smoke timed out\n");
            free(samples);
            audio_output_close(&output);
            SDL_Quit();
            return 1;
        }
        SDL_Delay(10);
    }

    const double played_sec = audio_output_played_seconds(&output);
    const double expected_sec = (double)frame_count / 48000.0;
    if (played_sec < (expected_sec * 0.90) || played_sec > (expected_sec * 1.10)) {
        fprintf(stderr, "unexpected played seconds: %.6f\n", played_sec);
        free(samples);
        audio_output_close(&output);
        SDL_Quit();
        return 1;
    }

    printf("audio_output_clock_smoke=played:%.3f\n", played_sec);

    free(samples);
    audio_output_close(&output);
    SDL_Quit();
    return 0;
}
