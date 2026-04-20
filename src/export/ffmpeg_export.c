#include "export/ffmpeg_export.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *dst, int dst_size, const char *message) {
    if (dst != NULL && dst_size > 0) {
        snprintf(dst, (size_t)dst_size, "%s", message);
    }
}

static char *dup_string(const char *src) {
    const size_t len = strlen(src) + 1;
    char *copy = malloc(len);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, src, len);
    return copy;
}

static int append_arg(ExportCommand *command, const char *arg, char *error_message, int error_message_size) {
    char **next = realloc(command->argv, ((size_t)command->argc + 2u) * sizeof(*command->argv));
    if (next == NULL) {
        set_error(error_message, error_message_size, "Failed to grow export command argv");
        return -1;
    }

    command->argv = next;
    command->argv[command->argc] = dup_string(arg);
    if (command->argv[command->argc] == NULL) {
        set_error(error_message, error_message_size, "Failed to allocate export command arg");
        return -1;
    }
    command->argc++;
    command->argv[command->argc] = NULL;
    return 0;
}

static int append_format_arg(
    ExportCommand *command,
    char *error_message,
    int error_message_size,
    const char *format,
    ...
) {
    char buffer[512] = {0};
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (written < 0 || written >= (int)sizeof(buffer)) {
        set_error(error_message, error_message_size, "Formatted export command arg was too long");
        return -1;
    }

    return append_arg(command, buffer, error_message, error_message_size);
}

const char *export_profile_label(ExportProfile profile) {
    switch (profile) {
        case EXPORT_PROFILE_CPU_FAST:
            return "CPU FAST";
        case EXPORT_PROFILE_CPU_BALANCED:
            return "CPU BAL";
        case EXPORT_PROFILE_INTEL_VAAPI:
            return "INTEL VAAPI";
        case EXPORT_PROFILE_INTEL_QSV:
            return "INTEL QSV";
        case EXPORT_PROFILE_NVIDIA_NVENC:
            return "NVIDIA NVENC";
        default:
            return "CPU FAST";
    }
}

static int enabled_audio_count(const ProjectState *project) {
    int enabled = 0;
    for (size_t i = 0; i < project->audio_track_count; ++i) {
        if (project->audio_tracks[i].enabled) {
            enabled++;
        }
    }
    return enabled;
}

static CropRect normalized_crop(const ProjectState *project) {
    CropRect crop = project->crop;

    if (crop.width <= 0 || crop.height <= 0) {
        crop = (CropRect){.x = 0, .y = 0, .width = project->width, .height = project->height};
    }

    if (crop.x < 0) {
        crop.x = 0;
    }
    if (crop.y < 0) {
        crop.y = 0;
    }
    if (crop.x >= project->width) {
        crop.x = project->width - 1;
    }
    if (crop.y >= project->height) {
        crop.y = project->height - 1;
    }
    if (crop.x + crop.width > project->width) {
        crop.width = project->width - crop.x;
    }
    if (crop.y + crop.height > project->height) {
        crop.height = project->height - crop.y;
    }

    /* yuv420p encoders expect even crop dimensions/offsets. */
    crop.x -= crop.x % 2;
    crop.y -= crop.y % 2;
    crop.width -= crop.width % 2;
    crop.height -= crop.height % 2;
    if (crop.width < 2) {
        crop.width = 2;
    }
    if (crop.height < 2) {
        crop.height = 2;
    }
    if (crop.x + crop.width > project->width) {
        crop.x = project->width - crop.width;
    }
    if (crop.y + crop.height > project->height) {
        crop.y = project->height - crop.height;
    }

    return crop;
}

static bool crop_is_full_frame(const ProjectState *project) {
    const CropRect crop = normalized_crop(project);
    return crop.x == 0 &&
           crop.y == 0 &&
           crop.width == project->width - (project->width % 2) &&
           crop.height == project->height - (project->height % 2);
}

