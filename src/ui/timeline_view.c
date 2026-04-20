#include "ui/timeline_view.h"

static bool point_in_rect(int x, int y, SDL_Rect rect) {
    return x >= rect.x && x < (rect.x + rect.w) && y >= rect.y && y < (rect.y + rect.h);
}

static bool point_in_rect_inclusive(int x, int y, SDL_Rect rect) {
    return x >= rect.x && x <= (rect.x + rect.w) && y >= rect.y && y <= (rect.y + rect.h);
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

static double timeline_ratio_for_x(SDL_Rect rect, int x) {
    if (rect.w <= 1) {
        return 0.0;
    }

    const double ratio = (double)(x - rect.x) / (double)rect.w;
    return clamp_double(ratio, 0.0, 1.0);
}

static double project_time_for_timeline_x(const ProjectState *project, SDL_Rect rect, int x) {
    return timeline_ratio_for_x(rect, x) * project->duration_sec;
}

static int timeline_x_for_time(SDL_Rect rect, double duration, double time_sec) {
    if (duration <= 0.0) {
        return rect.x;
    }
    return rect.x + (int)((time_sec / duration) * rect.w);
}

static SDL_Rect x_hit_rect(SDL_Rect timeline_rect, int center_x, int width) {
    SDL_Rect rect = {
        .x = center_x - (width / 2),
        .y = timeline_rect.y - 8,
        .w = width,
        .h = timeline_rect.h + 16,
    };
    return rect;
}

static SDL_Rect trim_handle_draw_rect(SDL_Rect timeline_rect, int center_x) {
    SDL_Rect rect = {
        .x = center_x - 2,
        .y = timeline_rect.y,
        .w = 4,
        .h = timeline_rect.h,
    };
    return rect;
}

static void draw_trim_handle(SDL_Renderer *renderer, SDL_Rect timeline_rect, int center_x) {
    const SDL_Rect handle = trim_handle_draw_rect(timeline_rect, center_x);

    SDL_SetRenderDrawColor(renderer, 174, 213, 255, 255);
    SDL_RenderFillRect(renderer, &handle);
    SDL_SetRenderDrawColor(renderer, 14, 18, 25, 255);
    SDL_RenderDrawRect(renderer, &handle);
}

SDL_Rect timeline_view_rect(int window_width, int window_height) {
    const SDL_Rect preview_panel = {
        .x = 28,
        .y = 18,
        .w = window_width - 56,
        .h = (window_height * 11) / 20,
    };
    const SDL_Rect controls = {
        .x = 28,
        .y = preview_panel.y + preview_panel.h + 18,
        .w = window_width - 56,
        .h = window_height - (preview_panel.y + preview_panel.h + 46),
    };
    const SDL_Rect timeline = {
        .x = controls.x + 20,
        .y = controls.y + 20,
        .w = controls.w - 40,
        .h = 36,
    };
    return timeline;
}

void timeline_view_draw(
    SDL_Renderer *renderer,
    SDL_Rect timeline_rect,
    const ProjectState *project,
    const TransportState *transport,
    const TimelineInteractionState *interaction
) {
    SDL_SetRenderDrawColor(renderer, 34, 51, 71, 255);
    SDL_RenderFillRect(renderer, &timeline_rect);
    SDL_SetRenderDrawColor(renderer, 98, 172, 255, 255);
    SDL_RenderDrawRect(renderer, &timeline_rect);

    const double duration = project->duration_sec > 0.0 ? project->duration_sec : 1.0;
    const int trim_x = timeline_x_for_time(timeline_rect, duration, transport->trim_start_sec);
    const int trim_end_x = timeline_x_for_time(timeline_rect, duration, transport->trim_end_sec);
    SDL_Rect trim_region = {
        .x = trim_x,
        .y = timeline_rect.y,
        .w = trim_end_x - trim_x,
        .h = timeline_rect.h,
    };

    SDL_SetRenderDrawColor(renderer, 55, 99, 146, 255);
    SDL_RenderFillRect(renderer, &trim_region);
    SDL_SetRenderDrawColor(renderer, 130, 196, 255, 255);
    SDL_RenderDrawRect(renderer, &trim_region);

    for (size_t i = 0; i < project->cut_count; ++i) {
        const int cut_start_x = timeline_x_for_time(timeline_rect, duration, project->cuts[i].start_sec);
        const int cut_end_x = timeline_x_for_time(timeline_rect, duration, project->cuts[i].end_sec);
        SDL_Rect cut = {
            .x = cut_start_x,
            .y = timeline_rect.y,
            .w = cut_end_x - cut_start_x,
            .h = timeline_rect.h,
        };
        if (cut.w > 0) {
            SDL_SetRenderDrawColor(renderer, 105, 36, 48, 255);
            SDL_RenderFillRect(renderer, &cut);
            SDL_SetRenderDrawColor(renderer, 242, 91, 107, 255);
            SDL_RenderDrawRect(renderer, &cut);
        }
    }

    if (interaction != NULL && interaction->mode == TIMELINE_DRAG_CUT_RANGE) {
        int start_x = timeline_x_for_time(timeline_rect, duration, interaction->anchor_sec);
        int end_x = timeline_x_for_time(timeline_rect, duration, interaction->current_sec);
        if (end_x < start_x) {
            const int tmp = start_x;
            start_x = end_x;
            end_x = tmp;
        }
        SDL_Rect pending_cut = {
            .x = start_x,
            .y = timeline_rect.y - 4,
            .w = end_x - start_x,
            .h = timeline_rect.h + 8,
        };
        if (pending_cut.w < 2) {
            pending_cut.w = 2;
        }
        SDL_SetRenderDrawColor(renderer, 132, 40, 54, 180);
        SDL_RenderFillRect(renderer, &pending_cut);
        SDL_SetRenderDrawColor(renderer, 255, 117, 130, 255);
        SDL_RenderDrawRect(renderer, &pending_cut);
    }

    draw_trim_handle(renderer, timeline_rect, trim_x);
    draw_trim_handle(renderer, timeline_rect, trim_end_x);

    const int playhead_x = timeline_x_for_time(timeline_rect, duration, transport->playhead_sec);
    SDL_SetRenderDrawColor(renderer, 243, 211, 85, 255);
    SDL_RenderDrawLine(renderer, playhead_x, timeline_rect.y, playhead_x, timeline_rect.y + timeline_rect.h);
    SDL_Rect playhead_knob = {
        .x = playhead_x - 5,
        .y = timeline_rect.y - 8,
        .w = 10,
        .h = 8,
    };
    SDL_RenderFillRect(renderer, &playhead_knob);
}

void timeline_view_reset_interaction(TimelineInteractionState *state) {
    if (state == NULL) {
        return;
    }

    state->mode = TIMELINE_DRAG_NONE;
    state->anchor_sec = 0.0;
    state->current_sec = 0.0;
    state->trim_anchor_start_sec = 0.0;
    state->trim_anchor_end_sec = 0.0;
}

bool timeline_view_handle_event(
    TimelineInteractionState *interaction,
    const SDL_Event *event,
    SDL_Rect timeline_rect,
    ProjectState *project,
    TransportState *transport
) {
    if (interaction == NULL || event == NULL || project == NULL || transport == NULL || !project_has_media(project)) {
        return false;
    }

    const double duration = project->duration_sec;
    if (duration <= 0.0) {
        return false;
    }

    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        const int trim_x = timeline_x_for_time(timeline_rect, duration, transport->trim_start_sec);
        const int trim_end_x = timeline_x_for_time(timeline_rect, duration, transport->trim_end_sec);
        const int playhead_x = timeline_x_for_time(timeline_rect, duration, transport->playhead_sec);
        const SDL_Rect trim_start_hit = x_hit_rect(timeline_rect, trim_x, 32);
        const SDL_Rect trim_end_hit = x_hit_rect(timeline_rect, trim_end_x, 32);
        const SDL_Rect playhead_hit = x_hit_rect(timeline_rect, playhead_x, 32);
        const bool inside_timeline = point_in_rect_inclusive(event->button.x, event->button.y, timeline_rect);
        const bool on_trim_start = point_in_rect(event->button.x, event->button.y, trim_start_hit);
        const bool on_trim_end = point_in_rect(event->button.x, event->button.y, trim_end_hit);
        const bool on_playhead = point_in_rect(event->button.x, event->button.y, playhead_hit);
        const bool shift_down = (SDL_GetModState() & KMOD_SHIFT) != 0;
        const bool ctrl_down = (SDL_GetModState() & KMOD_CTRL) != 0;

        if (!inside_timeline && !on_trim_start && !on_trim_end && !on_playhead) {
            return false;
        }

        const double click_sec = project_time_for_timeline_x(project, timeline_rect, event->button.x);

        interaction->anchor_sec = click_sec;
        interaction->current_sec = click_sec;
        interaction->trim_anchor_start_sec = transport->trim_start_sec;
        interaction->trim_anchor_end_sec = transport->trim_end_sec;

        if (ctrl_down && inside_timeline) {
            interaction->mode = TIMELINE_DRAG_CUT_RANGE;
        } else if (shift_down && inside_timeline) {
            interaction->mode = TIMELINE_DRAG_TRIM_WINDOW;
        } else if (on_trim_start) {
            interaction->mode = TIMELINE_DRAG_TRIM_START;
        } else if (on_trim_end) {
            interaction->mode = TIMELINE_DRAG_TRIM_END;
        } else {
            interaction->mode = TIMELINE_DRAG_SEEK;
            transport_seek(transport, click_sec);
            transport_apply_to_project(transport, project);
        }
        return true;
    }

    if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {
        if (interaction->mode == TIMELINE_DRAG_CUT_RANGE) {
            const double release_sec = project_time_for_timeline_x(project, timeline_rect, event->button.x);
            interaction->current_sec = release_sec;
            (void)project_add_cut(project, interaction->anchor_sec, release_sec);
        }
        const bool was_active = interaction->mode != TIMELINE_DRAG_NONE;
        timeline_view_reset_interaction(interaction);
        return was_active;
    }

    if (event->type != SDL_MOUSEMOTION || interaction->mode == TIMELINE_DRAG_NONE) {
        return false;
    }

    const double motion_sec = project_time_for_timeline_x(project, timeline_rect, event->motion.x);
    interaction->current_sec = motion_sec;
    const double min_span = duration > 0.25 ? 0.25 : duration;

    if (interaction->mode == TIMELINE_DRAG_SEEK) {
        transport_seek(transport, motion_sec);
    } else if (interaction->mode == TIMELINE_DRAG_TRIM_START) {
        const double new_start = motion_sec < 0.0 ? 0.0 : motion_sec;
        transport_set_trim(transport, new_start, transport->trim_end_sec);
        if ((transport->trim_end_sec - transport->trim_start_sec) < min_span) {
            transport->trim_start_sec = transport->trim_end_sec - min_span;
            if (transport->trim_start_sec < 0.0) {
                transport->trim_start_sec = 0.0;
            }
        }
    } else if (interaction->mode == TIMELINE_DRAG_TRIM_END) {
        const double new_end = motion_sec > duration ? duration : motion_sec;
        transport_set_trim(transport, transport->trim_start_sec, new_end);
        if ((transport->trim_end_sec - transport->trim_start_sec) < min_span) {
            transport->trim_end_sec = transport->trim_start_sec + min_span;
            if (transport->trim_end_sec > duration) {
                transport->trim_end_sec = duration;
            }
        }
    } else if (interaction->mode == TIMELINE_DRAG_TRIM_WINDOW) {
        const double span = interaction->trim_anchor_end_sec - interaction->trim_anchor_start_sec;
        double new_start = interaction->trim_anchor_start_sec + (motion_sec - interaction->anchor_sec);
        double new_end = new_start + span;
        if (new_start < 0.0) {
            new_end -= new_start;
            new_start = 0.0;
        }
        if (new_end > duration) {
            const double overflow = new_end - duration;
            new_start -= overflow;
            new_end = duration;
            if (new_start < 0.0) {
                new_start = 0.0;
            }
        }
        transport_set_trim(transport, new_start, new_end);
    } else if (interaction->mode == TIMELINE_DRAG_CUT_RANGE) {
        /* The cut is committed on mouse-up; during drag only the interaction state matters. */
        return true;
    }

    transport_apply_to_project(transport, project);
    return true;
}
