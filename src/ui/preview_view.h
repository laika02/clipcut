#ifndef CLIPCUT_UI_PREVIEW_VIEW_H
#define CLIPCUT_UI_PREVIEW_VIEW_H

#include <stdbool.h>

#include <SDL.h>

#include "model/project.h"

typedef enum PreviewDragMode {
    PREVIEW_DRAG_NONE = 0,
    PREVIEW_DRAG_MOVE,
    PREVIEW_DRAG_CREATE,
    PREVIEW_DRAG_RESIZE_NW,
    PREVIEW_DRAG_RESIZE_NE,
    PREVIEW_DRAG_RESIZE_SW,
    PREVIEW_DRAG_RESIZE_SE
} PreviewDragMode;

typedef struct PreviewInteractionState {
    PreviewDragMode mode;
    CropRect anchor_crop;
    int anchor_source_x;
    int anchor_source_y;
} PreviewInteractionState;

void preview_view_reset_interaction(PreviewInteractionState *state);
void preview_view_draw_overlay(
    SDL_Renderer *renderer,
    SDL_Rect preview_rect,
    const ProjectState *project
);
bool preview_view_handle_event(
    PreviewInteractionState *interaction,
    const SDL_Event *event,
    SDL_Rect preview_rect,
    ProjectState *project
);

#endif
