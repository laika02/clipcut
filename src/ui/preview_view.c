#include "ui/preview_view.h"

#include <stdlib.h>

typedef struct SourcePoint {
    int x;
    int y;
} SourcePoint;

static int clamp_int(int value, int min, int max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static bool point_in_rect(int x, int y, SDL_Rect rect) {
    return x >= rect.x && x < (rect.x + rect.w) && y >= rect.y && y < (rect.y + rect.h);
}

static bool point_in_rect_inclusive(int x, int y, SDL_Rect rect) {
    return x >= rect.x && x <= (rect.x + rect.w) && y >= rect.y && y <= (rect.y + rect.h);
}

static bool preview_drag_mode_is_resize(PreviewDragMode mode) {
    return mode == PREVIEW_DRAG_RESIZE_NW ||
           mode == PREVIEW_DRAG_RESIZE_NE ||
           mode == PREVIEW_DRAG_RESIZE_SW ||
           mode == PREVIEW_DRAG_RESIZE_SE;
}

static CropRect normalized_crop(CropRect crop, const ProjectState *project) {
    const int min_size = 8;
    const int max_width = project->width > 0 ? project->width : 0;
    const int max_height = project->height > 0 ? project->height : 0;

    if (crop.width < 0) {
        crop.x += crop.width;
        crop.width = -crop.width;
    }
    if (crop.height < 0) {
        crop.y += crop.height;
        crop.height = -crop.height;
    }

    crop.x = clamp_int(crop.x, 0, max_width);
    crop.y = clamp_int(crop.y, 0, max_height);
    crop.width = clamp_int(crop.width, min_size, max_width);
    crop.height = clamp_int(crop.height, min_size, max_height);

    if (crop.x + crop.width > max_width) {
        crop.width = max_width - crop.x;
    }
    if (crop.y + crop.height > max_height) {
        crop.height = max_height - crop.y;
    }

    if (crop.width < min_size) {
        crop.width = max_width >= min_size ? min_size : max_width;
        crop.x = clamp_int(crop.x, 0, max_width - crop.width);
    }
    if (crop.height < min_size) {
        crop.height = max_height >= min_size ? min_size : max_height;
        crop.y = clamp_int(crop.y, 0, max_height - crop.height);
    }

    return crop;
}

static SDL_Rect crop_to_preview_rect(SDL_Rect preview_rect, const ProjectState *project, CropRect crop) {
    SDL_Rect rect = {0};
    if (preview_rect.w <= 0 || preview_rect.h <= 0 || project->width <= 0 || project->height <= 0) {
        return rect;
    }

    const double scale_x = (double)preview_rect.w / (double)project->width;
    const double scale_y = (double)preview_rect.h / (double)project->height;

    rect.x = preview_rect.x + (int)(crop.x * scale_x);
    rect.y = preview_rect.y + (int)(crop.y * scale_y);
    rect.w = (int)(crop.width * scale_x);
    rect.h = (int)(crop.height * scale_y);

    if (rect.w < 1) {
        rect.w = 1;
    }
    if (rect.h < 1) {
        rect.h = 1;
    }

    return rect;
}

static SourcePoint preview_to_source_point(SDL_Rect preview_rect, const ProjectState *project, int x, int y) {
    SourcePoint point = {0};
    if (preview_rect.w <= 0 || preview_rect.h <= 0 || project->width <= 0 || project->height <= 0) {
        return point;
    }

    const double ratio_x = (double)(x - preview_rect.x) / (double)preview_rect.w;
    const double ratio_y = (double)(y - preview_rect.y) / (double)preview_rect.h;
    point.x = clamp_int((int)(ratio_x * project->width), 0, project->width);
    point.y = clamp_int((int)(ratio_y * project->height), 0, project->height);
    return point;
}

static SDL_Rect corner_handle_rect_with_size(SDL_Rect crop_rect, PreviewDragMode mode, int size) {
    SDL_Rect handle = {.w = size, .h = size};

    if (mode == PREVIEW_DRAG_RESIZE_NW) {
        handle.x = crop_rect.x - (size / 2);
        handle.y = crop_rect.y - (size / 2);
    } else if (mode == PREVIEW_DRAG_RESIZE_NE) {
        handle.x = crop_rect.x + crop_rect.w - (size / 2);
        handle.y = crop_rect.y - (size / 2);
    } else if (mode == PREVIEW_DRAG_RESIZE_SW) {
        handle.x = crop_rect.x - (size / 2);
        handle.y = crop_rect.y + crop_rect.h - (size / 2);
    } else {
        handle.x = crop_rect.x + crop_rect.w - (size / 2);
        handle.y = crop_rect.y + crop_rect.h - (size / 2);
    }

    return handle;
}

static SDL_Rect corner_handle_draw_rect(SDL_Rect crop_rect, PreviewDragMode mode) {
    return corner_handle_rect_with_size(crop_rect, mode, 12);
}

static SDL_Rect corner_handle_hit_rect(SDL_Rect crop_rect, PreviewDragMode mode) {
    return corner_handle_rect_with_size(crop_rect, mode, 32);
}

static PreviewDragMode hit_test_mode(SDL_Rect crop_rect, int x, int y) {
    const PreviewDragMode corners[] = {
        PREVIEW_DRAG_RESIZE_NW,
        PREVIEW_DRAG_RESIZE_NE,
        PREVIEW_DRAG_RESIZE_SW,
        PREVIEW_DRAG_RESIZE_SE,
    };

    for (size_t i = 0; i < sizeof(corners) / sizeof(corners[0]); ++i) {
        if (point_in_rect(x, y, corner_handle_hit_rect(crop_rect, corners[i]))) {
            return corners[i];
        }
    }

    if (point_in_rect(x, y, crop_rect)) {
        return PREVIEW_DRAG_MOVE;
    }

    return PREVIEW_DRAG_CREATE;
}

static void draw_outside_shade(SDL_Renderer *renderer, SDL_Rect preview_rect, SDL_Rect crop_rect) {
    SDL_Rect top = {.x = preview_rect.x, .y = preview_rect.y, .w = preview_rect.w, .h = crop_rect.y - preview_rect.y};
    SDL_Rect bottom = {
        .x = preview_rect.x,
        .y = crop_rect.y + crop_rect.h,
        .w = preview_rect.w,
        .h = (preview_rect.y + preview_rect.h) - (crop_rect.y + crop_rect.h)
    };
    SDL_Rect left = {.x = preview_rect.x, .y = crop_rect.y, .w = crop_rect.x - preview_rect.x, .h = crop_rect.h};
    SDL_Rect right = {
        .x = crop_rect.x + crop_rect.w,
        .y = crop_rect.y,
        .w = (preview_rect.x + preview_rect.w) - (crop_rect.x + crop_rect.w),
        .h = crop_rect.h
    };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 8, 10, 14, 122);
    if (top.w > 0 && top.h > 0) SDL_RenderFillRect(renderer, &top);
    if (bottom.w > 0 && bottom.h > 0) SDL_RenderFillRect(renderer, &bottom);
    if (left.w > 0 && left.h > 0) SDL_RenderFillRect(renderer, &left);
    if (right.w > 0 && right.h > 0) SDL_RenderFillRect(renderer, &right);
}

