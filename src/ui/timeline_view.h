#ifndef CLIPCUT_UI_TIMELINE_VIEW_H
#define CLIPCUT_UI_TIMELINE_VIEW_H

#include <stdbool.h>

#include <SDL.h>

#include "model/project.h"
#include "playback/transport.h"

typedef enum TimelineDragMode {
    TIMELINE_DRAG_NONE = 0,
    TIMELINE_DRAG_SEEK,
    TIMELINE_DRAG_TRIM_START,
    TIMELINE_DRAG_TRIM_END,
    TIMELINE_DRAG_TRIM_WINDOW,
    TIMELINE_DRAG_CUT_RANGE
} TimelineDragMode;

typedef struct TimelineInteractionState {
    TimelineDragMode mode;
    double anchor_sec;
    double current_sec;
    double trim_anchor_start_sec;
    double trim_anchor_end_sec;
} TimelineInteractionState;

SDL_Rect timeline_view_rect(int window_width, int window_height);
void timeline_view_draw(
    SDL_Renderer *renderer,
    SDL_Rect timeline_rect,
    const ProjectState *project,
    const TransportState *transport,
    const TimelineInteractionState *interaction
);
void timeline_view_reset_interaction(TimelineInteractionState *state);
bool timeline_view_handle_event(
    TimelineInteractionState *interaction,
    const SDL_Event *event,
    SDL_Rect timeline_rect,
    ProjectState *project,
    TransportState *transport
);

#endif