static int append_stream_copy_video_args(
    ExportCommand *command,
    char *error_message,
    int error_message_size
) {
    if (append_arg(command, "-map", error_message, error_message_size) != 0 ||
        append_arg(command, "0:v:0", error_message, error_message_size) != 0 ||
        append_arg(command, "-c:v", error_message, error_message_size) != 0 ||
        append_arg(command, "copy", error_message, error_message_size) != 0) {
        return -1;
    }

    return 0;
}

static const char *video_encoder_name(ExportProfile profile) {
    switch (profile) {
        case EXPORT_PROFILE_INTEL_VAAPI:
            return "h264_vaapi";
        case EXPORT_PROFILE_INTEL_QSV:
            return "h264_qsv";
        case EXPORT_PROFILE_NVIDIA_NVENC:
            return "h264_nvenc";
        case EXPORT_PROFILE_CPU_FAST:
        case EXPORT_PROFILE_CPU_BALANCED:
        default:
            return "libx264";
    }
}

static const char *profile_filter_suffix(ExportProfile profile) {
    switch (profile) {
        case EXPORT_PROFILE_INTEL_VAAPI:
            return ",format=nv12,hwupload";
        case EXPORT_PROFILE_INTEL_QSV:
            return ",format=nv12";
        case EXPORT_PROFILE_NVIDIA_NVENC:
        case EXPORT_PROFILE_CPU_FAST:
        case EXPORT_PROFILE_CPU_BALANCED:
        default:
            return "";
    }
}

static int append_profile_device_args(
    ExportCommand *command,
    ExportProfile profile,
    char *error_message,
    int error_message_size
) {
    if (profile != EXPORT_PROFILE_INTEL_VAAPI) {
        return 0;
    }

    if (append_arg(command, "-vaapi_device", error_message, error_message_size) != 0 ||
        append_arg(command, "/dev/dri/renderD128", error_message, error_message_size) != 0) {
        return -1;
    }
    return 0;
}

static int append_profile_video_encoder_args(
    ExportCommand *command,
    ExportProfile profile,
    char *error_message,
    int error_message_size
) {
    if (append_arg(command, "-c:v", error_message, error_message_size) != 0 ||
        append_arg(command, video_encoder_name(profile), error_message, error_message_size) != 0) {
        return -1;
    }

    switch (profile) {
        case EXPORT_PROFILE_CPU_FAST:
            if (append_arg(command, "-preset", error_message, error_message_size) != 0 ||
                append_arg(command, "veryfast", error_message, error_message_size) != 0 ||
                append_arg(command, "-crf", error_message, error_message_size) != 0 ||
                append_arg(command, "23", error_message, error_message_size) != 0 ||
                append_arg(command, "-pix_fmt", error_message, error_message_size) != 0 ||
                append_arg(command, "yuv420p", error_message, error_message_size) != 0) {
                return -1;
            }
            break;
        case EXPORT_PROFILE_CPU_BALANCED:
            if (append_arg(command, "-preset", error_message, error_message_size) != 0 ||
                append_arg(command, "faster", error_message, error_message_size) != 0 ||
                append_arg(command, "-crf", error_message, error_message_size) != 0 ||
                append_arg(command, "20", error_message, error_message_size) != 0 ||
                append_arg(command, "-pix_fmt", error_message, error_message_size) != 0 ||
                append_arg(command, "yuv420p", error_message, error_message_size) != 0) {
                return -1;
            }
            break;
        case EXPORT_PROFILE_INTEL_VAAPI:
            if (append_arg(command, "-qp", error_message, error_message_size) != 0 ||
                append_arg(command, "23", error_message, error_message_size) != 0) {
                return -1;
            }
            break;
        case EXPORT_PROFILE_INTEL_QSV:
            if (append_arg(command, "-global_quality", error_message, error_message_size) != 0 ||
                append_arg(command, "23", error_message, error_message_size) != 0) {
                return -1;
            }
            break;
        case EXPORT_PROFILE_NVIDIA_NVENC:
            if (append_arg(command, "-preset", error_message, error_message_size) != 0 ||
                append_arg(command, "p4", error_message, error_message_size) != 0 ||
                append_arg(command, "-cq", error_message, error_message_size) != 0 ||
                append_arg(command, "23", error_message, error_message_size) != 0 ||
                append_arg(command, "-pix_fmt", error_message, error_message_size) != 0 ||
                append_arg(command, "yuv420p", error_message, error_message_size) != 0) {
                return -1;
            }
            break;
        default:
            break;
    }
    return 0;
}

