#include "playback/audio_preview.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DecodedTrackBuffer {
    float *samples;
    size_t frame_count;
    size_t capacity_frames;
    int sample_rate;
    int channels;
} DecodedTrackBuffer;

static void set_error(AudioPreviewError *error, const char *message) {
    if (error == NULL) {
        return;
    }
    snprintf(error->message, sizeof(error->message), "%s", message);
}

static void set_av_error(AudioPreviewError *error, const char *prefix, int errnum) {
    char details[128] = {0};
    av_strerror(errnum, details, sizeof(details));
    if (error == NULL) {
        return;
    }
    snprintf(error->message, sizeof(error->message), "%s: %s", prefix, details);
}

static void decoded_track_buffer_reset(DecodedTrackBuffer *buffer) {
    if (buffer == NULL) {
        return;
    }
    free(buffer->samples);
    memset(buffer, 0, sizeof(*buffer));
}

void mixed_audio_buffer_reset(MixedAudioBuffer *buffer) {
    if (buffer == NULL) {
        return;
    }
    free(buffer->samples);
    memset(buffer, 0, sizeof(*buffer));
}

static int ensure_track_capacity(DecodedTrackBuffer *buffer, size_t additional_frames, AudioPreviewError *error) {
    const size_t needed = buffer->frame_count + additional_frames;
    if (needed <= buffer->capacity_frames) {
        return 0;
    }

    size_t new_capacity = buffer->capacity_frames == 0 ? 4096 : buffer->capacity_frames;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    float *resized = realloc(buffer->samples, new_capacity * (size_t)buffer->channels * sizeof(float));
    if (resized == NULL) {
        set_error(error, "Failed to grow decoded audio buffer");
        return -1;
    }

    buffer->samples = resized;
    buffer->capacity_frames = new_capacity;
    return 0;
}

static int decode_single_track(
    const char *source_path,
    int stream_index,
    int target_sample_rate,
    int target_channels,
    DecodedTrackBuffer *out,
    AudioPreviewError *error
) {
    AVFormatContext *format_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    AVPacket *packet = NULL;
    AVFrame *frame = NULL;
    SwrContext *swr = NULL;
    int rc = 0;
    int result = -1;

    rc = avformat_open_input(&format_ctx, source_path, NULL, NULL);
    if (rc < 0) {
        set_av_error(error, "Could not open media", rc);
        goto cleanup;
    }

    rc = avformat_find_stream_info(format_ctx, NULL);
    if (rc < 0) {
        set_av_error(error, "Could not read stream info", rc);
        goto cleanup;
    }

    if (stream_index < 0 || stream_index >= (int)format_ctx->nb_streams) {
        set_error(error, "Invalid audio stream index");
        goto cleanup;
    }

    AVStream *stream = format_ctx->streams[stream_index];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == NULL) {
        set_error(error, "No decoder available for audio stream");
        goto cleanup;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (codec_ctx == NULL) {
        set_error(error, "Could not allocate audio decoder context");
        goto cleanup;
    }

    rc = avcodec_parameters_to_context(codec_ctx, stream->codecpar);
    if (rc < 0) {
        set_av_error(error, "Could not copy audio decoder parameters", rc);
        goto cleanup;
    }

    rc = avcodec_open2(codec_ctx, codec, NULL);
    if (rc < 0) {
        set_av_error(error, "Could not open audio decoder", rc);
        goto cleanup;
    }

    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, target_channels);
    rc = swr_alloc_set_opts2(
        &swr,
        &out_layout,
        AV_SAMPLE_FMT_FLT,
        target_sample_rate,
        &codec_ctx->ch_layout,
        codec_ctx->sample_fmt,
        codec_ctx->sample_rate,
        0,
        NULL
    );
    av_channel_layout_uninit(&out_layout);
    if (rc < 0 || swr == NULL) {
        set_av_error(error, "Could not allocate resampler", rc);
        goto cleanup;
    }

    rc = swr_init(swr);
    if (rc < 0) {
        set_av_error(error, "Could not initialize resampler", rc);
        goto cleanup;
    }

    out->sample_rate = target_sample_rate;
    out->channels = target_channels;

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (packet == NULL || frame == NULL) {
        set_error(error, "Could not allocate audio decode buffers");
        goto cleanup;
    }

    while ((rc = av_read_frame(format_ctx, packet)) >= 0) {
        if (packet->stream_index != stream_index) {
            av_packet_unref(packet);
            continue;
        }

        rc = avcodec_send_packet(codec_ctx, packet);
        av_packet_unref(packet);
        if (rc < 0) {
            set_av_error(error, "Could not send audio packet", rc);
            goto cleanup;
        }

        while ((rc = avcodec_receive_frame(codec_ctx, frame)) >= 0) {
            const int out_frames = av_rescale_rnd(
                swr_get_delay(swr, codec_ctx->sample_rate) + frame->nb_samples,
                target_sample_rate,
                codec_ctx->sample_rate,
                AV_ROUND_UP
            );

            float *converted = NULL;
            rc = av_samples_alloc(
                (uint8_t **)&converted,
                NULL,
                target_channels,
                out_frames,
                AV_SAMPLE_FMT_FLT,
                0
            );
            if (rc < 0) {
                set_av_error(error, "Could not allocate converted audio buffer", rc);
                goto cleanup;
            }

            uint8_t *out_planes[1] = {(uint8_t *)converted};
            rc = swr_convert(swr, out_planes, out_frames, (const uint8_t * const *)frame->extended_data, frame->nb_samples);
            if (rc < 0) {
                av_freep(&converted);
                set_av_error(error, "Could not convert audio samples", rc);
                goto cleanup;
            }

            if (ensure_track_capacity(out, (size_t)rc, error) != 0) {
                av_freep(&converted);
                goto cleanup;
            }

            memcpy(
                out->samples + (out->frame_count * (size_t)target_channels),
                converted,
                (size_t)rc * (size_t)target_channels * sizeof(float)
            );
            out->frame_count += (size_t)rc;
            av_freep(&converted);
        }

        if (rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) {
            set_av_error(error, "Could not receive audio frame", rc);
            goto cleanup;
        }
    }

    rc = avcodec_send_packet(codec_ctx, NULL);
    if (rc < 0) {
        set_av_error(error, "Could not flush audio decoder", rc);
        goto cleanup;
    }

    while ((rc = avcodec_receive_frame(codec_ctx, frame)) >= 0) {
        const int out_frames = av_rescale_rnd(
            swr_get_delay(swr, codec_ctx->sample_rate) + frame->nb_samples,
            target_sample_rate,
            codec_ctx->sample_rate,
            AV_ROUND_UP
        );

        float *converted = NULL;
        rc = av_samples_alloc(
            (uint8_t **)&converted,
            NULL,
            target_channels,
            out_frames,
            AV_SAMPLE_FMT_FLT,
            0
        );
        if (rc < 0) {
            set_av_error(error, "Could not allocate converted audio buffer", rc);
            goto cleanup;
        }

        uint8_t *out_planes[1] = {(uint8_t *)converted};
        rc = swr_convert(swr, out_planes, out_frames, (const uint8_t * const *)frame->extended_data, frame->nb_samples);
        if (rc < 0) {
            av_freep(&converted);
            set_av_error(error, "Could not convert flushed audio samples", rc);
            goto cleanup;
        }

        if (ensure_track_capacity(out, (size_t)rc, error) != 0) {
            av_freep(&converted);
            goto cleanup;
        }

        memcpy(
            out->samples + (out->frame_count * (size_t)target_channels),
            converted,
            (size_t)rc * (size_t)target_channels * sizeof(float)
        );
        out->frame_count += (size_t)rc;
        av_freep(&converted);
    }

    result = 0;
    if (error != NULL) {
        error->message[0] = '\0';
    }

