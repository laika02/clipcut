#include "playback/video_preview.h"

#include <stdlib.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "usage: %s <media-file> [time_sec]\n", argv[0]);
        return 2;
    }

    PreviewFrame frame = {0};
    PreviewError error = {0};
    const double time_sec = argc == 3 ? atof(argv[2]) : 0.0;
    int rc = decode_preview_frame_at_time(argv[1], time_sec, &frame, &error);
    if (rc != 0) {
        fprintf(stderr, "preview decode failed: %s\n", error.message);
        preview_frame_reset(&frame);
        return 1;
    }

    printf("preview=%dx%d pitch=%d pixels=%s time=%0.3f\n",
        frame.width,
        frame.height,
        frame.pitch,
        frame.pixels != NULL ? "yes" : "no",
        time_sec);

    preview_frame_reset(&frame);
    return 0;
}
