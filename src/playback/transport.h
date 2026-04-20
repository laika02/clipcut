#ifndef CLIPCUT_PLAYBACK_TRANSPORT_H
#define CLIPCUT_PLAYBACK_TRANSPORT_H

#include <stdbool.h>

#include "model/project.h"

typedef enum PlaybackState {
    PLAYBACK_STOPPED = 0,
    PLAYBACK_PAUSED,
    PLAYBACK_PLAYING
} PlaybackState;

typedef struct TransportState {
    PlaybackState playback_state;
    double playhead_sec;
    double trim_start_sec;
    double trim_end_sec;
    double last_tick_sec;
    bool seeking;
    bool dragging_trim;
    bool dragging_crop;
} TransportState;

void transport_init(TransportState *transport);
void transport_sync_from_project(TransportState *transport, const ProjectState *project);
void transport_apply_to_project(const TransportState *transport, ProjectState *project);
void transport_play(TransportState *transport, double now_sec);
void transport_pause(TransportState *transport);
void transport_stop(TransportState *transport);
void transport_seek(TransportState *transport, double target_sec);
void transport_set_trim(TransportState *transport, double trim_start_sec, double trim_end_sec);
bool transport_tick(TransportState *transport, double now_sec);

#endif
