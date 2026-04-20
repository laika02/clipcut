#include "ui/audio_tracks_view.h"

#include "ui/bitmap_text.h"

static bool point_in_rect(int x, int y, SDL_Rect rect) {
    return x >= rect.x && x < (rect.x + rect.w) && y >= rect.y && y < (rect.y + rect.h);
}

static double clamp_double(double value, double min, double max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static size_t enabled_audio_track_count(const ProjectState *project) {
    size_t enabled = 0;
    if (project == NULL) {
        return 0;
    }

    for (size_t i = 0; i < project->audio_track_count; ++i) {
        if (project->audio_tracks[i].enabled) {
            enabled++;
        }
    }
    return enabled;
}

SDL_Rect audio_tracks_view_header_rect(SDL_Rect sidebar_rect) {
    const SDL_Rect rect = {
        .x = sidebar_rect.x + 20,
        .y = sidebar_rect.y + 72,
        .w = sidebar_rect.w - 40,
        .h = 24,
    };
    return rect;
}

SDL_Rect audio_tracks_view_row_rect(SDL_Rect sidebar_rect, size_t index) {
    const SDL_Rect header = audio_tracks_view_header_rect(sidebar_rect);
    const SDL_Rect rect = {
        .x = sidebar_rect.x + 20,
        .y = header.y + header.h + 12 + ((int)index * 76),
        .w = sidebar_rect.w - 40,
        .h = 64,
    };
    return rect;
}

static SDL_Rect audio_tracks_view_row_rect_scrolled(SDL_Rect sidebar_rect, size_t index, int scroll_y) {
    SDL_Rect row = audio_tracks_view_row_rect(sidebar_rect, index);
    row.y -= scroll_y;
    return row;
}

static SDL_Rect rows_clip_rect(SDL_Rect sidebar_rect) {
    const SDL_Rect header = audio_tracks_view_header_rect(sidebar_rect);
    const int y = header.y + header.h + 12;
    SDL_Rect rect = {
        .x = sidebar_rect.x + 20,
        .y = y,
        .w = sidebar_rect.w - 40,
        .h = (sidebar_rect.y + sidebar_rect.h - 10) - y,
    };
    if (rect.h < 0) {
        rect.h = 0;
    }
    return rect;
}

static SDL_Rect row_toggle_rect(SDL_Rect row) {
    return (SDL_Rect){.x = row.x + 8, .y = row.y + 8, .w = 46, .h = 20};
}

static SDL_Rect row_extract_rect(SDL_Rect row) {
    return (SDL_Rect){.x = row.x + row.w - 158, .y = row.y + 8, .w = 82, .h = 20};
}

static SDL_Rect row_preview_button_rect(SDL_Rect row) {
    return (SDL_Rect){.x = row.x + row.w - 68, .y = row.y + 8, .w = 60, .h = 20};
}

static SDL_Rect row_scrub_rect(SDL_Rect row) {
    return (SDL_Rect){.x = row.x + 10, .y = row.y + 44, .w = row.w - 20, .h = 8};
}

static SDL_Rect row_scrub_hit_rect(SDL_Rect row) {
    return (SDL_Rect){.x = row.x + 10, .y = row.y + 36, .w = row.w - 20, .h = 24};
}

static double scrub_time_for_x(const ProjectState *project, SDL_Rect scrub, int x) {
    if (project == NULL || project->duration_sec <= 0.0 || scrub.w <= 1) {
        return 0.0;
    }
    const double ratio = clamp_double((double)(x - scrub.x) / (double)scrub.w, 0.0, 1.0);
    return ratio * project->duration_sec;
}

static int scrub_x_for_time(const ProjectState *project, SDL_Rect scrub, double sec) {
    if (project == NULL || project->duration_sec <= 0.0) {
        return scrub.x;
    }
    const double ratio = clamp_double(sec / project->duration_sec, 0.0, 1.0);
    return scrub.x + (int)(ratio * scrub.w);
}

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
) {
    if (renderer == NULL || project == NULL || project->audio_track_count == 0) {
        return;
    }

    const SDL_Rect header = audio_tracks_view_header_rect(sidebar_rect);
    SDL_SetRenderDrawColor(renderer, 33, 38, 46, 255);
    SDL_RenderFillRect(renderer, &header);
    SDL_SetRenderDrawColor(renderer, 80, 95, 113, 255);
    SDL_RenderDrawRect(renderer, &header);
    bitmap_text_draw(renderer, header.x + 10, header.y + 8, 1, (SDL_Color){210, 220, 235, 255}, "AUDIO");

    const size_t enabled = enabled_audio_track_count(project);
    char enabled_label[32] = {0};
    SDL_snprintf(enabled_label, sizeof(enabled_label), "%d/%d ON", (int)enabled, (int)project->audio_track_count);
    bitmap_text_draw(renderer, header.x + 82, header.y + 8, 1, (SDL_Color){132, 150, 171, 255}, enabled_label);

    if (collapsed) {
        bitmap_text_draw(renderer, header.x + 10, header.y + 34, 1, (SDL_Color){118, 136, 157, 255}, "CLICK AUDIO TO EXPAND");
        return;
    }

    const SDL_Rect clip = rows_clip_rect(sidebar_rect);
    SDL_RenderSetClipRect(renderer, &clip);
    for (size_t i = 0; i < project->audio_track_count; ++i) {
        const SDL_Rect row = audio_tracks_view_row_rect_scrolled(sidebar_rect, i, scroll_y);
        if (row.y + row.h < clip.y || row.y > clip.y + clip.h) {
            continue;
        }
        const bool is_enabled = project->audio_tracks[i].enabled;
        const bool has_preview_position = preview_position_visible && preview_track_index == i;
        const bool is_previewing = preview_active && preview_track_index == i;
        SDL_SetRenderDrawColor(renderer, is_previewing ? 45 : 34, is_previewing ? 56 : 42, is_previewing ? 63 : 49, 255);
        SDL_RenderFillRect(renderer, &row);
        SDL_SetRenderDrawColor(renderer,
            is_enabled ? 95 : 74,
            is_enabled ? 201 : 84,
            is_enabled ? 142 : 94,
            255);
        SDL_RenderDrawRect(renderer, &row);

        const SDL_Rect toggle = row_toggle_rect(row);
        SDL_SetRenderDrawColor(renderer,
            is_enabled ? 82 : 64,
            is_enabled ? 165 : 72,
            is_enabled ? 119 : 80,
            255);
        SDL_RenderFillRect(renderer, &toggle);
        SDL_SetRenderDrawColor(renderer, 130, 151, 171, 255);
        SDL_RenderDrawRect(renderer, &toggle);

        const SDL_Rect extract = row_extract_rect(row);
        SDL_SetRenderDrawColor(renderer, 48, 62, 78, 255);
        SDL_RenderFillRect(renderer, &extract);
        SDL_SetRenderDrawColor(renderer, 118, 151, 188, 255);
        SDL_RenderDrawRect(renderer, &extract);

        const SDL_Rect preview_button = row_preview_button_rect(row);
        SDL_SetRenderDrawColor(renderer, is_previewing ? 123 : 48, is_previewing ? 174 : 62, is_previewing ? 116 : 78, 255);
        SDL_RenderFillRect(renderer, &preview_button);
        SDL_SetRenderDrawColor(renderer, 149, 173, 199, 255);
        SDL_RenderDrawRect(renderer, &preview_button);

        char track_label[32] = {0};
        char toggle_label[16] = {0};
        SDL_snprintf(track_label, sizeof(track_label), "TRACK %d", (int)i + 1);
        SDL_snprintf(toggle_label, sizeof(toggle_label), "%s", is_enabled ? "ON" : "OFF");
        bitmap_text_draw(renderer, toggle.x + 9, toggle.y + 7, 1, (SDL_Color){218, 235, 224, 255}, toggle_label);
        bitmap_text_draw(renderer, row.x + 64, row.y + 14, 1, (SDL_Color){210, 220, 235, 255}, track_label);
        bitmap_text_draw(renderer, extract.x + 8, extract.y + 7, 1, (SDL_Color){196, 213, 232, 255}, "EXTRACT");
        bitmap_text_draw(
            renderer,
            preview_button.x + 8,
            preview_button.y + 7,
            1,
            (SDL_Color){220, 231, 240, 255},
            is_previewing ? "PAUSE" : "PLAY"
        );

        const SDL_Rect scrub = row_scrub_rect(row);
        SDL_SetRenderDrawColor(renderer, 20, 24, 30, 255);
        SDL_RenderFillRect(renderer, &scrub);
        SDL_SetRenderDrawColor(renderer, 74, 88, 106, 255);
        SDL_RenderDrawRect(renderer, &scrub);
        if (has_preview_position) {
            const int scrub_x = scrub_x_for_time(project, scrub, preview_sec);
            SDL_Rect filled = {.x = scrub.x, .y = scrub.y, .w = scrub_x - scrub.x, .h = scrub.h};
            if (filled.w > 0) {
                SDL_SetRenderDrawColor(renderer, 123, 174, 116, 255);
                SDL_RenderFillRect(renderer, &filled);
            }
            SDL_SetRenderDrawColor(renderer, 243, 211, 85, 255);
            SDL_RenderDrawLine(renderer, scrub_x, scrub.y - 4, scrub_x, scrub.y + scrub.h + 4);
        }
    }
    SDL_RenderSetClipRect(renderer, NULL);

    const int max_scroll = audio_tracks_view_max_scroll(sidebar_rect, project, collapsed);
    if (max_scroll > 0) {
        const SDL_Rect track = {
            .x = sidebar_rect.x + sidebar_rect.w - 12,
            .y = clip.y,
            .w = 4,
            .h = clip.h,
        };
        const int thumb_h = clip.h > 24 ? (clip.h * clip.h) / (clip.h + max_scroll) : clip.h;
        const int thumb_y = clip.y + ((clip.h - thumb_h) * scroll_y) / max_scroll;
        const SDL_Rect thumb = {.x = track.x, .y = thumb_y, .w = track.w, .h = thumb_h};
        SDL_SetRenderDrawColor(renderer, 38, 45, 54, 255);
        SDL_RenderFillRect(renderer, &track);
        SDL_SetRenderDrawColor(renderer, 105, 139, 174, 255);
        SDL_RenderFillRect(renderer, &thumb);
    }
}