static int append_common_video_args(
    ExportCommand *command,
    const ProjectState *project,
    ExportProfile profile,
    char *error_message,
    int error_message_size
) {
    const CropRect crop = normalized_crop(project);
    const char *suffix = profile_filter_suffix(profile);
    if (append_arg(command, "-map", error_message, error_message_size) != 0 ||
        append_arg(command, "0:v:0", error_message, error_message_size) != 0 ||
        append_arg(command, "-vf", error_message, error_message_size) != 0 ||
        append_format_arg(
            command,
            error_message,
            error_message_size,
            "crop=%d:%d:%d:%d%s",
            crop.width,
            crop.height,
            crop.x,
            crop.y,
            suffix
        ) != 0) {
        return -1;
    }

    return append_profile_video_encoder_args(command, profile, error_message, error_message_size);
}

static int append_preserved_audio_args(
    ExportCommand *command,
    const ProjectState *project,
    bool copy_audio,
    char *error_message,
    int error_message_size
) {
    for (size_t i = 0; i < project->audio_track_count; ++i) {
        if (!project->audio_tracks[i].enabled) {
            continue;
        }

        if (append_arg(command, "-map", error_message, error_message_size) != 0 ||
            append_format_arg(
                command,
                error_message,
                error_message_size,
                "0:%d",
                project->audio_tracks[i].stream_index
            ) != 0) {
            return -1;
        }
    }

    if (append_arg(command, "-c:a", error_message, error_message_size) != 0 ||
        append_arg(command, copy_audio ? "copy" : "aac", error_message, error_message_size) != 0) {
        return -1;
    }

    return 0;
}

static int append_collapsed_audio_args(
    ExportCommand *command,
    const ProjectState *project,
    int enabled_count,
    char *error_message,
    int error_message_size
) {
    if (enabled_count == 1) {
        return append_preserved_audio_args(command, project, false, error_message, error_message_size);
    }

    char filter[1024] = {0};
    int offset = 0;
    for (size_t i = 0; i < project->audio_track_count; ++i) {
        if (!project->audio_tracks[i].enabled) {
            continue;
        }

        const int written = snprintf(
            filter + offset,
            sizeof(filter) - (size_t)offset,
            "[0:%d]",
            project->audio_tracks[i].stream_index
        );
        if (written < 0 || written >= (int)(sizeof(filter) - (size_t)offset)) {
            set_error(error_message, error_message_size, "Audio mix filter was too long");
            return -1;
        }
        offset += written;
    }

    const int written = snprintf(
        filter + offset,
        sizeof(filter) - (size_t)offset,
        "amix=inputs=%d:duration=longest:normalize=0[aout]",
        enabled_count
    );
    if (written < 0 || written >= (int)(sizeof(filter) - (size_t)offset)) {
        set_error(error_message, error_message_size, "Audio mix filter was too long");
        return -1;
    }

    if (append_arg(command, "-filter_complex", error_message, error_message_size) != 0 ||
        append_arg(command, filter, error_message, error_message_size) != 0 ||
        append_arg(command, "-map", error_message, error_message_size) != 0 ||
        append_arg(command, "[aout]", error_message, error_message_size) != 0 ||
        append_arg(command, "-c:a", error_message, error_message_size) != 0 ||
        append_arg(command, "aac", error_message, error_message_size) != 0) {
        return -1;
    }

    return 0;
}