static CropRect resize_crop(
    PreviewDragMode mode,
    CropRect anchor_crop,
    SourcePoint current,
    const ProjectState *project
) {
    CropRect crop = anchor_crop;

    if (mode == PREVIEW_DRAG_RESIZE_NW) {
        crop.x = current.x;
        crop.y = current.y;
        crop.width = (anchor_crop.x + anchor_crop.width) - current.x;
        crop.height = (anchor_crop.y + anchor_crop.height) - current.y;
    } else if (mode == PREVIEW_DRAG_RESIZE_NE) {
        crop.y = current.y;
        crop.width = current.x - anchor_crop.x;
        crop.height = (anchor_crop.y + anchor_crop.height) - current.y;
    } else if (mode == PREVIEW_DRAG_RESIZE_SW) {
        crop.x = current.x;
        crop.width = (anchor_crop.x + anchor_crop.width) - current.x;
        crop.height = current.y - anchor_crop.y;
    } else if (mode == PREVIEW_DRAG_RESIZE_SE) {
        crop.width = current.x - anchor_crop.x;
        crop.height = current.y - anchor_crop.y;
    }

    return normalized_crop(crop, project);
}

void preview_view_reset_interaction(PreviewInteractionState *state) {
    if (state == NULL) {
        return;
    }

    state->mode = PREVIEW_DRAG_NONE;
    state->anchor_crop = (CropRect){0};
    state->anchor_source_x = 0;
    state->anchor_source_y = 0;
}