int audio_tracks_view_content_bottom(SDL_Rect sidebar_rect, const ProjectState *project, bool collapsed) {
    const SDL_Rect header = audio_tracks_view_header_rect(sidebar_rect);
    if (project == NULL || project->audio_track_count == 0 || collapsed) {
        return header.y + header.h + 32;
    }

    const SDL_Rect row = audio_tracks_view_row_rect(sidebar_rect, project->audio_track_count - 1);
    return row.y + row.h;
}

int audio_tracks_view_max_scroll(SDL_Rect sidebar_rect, const ProjectState *project, bool collapsed) {
    if (project == NULL || project->audio_track_count == 0 || collapsed) {
        return 0;
    }

    const SDL_Rect clip = rows_clip_rect(sidebar_rect);
    const SDL_Rect last_row = audio_tracks_view_row_rect(sidebar_rect, project->audio_track_count - 1);
    const int content_bottom = last_row.y + last_row.h;
    const int viewport_bottom = clip.y + clip.h;
    return content_bottom > viewport_bottom ? content_bottom - viewport_bottom : 0;
}

bool audio_tracks_view_handle_event(
    const SDL_Event *event,
    SDL_Rect sidebar_rect,
    ProjectState *project,
    bool *collapsed,
    int scroll_y
) {
    if (event == NULL || project == NULL || collapsed == NULL || project->audio_track_count == 0) {
        return false;
    }
    if (event->type != SDL_MOUSEBUTTONDOWN || event->button.button != SDL_BUTTON_LEFT) {
        return false;
    }

    const int x = event->button.x;
    const int y = event->button.y;
    const SDL_Rect header = audio_tracks_view_header_rect(sidebar_rect);
    if (point_in_rect(x, y, header)) {
        *collapsed = !*collapsed;
        return true;
    }

    if (*collapsed) {
        return false;
    }

    const SDL_Rect clip = rows_clip_rect(sidebar_rect);
    if (!point_in_rect(x, y, clip)) {
        return false;
    }

    for (size_t i = 0; i < project->audio_track_count; ++i) {
        const SDL_Rect row = audio_tracks_view_row_rect_scrolled(sidebar_rect, i, scroll_y);
        if (!point_in_rect(x, y, row)) {
            continue;
        }

        const SDL_Rect toggle = row_toggle_rect(row);
        if (!point_in_rect(x, y, toggle) && y >= row.y + 36) {
            return false;
        }

        project->audio_tracks[i].enabled = !project->audio_tracks[i].enabled;
        return true;
    }

    return false;
}

