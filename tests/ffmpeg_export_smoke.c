#include "export/ffmpeg_export.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static ProjectState make_project(AudioTrack *tracks, size_t track_count) {
    ProjectState project;
    memset(&project, 0, sizeof(project));
    project.source_path = "input with spaces.mp4";
    project.duration_sec = 12.0;
    project.width = 1920;
    project.height = 1080;
    project.trim_start_sec = 1.25;
    project.trim_end_sec = 8.75;
    project.crop = (CropRect){.x = 11, .y = 13, .width = 639, .height = 359};
    project.audio_tracks = tracks;
    project.audio_track_count = track_count;
    return project;
}

static int arg_index(const ExportCommand *command, const char *arg) {
    for (int i = 0; i < command->argc; ++i) {
        if (strcmp(command->argv[i], arg) == 0) {
            return i;
        }
    }
    return -1;
}

int main(void) {
    AudioTrack tracks[3] = {
        {.stream_index = 1, .enabled = true},
        {.stream_index = 2, .enabled = false},
        {.stream_index = 3, .enabled = true},
    };
    ProjectState project = make_project(tracks, 3);
    ExportCommand command = {0};
    char error[256] = {0};
    char display[2048] = {0};

    assert(export_build_ffmpeg_command(&project, "out.mp4", false, EXPORT_PROFILE_CPU_FAST, &command, error, (int)sizeof(error)) == 0);
    assert(command.argc > 0);
    assert(strcmp(command.argv[0], "ffmpeg") == 0);
    assert(arg_index(&command, "input with spaces.mp4") >= 0);
    assert(arg_index(&command, "-t") >= 0);
    assert(arg_index(&command, "7.500000") >= 0);
    assert(arg_index(&command, "crop=638:358:10:12") >= 0);
    assert(arg_index(&command, "veryfast") >= 0);
    assert(arg_index(&command, "23") >= 0);
    assert(arg_index(&command, "0:1") >= 0);
    assert(arg_index(&command, "0:2") < 0);
    assert(arg_index(&command, "0:3") >= 0);
    assert(arg_index(&command, "-filter_complex") < 0);
    assert(export_command_join_for_display(&command, display, (int)sizeof(display)) == 0);
    printf("preserve=%s\n", display);
    export_command_reset(&command);

    project.crop = (CropRect){.x = 0, .y = 0, .width = 1920, .height = 1080};
    assert(export_build_ffmpeg_command(&project, "copy.mp4", false, EXPORT_PROFILE_NVIDIA_NVENC, &command, error, (int)sizeof(error)) == 0);
    assert(arg_index(&command, "-c:v") >= 0);
    assert(arg_index(&command, "copy") >= 0);
    assert(arg_index(&command, "h264_nvenc") < 0);
    assert(arg_index(&command, "-vf") < 0);
    export_command_reset(&command);

    assert(export_build_ffmpeg_command(&project, "vaapi.mp4", false, EXPORT_PROFILE_INTEL_VAAPI, &command, error, (int)sizeof(error)) == 0);
    assert(arg_index(&command, "-vaapi_device") < 0);
    assert(arg_index(&command, "copy") >= 0);
    export_command_reset(&command);

    project.crop = (CropRect){.x = 2, .y = 2, .width = 1900, .height = 1000};
    assert(export_build_ffmpeg_command(&project, "vaapi-crop.mp4", false, EXPORT_PROFILE_INTEL_VAAPI, &command, error, (int)sizeof(error)) == 0);
    assert(arg_index(&command, "-vaapi_device") >= 0);
    assert(arg_index(&command, "h264_vaapi") >= 0);
    assert(arg_index(&command, "crop=1900:1000:2:2,format=nv12,hwupload") >= 0);
    export_command_reset(&command);

    assert(export_build_ffmpeg_command(&project, "qsv-crop.mp4", false, EXPORT_PROFILE_INTEL_QSV, &command, error, (int)sizeof(error)) == 0);
    assert(arg_index(&command, "h264_qsv") >= 0);
    assert(arg_index(&command, "crop=1900:1000:2:2,format=nv12") >= 0);
    export_command_reset(&command);

    assert(export_build_ffmpeg_command(&project, "nvenc-crop.mp4", false, EXPORT_PROFILE_NVIDIA_NVENC, &command, error, (int)sizeof(error)) == 0);
    assert(arg_index(&command, "h264_nvenc") >= 0);
    assert(arg_index(&command, "p4") >= 0);
    export_command_reset(&command);
    project.crop = (CropRect){.x = 0, .y = 0, .width = 1920, .height = 1080};

    CutRange cuts[1] = {{.start_sec = 3.0, .end_sec = 4.0}};
    project.cuts = cuts;
    project.cut_count = 1;
    assert(export_build_ffmpeg_command(&project, "cut.mp4", false, EXPORT_PROFILE_CPU_BALANCED, &command, error, (int)sizeof(error)) == 0);
    assert(arg_index(&command, "-filter_complex") >= 0);
    assert(arg_index(&command, "-ss") < 0);
    assert(arg_index(&command, "-t") < 0);
    assert(arg_index(&command, "[vout]") >= 0);
    assert(arg_index(&command, "faster") >= 0);
    assert(export_command_join_for_display(&command, display, (int)sizeof(display)) == 0);
    printf("cut=%s\n", display);
    export_command_reset(&command);
    project.cuts = NULL;
    project.cut_count = 0;
    project.crop = (CropRect){.x = 11, .y = 13, .width = 639, .height = 359};

    assert(export_build_ffmpeg_command(&project, "out.mp4", true, EXPORT_PROFILE_CPU_FAST, &command, error, (int)sizeof(error)) == 0);
    assert(arg_index(&command, "-filter_complex") >= 0);
    assert(arg_index(&command, "[0:1][0:3]amix=inputs=2:duration=longest:normalize=0[aout]") >= 0);
    assert(arg_index(&command, "[aout]") >= 0);
    assert(export_command_join_for_display(&command, display, (int)sizeof(display)) == 0);
    printf("collapse=%s\n", display);
    export_command_reset(&command);

    assert(export_build_extract_mp3_command(&project, 2, "track 3.mp3", &command, error, (int)sizeof(error)) == 0);
    assert(arg_index(&command, "0:3") >= 0);
    assert(arg_index(&command, "-vn") >= 0);
    assert(arg_index(&command, "libmp3lame") >= 0);
    assert(arg_index(&command, "192k") >= 0);
    assert(arg_index(&command, "track 3.mp3") >= 0);
    assert(export_command_join_for_display(&command, display, (int)sizeof(display)) == 0);
    printf("mp3=%s\n", display);
    export_command_reset(&command);

    tracks[0].enabled = false;
    tracks[2].enabled = false;
    assert(export_build_ffmpeg_command(&project, "out.mp4", false, EXPORT_PROFILE_CPU_FAST, &command, error, (int)sizeof(error)) == 0);
    assert(arg_index(&command, "-an") >= 0);
    assert(arg_index(&command, "-c:a") < 0);
    export_command_reset(&command);

    assert(export_build_extract_mp3_command(&project, 99, "bad.mp3", &command, error, (int)sizeof(error)) != 0);
    assert(strstr(error, "Invalid audio track index") != NULL);

    return 0;
}
