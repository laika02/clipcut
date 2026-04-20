#include "media/probe.h"

#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(ProbeError *error, const char *message) {
    if (error == NULL) {
        return;
    }

    snprintf(error->message, sizeof(error->message), "%s", message);
}

static void set_av_error(ProbeError *error, const char *prefix, int errnum) {
    char details[128] = {0};
    av_strerror(errnum, details, sizeof(details));
    if (error == NULL) {
        return;
    }

    snprintf(error->message, sizeof(error->message), "%s: %s", prefix, details);
}

static void copy_string(char *dst, size_t dst_size, const char *src, const char *fallback) {
    const char *value = src;
    if (value == NULL || value[0] == '\0') {
        value = fallback;
    }

    snprintf(dst, dst_size, "%s", value);
}

static char *dup_string(const char *src) {
    size_t len = strlen(src) + 1;
    char *copy = malloc(len);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, src, len);
    return copy;
}

static int populate_audio_tracks(AVFormatContext *format_ctx, ProjectState *project, ProbeError *error) {
    size_t audio_count = 0;
    for (unsigned int i = 0; i < format_ctx->nb_streams; ++i) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_count++;
        }
    }

    if (audio_count == 0) {
        project->audio_tracks = NULL;
        project->audio_track_count = 0;
        return 0;
    }

    AudioTrack *tracks = calloc(audio_count, sizeof(*tracks));
    if (tracks == NULL) {
        set_error(error, "Failed to allocate audio track list");
        return -1;
    }

    size_t audio_order = 0;
    for (unsigned int i = 0; i < format_ctx->nb_streams; ++i) {
        AVStream *stream = format_ctx->streams[i];
        AVCodecParameters *codecpar = stream->codecpar;
        if (codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
            continue;
        }

        AudioTrack *track = &tracks[audio_order];
        track->stream_index = (int)i;
        track->stream_order_among_audio = (int)audio_order;
        copy_string(track->codec_name, sizeof(track->codec_name),
            avcodec_get_name(codecpar->codec_id), "unknown");
        track->channels = codecpar->ch_layout.nb_channels > 0 ?
            (int)codecpar->ch_layout.nb_channels : 0;
        track->sample_rate = codecpar->sample_rate > 0 ? codecpar->sample_rate : 0;
        copy_string(track->language, sizeof(track->language),
            av_dict_get(stream->metadata, "language", NULL, 0) != NULL ?
                av_dict_get(stream->metadata, "language", NULL, 0)->value : NULL,
            "");
        copy_string(track->title, sizeof(track->title),
            av_dict_get(stream->metadata, "title", NULL, 0) != NULL ?
                av_dict_get(stream->metadata, "title", NULL, 0)->value : NULL,
            "");
        track->enabled = true;
        audio_order++;
    }

    project->audio_tracks = tracks;
    project->audio_track_count = audio_count;
    return 0;
}

int probe_media_file(const char *path, ProjectState *project, ProbeError *error) {
    if (path == NULL || path[0] == '\0' || project == NULL) {
        set_error(error, "Missing media path");
        return -1;
    }

    AVFormatContext *format_ctx = NULL;
    int rc = avformat_open_input(&format_ctx, path, NULL, NULL);
    if (rc < 0) {
        set_av_error(error, "Could not open media", rc);
        return -1;
    }

    rc = avformat_find_stream_info(format_ctx, NULL);
    if (rc < 0) {
        set_av_error(error, "Could not read stream info", rc);
        avformat_close_input(&format_ctx);
        return -1;
    }

    int video_stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_index < 0) {
        set_error(error, "No video stream found");
        avformat_close_input(&format_ctx);
        return -1;
    }

    AVStream *video_stream = format_ctx->streams[video_stream_index];
    AVCodecParameters *video_codecpar = video_stream->codecpar;

    project_reset(project);

    project->source_path = dup_string(path);
    if (project->source_path == NULL) {
        set_error(error, "Failed to allocate source path");
        avformat_close_input(&format_ctx);
        return -1;
    }

    project->duration_sec = format_ctx->duration > 0 ?
        ((double)format_ctx->duration / (double)AV_TIME_BASE) : 0.0;
    project->width = video_codecpar->width;
    project->height = video_codecpar->height;
    copy_string(project->video_codec_name, sizeof(project->video_codec_name),
        avcodec_get_name(video_codecpar->codec_id), "unknown");
    project->trim_start_sec = 0.0;
    project->trim_end_sec = project->duration_sec;
    project->playhead_sec = 0.0;
    project->crop.x = 0;
    project->crop.y = 0;
    project->crop.width = project->width;
    project->crop.height = project->height;
    project->collapse_audio = true;

    if (populate_audio_tracks(format_ctx, project, error) != 0) {
        project_reset(project);
        avformat_close_input(&format_ctx);
        return -1;
    }

    avformat_close_input(&format_ctx);
    if (error != NULL) {
        error->message[0] = '\0';
    }
    return 0;
}
