#include "media/probe.h"
#include "model/project.h"

#include <assert.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <media-file>\n", argv[0]);
        return 2;
    }

    ProjectState project;
    ProbeError error = {0};
    project_init(&project);

    int rc = probe_media_file(argv[1], &project, &error);
    if (rc != 0) {
        fprintf(stderr, "probe failed: %s\n", error.message);
        project_reset(&project);
        return 1;
    }

    printf("source=%s\n", project.source_path);
    printf("duration=%.3f\n", project.duration_sec);
    printf("video=%s %dx%d\n", project.video_codec_name, project.width, project.height);
    printf("trim=%.3f %.3f\n", project.trim_start_sec, project.trim_end_sec);
    printf("audio_tracks=%zu\n", project.audio_track_count);
    assert(project.collapse_audio);

    for (size_t i = 0; i < project.audio_track_count; ++i) {
        const AudioTrack *track = &project.audio_tracks[i];
        printf(
            "track[%zu]=stream:%d order:%d codec:%s channels:%d sample_rate:%d language:%s title:%s enabled:%d\n",
            i,
            track->stream_index,
            track->stream_order_among_audio,
            track->codec_name,
            track->channels,
            track->sample_rate,
            track->language,
            track->title,
            track->enabled ? 1 : 0
        );
    }

    project_reset(&project);
    return 0;
}
