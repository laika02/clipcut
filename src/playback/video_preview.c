#include "playback/video_preview.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(PreviewError *error, const char *message) {
    if (error == NULL) {
        return;
    }
    snprintf(error->message, sizeof(error->message), "%s", message);
}

static void set_av_error(PreviewError *error, const char *prefix, int errnum) {
    char details[128] = {0};
    av_strerror(errnum, details, sizeof(details));
    if (error == NULL) {
        return;
    }
    snprintf(error->message, sizeof(error->message), "%s: %s", prefix, details);
}

void preview_frame_reset(PreviewFrame *frame) {
    if (frame == NULL) {
        return;
    }
    av_free(frame->pixels);
    memset(frame, 0, sizeof(*frame));
}

static int convert_frame(
    const AVFrame *source,
    PreviewFrame *out_frame,
    PreviewError *error
) {
    struct SwsContext *sws = sws_getContext(
        source->width,
        source->height,
        (enum AVPixelFormat)source->format,
        source->width,
        source->height,
        AV_PIX_FMT_RGBA,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
    );
    if (sws == NULL) {
        set_error(error, "Could not create scaling context");
        return -1;
    }

    uint8_t *dst_data[4] = {0};
    int dst_linesize[4] = {0};
    int rc = av_image_alloc(dst_data, dst_linesize, source->width, source->height, AV_PIX_FMT_RGBA, 1);
    if (rc < 0) {
        sws_freeContext(sws);
        set_av_error(error, "Could not allocate preview frame", rc);
        return -1;
    }

    sws_scale(
        sws,
        (const uint8_t *const *)source->data,
        source->linesize,
        0,
        source->height,
        dst_data,
        dst_linesize
    );

    preview_frame_reset(out_frame);
    out_frame->width = source->width;
    out_frame->height = source->height;
    out_frame->pitch = dst_linesize[0];
    out_frame->pixels = dst_data[0];

    sws_freeContext(sws);
    return 0;
}

static int decode_next_available_frame(
    AVFormatContext *format_ctx,
    AVCodecContext *codec_ctx,
    int video_stream_index,
    AVPacket *packet,
    AVFrame *decoded,
    AVStream *video_stream,
    double target_sec,
    PreviewFrame *frame,
    PreviewError *error
) {
    int rc = 0;

    while ((rc = av_read_frame(format_ctx, packet)) >= 0) {
        if (packet->stream_index != video_stream_index) {
            av_packet_unref(packet);
            continue;
        }

        rc = avcodec_send_packet(codec_ctx, packet);
        av_packet_unref(packet);
        if (rc < 0) {
            set_av_error(error, "Could not send video packet", rc);
            return -1;
        }

        while ((rc = avcodec_receive_frame(codec_ctx, decoded)) >= 0) {
            if (target_sec > 0.0) {
                const int64_t pts = decoded->best_effort_timestamp;
                if (pts != AV_NOPTS_VALUE) {
                    const double frame_sec = av_q2d(video_stream->time_base) * (double)pts;
                    if (frame_sec + 0.0001 < target_sec) {
                        av_frame_unref(decoded);
                        continue;
                    }
                }
            }

            if (convert_frame(decoded, frame, error) != 0) {
                return -1;
            }
            return 0;
        }

        if (rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) {
            set_av_error(error, "Could not receive decoded frame", rc);
            return -1;
        }
    }

    rc = avcodec_send_packet(codec_ctx, NULL);
    if (rc < 0) {
        set_av_error(error, "Could not flush video decoder", rc);
        return -1;
    }

    while ((rc = avcodec_receive_frame(codec_ctx, decoded)) >= 0) {
        if (convert_frame(decoded, frame, error) != 0) {
            return -1;
        }
        return 0;
    }

    set_error(error, "No decodable video frame found");
    return -1;
}

int decode_preview_frame_at_time(const char *path, double time_sec, PreviewFrame *frame, PreviewError *error) {
    if (path == NULL || path[0] == '\0' || frame == NULL) {
        set_error(error, "Missing media path");
        return -1;
    }

    AVFormatContext *format_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    AVPacket *packet = NULL;
    AVFrame *decoded = NULL;
    int video_stream_index = -1;
    int rc = 0;
    int result = -1;

    rc = avformat_open_input(&format_ctx, path, NULL, NULL);
    if (rc < 0) {
        set_av_error(error, "Could not open media", rc);
        goto cleanup;
    }

    rc = avformat_find_stream_info(format_ctx, NULL);
    if (rc < 0) {
        set_av_error(error, "Could not read stream info", rc);
        goto cleanup;
    }

    video_stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_index < 0) {
        set_error(error, "No video stream found");
        goto cleanup;
    }

    AVStream *video_stream = format_ctx->streams[video_stream_index];
    const AVCodec *codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (codec == NULL) {
        set_error(error, "No decoder available for video stream");
        goto cleanup;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (codec_ctx == NULL) {
        set_error(error, "Could not allocate decoder context");
        goto cleanup;
    }

    rc = avcodec_parameters_to_context(codec_ctx, video_stream->codecpar);
    if (rc < 0) {
        set_av_error(error, "Could not copy decoder parameters", rc);
        goto cleanup;
    }

    rc = avcodec_open2(codec_ctx, codec, NULL);
    if (rc < 0) {
        set_av_error(error, "Could not open video decoder", rc);
        goto cleanup;
    }

    packet = av_packet_alloc();
    decoded = av_frame_alloc();
    if (packet == NULL || decoded == NULL) {
        set_error(error, "Could not allocate decode buffers");
        goto cleanup;
    }

    if (time_sec > 0.0) {
        const int64_t seek_target = av_rescale_q(
            (int64_t)llround(time_sec * (double)AV_TIME_BASE),
            AV_TIME_BASE_Q,
            video_stream->time_base
        );
        rc = av_seek_frame(format_ctx, video_stream_index, seek_target, AVSEEK_FLAG_BACKWARD);
        if (rc < 0) {
            set_av_error(error, "Could not seek video preview stream", rc);
            goto cleanup;
        }
        avcodec_flush_buffers(codec_ctx);
    }

    if (decode_next_available_frame(
            format_ctx,
            codec_ctx,
            video_stream_index,
            packet,
            decoded,
            video_stream,
            time_sec,
            frame,
            error
        ) == 0) {
        result = 0;
    }

cleanup:
    av_packet_free(&packet);
    av_frame_free(&decoded);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format_ctx);
    return result;
}

int decode_preview_frame(const char *path, PreviewFrame *frame, PreviewError *error) {
    return decode_preview_frame_at_time(path, 0.0, frame, error);
}
