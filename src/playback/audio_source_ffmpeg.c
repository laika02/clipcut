#include "playback/audio_source_ffmpeg.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct FfmpegTrackState {
    AVFormatContext *format_ctx;
    AVCodecContext *codec_ctx;
    AVPacket *packet;
    AVFrame *frame;
    SwrContext *swr;
    int stream_index;
    bool input_eof;
    bool flush_sent;
    bool finished;
    float *fifo;
    size_t fifo_frames;
    size_t fifo_capacity_frames;
} FfmpegTrackState;

typedef struct FfmpegAudioSourceContext {
    FfmpegTrackState *tracks;
    size_t track_count;
    int sample_rate;
    int channels;
    size_t frames_to_skip;
    size_t remaining_frames;
} FfmpegAudioSourceContext;

static void set_error(char *dst, int dst_size, const char *message) {
    if (dst != NULL && dst_size > 0) {
        snprintf(dst, (size_t)dst_size, "%s", message);
    }
}

static void set_av_error(char *dst, int dst_size, const char *prefix, int errnum) {
    char details[128] = {0};
    av_strerror(errnum, details, sizeof(details));
    if (dst != NULL && dst_size > 0) {
        snprintf(dst, (size_t)dst_size, "%s: %s", prefix, details);
    }
}

static void ffmpeg_track_reset(FfmpegTrackState *track) {
    if (track == NULL) {
        return;
    }

    free(track->fifo);
    swr_free(&track->swr);
    av_frame_free(&track->frame);
    av_packet_free(&track->packet);
    avcodec_free_context(&track->codec_ctx);
    avformat_close_input(&track->format_ctx);
    memset(track, 0, sizeof(*track));
}

static int ensure_fifo_capacity(FfmpegTrackState *track, size_t additional_frames, int channels, char *error_message, int error_message_size) {
    const size_t needed = track->fifo_frames + additional_frames;
    if (needed <= track->fifo_capacity_frames) {
        return 0;
    }

    size_t new_capacity = track->fifo_capacity_frames == 0 ? 4096 : track->fifo_capacity_frames;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    float *resized = realloc(track->fifo, new_capacity * (size_t)channels * sizeof(float));
    if (resized == NULL) {
        set_error(error_message, error_message_size, "Failed to grow FFmpeg audio FIFO");
        return -1;
    }

    track->fifo = resized;
    track->fifo_capacity_frames = new_capacity;
    return 0;
}

static void fifo_discard_frames(FfmpegTrackState *track, size_t frames, int channels) {
    if (track == NULL || frames == 0 || track->fifo_frames == 0) {
        return;
    }

    if (frames >= track->fifo_frames) {
        track->fifo_frames = 0;
        return;
    }

    const size_t remaining = track->fifo_frames - frames;
    memmove(
        track->fifo,
        track->fifo + (frames * (size_t)channels),
        remaining * (size_t)channels * sizeof(float)
    );
    track->fifo_frames = remaining;
}

static int append_converted_frame(
    FfmpegTrackState *track,
    int target_sample_rate,
    int target_channels,
    char *error_message,
    int error_message_size
) {
    const int out_frames = av_rescale_rnd(
        swr_get_delay(track->swr, track->codec_ctx->sample_rate) + track->frame->nb_samples,
        target_sample_rate,
        track->codec_ctx->sample_rate,
        AV_ROUND_UP
    );

    float *converted = NULL;
    int rc = av_samples_alloc(
        (uint8_t **)&converted,
        NULL,
        target_channels,
        out_frames,
        AV_SAMPLE_FMT_FLT,
        0
    );
    if (rc < 0) {
        set_av_error(error_message, error_message_size, "Could not allocate converted audio buffer", rc);
        return -1;
    }

    uint8_t *out_planes[1] = {(uint8_t *)converted};
    rc = swr_convert(
        track->swr,
        out_planes,
        out_frames,
        (const uint8_t * const *)track->frame->extended_data,
        track->frame->nb_samples
    );
    if (rc < 0) {
        av_freep(&converted);
        set_av_error(error_message, error_message_size, "Could not convert streamed audio samples", rc);
        return -1;
    }

    if (ensure_fifo_capacity(track, (size_t)rc, target_channels, error_message, error_message_size) != 0) {
        av_freep(&converted);
        return -1;
    }

    memcpy(
        track->fifo + (track->fifo_frames * (size_t)target_channels),
        converted,
        (size_t)rc * (size_t)target_channels * sizeof(float)
    );
    track->fifo_frames += (size_t)rc;
    av_freep(&converted);
    return 0;
}

