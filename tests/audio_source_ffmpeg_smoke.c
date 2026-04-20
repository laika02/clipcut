#include "media/probe.h"
#include "model/project.h"
#include "playback/audio_source.h"
#include "playback/audio_source_ffmpeg.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 2 && argc != 4) {
        fprintf(stderr, "usage: %s <media-file> [start_sec end_sec]\n", argv[0]);
        return 2;
    }

    ProjectState project;
    ProbeError probe_error = {0};
    char source_error[256] = {0};
    AudioSource source = {0};
    double start_sec = 0.0;
    double end_sec = 0.0;

    if (argc == 4) {
        start_sec = atof(argv[2]);
        end_sec = atof(argv[3]);
    }

    project_init(&project);
    if (probe_media_file(argv[1], &project, &probe_error) != 0) {
        fprintf(stderr, "probe failed: %s\n", probe_error.message);
        project_reset(&project);
        return 1;
    }

    if (argc != 4) {
        start_sec = project.trim_start_sec;
        end_sec = project.trim_end_sec;
    }

    if (audio_source_from_ffmpeg_stream(
            &project,
            start_sec,
            end_sec,
            &source,
            source_error,
            (int)sizeof(source_error)
        ) != 0) {
        fprintf(stderr, "audio_source_from_ffmpeg_stream failed: %s\n", source_error);
        project_reset(&project);
        return 1;
    }

    const size_t chunk_frames = 1024;
    float *chunk = calloc(chunk_frames * (size_t)source.channels, sizeof(float));
    if (chunk == NULL) {
        fprintf(stderr, "failed to allocate chunk buffer\n");
        audio_source_destroy(&source);
        project_reset(&project);
        return 1;
    }

    size_t total_frames = 0;
    double energy = 0.0;
    while (true) {
        const size_t frames = audio_source_read_frames(&source, chunk, chunk_frames);
        if (frames == 0) {
            break;
        }

        total_frames += frames;
        for (size_t i = 0; i < frames * (size_t)source.channels; ++i) {
            energy += fabs((double)chunk[i]);
        }
    }

    size_t remaining = 0;
    (void)audio_source_remaining_frames(&source, &remaining);
    printf("audio_source_ffmpeg=rate:%d channels:%d frames:%zu remaining:%zu avg_abs:%0.6f\n",
        source.sample_rate,
        source.channels,
        total_frames,
        remaining,
        total_frames > 0 ? energy / (double)(total_frames * (size_t)source.channels) : 0.0);

    free(chunk);
    audio_source_destroy(&source);
    project_reset(&project);
    return 0;
}
