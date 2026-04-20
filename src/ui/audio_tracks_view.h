#ifndef CLIPCUT_UI_AUDIO_TRACKS_VIEW_H
#define CLIPCUT_UI_AUDIO_TRACKS_VIEW_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL.h>

#include "model/project.h"

typedef enum AudioTracksViewActionKind {
    AUDIO_TRACKS_VIEW_ACTION_NONE = 0,
    AUDIO_TRACKS_VIEW_ACTION_TOGGLE,
    AUDIO_TRACKS_VIEW_ACTION_EXTRACT,
    AUDIO_TRACKS_VIEW_ACTION_PREVIEW_TOGGLE,
    AUDIO_TRACKS_VIEW_ACTION_PREVIEW_SEEK
} AudioTracksViewActionKind;

typedef struct AudioTracksViewAction {
    AudioTracksViewActionKind kind;
    size_t track_index;
    double seek_sec;
} AudioTracksViewAction;

SDL_Rect audio_tracks_view_header_rect(SDL_Rect sidebar_rect);
SDL_Rect audio_tracks_view_row_rect(SDL_Rect sidebar_rect, size_t index);
void audio_tracks_view_draw(
    SDL_Renderer *renderer,
    SDL_Rect sidebar_rect,
    const ProjectState *project,
    bool collapsed,
    int scroll_y,
    bool preview_position_visible,
    bool preview_active,
    size_t preview_track_index,
    double preview_sec
);
int audio_tracks_view_content_bottom(SDL_Rect sidebar_rect, const ProjectState *project, bool collapsed);
int audio_tracks_view_max_scroll(SDL_Rect sidebar_rect, const ProjectState *project, bool collapsed);
bool audio_tracks_view_handle_event(
    const SDL_Event *event,
    SDL_Rect sidebar_rect,
    ProjectState *project,
    bool *collapsed,
    int scroll_y
);
AudioTracksViewAction audio_tracks_view_action_at_point(
    SDL_Rect sidebar_rect,
    const ProjectState *project,
    bool collapsed,
    int scroll_y,
    bool preview_active,
    size_t preview_track_index,
    int x,
    int y
);
bool audio_tracks_view_scrub_time_for_track(
    SDL_Rect sidebar_rect,
    const ProjectState *project,
    size_t track_index,
    int scroll_y,
    int x,
    double *seek_sec
);
bool audio_tracks_view_track_at_point(
    SDL_Rect sidebar_rect,
    const ProjectState *project,
    bool collapsed,
    int scroll_y,
    int x,
    int y,
    size_t *track_index
);

#endif
