#include "playback/video_stream.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct VideoDecodeSession {
    char source_path[1024];
    AVFormatContext *format_ctx;
    AVCodecContext *codec_ctx;
    AVPacket *packet;
    AVFrame *frame;
    AVStream *video_stream;
    struct SwsContext *sws_ctx;
    int video_stream_index;
    double current_time_sec;
    bool has_current_time;
} VideoDecodeSession;

static void set_error(PreviewError *error, const char *message) {
    if (error != NULL) {
        snprintf(error->message, sizeof(error->message), "%s", message);
    }
}

static void set_av_error(PreviewError *error, const char *prefix, int errnum) {
    char details[128] = {0};
    av_strerror(errnum, details, sizeof(details));
    if (error != NULL) {
        snprintf(error->message, sizeof(error->message), "%s: %s", prefix, details);
    }
}

static void video_decode_session_reset(VideoDecodeSession *session) {
    if (session == NULL) {
        return;
    }

    sws_freeContext(session->sws_ctx);
    av_frame_free(&session->frame);
    av_packet_free(&session->packet);
    avcodec_free_context(&session->codec_ctx);
    avformat_close_input(&session->format_ctx);
    memset(session, 0, sizeof(*session));
    session->video_stream_index = -1;
}

static int video_decode_session_open(VideoDecodeSession *session, const char *source_path, PreviewError *error) {
    video_decode_session_reset(session);
    snprintf(session->source_path, sizeof(session->source_path), "%s", source_path);

    int rc = avformat_open_input(&session->format_ctx, source_path, NULL, NULL);
    if (rc < 0) {
        set_av_error(error, "Could not open media for video stream", rc);
        goto fail;
    }

    rc = avformat_find_stream_info(session->format_ctx, NULL);
    if (rc < 0) {
        set_av_error(error, "Could not read stream info for video stream", rc);
        goto fail;
    }

    session->video_stream_index = av_find_best_stream(session->format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (session->video_stream_index < 0) {
        set_error(error, "No video stream found");
        goto fail;
    }

    session->video_stream = session->format_ctx->streams[session->video_stream_index];
    const AVCodec *codec = avcodec_find_decoder(session->video_stream->codecpar->codec_id);
    if (codec == NULL) {
        set_error(error, "No decoder available for video stream");
        goto fail;
    }

    session->codec_ctx = avcodec_alloc_context3(codec);
    if (session->codec_ctx == NULL) {
        set_error(error, "Could not allocate video decoder context");
        goto fail;
    }

    rc = avcodec_parameters_to_context(session->codec_ctx, session->video_stream->codecpar);
    if (rc < 0) {
        set_av_error(error, "Could not copy video decoder parameters", rc);
        goto fail;
    }

    rc = avcodec_open2(session->codec_ctx, codec, NULL);
    if (rc < 0) {
        set_av_error(error, "Could not open video decoder", rc);
        goto fail;
    }

    session->packet = av_packet_alloc();
    session->frame = av_frame_alloc();
    if (session->packet == NULL || session->frame == NULL) {
        set_error(error, "Could not allocate video decode buffers");
        goto fail;
    }

    return 0;

fail:
    video_decode_session_reset(session);
    return -1;
}

static int video_decode_session_seek(VideoDecodeSession *session, double time_sec, PreviewError *error) {
    const int64_t seek_target = av_rescale_q(
        (int64_t)llround(time_sec * (double)AV_TIME_BASE),
        AV_TIME_BASE_Q,
        session->video_stream->time_base
    );
    const int rc = av_seek_frame(session->format_ctx, session->video_stream_index, seek_target, AVSEEK_FLAG_BACKWARD);
    if (rc < 0) {
        set_av_error(error, "Could not seek video stream", rc);
        return -1;
    }

    avcodec_flush_buffers(session->codec_ctx);
    session->has_current_time = false;
    return 0;
}

static int convert_session_frame(VideoDecodeSession *session, PreviewFrame *out_frame, PreviewError *error) {
    session->sws_ctx = sws_getCachedContext(
        session->sws_ctx,
        session->frame->width,
        session->frame->height,
        (enum AVPixelFormat)session->frame->format,
        session->frame->width,
        session->frame->height,
        AV_PIX_FMT_RGBA,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
    );
    if (session->sws_ctx == NULL) {
        set_error(error, "Could not create video scaler");
        return -1;
    }

    uint8_t *dst_data[4] = {0};
    int dst_linesize[4] = {0};
    const int rc = av_image_alloc(
        dst_data,
        dst_linesize,
        session->frame->width,
        session->frame->height,
        AV_PIX_FMT_RGBA,
        1
    );
    if (rc < 0) {
        set_av_error(error, "Could not allocate video frame", rc);
        return -1;
    }

    sws_scale(
        session->sws_ctx,
        (const uint8_t *const *)session->frame->data,
        session->frame->linesize,
        0,
        session->frame->height,
        dst_data,
        dst_linesize
    );

    preview_frame_reset(out_frame);
    out_frame->width = session->frame->width;
    out_frame->height = session->frame->height;
    out_frame->pitch = dst_linesize[0];
    out_frame->pixels = dst_data[0];
    return 0;
}

static int video_decode_session_next_frame(
    VideoDecodeSession *session,
    double target_sec,
    PreviewFrame *out_frame,
    PreviewError *error
) {
    while (true) {
        int rc = avcodec_receive_frame(session->codec_ctx, session->frame);
        if (rc >= 0) {
            double frame_sec = target_sec;
            const int64_t pts = session->frame->best_effort_timestamp;
            if (pts != AV_NOPTS_VALUE) {
                frame_sec = av_q2d(session->video_stream->time_base) * (double)pts;
                if (frame_sec + 0.0001 < target_sec) {
                    session->current_time_sec = frame_sec;
                    session->has_current_time = true;
                    av_frame_unref(session->frame);
                    continue;
                }
            }

            if (convert_session_frame(session, out_frame, error) != 0) {
                av_frame_unref(session->frame);
                return -1;
            }
            session->current_time_sec = frame_sec;
            session->has_current_time = true;
            av_frame_unref(session->frame);
            return 0;
        }

        if (rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) {
            set_av_error(error, "Could not receive video frame", rc);
            return -1;
        }
        if (rc == AVERROR_EOF) {
            set_error(error, "No decodable video frame found");
            return -1;
        }

        rc = av_read_frame(session->format_ctx, session->packet);
        if (rc < 0) {
            rc = avcodec_send_packet(session->codec_ctx, NULL);
            if (rc < 0) {
                set_av_error(error, "Could not flush video decoder", rc);
                return -1;
            }
            continue;
        }

        if (session->packet->stream_index != session->video_stream_index) {
            av_packet_unref(session->packet);
            continue;
        }

        rc = avcodec_send_packet(session->codec_ctx, session->packet);
        av_packet_unref(session->packet);
        if (rc < 0 && rc != AVERROR(EAGAIN)) {
            set_av_error(error, "Could not send video packet", rc);
            return -1;
        }
    }
}

static int video_decode_session_frame_at_time(
    VideoDecodeSession *session,
    const char *source_path,
    double time_sec,
    PreviewFrame *out_frame,
    PreviewError *error
) {
    if (session->format_ctx == NULL || strcmp(session->source_path, source_path) != 0) {
        if (video_decode_session_open(session, source_path, error) != 0) {
            return -1;
        }
        if (time_sec > 0.0 && video_decode_session_seek(session, time_sec, error) != 0) {
            return -1;
        }
    } else if (!session->has_current_time ||
        time_sec + 0.020 < session->current_time_sec ||
        time_sec > session->current_time_sec + 2.0) {
        if (video_decode_session_seek(session, time_sec, error) != 0) {
            return -1;
        }
    }

    return video_decode_session_next_frame(session, time_sec, out_frame, error);
}

static int video_stream_thread_main(void *userdata) {
    VideoStreamState *state = userdata;
    VideoDecodeSession session = {.video_stream_index = -1};

    while (true) {
        char source_path[1024] = {0};
        double requested_time_sec = 0.0;
        uint64_t request_serial = 0;

        SDL_LockMutex(state->mutex);
        const bool running = state->running;
        if (!running) {
            SDL_UnlockMutex(state->mutex);
            break;
        }

        if (state->request_serial == state->handled_serial || state->source_path[0] == '\0') {
            SDL_UnlockMutex(state->mutex);
            SDL_Delay(5);
            continue;
        }

        snprintf(source_path, sizeof(source_path), "%s", state->source_path);
        requested_time_sec = state->requested_time_sec;
        request_serial = state->request_serial;
        SDL_UnlockMutex(state->mutex);

        PreviewFrame next_frame = {0};
        PreviewError error = {0};
        if (video_decode_session_frame_at_time(
                &session,
                source_path,
                requested_time_sec,
                &next_frame,
                &error
            ) != 0) {
            SDL_LockMutex(state->mutex);
            if (request_serial == state->request_serial) {
                snprintf(state->last_error, sizeof(state->last_error), "%s", error.message);
                state->handled_serial = request_serial;
            }
            SDL_UnlockMutex(state->mutex);
            preview_frame_reset(&next_frame);
            SDL_Delay(5);
            continue;
        }

        SDL_LockMutex(state->mutex);
        if (request_serial == state->request_serial) {
            preview_frame_reset(&state->latest_frame);
            state->latest_frame = next_frame;
            state->latest_frame_serial = request_serial;
            state->handled_serial = request_serial;
            state->last_error[0] = '\0';
            memset(&next_frame, 0, sizeof(next_frame));
        }
        SDL_UnlockMutex(state->mutex);
        preview_frame_reset(&next_frame);
    }

    video_decode_session_reset(&session);
    return 0;
}

int video_stream_init(VideoStreamState *state, char *error_message, int error_message_size) {
    if (state == NULL) {
        if (error_message != NULL && error_message_size > 0) {
            snprintf(error_message, (size_t)error_message_size, "%s", "Missing video stream state");
        }
        return -1;
    }

    memset(state, 0, sizeof(*state));
    state->mutex = SDL_CreateMutex();
    if (state->mutex == NULL) {
        if (error_message != NULL && error_message_size > 0) {
            snprintf(error_message, (size_t)error_message_size, "SDL_CreateMutex failed");
        }
        return -1;
    }

    state->running = true;
    state->thread = SDL_CreateThread(video_stream_thread_main, "clipcut-video-stream", state);
    if (state->thread == NULL) {
        if (error_message != NULL && error_message_size > 0) {
            snprintf(error_message, (size_t)error_message_size, "SDL_CreateThread failed");
        }
        SDL_DestroyMutex(state->mutex);
        memset(state, 0, sizeof(*state));
        return -1;
    }

    return 0;
}

void video_stream_shutdown(VideoStreamState *state) {
    if (state == NULL) {
        return;
    }

    if (state->mutex != NULL) {
        SDL_LockMutex(state->mutex);
        state->running = false;
        SDL_UnlockMutex(state->mutex);
    }
    if (state->thread != NULL) {
        SDL_WaitThread(state->thread, NULL);
    }
    preview_frame_reset(&state->latest_frame);
    if (state->mutex != NULL) {
        SDL_DestroyMutex(state->mutex);
    }
    memset(state, 0, sizeof(*state));
}

void video_stream_request_frame(VideoStreamState *state, const char *source_path, double time_sec) {
    if (state == NULL || state->mutex == NULL || source_path == NULL || source_path[0] == '\0') {
        return;
    }

    SDL_LockMutex(state->mutex);
    snprintf(state->source_path, sizeof(state->source_path), "%s", source_path);
    state->requested_time_sec = time_sec;
    state->request_serial += 1;
    SDL_UnlockMutex(state->mutex);
}

bool video_stream_take_frame(VideoStreamState *state, PreviewFrame *frame_out, uint64_t *serial_out) {
    if (state == NULL || state->mutex == NULL || frame_out == NULL) {
        return false;
    }

    SDL_LockMutex(state->mutex);
    if (state->latest_frame.pixels == NULL) {
        SDL_UnlockMutex(state->mutex);
        return false;
    }

    PreviewFrame next = state->latest_frame;
    memset(&state->latest_frame, 0, sizeof(state->latest_frame));
    const uint64_t serial = state->latest_frame_serial;
    SDL_UnlockMutex(state->mutex);

    preview_frame_reset(frame_out);
    *frame_out = next;
    if (serial_out != NULL) {
        *serial_out = serial;
    }
    return true;
}

bool video_stream_take_error(VideoStreamState *state, char *error_message, int error_message_size) {
    if (state == NULL || state->mutex == NULL) {
        return false;
    }

    SDL_LockMutex(state->mutex);
    const bool has_error = state->last_error[0] != '\0';
    if (has_error && error_message != NULL && error_message_size > 0) {
        snprintf(error_message, (size_t)error_message_size, "%s", state->last_error);
    }
    state->last_error[0] = '\0';
    SDL_UnlockMutex(state->mutex);
    return has_error;
}
