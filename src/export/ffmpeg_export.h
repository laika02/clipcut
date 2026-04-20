#ifndef CLIPCUT_EXPORT_FFMPEG_EXPORT_H
#define CLIPCUT_EXPORT_FFMPEG_EXPORT_H

#include <stdbool.h>

#include "model/project.h"

typedef struct ExportCommand {
    char **argv;
    int argc;
} ExportCommand;

typedef enum ExportProfile {
    EXPORT_PROFILE_CPU_FAST = 0,
    EXPORT_PROFILE_CPU_BALANCED,
    EXPORT_PROFILE_INTEL_VAAPI,
    EXPORT_PROFILE_INTEL_QSV,
    EXPORT_PROFILE_NVIDIA_NVENC,
    EXPORT_PROFILE_COUNT
} ExportProfile;

const char *export_profile_label(ExportProfile profile);
void export_command_reset(ExportCommand *command);
int export_build_ffmpeg_command(
    const ProjectState *project,
    const char *output_path,
    bool collapse_audio,
    ExportProfile profile,
    ExportCommand *command,
    char *error_message,
    int error_message_size
);
int export_build_extract_mp3_command(
    const ProjectState *project,
    size_t audio_track_index,
    const char *output_path,
    ExportCommand *command,
    char *error_message,
    int error_message_size
);
int export_command_join_for_display(
    const ExportCommand *command,
    char *buffer,
    int buffer_size
);

#endif
