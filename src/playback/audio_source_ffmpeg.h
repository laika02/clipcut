#ifndef CLIPCUT_PLAYBACK_AUDIO_SOURCE_FFMPEG_H
#define CLIPCUT_PLAYBACK_AUDIO_SOURCE_FFMPEG_H

#include "model/project.h"
#include "playback/audio_source.h"

int audio_source_from_ffmpeg_stream(
    const ProjectState *project,
    double start_sec,
    double end_sec,
    AudioSource *source,
    char *error_message,
    int error_message_size
);

#endif