static int fill_track_fifo(
    FfmpegTrackState *track,
    size_t target_frames,
    int target_sample_rate,
    int target_channels,
    char *error_message,
    int error_message_size
) {
    int rc = 0;

    while (track->fifo_frames < target_frames && !track->finished) {
        if (!track->input_eof) {
            rc = av_read_frame(track->format_ctx, track->packet);
            if (rc >= 0) {
                if (track->packet->stream_index != track->stream_index) {
                    av_packet_unref(track->packet);
                    continue;
                }

                rc = avcodec_send_packet(track->codec_ctx, track->packet);
                av_packet_unref(track->packet);
                if (rc < 0) {
                    set_av_error(error_message, error_message_size, "Could not send streamed audio packet", rc);
                    return -1;
                }
            } else if (rc == AVERROR_EOF) {
                track->input_eof = true;
            } else {
                set_av_error(error_message, error_message_size, "Could not read streamed audio packet", rc);
                return -1;
            }
        }

        while (true) {
            rc = avcodec_receive_frame(track->codec_ctx, track->frame);
            if (rc == AVERROR(EAGAIN)) {
                break;
            }
            if (rc == AVERROR_EOF) {
                track->finished = true;
                break;
            }
            if (rc < 0) {
                set_av_error(error_message, error_message_size, "Could not receive streamed audio frame", rc);
                return -1;
            }

            if (append_converted_frame(track, target_sample_rate, target_channels, error_message, error_message_size) != 0) {
                return -1;
            }
        }

        if (track->input_eof && !track->flush_sent) {
            rc = avcodec_send_packet(track->codec_ctx, NULL);
            if (rc < 0) {
                set_av_error(error_message, error_message_size, "Could not flush streamed audio decoder", rc);
                return -1;
            }
            track->flush_sent = true;
        } else if (track->input_eof && track->flush_sent && track->fifo_frames >= target_frames) {
            break;
        } else if (track->input_eof && track->flush_sent && track->finished) {
            break;
        }
    }

    return 0;
}

static size_t ffmpeg_audio_source_read_frames(void *ctx, float *dst, size_t max_frames, int channels) {
    FfmpegAudioSourceContext *source = ctx;
    if (source == NULL || dst == NULL || max_frames == 0 || source->tracks == NULL) {
        return 0;
    }

    size_t requested = max_frames;
    if (requested > source->remaining_frames) {
        requested = source->remaining_frames;
    }
    if (requested == 0) {
        return 0;
    }

    const size_t target_frames = requested + source->frames_to_skip;
    for (size_t i = 0; i < source->track_count; ++i) {
        char error_message[256] = {0};
        if (fill_track_fifo(&source->tracks[i], target_frames, source->sample_rate, source->channels, error_message, (int)sizeof(error_message)) != 0) {
            return 0;
        }
    }

    if (source->frames_to_skip > 0) {
        for (size_t i = 0; i < source->track_count; ++i) {
            fifo_discard_frames(&source->tracks[i], source->frames_to_skip, source->channels);
        }
        source->frames_to_skip = 0;
    }

    memset(dst, 0, requested * (size_t)channels * sizeof(float));
    for (size_t i = 0; i < source->track_count; ++i) {
        FfmpegTrackState *track = &source->tracks[i];
        size_t available = track->fifo_frames;
        if (available > requested) {
            available = requested;
        }

        for (size_t frame = 0; frame < available; ++frame) {
            for (int ch = 0; ch < channels; ++ch) {
                dst[(frame * (size_t)channels) + (size_t)ch] +=
                    track->fifo[(frame * (size_t)channels) + (size_t)ch];
            }
        }

        fifo_discard_frames(track, available, source->channels);
    }

    if (source->track_count > 1) {
        const float scale = 1.0f / (float)source->track_count;
        for (size_t i = 0; i < requested * (size_t)channels; ++i) {
            dst[i] *= scale;
        }
    }

    source->remaining_frames -= requested;
    return requested;
}

static int ffmpeg_audio_source_remaining_frames(void *ctx, size_t *remaining) {
    FfmpegAudioSourceContext *source = ctx;
    if (source == NULL) {
        if (remaining != NULL) {
            *remaining = 0;
        }
        return -1;
    }

    if (remaining != NULL) {
        *remaining = source->remaining_frames;
    }
    return 0;
}

static void ffmpeg_audio_source_destroy(void *ctx) {
    FfmpegAudioSourceContext *source = ctx;
    if (source == NULL) {
        return;
    }

    if (source->tracks != NULL) {
        for (size_t i = 0; i < source->track_count; ++i) {
            ffmpeg_track_reset(&source->tracks[i]);
        }
    }
    free(source->tracks);
    free(source);
}