void preview_view_draw_overlay(SDL_Renderer *renderer, SDL_Rect preview_rect, const ProjectState *project) {
    if (renderer == NULL || project == NULL || !project_has_media(project)) {
        return;
    }

    const CropRect crop = normalized_crop(project->crop, project);
    const SDL_Rect crop_rect = crop_to_preview_rect(preview_rect, project, crop);
    draw_outside_shade(renderer, preview_rect, crop_rect);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 246, 198, 78, 255);
    SDL_RenderDrawRect(renderer, &crop_rect);

    const PreviewDragMode corners[] = {
        PREVIEW_DRAG_RESIZE_NW,
        PREVIEW_DRAG_RESIZE_NE,
        PREVIEW_DRAG_RESIZE_SW,
        PREVIEW_DRAG_RESIZE_SE,
    };
    for (size_t i = 0; i < sizeof(corners) / sizeof(corners[0]); ++i) {
        const SDL_Rect handle = corner_handle_draw_rect(crop_rect, corners[i]);
        SDL_SetRenderDrawColor(renderer, 246, 198, 78, 255);
        SDL_RenderFillRect(renderer, &handle);
        SDL_SetRenderDrawColor(renderer, 34, 36, 42, 255);
        SDL_RenderDrawRect(renderer, &handle);
    }
}

bool preview_view_handle_event(
    PreviewInteractionState *interaction,
    const SDL_Event *event,
    SDL_Rect preview_rect,
    ProjectState *project
) {
    if (interaction == NULL || event == NULL || project == NULL || !project_has_media(project)) {
        return false;
    }

    const CropRect current_crop = normalized_crop(project->crop, project);
    const SDL_Rect crop_rect = crop_to_preview_rect(preview_rect, project, current_crop);

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        const PreviewDragMode mode = hit_test_mode(crop_rect, event->button.x, event->button.y);
        const bool inside_preview = point_in_rect_inclusive(event->button.x, event->button.y, preview_rect);
        if (!inside_preview && !preview_drag_mode_is_resize(mode)) {
            return false;
        }

        interaction->mode = mode;
        interaction->anchor_crop = current_crop;
        const SourcePoint anchor = preview_to_source_point(preview_rect, project, event->button.x, event->button.y);
        interaction->anchor_source_x = anchor.x;
        interaction->anchor_source_y = anchor.y;

        if (interaction->mode == PREVIEW_DRAG_CREATE) {
            project->crop = normalized_crop((CropRect){
                .x = anchor.x,
                .y = anchor.y,
                .width = 8,
                .height = 8,
            }, project);
        }
        return true;
    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_RIGHT) {
        if (!point_in_rect_inclusive(event->button.x, event->button.y, preview_rect)) {
            return false;
        }
        project->crop = (CropRect){.x = 0, .y = 0, .width = project->width, .height = project->height};
        preview_view_reset_interaction(interaction);
        return true;
    }

    if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {
        const bool was_active = interaction->mode != PREVIEW_DRAG_NONE;
        preview_view_reset_interaction(interaction);
        return was_active;
    }

    if (event->type != SDL_MOUSEMOTION || interaction->mode == PREVIEW_DRAG_NONE) {
        return false;
    }

    const SourcePoint current = preview_to_source_point(preview_rect, project, event->motion.x, event->motion.y);

    if (interaction->mode == PREVIEW_DRAG_MOVE) {
        const int delta_x = current.x - interaction->anchor_source_x;
        const int delta_y = current.y - interaction->anchor_source_y;
        CropRect moved = interaction->anchor_crop;
        moved.x += delta_x;
        moved.y += delta_y;
        moved.x = clamp_int(moved.x, 0, project->width - moved.width);
        moved.y = clamp_int(moved.y, 0, project->height - moved.height);
        project->crop = moved;
    } else if (interaction->mode == PREVIEW_DRAG_CREATE) {
        project->crop = normalized_crop((CropRect){
            .x = interaction->anchor_source_x,
            .y = interaction->anchor_source_y,
            .width = current.x - interaction->anchor_source_x,
            .height = current.y - interaction->anchor_source_y,
        }, project);
    } else {
        project->crop = resize_crop(interaction->mode, interaction->anchor_crop, current, project);
    }

    return true;
}