cleanup:
    swr_free(&swr);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format_ctx);
    return result;
}

int render_preview_audio_buffer(
    const ProjectState *project,
    double start_sec,
    double end_sec,
    MixedAudioBuffer *buffer,
    AudioPreviewError *error
) {
    const int target_sample_rate = 48000;
    const int target_channels = 2;
    size_t enabled_count = 0;

    if (project == NULL || buffer == NULL || project->source_path == NULL) {
        set_error(error, "Missing project state");
        return -1;
    }
    if (end_sec <= start_sec) {
        set_error(error, "Invalid preview audio range");
        return -1;
    }

    for (size_t i = 0; i < project->audio_track_count; ++i) {
        if (project->audio_tracks[i].enabled) {
            enabled_count++;
        }
    }
    if (enabled_count == 0) {
        set_error(error, "No enabled audio tracks available");
        return -1;
    }

    mixed_audio_buffer_reset(buffer);
    buffer->sample_rate = target_sample_rate;
    buffer->channels = target_channels;
    buffer->frame_count = (size_t)llround((end_sec - start_sec) * (double)target_sample_rate);
    buffer->samples = calloc(buffer->frame_count * (size_t)target_channels, sizeof(float));
    if (buffer->samples == NULL) {
        set_error(error, "Failed to allocate mixed audio buffer");
        return -1;
    }

    const size_t start_frame = (size_t)llround(start_sec * (double)target_sample_rate);
    size_t decoded_tracks = 0;
    for (size_t i = 0; i < project->audio_track_count; ++i) {
        const AudioTrack *track = &project->audio_tracks[i];
        if (!track->enabled) {
            continue;
        }

        DecodedTrackBuffer decoded = {0};
        if (decode_single_track(project->source_path, track->stream_index, target_sample_rate, target_channels, &decoded, error) != 0) {
            decoded_track_buffer_reset(&decoded);
            mixed_audio_buffer_reset(buffer);
            return -1;
        }

        for (size_t frame = 0; frame < buffer->frame_count; ++frame) {
            const size_t source_frame = start_frame + frame;
            if (source_frame >= decoded.frame_count) {
                break;
            }
            for (int channel = 0; channel < target_channels; ++channel) {
                buffer->samples[(frame * (size_t)target_channels) + (size_t)channel] +=
                    decoded.samples[(source_frame * (size_t)target_channels) + (size_t)channel];
            }
        }

        decoded_tracks++;
        decoded_track_buffer_reset(&decoded);
    }

    if (decoded_tracks > 1) {
        const float scale = 1.0f / (float)decoded_tracks;
        for (size_t i = 0; i < buffer->frame_count * (size_t)target_channels; ++i) {
            buffer->samples[i] *= scale;
        }
    }

    if (error != NULL) {
        error->message[0] = '\0';
    }
    return 0;
}