AudioTracksViewAction audio_tracks_view_action_at_point(
    SDL_Rect sidebar_rect,
    const ProjectState *project,
    bool collapsed,
    int scroll_y,
    bool preview_active,
    size_t preview_track_index,
    int x,
    int y
) {
    AudioTracksViewAction action = {.kind = AUDIO_TRACKS_VIEW_ACTION_NONE, .track_index = 0, .seek_sec = 0.0};
    if (project == NULL || project->audio_track_count == 0 || collapsed) {
        return action;
    }

    const SDL_Rect clip = rows_clip_rect(sidebar_rect);
    if (!point_in_rect(x, y, clip)) {
        return action;
    }

    for (size_t i = 0; i < project->audio_track_count; ++i) {
        const SDL_Rect row = audio_tracks_view_row_rect_scrolled(sidebar_rect, i, scroll_y);
        if (!point_in_rect(x, y, row)) {
            continue;
        }

        action.track_index = i;
        const SDL_Rect extract = row_extract_rect(row);
        if (point_in_rect(x, y, extract)) {
            action.kind = AUDIO_TRACKS_VIEW_ACTION_EXTRACT;
            return action;
        }

        const SDL_Rect preview_button = row_preview_button_rect(row);
        if (point_in_rect(x, y, preview_button)) {
            action.kind = AUDIO_TRACKS_VIEW_ACTION_PREVIEW_TOGGLE;
            action.seek_sec = project->trim_start_sec;
            if (preview_active && preview_track_index == i) {
                action.seek_sec = 0.0;
            }
            return action;
        }

        const SDL_Rect scrub = row_scrub_hit_rect(row);
        if (point_in_rect(x, y, scrub)) {
            action.kind = AUDIO_TRACKS_VIEW_ACTION_PREVIEW_SEEK;
            action.seek_sec = scrub_time_for_x(project, scrub, x);
            return action;
        }

        action.kind = AUDIO_TRACKS_VIEW_ACTION_TOGGLE;
        return action;
    }

    return action;
}

