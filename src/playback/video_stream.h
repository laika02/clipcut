#ifndef CLIPCUT_PLAYBACK_VIDEO_STREAM_H
#define CLIPCUT_PLAYBACK_VIDEO_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL.h>

#include "playback/video_preview.h"

typedef struct VideoStreamState {
    SDL_Thread *thread;
    SDL_mutex *mutex;
    bool running;
    char source_path[1024];
    double requested_time_sec;
    uint64_t request_serial;
    uint64_t handled_serial;
    PreviewFrame latest_frame;
    uint64_t latest_frame_serial;
    char last_error[256];
} VideoStreamState;

int video_stream_init(VideoStreamState *state, char *error_message, int error_message_size);
void video_stream_shutdown(VideoStreamState *state);
void video_stream_request_frame(VideoStreamState *state, const char *source_path, double time_sec);
bool video_stream_take_frame(VideoStreamState *state, PreviewFrame *frame_out, uint64_t *serial_out);
bool video_stream_take_error(VideoStreamState *state, char *error_message, int error_message_size);

#endif
