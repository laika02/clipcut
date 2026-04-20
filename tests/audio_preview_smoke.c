#include "media/probe.h"
#include "model/project.h"
#include "playback/audio_preview.h"

#include <math.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <media-file>\n", argv[0]);
        return 2;
    }

    ProjectState project;
    ProbeError probe_error = {0};
    AudioPreviewError audio_error = {0};
    MixedAudioBuffer buffer = {0};

    project_init(&project);
    if (probe_media_file(argv[1], &project, &probe_error) != 0) {
        fprintf(stderr, "probe failed: %s\n", probe_error.message);
        project_reset(&project);
        return 1;
    }

    if (render_preview_audio_buffer(&project, project.trim_start_sec, project.trim_end_sec, &buffer, &audio_error) != 0) {
        fprintf(stderr, "audio preview failed: %s\n", audio_error.message);
        mixed_audio_buffer_reset(&buffer);
        project_reset(&project);
        return 1;
    }

    double energy = 0.0;
    const size_t sample_count = buffer.frame_count * (size_t)buffer.channels;
    for (size_t i = 0; i < sample_count; ++i) {
        energy += fabs((double)buffer.samples[i]);
    }

    printf("audio_preview=rate:%d channels:%d frames:%zu avg_abs:%0.6f\n",
        buffer.sample_rate,
        buffer.channels,
        buffer.frame_count,
        sample_count > 0 ? energy / (double)sample_count : 0.0);

    mixed_audio_buffer_reset(&buffer);
    project_reset(&project);
    return 0;
}