static int build_keep_expression(
    const ProjectState *project,
    char *buffer,
    size_t buffer_size,
    char *error_message,
    int error_message_size
) {
    int offset = 0;
    double cursor = project->trim_start_sec;
    buffer[0] = '\0';

    for (size_t i = 0; i <= project->cut_count; ++i) {
        double end = project->trim_end_sec;
        if (i < project->cut_count) {
            if (project->cuts[i].end_sec <= project->trim_start_sec) {
                continue;
            }
            if (project->cuts[i].start_sec >= project->trim_end_sec) {
                end = project->trim_end_sec;
            } else {
                end = project->cuts[i].start_sec;
            }
        }
        if (end > project->trim_end_sec) {
            end = project->trim_end_sec;
        }
        if (end > cursor) {
            const int written = snprintf(
                buffer + offset,
                buffer_size - (size_t)offset,
                "%sbetween(t,%.6f,%.6f)",
                offset == 0 ? "" : "+",
                cursor,
                end
            );
            if (written < 0 || written >= (int)(buffer_size - (size_t)offset)) {
                set_error(error_message, error_message_size, "Cut filter expression was too long");
                return -1;
            }
            offset += written;
        }
        if (i < project->cut_count && project->cuts[i].end_sec > cursor) {
            cursor = project->cuts[i].end_sec;
            if (cursor > project->trim_end_sec) {
                cursor = project->trim_end_sec;
            }
        }
    }

    if (offset == 0) {
        set_error(error_message, error_message_size, "Cuts remove the entire trim range");
        return -1;
    }
    return 0;
}

static int append_cut_filter_args(
    ExportCommand *command,
    const ProjectState *project,
    bool collapse_audio,
    ExportProfile profile,
    int enabled_count,
    char *error_message,
    int error_message_size
) {
    char expr[2048] = {0};
    char filter[4096] = {0};
    int offset = 0;
    const CropRect crop = normalized_crop(project);
    const char *suffix = profile_filter_suffix(profile);

    if (build_keep_expression(project, expr, sizeof(expr), error_message, error_message_size) != 0) {
        return -1;
    }

    int written = snprintf(
        filter,
        sizeof(filter),
        "[0:v:0]select='%s',setpts=N/FRAME_RATE/TB,crop=%d:%d:%d:%d%s[vout]",
        expr,
        crop.width,
        crop.height,
        crop.x,
        crop.y,
        suffix
    );
    if (written < 0 || written >= (int)sizeof(filter)) {
        set_error(error_message, error_message_size, "Cut filter graph was too long");
        return -1;
    }
    offset = written;

    int audio_label = 0;
    for (size_t i = 0; i < project->audio_track_count; ++i) {
        if (!project->audio_tracks[i].enabled) {
            continue;
        }

        written = snprintf(
            filter + offset,
            sizeof(filter) - (size_t)offset,
            ";[0:%d]aselect='%s',asetpts=N/SR/TB[a%d]",
            project->audio_tracks[i].stream_index,
            expr,
            audio_label
        );
        if (written < 0 || written >= (int)(sizeof(filter) - (size_t)offset)) {
            set_error(error_message, error_message_size, "Cut audio filter graph was too long");
            return -1;
        }
        offset += written;
        audio_label++;
    }

    if (collapse_audio && enabled_count > 1) {
        written = snprintf(filter + offset, sizeof(filter) - (size_t)offset, ";");
        if (written < 0 || written >= (int)(sizeof(filter) - (size_t)offset)) {
            set_error(error_message, error_message_size, "Cut mix filter graph was too long");
            return -1;
        }
        offset += written;
        for (int i = 0; i < enabled_count; ++i) {
            written = snprintf(filter + offset, sizeof(filter) - (size_t)offset, "[a%d]", i);
            if (written < 0 || written >= (int)(sizeof(filter) - (size_t)offset)) {
                set_error(error_message, error_message_size, "Cut mix filter graph was too long");
                return -1;
            }
            offset += written;
        }
        written = snprintf(
            filter + offset,
            sizeof(filter) - (size_t)offset,
            "amix=inputs=%d:duration=longest:normalize=0[aout]",
            enabled_count
        );
        if (written < 0 || written >= (int)(sizeof(filter) - (size_t)offset)) {
            set_error(error_message, error_message_size, "Cut mix filter graph was too long");
            return -1;
        }
    }

    if (append_arg(command, "-filter_complex", error_message, error_message_size) != 0 ||
        append_arg(command, filter, error_message, error_message_size) != 0 ||
        append_arg(command, "-map", error_message, error_message_size) != 0 ||
        append_arg(command, "[vout]", error_message, error_message_size) != 0 ||
        append_profile_video_encoder_args(command, profile, error_message, error_message_size) != 0) {
        return -1;
    }

    if (enabled_count == 0) {
        return append_arg(command, "-an", error_message, error_message_size);
    }

    if (collapse_audio && enabled_count > 1) {
        if (append_arg(command, "-map", error_message, error_message_size) != 0 ||
            append_arg(command, "[aout]", error_message, error_message_size) != 0) {
            return -1;
        }
    } else {
        for (int i = 0; i < enabled_count; ++i) {
            if (append_arg(command, "-map", error_message, error_message_size) != 0 ||
                append_format_arg(command, error_message, error_message_size, "[a%d]", i) != 0) {
                return -1;
            }
        }
    }

    if (append_arg(command, "-c:a", error_message, error_message_size) != 0 ||
        append_arg(command, "aac", error_message, error_message_size) != 0) {
        return -1;
    }

    return 0;
}

