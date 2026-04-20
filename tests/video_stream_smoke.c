#include "playback/video_stream.h"

#include <SDL.h>

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <media-file> <time_sec>\n", argv[0]);
        return 2;
    }

    if (SDL_Init(SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    VideoStreamState stream;
    char error[256] = {0};
    if (video_stream_init(&stream, error, (int)sizeof(error)) != 0) {
        fprintf(stderr, "video_stream_init failed: %s\n", error);
        SDL_Quit();
        return 1;
    }

    const double time_sec = atof(argv[2]);
    video_stream_request_frame(&stream, argv[1], time_sec);

    PreviewFrame frame = {0};
    uint64_t serial = 0;
    Uint32 started = SDL_GetTicks();
    while (!video_stream_take_frame(&stream, &frame, &serial)) {
        if (video_stream_take_error(&stream, error, (int)sizeof(error))) {
            fprintf(stderr, "video_stream error: %s\n", error);
            video_stream_shutdown(&stream);
            SDL_Quit();
            return 1;
        }
        if ((SDL_GetTicks() - started) > 3000) {
            fprintf(stderr, "video stream smoke timed out\n");
            video_stream_shutdown(&stream);
            SDL_Quit();
            return 1;
        }
        SDL_Delay(10);
    }

    printf("video_stream_smoke=serial:%llu %dx%d pitch:%d time:%0.3f\n",
        (unsigned long long)serial,
        frame.width,
        frame.height,
        frame.pitch,
        time_sec);

    preview_frame_reset(&frame);
    video_stream_shutdown(&stream);
    SDL_Quit();
    return 0;
}