static int init_track_from_audio_track(
    const ProjectState *project,
    const AudioTrack *audio_track,
    double start_sec,
    int target_sample_rate,
    int target_channels,
    FfmpegTrackState *track,
    char *error_message,
    int error_message_size
) {
    int rc = avformat_open_input(&track->format_ctx, project->source_path, NULL, NULL);
    if (rc < 0) {
        set_av_error(error_message, error_message_size, "Could not open media for streaming source", rc);
        return -1;
    }

    rc = avformat_find_stream_info(track->format_ctx, NULL);
    if (rc < 0) {
        set_av_error(error_message, error_message_size, "Could not read stream info for streaming source", rc);
        return -1;
    }

    if (audio_track->stream_index < 0 || audio_track->stream_index >= (int)track->format_ctx->nb_streams) {
        set_error(error_message, error_message_size, "Invalid audio stream index in streaming source");
        return -1;
    }

    track->stream_index = audio_track->stream_index;
    AVStream *stream = track->format_ctx->streams[track->stream_index];
    if (start_sec > 0.0) {
        const int64_t seek_target = av_rescale_q(
            (int64_t)llround(start_sec * (double)AV_TIME_BASE),
            AV_TIME_BASE_Q,
            stream->time_base
        );
        rc = av_seek_frame(track->format_ctx, track->stream_index, seek_target, AVSEEK_FLAG_BACKWARD);
        if (rc < 0) {
            set_av_error(error_message, error_message_size, "Could not seek streaming audio source", rc);
            return -1;
        }
    }

    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == NULL) {
        set_error(error_message, error_message_size, "No decoder available for streaming audio source");
        return -1;
    }

    track->codec_ctx = avcodec_alloc_context3(codec);
    if (track->codec_ctx == NULL) {
        set_error(error_message, error_message_size, "Could not allocate streaming audio decoder context");
        return -1;
    }

    rc = avcodec_parameters_to_context(track->codec_ctx, stream->codecpar);
    if (rc < 0) {
        set_av_error(error_message, error_message_size, "Could not copy streaming decoder parameters", rc);
        return -1;
    }

    rc = avcodec_open2(track->codec_ctx, codec, NULL);
    if (rc < 0) {
        set_av_error(error_message, error_message_size, "Could not open streaming audio decoder", rc);
        return -1;
    }

    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, target_channels);
    rc = swr_alloc_set_opts2(
        &track->swr,
        &out_layout,
        AV_SAMPLE_FMT_FLT,
        target_sample_rate,
        &track->codec_ctx->ch_layout,
        track->codec_ctx->sample_fmt,
        track->codec_ctx->sample_rate,
        0,
        NULL
    );
    av_channel_layout_uninit(&out_layout);
    if (rc < 0 || track->swr == NULL) {
        set_av_error(error_message, error_message_size, "Could not allocate streaming resampler", rc);
        return -1;
    }

    rc = swr_init(track->swr);
    if (rc < 0) {
        set_av_error(error_message, error_message_size, "Could not initialize streaming resampler", rc);
        return -1;
    }

    track->packet = av_packet_alloc();
    track->frame = av_frame_alloc();
    if (track->packet == NULL || track->frame == NULL) {
        set_error(error_message, error_message_size, "Could not allocate streaming decode buffers");
        return -1;
    }

    return 0;
}

int audio_source_from_ffmpeg_stream(
    const ProjectState *project,
    double start_sec,
    double end_sec,
    AudioSource *source,
    char *error_message,
    int error_message_size
) {
    static const AudioSourceVTable vtable = {
        .read_frames = ffmpeg_audio_source_read_frames,
        .remaining_frames = ffmpeg_audio_source_remaining_frames,
        .destroy = ffmpeg_audio_source_destroy,
    };

    if (project == NULL || source == NULL || project->source_path == NULL) {
        set_error(error_message, error_message_size, "Missing project state for FFmpeg audio source");
        return -1;
    }
    if (end_sec <= start_sec) {
        set_error(error_message, error_message_size, "Invalid streaming audio range");
        return -1;
    }

    size_t enabled_count = 0;
    for (size_t i = 0; i < project->audio_track_count; ++i) {
        if (project->audio_tracks[i].enabled) {
            enabled_count++;
        }
    }
    if (enabled_count == 0) {
        set_error(error_message, error_message_size, "No enabled audio tracks for FFmpeg audio source");
        return -1;
    }

    FfmpegAudioSourceContext *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        set_error(error_message, error_message_size, "Failed to allocate FFmpeg audio source context");
        return -1;
    }

    ctx->tracks = calloc(enabled_count, sizeof(*ctx->tracks));
    if (ctx->tracks == NULL) {
        free(ctx);
        set_error(error_message, error_message_size, "Failed to allocate FFmpeg audio track list");
        return -1;
    }

    ctx->track_count = enabled_count;
    ctx->sample_rate = 48000;
    ctx->channels = 2;
    ctx->frames_to_skip = 0;
    ctx->remaining_frames = (size_t)llround((end_sec - start_sec) * (double)ctx->sample_rate);

    size_t out_index = 0;
    for (size_t i = 0; i < project->audio_track_count; ++i) {
        if (!project->audio_tracks[i].enabled) {
            continue;
        }

        if (init_track_from_audio_track(
                project,
                &project->audio_tracks[i],
                start_sec,
                ctx->sample_rate,
                ctx->channels,
                &ctx->tracks[out_index],
                error_message,
                error_message_size
            ) != 0) {
            ffmpeg_audio_source_destroy(ctx);
            return -1;
        }
        out_index++;
    }

    source->ctx = ctx;
    source->sample_rate = ctx->sample_rate;
    source->channels = ctx->channels;
    source->vtable = &vtable;
    return 0;
}
