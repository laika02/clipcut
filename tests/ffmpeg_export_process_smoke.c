#include "export/ffmpeg_export.h"
#include "export/process.h"
#include "model/project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ProjectState make_fixture_project(AudioTrack *tracks, size_t track_count, const char *source_path) {
    ProjectState project;
    memset(&project, 0, sizeof(project));
    project.source_path = (char *)source_path;
    project.duration_sec = 3.0;
    project.width = 320;
    project.height = 240;
    project.trim_start_sec = 0.25;
    project.trim_end_sec = 1.0;
    project.crop = (CropRect){.x = 0, .y = 0, .width = 318, .height = 238};
    project.audio_tracks = tracks;
    project.audio_track_count = track_count;
    return project;
}

static int run_command(ExportCommand *command, const char *label) {
    ProcessResult result = {0};
    char error[256] = {0};
    if (export_run_command_sync(command, &result, error, (int)sizeof(error)) != 0) {
        fprintf(stderr, "%s failed: %s\n", label, error);
        return 1;
    }
    return 0;
}

static void temp_path(const char *filename, char *buffer, size_t buffer_size) {
#ifdef _WIN32
    const char *temp = getenv("TEMP");
    if (temp != NULL && temp[0] != '\0') {
        snprintf(buffer, buffer_size, "%s\\%s", temp, filename);
        return;
    }
#endif
    snprintf(buffer, buffer_size, "/tmp/%s", filename);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <fixture.mp4>\n", argv[0]);
        return 1;
    }

    AudioTrack tracks[2] = {
        {.stream_index = 1, .enabled = true},
        {.stream_index = 2, .enabled = true},
    };
    ProjectState project = make_fixture_project(tracks, 2, argv[1]);
    ExportCommand command = {0};
    char error[256] = {0};
    char preserve_path[1024] = {0};
    char collapse_path[1024] = {0};
    char mp3_path[1024] = {0};
    temp_path("clipcut_export_process_preserve.mp4", preserve_path, sizeof(preserve_path));
    temp_path("clipcut_export_process_collapse.mp4", collapse_path, sizeof(collapse_path));
    temp_path("clipcut_export_process_track2.mp3", mp3_path, sizeof(mp3_path));

    if (export_build_ffmpeg_command(
            &project,
            preserve_path,
            false,
            EXPORT_PROFILE_CPU_FAST,
            &command,
            error,
            (int)sizeof(error)
        ) != 0) {
        fprintf(stderr, "preserve command failed: %s\n", error);
        return 1;
    }
    if (run_command(&command, "preserve export") != 0) {
        export_command_reset(&command);
        return 1;
    }
    export_command_reset(&command);

    if (export_build_ffmpeg_command(
            &project,
            collapse_path,
            true,
            EXPORT_PROFILE_CPU_FAST,
            &command,
            error,
            (int)sizeof(error)
        ) != 0) {
        fprintf(stderr, "collapse command failed: %s\n", error);
        return 1;
    }
    if (run_command(&command, "collapse export") != 0) {
        export_command_reset(&command);
        return 1;
    }
    export_command_reset(&command);

    if (export_build_extract_mp3_command(
            &project,
            1,
            mp3_path,
            &command,
            error,
            (int)sizeof(error)
        ) != 0) {
        fprintf(stderr, "mp3 command failed: %s\n", error);
        return 1;
    }
    if (run_command(&command, "mp3 extract") != 0) {
        export_command_reset(&command);
        return 1;
    }
    export_command_reset(&command);

    printf("ffmpeg_export_process_smoke=ok\n");
    return 0;
}