bool audio_tracks_view_scrub_time_for_track(
    SDL_Rect sidebar_rect,
    const ProjectState *project,
    size_t track_index,
    int scroll_y,
    int x,
    double *seek_sec
) {
    if (project == NULL || seek_sec == NULL || track_index >= project->audio_track_count) {
        return false;
    }

    const SDL_Rect row = audio_tracks_view_row_rect_scrolled(sidebar_rect, track_index, scroll_y);
    const SDL_Rect scrub = row_scrub_hit_rect(row);
    *seek_sec = scrub_time_for_x(project, scrub, x);
    return true;
}

bool audio_tracks_view_track_at_point(
    SDL_Rect sidebar_rect,
    const ProjectState *project,
    bool collapsed,
    int scroll_y,
    int x,
    int y,
    size_t *track_index
) {
    if (project == NULL || track_index == NULL || project->audio_track_count == 0 || collapsed) {
        return false;
    }

    const SDL_Rect clip = rows_clip_rect(sidebar_rect);
    if (!point_in_rect(x, y, clip)) {
        return false;
    }

    for (size_t i = 0; i < project->audio_track_count; ++i) {
        const SDL_Rect row = audio_tracks_view_row_rect_scrolled(sidebar_rect, i, scroll_y);
        if (point_in_rect(x, y, row)) {
            *track_index = i;
            return true;
        }
    }

    return false;
}