void export_command_reset(ExportCommand *command) {
    if (command == NULL) {
        return;
    }

    for (int i = 0; i < command->argc; ++i) {
        free(command->argv[i]);
    }
    free(command->argv);
    memset(command, 0, sizeof(*command));
}

int export_build_ffmpeg_command(
    const ProjectState *project,
    const char *output_path,
    bool collapse_audio,
    ExportProfile profile,
    ExportCommand *command,
    char *error_message,
    int error_message_size
) {
    if (command == NULL) {
        set_error(error_message, error_message_size, "Missing export command output");
        return -1;
    }

    export_command_reset(command);
    if (!project_has_media(project) || output_path == NULL || output_path[0] == '\0') {
        set_error(error_message, error_message_size, "Missing project media or output path");
        return -1;
    }
    if (project->trim_end_sec <= project->trim_start_sec) {
        set_error(error_message, error_message_size, "Invalid export trim range");
        return -1;
    }

    const int enabled_count = enabled_audio_count(project);
    const bool has_cuts = project->cut_count > 0;
    const bool can_stream_copy = !has_cuts && crop_is_full_frame(project) && (!collapse_audio || enabled_count <= 1);

    if (append_arg(command, "ffmpeg", error_message, error_message_size) != 0 ||
        append_arg(command, "-y", error_message, error_message_size) != 0 ||
        (!can_stream_copy && append_profile_device_args(command, profile, error_message, error_message_size) != 0)) {
        export_command_reset(command);
        return -1;
    }
    if (!has_cuts) {
        if (append_arg(command, "-ss", error_message, error_message_size) != 0 ||
            append_format_arg(command, error_message, error_message_size, "%.6f", project->trim_start_sec) != 0) {
            export_command_reset(command);
            return -1;
        }
    }
    if (append_arg(command, "-i", error_message, error_message_size) != 0 ||
        append_arg(command, project->source_path, error_message, error_message_size) != 0) {
        export_command_reset(command);
        return -1;
    }
    if (!has_cuts) {
        if (append_arg(command, "-t", error_message, error_message_size) != 0 ||
            append_format_arg(
                command,
                error_message,
                error_message_size,
                "%.6f",
                project->trim_end_sec - project->trim_start_sec
            ) != 0) {
            export_command_reset(command);
            return -1;
        }
    }

    if (has_cuts) {
        if (append_cut_filter_args(
                command,
                project,
                collapse_audio,
                profile,
                enabled_count,
                error_message,
                error_message_size
            ) != 0) {
            export_command_reset(command);
            return -1;
        }
    } else if (can_stream_copy) {
        if (append_stream_copy_video_args(command, error_message, error_message_size) != 0) {
            export_command_reset(command);
            return -1;
        }
    } else if (append_common_video_args(command, project, profile, error_message, error_message_size) != 0) {
        export_command_reset(command);
        return -1;
    }

    if (has_cuts) {
        /* Audio was already mapped through the cut filter graph above. */
    } else if (enabled_count > 0) {
        int rc = 0;
        if (collapse_audio && enabled_count > 1) {
            rc = append_collapsed_audio_args(command, project, enabled_count, error_message, error_message_size);
        } else {
            rc = append_preserved_audio_args(command, project, can_stream_copy, error_message, error_message_size);
        }
        if (rc != 0) {
            export_command_reset(command);
            return -1;
        }
    } else if (append_arg(command, "-an", error_message, error_message_size) != 0) {
        export_command_reset(command);
        return -1;
    }

    if (append_arg(command, "-movflags", error_message, error_message_size) != 0 ||
        append_arg(command, "+faststart", error_message, error_message_size) != 0 ||
        append_arg(command, output_path, error_message, error_message_size) != 0) {
        export_command_reset(command);
        return -1;
    }

    if (error_message != NULL && error_message_size > 0) {
        error_message[0] = '\0';
    }
    return 0;
}

