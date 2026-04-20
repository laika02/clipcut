#include "playback/transport.h"

static double clamp_double(double value, double min, double max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

void transport_init(TransportState *transport) {
    if (transport == NULL) {
        return;
    }

    transport->playback_state = PLAYBACK_STOPPED;
    transport->playhead_sec = 0.0;
    transport->trim_start_sec = 0.0;
    transport->trim_end_sec = 0.0;
    transport->last_tick_sec = 0.0;
    transport->seeking = false;
    transport->dragging_trim = false;
    transport->dragging_crop = false;
}

void transport_sync_from_project(TransportState *transport, const ProjectState *project) {
    if (transport == NULL || project == NULL) {
        return;
    }

    transport->playhead_sec = project->playhead_sec;
    transport->trim_start_sec = project->trim_start_sec;
    transport->trim_end_sec = project->trim_end_sec;
    transport->last_tick_sec = 0.0;
}

void transport_apply_to_project(const TransportState *transport, ProjectState *project) {
    if (transport == NULL || project == NULL) {
        return;
    }

    project->playhead_sec = transport->playhead_sec;
    project->trim_start_sec = transport->trim_start_sec;
    project->trim_end_sec = transport->trim_end_sec;
    project_clamp_playhead(project);
}

void transport_play(TransportState *transport, double now_sec) {
    if (transport == NULL) {
        return;
    }

    transport->playback_state = PLAYBACK_PLAYING;
    transport->last_tick_sec = now_sec;
}

void transport_pause(TransportState *transport) {
    if (transport == NULL) {
        return;
    }

    if (transport->playback_state == PLAYBACK_PLAYING) {
        transport->playback_state = PLAYBACK_PAUSED;
    }
    transport->last_tick_sec = 0.0;
}

void transport_stop(TransportState *transport) {
    if (transport == NULL) {
        return;
    }

    transport->playback_state = PLAYBACK_STOPPED;
    transport->playhead_sec = transport->trim_start_sec;
    transport->last_tick_sec = 0.0;
}

void transport_seek(TransportState *transport, double target_sec) {
    if (transport == NULL) {
        return;
    }

    transport->playhead_sec = clamp_double(target_sec, transport->trim_start_sec, transport->trim_end_sec);
}

void transport_set_trim(TransportState *transport, double trim_start_sec, double trim_end_sec) {
    if (transport == NULL) {
        return;
    }

    if (trim_end_sec < trim_start_sec) {
        const double tmp = trim_start_sec;
        trim_start_sec = trim_end_sec;
        trim_end_sec = tmp;
    }

    transport->trim_start_sec = trim_start_sec;
    transport->trim_end_sec = trim_end_sec;
    transport_seek(transport, transport->playhead_sec);
}

bool transport_tick(TransportState *transport, double now_sec) {
    if (transport == NULL || transport->playback_state != PLAYBACK_PLAYING) {
        return false;
    }

    const double previous = transport->last_tick_sec > 0.0 ? transport->last_tick_sec : now_sec;
    const double delta = now_sec - previous;
    transport->last_tick_sec = now_sec;
    if (delta <= 0.0) {
        return false;
    }

    transport->playhead_sec += delta;
    if (transport->playhead_sec >= transport->trim_end_sec) {
        transport->playhead_sec = transport->trim_end_sec;
        transport_pause(transport);
    }

    return true;
}
