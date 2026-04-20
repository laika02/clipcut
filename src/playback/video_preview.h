#ifndef CLIPCUT_PLAYBACK_VIDEO_PREVIEW_H
#define CLIPCUT_PLAYBACK_VIDEO_PREVIEW_H

#include <stdint.h>

typedef struct PreviewFrame {
    int width;
    int height;
    int pitch;
    uint8_t *pixels;
} PreviewFrame;

typedef struct PreviewError {
    char message[256];
} PreviewError;

void preview_frame_reset(PreviewFrame *frame);
int decode_preview_frame_at_time(const char *path, double time_sec, PreviewFrame *frame, PreviewError *error);
int decode_preview_frame(const char *path, PreviewFrame *frame, PreviewError *error);

#endif