int export_build_extract_mp3_command(
    const ProjectState *project,
    size_t audio_track_index,
    const char *output_path,
    ExportCommand *command,
    char *error_message,
    int error_message_size
) {
    if (command == NULL) {
        set_error(error_message, error_message_size, "Missing MP3 extract command output");
        return -1;
    }

    export_command_reset(command);
    if (!project_has_media(project) || output_path == NULL || output_path[0] == '\0') {
        set_error(error_message, error_message_size, "Missing project media or MP3 output path");
        return -1;
    }
    if (audio_track_index >= project->audio_track_count) {
        set_error(error_message, error_message_size, "Invalid audio track index for MP3 extraction");
        return -1;
    }
    if (project->trim_end_sec <= project->trim_start_sec) {
        set_error(error_message, error_message_size, "Invalid MP3 extraction trim range");
        return -1;
    }

    const AudioTrack *track = &project->audio_tracks[audio_track_index];
    if (append_arg(command, "ffmpeg", error_message, error_message_size) != 0 ||
        append_arg(command, "-y", error_message, error_message_size) != 0 ||
        append_arg(command, "-ss", error_message, error_message_size) != 0 ||
        append_format_arg(command, error_message, error_message_size, "%.6f", project->trim_start_sec) != 0 ||
        append_arg(command, "-i", error_message, error_message_size) != 0 ||
        append_arg(command, project->source_path, error_message, error_message_size) != 0 ||
        append_arg(command, "-t", error_message, error_message_size) != 0 ||
        append_format_arg(
            command,
            error_message,
            error_message_size,
            "%.6f",
            project->trim_end_sec - project->trim_start_sec
        ) != 0 ||
        append_arg(command, "-map", error_message, error_message_size) != 0 ||
        append_format_arg(command, error_message, error_message_size, "0:%d", track->stream_index) != 0 ||
        append_arg(command, "-vn", error_message, error_message_size) != 0 ||
        append_arg(command, "-c:a", error_message, error_message_size) != 0 ||
        append_arg(command, "libmp3lame", error_message, error_message_size) != 0 ||
        append_arg(command, "-b:a", error_message, error_message_size) != 0 ||
        append_arg(command, "192k", error_message, error_message_size) != 0 ||
        append_arg(command, output_path, error_message, error_message_size) != 0) {
        export_command_reset(command);
        return -1;
    }

    if (error_message != NULL && error_message_size > 0) {
        error_message[0] = '\0';
    }
    return 0;
}

int export_command_join_for_display(const ExportCommand *command, char *buffer, int buffer_size) {
    if (command == NULL || buffer == NULL || buffer_size <= 0) {
        return -1;
    }

    int offset = 0;
    buffer[0] = '\0';
    for (int i = 0; i < command->argc; ++i) {
        const char *separator = i == 0 ? "" : " ";
        const int written = snprintf(
            buffer + offset,
            (size_t)buffer_size - (size_t)offset,
            "%s%s",
            separator,
            command->argv[i]
        );
        if (written < 0 || written >= (buffer_size - offset)) {
            return -1;
        }
        offset += written;
    }

    return 0;
}
