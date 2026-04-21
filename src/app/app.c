#include "app/app.h"

#include "export/export_worker.h"
#include "export/ffmpeg_export.h"
#include "media/probe.h"
#include "platform/file_dialog.h"
#include "playback/audio_output.h"
#include "playback/audio_source.h"
#include "playback/audio_source_ffmpeg.h"
#include "playback/audio_source_mixed_buffer.h"
#include "playback/audio_stream.h"
#include "playback/audio_preview.h"
#include "model/project.h"
#include "playback/transport.h"
#include "playback/video_preview.h"
#include "playback/video_stream.h"
#include "ui/bitmap_text.h"
#include "ui/audio_tracks_view.h"
#include "ui/preview_view.h"
#include "ui/timeline_view.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <libavutil/log.h>

typedef struct AppState {
    ProjectState project;
    TransportState transport;
    MixedAudioBuffer mixed_audio;
    AudioOutputState audio_output;
    AudioStreamState audio_stream;
    AudioOutputState solo_audio_output;
    AudioStreamState solo_audio_stream;
    VideoStreamState video_stream;
    ExportWorkerState export_worker;
    double mixed_audio_start_sec;
    double mixed_audio_end_sec;
    double audio_clock_start_sec;
    double last_preview_refresh_playhead_sec;
    double last_window_title_update_sec;
    uint64_t last_applied_preview_serial;
    PreviewFrame preview_frame;
    SDL_Texture *preview_texture;
    bool audio_tracks_collapsed;
    bool solo_audio_active;
    bool solo_audio_has_position;
    bool solo_audio_scrubbing;
    int audio_tracks_scroll_y;
    ExportProfile export_profile;
    size_t solo_audio_track_index;
    double solo_audio_start_sec;
    bool export_filename_prompt_active;
    char export_filename_prompt[256];
    size_t export_filename_prompt_len;
    char last_error[256];
} AppState;

static SDL_Rect compute_sidebar_rect(int width, int height);
static int start_audio_playback(AppState *state);
static double solo_audio_preview_sec(const AppState *state);
static void update_window_title(SDL_Window *window, const AppConfig *config, const AppState *state);

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

static void snap_trim_start_to_playhead(AppState *state) {
    if (state == NULL || state->project.duration_sec <= 0.0) {
        return;
    }

    const double min_span = state->project.duration_sec > 0.25 ? 0.25 : state->project.duration_sec;
    const double max_start = state->transport.trim_end_sec - min_span;
    const double new_start = clamp_double(state->transport.playhead_sec, 0.0, max_start);
    transport_set_trim(&state->transport, new_start, state->transport.trim_end_sec);
    transport_apply_to_project(&state->transport, &state->project);
}

static void snap_trim_end_to_playhead(AppState *state) {
    if (state == NULL || state->project.duration_sec <= 0.0) {
        return;
    }

    const double min_span = state->project.duration_sec > 0.25 ? 0.25 : state->project.duration_sec;
    const double min_end = state->transport.trim_start_sec + min_span;
    const double new_end = clamp_double(state->transport.playhead_sec, min_end, state->project.duration_sec);
    transport_set_trim(&state->transport, state->transport.trim_start_sec, new_end);
    transport_apply_to_project(&state->transport, &state->project);
}

static void reset_trim_start(AppState *state) {
    if (state == NULL) {
        return;
    }

    transport_set_trim(&state->transport, 0.0, state->transport.trim_end_sec);
    transport_apply_to_project(&state->transport, &state->project);
}

static void reset_trim_end(AppState *state) {
    if (state == NULL) {
        return;
    }

    transport_set_trim(&state->transport, state->transport.trim_start_sec, state->project.duration_sec);
    transport_apply_to_project(&state->transport, &state->project);
}

static size_t app_enabled_audio_track_count(const ProjectState *project) {
    size_t enabled = 0;
    if (project == NULL || project->audio_tracks == NULL) {
        return 0;
    }

    for (size_t i = 0; i < project->audio_track_count; ++i) {
        if (project->audio_tracks[i].enabled) {
            enabled++;
        }
    }
    return enabled;
}

static SDL_Rect empty_open_button_rect(SDL_Rect preview) {
    const SDL_Rect button = {
        .x = preview.x + 72,
        .y = preview.y + 124,
        .w = 156,
        .h = 34,
    };
    return button;
}

static SDL_Rect sidebar_open_button_rect(SDL_Rect sidebar) {
    const int editor_width = sidebar.w > 680 ? 220 : 170;
    const SDL_Rect editor = {
        .x = sidebar.x + sidebar.w - editor_width - 20,
        .y = sidebar.y + 72,
        .w = editor_width,
        .h = sidebar.h - 92,
    };
    int button_y = editor.y + 146;
    const int max_button_y = editor.y + editor.h - 76;
    if (button_y > max_button_y) {
        button_y = max_button_y;
    }
    const SDL_Rect button = {
        .x = editor.x + 10,
        .y = button_y,
        .w = editor.w - 20,
        .h = 30,
    };
    return button;
}

static SDL_Rect sidebar_profile_button_rect(SDL_Rect sidebar) {
    SDL_Rect button = sidebar_open_button_rect(sidebar);
    button.y -= 36;
    return button;
}

static SDL_Rect sidebar_export_button_rect(SDL_Rect sidebar) {
    SDL_Rect button = sidebar_open_button_rect(sidebar);
    button.y += 36;
    return button;
}

static SDL_Rect controls_audio_rect(SDL_Rect controls) {
    const int editor_width = controls.w > 680 ? 220 : 170;
    const int audio_width = controls.w - editor_width - 60;
    SDL_Rect rect = {
        .x = controls.x,
        .y = controls.y,
        .w = audio_width > 260 ? audio_width : controls.w,
        .h = controls.h,
    };
    return rect;
}

static SDL_Rect controls_editor_rect(SDL_Rect controls) {
    const int editor_width = controls.w > 680 ? 220 : 170;
    SDL_Rect rect = {
        .x = controls.x + controls.w - editor_width - 20,
        .y = controls.y + 72,
        .w = editor_width,
        .h = controls.h - 92,
    };
    if (controls.w - editor_width - 60 <= 260) {
        rect.w = 0;
        rect.h = 0;
    }
    return rect;
}

static void draw_button(SDL_Renderer *renderer, SDL_Rect button, const char *label) {
    SDL_SetRenderDrawColor(renderer, 52, 73, 96, 255);
    SDL_RenderFillRect(renderer, &button);
    SDL_SetRenderDrawColor(renderer, 105, 139, 174, 255);
    SDL_RenderDrawRect(renderer, &button);
    bitmap_text_draw(renderer, button.x + 13, button.y + 11, 1, (SDL_Color){222, 232, 244, 255}, label);
}

static void cycle_export_profile(AppState *state) {
    if (state == NULL) {
        return;
    }

    do {
        state->export_profile = (ExportProfile)(((int)state->export_profile + 1) % EXPORT_PROFILE_COUNT);
#ifdef _WIN32
    } while (state->export_profile == EXPORT_PROFILE_INTEL_VAAPI);
#else
    } while (false);
#endif
}

static void draw_empty_state(SDL_Renderer *renderer, SDL_Rect preview, SDL_Rect sidebar) {
    const SDL_Rect drop_zone = {
        .x = preview.x + 36,
        .y = preview.y + 42,
        .w = preview.w - 72,
        .h = preview.h - 84,
    };

    SDL_SetRenderDrawColor(renderer, 22, 26, 32, 255);
    SDL_RenderFillRect(renderer, &drop_zone);
    SDL_SetRenderDrawColor(renderer, 68, 92, 121, 255);
    SDL_RenderDrawRect(renderer, &drop_zone);

    bitmap_text_draw(renderer, drop_zone.x + 36, drop_zone.y + 28, 1, (SDL_Color){215, 224, 236, 255}, "OPEN A VIDEO");
    bitmap_text_draw(renderer, drop_zone.x + 36, drop_zone.y + 52, 1, (SDL_Color){132, 150, 171, 255}, "DROP A FILE HERE OR CHOOSE ONE FROM DISK");
    draw_button(renderer, empty_open_button_rect(preview), "SELECT MEDIA");
    bitmap_text_draw(renderer, drop_zone.x + 36, drop_zone.y + 134, 1, (SDL_Color){122, 138, 158, 255}, "TIP  YOU CAN ALSO LAUNCH WITH A FILE PATH");

    const SDL_Rect side_card_1 = {
        .x = sidebar.x + 22,
        .y = sidebar.y + 24,
        .w = sidebar.w - 44,
        .h = 92,
    };
    const SDL_Rect side_card_2 = {
        .x = sidebar.x + 22,
        .y = side_card_1.y + side_card_1.h + 18,
        .w = sidebar.w - 44,
        .h = 104,
    };
    const SDL_Rect side_card_3 = {
        .x = sidebar.x + 22,
        .y = side_card_2.y + side_card_2.h + 18,
        .w = sidebar.w - 44,
        .h = 104,
    };

    SDL_SetRenderDrawColor(renderer, 27, 31, 38, 255);
    SDL_RenderFillRect(renderer, &side_card_1);
    SDL_RenderFillRect(renderer, &side_card_2);
    SDL_RenderFillRect(renderer, &side_card_3);
    SDL_SetRenderDrawColor(renderer, 63, 69, 82, 255);
    SDL_RenderDrawRect(renderer, &side_card_1);
    SDL_RenderDrawRect(renderer, &side_card_2);
    SDL_RenderDrawRect(renderer, &side_card_3);

    bitmap_text_draw(renderer, side_card_1.x + 14, side_card_1.y + 14, 1, (SDL_Color){203, 214, 232, 255}, "GET STARTED");
    bitmap_text_draw(renderer, side_card_1.x + 14, side_card_1.y + 40, 1, (SDL_Color){121, 142, 166, 255}, "SELECT MEDIA OR DRAG A FILE");
    bitmap_text_draw(renderer, side_card_1.x + 14, side_card_1.y + 60, 1, (SDL_Color){121, 142, 166, 255}, "NO PROJECT FILE NEEDED");

    bitmap_text_draw(renderer, side_card_2.x + 14, side_card_2.y + 14, 1, (SDL_Color){203, 214, 232, 255}, "PLAYBACK");
    bitmap_text_draw(renderer, side_card_2.x + 14, side_card_2.y + 40, 1, (SDL_Color){121, 142, 166, 255}, "SPACE PLAY PAUSE");
    bitmap_text_draw(renderer, side_card_2.x + 14, side_card_2.y + 60, 1, (SDL_Color){121, 142, 166, 255}, "LEFT RIGHT SEEK");
    bitmap_text_draw(renderer, side_card_2.x + 14, side_card_2.y + 80, 1, (SDL_Color){121, 142, 166, 255}, "S STOP");

    bitmap_text_draw(renderer, side_card_3.x + 14, side_card_3.y + 14, 1, (SDL_Color){203, 214, 232, 255}, "EDIT");
    bitmap_text_draw(renderer, side_card_3.x + 14, side_card_3.y + 40, 1, (SDL_Color){121, 142, 166, 255}, "DRAG CROP HANDLES");
    bitmap_text_draw(renderer, side_card_3.x + 14, side_card_3.y + 60, 1, (SDL_Color){121, 142, 166, 255}, "E EXPORT");
    bitmap_text_draw(renderer, side_card_3.x + 14, side_card_3.y + 80, 1, (SDL_Color){121, 142, 166, 255}, "M AUDIO MODE");
}

static void draw_loaded_media_status(SDL_Renderer *renderer, SDL_Rect sidebar, const AppState *state) {
    const SDL_Rect editor = controls_editor_rect(sidebar);
    if (editor.w <= 0 || editor.h <= 0) {
        return;
    }
    const SDL_Rect card = {
        .x = editor.x,
        .y = editor.y,
        .w = editor.w,
        .h = editor.h,
    };

    SDL_SetRenderDrawColor(renderer, 27, 31, 38, 255);
    SDL_RenderFillRect(renderer, &card);
    SDL_SetRenderDrawColor(renderer, 63, 69, 82, 255);
    SDL_RenderDrawRect(renderer, &card);

    char line[64] = {0};
    bitmap_text_draw(renderer, card.x + 14, card.y + 14, 1, (SDL_Color){203, 214, 232, 255}, "EDITOR");

    SDL_snprintf(line, sizeof(line), "TIME %.2f", state->transport.playhead_sec);
    bitmap_text_draw(renderer, card.x + 14, card.y + 40, 1, (SDL_Color){121, 142, 166, 255}, line);

    SDL_snprintf(line, sizeof(line), "TRIM %.2f %.2f", state->transport.trim_start_sec, state->transport.trim_end_sec);
    bitmap_text_draw(renderer, card.x + 14, card.y + 58, 1, (SDL_Color){121, 142, 166, 255}, line);

    SDL_snprintf(line, sizeof(line), "CROP %dX%d", state->project.crop.width, state->project.crop.height);
    bitmap_text_draw(renderer, card.x + 14, card.y + 76, 1, (SDL_Color){121, 142, 166, 255}, line);

    SDL_snprintf(line, sizeof(line), "AUDIO %s", state->project.collapse_audio ? "COLLAPSE" : "PRESERVE");
    bitmap_text_draw(renderer, card.x + 14, card.y + 94, 1, (SDL_Color){121, 142, 166, 255}, line);

    const SDL_Rect open_button = sidebar_open_button_rect(sidebar);
    SDL_snprintf(line, sizeof(line), "PROFILE %s", export_profile_label(state->export_profile));
    draw_button(renderer, sidebar_profile_button_rect(sidebar), line);
    draw_button(renderer, open_button, "OPEN MEDIA");
    draw_button(renderer, sidebar_export_button_rect(sidebar), "EXPORT");

    if (export_worker_is_running((ExportWorkerState *)&state->export_worker)) {
        const SDL_Rect progress = {
            .x = open_button.x,
            .y = open_button.y + 72,
            .w = open_button.w,
            .h = 12,
        };
        const int phase = (SDL_GetTicks() / 10u) % (unsigned int)(progress.w > 1 ? progress.w : 1);
        SDL_Rect fill = {
            .x = progress.x,
            .y = progress.y,
            .w = phase,
            .h = progress.h,
        };
        SDL_SetRenderDrawColor(renderer, 20, 24, 30, 255);
        SDL_RenderFillRect(renderer, &progress);
        SDL_SetRenderDrawColor(renderer, 243, 211, 85, 255);
        SDL_RenderFillRect(renderer, &fill);
        SDL_SetRenderDrawColor(renderer, 88, 101, 119, 255);
        SDL_RenderDrawRect(renderer, &progress);
        bitmap_text_draw(renderer, progress.x, progress.y + 18, 1, (SDL_Color){245, 206, 102, 255}, "EXPORTING");
    }
}

static void draw_keybind_hints(SDL_Renderer *renderer, SDL_Rect preview, SDL_Rect controls) {
    const int y = preview.y + preview.h + ((controls.y - (preview.y + preview.h)) / 2) - 4;
    const char *wide_hints[] = {
        "SPACE PLAY/PAUSE",
        "LEFT/RIGHT SEEK",
        "E EXPORT",
        "M AUDIO MODE",
        "J/K SNAP",
        "CTRL J/K RESET",
        "CTRL DRAG CUT",
        "CTRL Z UNDO",
        "P PROFILE",
    };
    const char *compact_hints[] = {
        "SPACE PLAY",
        "LEFT/RIGHT",
        "E EXPORT",
        "M AUDIO",
        "J/K SNAP",
        "CTRL J/K",
        "CTRL DRAG",
        "CTRL Z",
        "P PROFILE",
    };
    const char **hints = preview.w < 1120 ? compact_hints : wide_hints;
    const int count = 9;
    const int glyph_step = 6;
    int total_text_width = 0;
    for (int i = 0; i < count; ++i) {
        total_text_width += (int)strlen(hints[i]) * glyph_step;
    }
    int gap = (preview.w - total_text_width - 20) / (count - 1);
    if (gap > 36) {
        gap = 36;
    }
    if (gap < 8) {
        gap = 8;
    }

    int x = preview.x + 10;
    for (int i = 0; i < count && x < preview.x + preview.w - 20; ++i) {
        bitmap_text_draw(renderer, x, y, 1, (SDL_Color){245, 206, 102, 255}, hints[i]);
        x += ((int)strlen(hints[i]) * glyph_step) + gap;
    }
}

static void draw_export_filename_prompt(SDL_Renderer *renderer, int width, int height, const AppState *state) {
    if (state == NULL || !state->export_filename_prompt_active) {
        return;
    }

    SDL_Rect overlay = {
        .x = 0,
        .y = 0,
        .w = width,
        .h = height,
    };
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 132);
    SDL_RenderFillRect(renderer, &overlay);

    SDL_Rect box = {
        .x = (width - 560) / 2,
        .y = (height - 122) / 2,
        .w = 560,
        .h = 122,
    };
    SDL_SetRenderDrawColor(renderer, 24, 28, 34, 245);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 118, 148, 184, 255);
    SDL_RenderDrawRect(renderer, &box);

    bitmap_text_draw(renderer, box.x + 18, box.y + 16, 1, (SDL_Color){221, 232, 245, 255}, "EXPORT FILENAME");
    bitmap_text_draw(renderer, box.x + 18, box.y + 36, 1, (SDL_Color){123, 142, 164, 255}, "ENTER TO USE DEFAULT");
    bitmap_text_draw(renderer, box.x + 18, box.y + 54, 1, (SDL_Color){123, 142, 164, 255}, "ESC TO CANCEL");

    SDL_Rect input = {
        .x = box.x + 16,
        .y = box.y + 74,
        .w = box.w - 32,
        .h = 28,
    };
    SDL_SetRenderDrawColor(renderer, 15, 18, 23, 255);
    SDL_RenderFillRect(renderer, &input);
    SDL_SetRenderDrawColor(renderer, 84, 95, 112, 255);
    SDL_RenderDrawRect(renderer, &input);

    char display[280] = {0};
    if (state->export_filename_prompt[0] != '\0') {
        snprintf(display, sizeof(display), "%s", state->export_filename_prompt);
    } else {
        snprintf(display, sizeof(display), "%s", "");
    }
    const size_t display_len = strlen(display);
    if (display_len > 0) {
        bitmap_text_draw(renderer, input.x + 10, input.y + 9, 1, (SDL_Color){228, 238, 250, 255}, display);
    } else {
        bitmap_text_draw(renderer, input.x + 10, input.y + 9, 1, (SDL_Color){95, 110, 128, 255}, "TYPE A NAME OR LEAVE BLANK");
    }

    bitmap_text_draw(renderer, box.x + 18, box.y + 106, 1, (SDL_Color){245, 206, 102, 255}, "ENTER EXPORT  ESC CANCEL");
}

static SDL_Rect compute_preview_rect(
    const ProjectState *project,
    int window_width,
    int window_height
) {
    const SDL_Rect preview_panel = {
        .x = 28,
        .y = 18,
        .w = window_width - 56,
        .h = (window_height * 11) / 20,
    };

    if (!project_has_media(project) || project->width <= 0 || project->height <= 0) {
        return preview_panel;
    }

    const double source_aspect = (double)project->width / (double)project->height;
    const double panel_aspect = (double)preview_panel.w / (double)preview_panel.h;
    SDL_Rect fitted = preview_panel;

    if (source_aspect > panel_aspect) {
        fitted.h = (int)((double)fitted.w / source_aspect);
        fitted.y = preview_panel.y + ((preview_panel.h - fitted.h) / 2);
    } else {
        fitted.w = (int)((double)fitted.h * source_aspect);
        fitted.x = preview_panel.x + ((preview_panel.w - fitted.w) / 2);
    }

    return fitted;
}

static SDL_Rect compute_sidebar_rect(int width, int height) {
    const SDL_Rect preview_panel = {
        .x = 28,
        .y = 18,
        .w = width - 56,
        .h = (height * 11) / 20,
    };

    const SDL_Rect sidebar = {
        .x = 28,
        .y = preview_panel.y + preview_panel.h + 18,
        .w = width - 56,
        .h = height - (preview_panel.y + preview_panel.h + 46),
    };
    return sidebar;
}

static void destroy_preview_texture(AppState *state, SDL_Renderer *renderer) {
    (void)renderer;
    if (state->preview_texture != NULL) {
        SDL_DestroyTexture(state->preview_texture);
        state->preview_texture = NULL;
    }
}

static void clear_preview(AppState *state, SDL_Renderer *renderer) {
    destroy_preview_texture(state, renderer);
    preview_frame_reset(&state->preview_frame);
}

static void clear_mixed_audio(AppState *state) {
    mixed_audio_buffer_reset(&state->mixed_audio);
    state->mixed_audio_start_sec = 0.0;
    state->mixed_audio_end_sec = 0.0;
}

static void request_preview_frame(AppState *state) {
    if (!project_has_media(&state->project) || state->project.source_path == NULL) {
        return;
    }

    video_stream_request_frame(
        &state->video_stream,
        state->project.source_path,
        state->transport.playhead_sec
    );
    state->last_preview_refresh_playhead_sec = state->transport.playhead_sec;
}

static int ensure_mixed_audio(AppState *state) {
    AudioPreviewError error = {0};

    if (state->mixed_audio.samples != NULL &&
        state->mixed_audio_start_sec == state->project.trim_start_sec &&
        state->mixed_audio_end_sec == state->project.trim_end_sec) {
        return 0;
    }

    clear_mixed_audio(state);
    if (render_preview_audio_buffer(
            &state->project,
            state->project.trim_start_sec,
            state->project.trim_end_sec,
            &state->mixed_audio,
            &error
        ) != 0) {
        snprintf(state->last_error, sizeof(state->last_error), "%s", error.message);
        return -1;
    }

    state->mixed_audio_start_sec = state->project.trim_start_sec;
    state->mixed_audio_end_sec = state->project.trim_end_sec;
    return 0;
}

static void invalidate_trim_dependent_state(AppState *state) {
    audio_stream_stop(&state->audio_stream);
    audio_stream_stop(&state->solo_audio_stream);
    state->solo_audio_active = false;
    state->solo_audio_has_position = false;
    state->solo_audio_scrubbing = false;
    state->solo_audio_start_sec = 0.0;
    state->audio_clock_start_sec = 0.0;
    clear_mixed_audio(state);
}

static void restart_audio_playback_if_needed(AppState *state, SDL_Window *window, const AppConfig *config) {
    if (state->transport.playback_state != PLAYBACK_PLAYING) {
        return;
    }

    if (start_audio_playback(state) != 0) {
        transport_pause(&state->transport);
        transport_apply_to_project(&state->transport, &state->project);
        update_window_title(window, config, state);
    }
}

static void refresh_after_trim_shortcut(
    AppState *state,
    SDL_Window *window,
    const AppConfig *config
) {
    invalidate_trim_dependent_state(state);
    request_preview_frame(state);
    restart_audio_playback_if_needed(state, window, config);
    update_window_title(window, config, state);
}

static void handle_timeline_dependent_update(
    AppState *state,
    SDL_Window *window,
    const AppConfig *config,
    const SDL_Event *event,
    TimelineDragMode previous_timeline_mode
) {
    const bool was_timeline_dragging = previous_timeline_mode != TIMELINE_DRAG_NONE;
    const bool is_drag_motion = event != NULL &&
        event->type == SDL_MOUSEMOTION &&
        was_timeline_dragging;
    const bool is_drag_release = event != NULL &&
        event->type == SDL_MOUSEBUTTONUP &&
        event->button.button == SDL_BUTTON_LEFT &&
        was_timeline_dragging;
    const bool updates_playhead =
        previous_timeline_mode == TIMELINE_DRAG_SEEK ||
        (!was_timeline_dragging && event != NULL && event->type == SDL_MOUSEBUTTONDOWN);
    const bool changes_trim_or_cuts =
        previous_timeline_mode == TIMELINE_DRAG_TRIM_START ||
        previous_timeline_mode == TIMELINE_DRAG_TRIM_END ||
        previous_timeline_mode == TIMELINE_DRAG_TRIM_WINDOW ||
        previous_timeline_mode == TIMELINE_DRAG_CUT_RANGE;

    if (updates_playhead && state->project.source_path != NULL) {
        request_preview_frame(state);
    }

    if (is_drag_motion) {
        update_window_title(window, config, state);
        return;
    }

    if (!changes_trim_or_cuts && (is_drag_release || (event != NULL && event->type != SDL_MOUSEMOTION))) {
        invalidate_trim_dependent_state(state);
        restart_audio_playback_if_needed(state, window, config);
    } else if (changes_trim_or_cuts && !is_drag_motion) {
        clear_mixed_audio(state);
    }

    update_window_title(window, config, state);
}

static int build_default_export_path(const ProjectState *project, char *buffer, size_t buffer_size) {
    if (project == NULL || project->source_path == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }

    const int written = snprintf(buffer, buffer_size, "%s.clipcut.mp4", project->source_path);
    return written >= 0 && written < (int)buffer_size ? 0 : -1;
}

static int increment_path_if_exists(char *path, size_t path_size);

static void trim_ascii_whitespace_inplace(char *value) {
    if (value == NULL) {
        return;
    }

    char *start = value;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    if (start != value) {
        memmove(value, start, strlen(start) + 1u);
    }

    size_t len = strlen(value);
    while (len > 0 && (value[len - 1u] == ' ' || value[len - 1u] == '\t' || value[len - 1u] == '\r' || value[len - 1u] == '\n')) {
        value[len - 1u] = '\0';
        len--;
    }
}

static const char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        slash = backslash;
    }
#endif
    return slash != NULL ? slash + 1 : path;
}

static bool path_has_separator(const char *path) {
    return path != NULL && (strchr(path, '/') != NULL
#ifdef _WIN32
        || strchr(path, '\\') != NULL || strchr(path, ':') != NULL
#endif
    );
}

static bool basename_has_extension(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    const char *base = path_basename(path);
    const char *dot = strrchr(base, '.');
    return dot != NULL && dot != base && dot[1] != '\0';
}

static int build_export_output_path_from_prompt(
    const ProjectState *project,
    const char *prompt_text,
    char *buffer,
    size_t buffer_size
) {
    if (project == NULL || project->source_path == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }

    char trimmed[1024] = {0};
    if (prompt_text != NULL) {
        snprintf(trimmed, sizeof(trimmed), "%s", prompt_text);
    }
    trim_ascii_whitespace_inplace(trimmed);

    if (trimmed[0] == '\0') {
        if (build_default_export_path(project, buffer, buffer_size) != 0) {
            return -1;
        }
        return increment_path_if_exists(buffer, buffer_size);
    }

    if (path_has_separator(trimmed)) {
        if (snprintf(buffer, buffer_size, "%s", trimmed) >= (int)buffer_size) {
            return -1;
        }
    } else {
        const char *source_sep = strrchr(project->source_path, '/');
#ifdef _WIN32
        const char *source_backslash = strrchr(project->source_path, '\\');
        if (source_backslash != NULL && (source_sep == NULL || source_backslash > source_sep)) {
            source_sep = source_backslash;
        }
#endif
        if (source_sep != NULL) {
            const size_t dir_len = (size_t)(source_sep - project->source_path + 1);
            if (dir_len >= buffer_size) {
                return -1;
            }
            memcpy(buffer, project->source_path, dir_len);
            buffer[dir_len] = '\0';
            if (snprintf(buffer + dir_len, buffer_size - dir_len, "%s", trimmed) >= (int)(buffer_size - dir_len)) {
                return -1;
            }
        } else {
            if (snprintf(buffer, buffer_size, "%s", trimmed) >= (int)buffer_size) {
                return -1;
            }
        }
    }

    if (!basename_has_extension(buffer)) {
        const size_t len = strlen(buffer);
        if (len + 4u >= buffer_size) {
            return -1;
        }
        memcpy(buffer + len, ".mp4", 5u);
    }

    return increment_path_if_exists(buffer, buffer_size);
}

static bool path_exists(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    SDL_RWops *file = SDL_RWFromFile(path, "rb");
    if (file == NULL) {
        return false;
    }
    SDL_RWclose(file);
    return true;
}

static int increment_path_if_exists(char *path, size_t path_size) {
    if (path == NULL || path_size == 0 || !path_exists(path)) {
        return 0;
    }

    char original[1024] = {0};
    snprintf(original, sizeof(original), "%s", path);

    const char *slash = strrchr(original, '/');
#ifdef _WIN32
    const char *backslash = strrchr(original, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        slash = backslash;
    }
#endif
    const char *basename = slash != NULL ? slash + 1 : original;
    const char *dot = strrchr(basename, '.');
    const size_t stem_len = dot != NULL ? (size_t)(dot - original) : strlen(original);
    const char *ext = dot != NULL ? dot : "";

    for (int suffix = 1; suffix < 10000; ++suffix) {
        const int written = snprintf(path, path_size, "%.*s.%d%s", (int)stem_len, original, suffix, ext);
        if (written < 0 || written >= (int)path_size) {
            return -1;
        }
        if (!path_exists(path)) {
            return 0;
        }
    }

    return -1;
}

static int build_default_mp3_path(
    const ProjectState *project,
    size_t audio_track_index,
    char *buffer,
    size_t buffer_size
) {
    if (project == NULL || project->source_path == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }

    const int written = snprintf(
        buffer,
        buffer_size,
        "%s.track%zu.mp3",
        project->source_path,
        audio_track_index + 1u
    );
    return written >= 0 && written < (int)buffer_size ? 0 : -1;
}

static int run_export_with_output_path(AppState *state, const char *output_path) {
    if (state == NULL || output_path == NULL || output_path[0] == '\0') {
        return -1;
    }

    if (state->transport.playback_state == PLAYBACK_PLAYING) {
        transport_pause(&state->transport);
        audio_stream_stop(&state->audio_stream);
        transport_apply_to_project(&state->transport, &state->project);
    }

    char display[4096] = {0};
    ExportCommand command = {0};

    if (export_build_ffmpeg_command(
            &state->project,
            output_path,
            state->project.collapse_audio,
            state->export_profile,
            &command,
            state->last_error,
            (int)sizeof(state->last_error)
        ) != 0) {
        return -1;
    }

    if (export_command_join_for_display(&command, display, (int)sizeof(display)) == 0) {
        fprintf(stderr, "Running export: %s\n", display);
    }

    if (export_worker_start(
            &state->export_worker,
            &command,
            EXPORT_WORKER_KIND_EXPORT,
            output_path,
            state->last_error,
            (int)sizeof(state->last_error)
        ) != 0) {
        export_command_reset(&command);
        return -1;
    }

    fprintf(stderr, "Export started: %s\n", output_path);
    export_command_reset(&command);
    state->last_error[0] = '\0';
    return 0;
}

static int run_mp3_extract(AppState *state, size_t audio_track_index) {
    char output_path[1024] = {0};
    char display[4096] = {0};
    ExportCommand command = {0};

    if (build_default_mp3_path(&state->project, audio_track_index, output_path, sizeof(output_path)) != 0) {
        snprintf(state->last_error, sizeof(state->last_error), "%s", "Failed to build default MP3 output path");
        return -1;
    }
    if (increment_path_if_exists(output_path, sizeof(output_path)) != 0) {
        snprintf(state->last_error, sizeof(state->last_error), "%s", "Failed to build unique MP3 output path");
        return -1;
    }

    if (export_build_extract_mp3_command(
            &state->project,
            audio_track_index,
            output_path,
            &command,
            state->last_error,
            (int)sizeof(state->last_error)
        ) != 0) {
        return -1;
    }

    if (export_command_join_for_display(&command, display, (int)sizeof(display)) == 0) {
        fprintf(stderr, "Running MP3 extract: %s\n", display);
    }

    if (export_worker_start(
            &state->export_worker,
            &command,
            EXPORT_WORKER_KIND_MP3,
            output_path,
            state->last_error,
            (int)sizeof(state->last_error)
        ) != 0) {
        export_command_reset(&command);
        return -1;
    }

    fprintf(stderr, "MP3 extract started: %s\n", output_path);
    export_command_reset(&command);
    state->last_error[0] = '\0';
    return 0;
}

static void start_export_filename_prompt(AppState *state, SDL_Window *window) {
    if (state == NULL) {
        return;
    }

    state->export_filename_prompt_active = true;
    state->export_filename_prompt[0] = '\0';
    state->export_filename_prompt_len = 0;
    SDL_StartTextInput();
    SDL_Rect prompt_rect = {
        .x = 0,
        .y = 0,
        .w = 1,
        .h = 1,
    };
    SDL_SetTextInputRect(&prompt_rect);
    (void)window;
}

static void stop_export_filename_prompt(AppState *state) {
    if (state == NULL) {
        return;
    }

    state->export_filename_prompt_active = false;
    state->export_filename_prompt[0] = '\0';
    state->export_filename_prompt_len = 0;
    SDL_StopTextInput();
}

static void append_export_filename_prompt_text(AppState *state, const char *text) {
    if (state == NULL || text == NULL || text[0] == '\0') {
        return;
    }

    const size_t current_len = strlen(state->export_filename_prompt);
    const size_t add_len = strlen(text);
    if (current_len + add_len >= sizeof(state->export_filename_prompt)) {
        return;
    }
    memcpy(state->export_filename_prompt + current_len, text, add_len + 1u);
    state->export_filename_prompt_len = current_len + add_len;
}

static void backspace_export_filename_prompt(AppState *state) {
    if (state == NULL) {
        return;
    }

    size_t len = strlen(state->export_filename_prompt);
    if (len == 0) {
        state->export_filename_prompt_len = 0;
        return;
    }

    while (len > 0) {
        len--;
        unsigned char byte = (unsigned char)state->export_filename_prompt[len];
        if ((byte & 0xC0u) != 0x80u) {
            state->export_filename_prompt[len] = '\0';
            state->export_filename_prompt_len = len;
            return;
        }
    }

    state->export_filename_prompt[0] = '\0';
    state->export_filename_prompt_len = 0;
}

static int submit_export_filename_prompt(AppState *state) {
    if (state == NULL) {
        return -1;
    }

    char output_path[1024] = {0};
    if (build_export_output_path_from_prompt(
            &state->project,
            state->export_filename_prompt,
            output_path,
            sizeof(output_path)
        ) != 0) {
        snprintf(state->last_error, sizeof(state->last_error), "%s", "Failed to build export filename");
        return -1;
    }
    if (run_export_with_output_path(state, output_path) != 0) {
        return -1;
    }

    stop_export_filename_prompt(state);
    return 0;
}

static int start_solo_audio_preview(AppState *state, size_t audio_track_index, double start_sec) {
    if (audio_track_index >= state->project.audio_track_count) {
        snprintf(state->last_error, sizeof(state->last_error), "%s", "Invalid solo audio track index");
        return -1;
    }
    if (start_sec < state->project.trim_start_sec) {
        start_sec = state->project.trim_start_sec;
    }
    if (start_sec > state->project.trim_end_sec) {
        start_sec = state->project.trim_end_sec;
    }

    audio_stream_stop(&state->audio_stream);
    audio_stream_stop(&state->solo_audio_stream);
    if (audio_output_open(&state->solo_audio_output, state->last_error, (int)sizeof(state->last_error)) != 0) {
        return -1;
    }

    AudioTrack *solo_tracks = calloc(state->project.audio_track_count, sizeof(*solo_tracks));
    if (solo_tracks == NULL) {
        snprintf(state->last_error, sizeof(state->last_error), "%s", "Failed to allocate solo audio track list");
        return -1;
    }
    memcpy(
        solo_tracks,
        state->project.audio_tracks,
        state->project.audio_track_count * sizeof(*solo_tracks)
    );
    for (size_t i = 0; i < state->project.audio_track_count; ++i) {
        solo_tracks[i].enabled = i == audio_track_index;
    }

    ProjectState solo_project = state->project;
    solo_project.audio_tracks = solo_tracks;
    AudioSource source = {0};
    const int source_rc = audio_source_from_ffmpeg_stream(
        &solo_project,
        start_sec,
        state->project.trim_end_sec,
        &source,
        state->last_error,
        (int)sizeof(state->last_error)
    );
    free(solo_tracks);
    if (source_rc != 0) {
        return -1;
    }

    if (audio_stream_start_mixed_buffer(
            &state->solo_audio_stream,
            &state->solo_audio_output,
            &source,
            state->last_error,
            (int)sizeof(state->last_error)
        ) != 0) {
        audio_source_destroy(&source);
        return -1;
    }

    state->solo_audio_active = true;
    state->solo_audio_has_position = true;
    state->solo_audio_track_index = audio_track_index;
    state->solo_audio_start_sec = start_sec;
    fprintf(stderr, "Solo audio preview: track %zu at %.2fs\n", audio_track_index + 1u, start_sec);
    return 0;
}

static void pause_solo_audio_preview(AppState *state) {
    if (state == NULL || !state->solo_audio_active) {
        return;
    }

    state->solo_audio_start_sec = solo_audio_preview_sec(state);
    state->solo_audio_has_position = true;
    audio_stream_stop(&state->solo_audio_stream);
    state->solo_audio_active = false;
}

static double solo_audio_preview_sec(const AppState *state) {
    if (state == NULL || !state->solo_audio_has_position) {
        return 0.0;
    }
    if (!state->solo_audio_active) {
        return state->solo_audio_start_sec;
    }
    double sec = state->solo_audio_start_sec + audio_output_played_seconds(&state->solo_audio_output);
    if (sec > state->project.trim_end_sec) {
        sec = state->project.trim_end_sec;
    }
    return sec;
}

static void sync_audio_on_transport_change(AppState *state) {
    if (state->transport.playback_state != PLAYBACK_PLAYING) {
        audio_stream_stop(&state->audio_stream);
    }
}

static bool skip_cut_at_playhead(AppState *state) {
    if (state == NULL) {
        return false;
    }

    const double before = state->transport.playhead_sec;
    const double after = project_skip_cut_forward(&state->project, before);
    if (after == before) {
        return false;
    }

    transport_seek(&state->transport, after);
    transport_apply_to_project(&state->transport, &state->project);
    return true;
}

static int start_audio_playback(AppState *state) {
    if (app_enabled_audio_track_count(&state->project) == 0) {
        audio_stream_stop(&state->audio_stream);
        audio_output_stop(&state->audio_output);
        state->audio_clock_start_sec = state->transport.playhead_sec;
        return 0;
    }

    if (audio_output_open(&state->audio_output, state->last_error, (int)sizeof(state->last_error)) != 0) {
        return -1;
    }

    double offset_sec = state->transport.playhead_sec - state->project.trim_start_sec;
    if (offset_sec < 0.0) {
        offset_sec = 0.0;
    }
    AudioSource source = {0};
    if (audio_source_from_ffmpeg_stream(
            &state->project,
            state->transport.playhead_sec,
            state->project.trim_end_sec,
            &source,
            state->last_error,
            (int)sizeof(state->last_error)
        ) != 0) {
        if (ensure_mixed_audio(state) != 0) {
            return -1;
        }

        const size_t start_frame = (size_t)(offset_sec * (double)state->mixed_audio.sample_rate);
        if (audio_source_from_mixed_buffer(&state->mixed_audio, start_frame, &source) != 0) {
            snprintf(state->last_error, sizeof(state->last_error), "%s", "Failed to create audio source");
            return -1;
        }
    }

    if (audio_stream_start_mixed_buffer(
            &state->audio_stream,
            &state->audio_output,
            &source,
            state->last_error,
            (int)sizeof(state->last_error)
        ) != 0) {
        audio_source_destroy(&source);
        return -1;
    }

    state->audio_clock_start_sec = state->transport.playhead_sec;
    return 0;
}

static void sync_transport_to_audio_clock(AppState *state) {
    if (state == NULL || state->transport.playback_state != PLAYBACK_PLAYING) {
        return;
    }

    double playhead_sec = state->audio_clock_start_sec + audio_output_played_seconds(&state->audio_output);
    if (playhead_sec >= state->transport.trim_end_sec) {
        playhead_sec = state->transport.trim_end_sec;
        transport_pause(&state->transport);
        audio_stream_stop(&state->audio_stream);
    }

    transport_seek(&state->transport, playhead_sec);
}

static int rebuild_preview_texture(SDL_Renderer *renderer, AppState *state) {
    destroy_preview_texture(state, renderer);

    if (state->preview_frame.pixels == NULL) {
        return 0;
    }

    state->preview_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC,
        state->preview_frame.width,
        state->preview_frame.height
    );
    if (state->preview_texture == NULL) {
        snprintf(state->last_error, sizeof(state->last_error), "SDL_CreateTexture failed: %s", SDL_GetError());
        return -1;
    }

    if (SDL_UpdateTexture(
            state->preview_texture,
            NULL,
            state->preview_frame.pixels,
            state->preview_frame.pitch
        ) != 0) {
        snprintf(state->last_error, sizeof(state->last_error), "SDL_UpdateTexture failed: %s", SDL_GetError());
        destroy_preview_texture(state, renderer);
        return -1;
    }

    SDL_SetTextureBlendMode(state->preview_texture, SDL_BLENDMODE_BLEND);
    return 0;
}

static int refresh_preview_if_due(SDL_Renderer *renderer, AppState *state) {
    (void)renderer;
    if (!project_has_media(&state->project) || state->transport.playback_state != PLAYBACK_PLAYING) {
        return 0;
    }

    const double frame_interval_sec = 1.0 / 24.0;
    const double delta = state->transport.playhead_sec - state->last_preview_refresh_playhead_sec;
    if (delta < frame_interval_sec) {
        return 0;
    }

    video_stream_request_frame(
        &state->video_stream,
        state->project.source_path,
        state->transport.playhead_sec
    );
    state->last_preview_refresh_playhead_sec = state->transport.playhead_sec;
    return 0;
}

static int apply_latest_video_frame(SDL_Renderer *renderer, AppState *state) {
    PreviewFrame next_frame = {0};
    uint64_t serial = 0;
    if (!video_stream_take_frame(&state->video_stream, &next_frame, &serial)) {
        return 0;
    }
    if (serial == state->last_applied_preview_serial) {
        preview_frame_reset(&next_frame);
        return 0;
    }

    preview_frame_reset(&state->preview_frame);
    state->preview_frame = next_frame;
    state->last_applied_preview_serial = serial;
    return rebuild_preview_texture(renderer, state);
}

static void draw_placeholder(
    SDL_Renderer *renderer,
    const AppState *state,
    const TimelineInteractionState *timeline_interaction,
    int width,
    int height
) {
    const SDL_Rect preview = compute_preview_rect(&state->project, width, height);
    const SDL_Rect sidebar = compute_sidebar_rect(width, height);
    const SDL_Rect audio_controls = controls_audio_rect(sidebar);
    const int max_audio_scroll = audio_tracks_view_max_scroll(
        audio_controls,
        &state->project,
        state->audio_tracks_collapsed
    );
    int audio_scroll_y = state->audio_tracks_scroll_y;
    if (audio_scroll_y > max_audio_scroll) {
        audio_scroll_y = max_audio_scroll;
    }

    SDL_SetRenderDrawColor(renderer, 18, 18, 22, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 28, 30, 36, 255);
    SDL_RenderFillRect(renderer, &preview);

    SDL_SetRenderDrawColor(renderer, 84, 135, 187, 255);
    SDL_RenderDrawRect(renderer, &preview);

    SDL_SetRenderDrawColor(renderer, 24, 26, 31, 255);
    SDL_RenderFillRect(renderer, &sidebar);

    SDL_SetRenderDrawColor(renderer, 74, 78, 90, 255);
    SDL_RenderDrawRect(renderer, &sidebar);

    if (project_has_media(&state->project)) {
        if (state->preview_texture != NULL) {
            SDL_RenderCopy(renderer, state->preview_texture, NULL, &preview);
        }

        preview_view_draw_overlay(renderer, preview, &state->project);
        draw_keybind_hints(renderer, preview, sidebar);

        const SDL_Rect timeline = timeline_view_rect(width, height);
        timeline_view_draw(renderer, timeline, &state->project, &state->transport, timeline_interaction);

        audio_tracks_view_draw(
            renderer,
            audio_controls,
            &state->project,
            state->audio_tracks_collapsed,
            audio_scroll_y,
            state->solo_audio_active || state->solo_audio_has_position,
            state->solo_audio_active,
            state->solo_audio_track_index,
            solo_audio_preview_sec(state)
        );
        draw_loaded_media_status(renderer, sidebar, state);
    } else {
        draw_empty_state(renderer, preview, sidebar);
    }

    draw_export_filename_prompt(renderer, width, height, state);

    SDL_RenderPresent(renderer);
}

static void update_window_title(SDL_Window *window, const AppConfig *config, const AppState *state) {
    char title[512] = {0};
    if (project_has_media(&state->project)) {
        const char *playback_label = "stopped";
        if (state->transport.playback_state == PLAYBACK_PLAYING) {
            playback_label = "playing";
        } else if (state->transport.playback_state == PLAYBACK_PAUSED) {
            playback_label = "paused";
        }
        snprintf(
            title,
            sizeof(title),
            "%s | %s | %dx%d | %.2fs | %zu audio | %s | %.2fs",
            config->window_title,
            state->project.source_path,
            state->project.width,
            state->project.height,
            state->project.duration_sec,
            state->project.audio_track_count,
            playback_label,
            state->transport.playhead_sec
        );
    } else if (state->last_error[0] != '\0') {
        snprintf(title, sizeof(title), "%s | %s", config->window_title, state->last_error);
    } else {
        snprintf(title, sizeof(title), "%s", config->window_title);
    }

    SDL_SetWindowTitle(window, title);
}

static void update_window_title_if_due(
    SDL_Window *window,
    const AppConfig *config,
    AppState *state,
    double now_sec
) {
    if ((now_sec - state->last_window_title_update_sec) < 0.25) {
        return;
    }

    update_window_title(window, config, state);
    state->last_window_title_update_sec = now_sec;
}

static int load_media(
    SDL_Window *window,
    SDL_Renderer *renderer,
    const AppConfig *config,
    AppState *state,
    const char *path
) {
    ProbeError error = {0};
    PreviewError preview_error = {0};

    clear_preview(state, renderer);
    invalidate_trim_dependent_state(state);
    state->audio_tracks_scroll_y = 0;

    if (probe_media_file(path, &state->project, &error) != 0) {
        snprintf(state->last_error, sizeof(state->last_error), "%s", error.message);
        fprintf(stderr, "Failed to load '%s': %s\n", path, error.message);
        update_window_title(window, config, state);
        return -1;
    }

    if (decode_preview_frame(path, &state->preview_frame, &preview_error) != 0) {
        snprintf(state->last_error, sizeof(state->last_error), "%s", preview_error.message);
        fprintf(stderr, "Failed to decode preview for '%s': %s\n", path, preview_error.message);
        update_window_title(window, config, state);
        return -1;
    }

    if (rebuild_preview_texture(renderer, state) != 0) {
        fprintf(stderr, "Failed to build preview texture for '%s': %s\n", path, state->last_error);
        update_window_title(window, config, state);
        return -1;
    }

    state->last_error[0] = '\0';
    transport_sync_from_project(&state->transport, &state->project);
    state->last_preview_refresh_playhead_sec = state->project.playhead_sec;
    state->last_applied_preview_serial = 0;
    fprintf(
        stderr,
        "Loaded media: %s | video=%s %dx%d | duration=%.2fs | audio_tracks=%zu\n",
        state->project.source_path,
        state->project.video_codec_name,
        state->project.width,
        state->project.height,
        state->project.duration_sec,
        state->project.audio_track_count
    );
    update_window_title(window, config, state);
    return 0;
}

static void open_media_from_dialog(
    SDL_Window *window,
    SDL_Renderer *renderer,
    const AppConfig *config,
    AppState *state
) {
    char dialog_error[256] = {0};
    char *path = file_dialog_open_media(dialog_error, (int)sizeof(dialog_error));
    if (path == NULL) {
        snprintf(state->last_error, sizeof(state->last_error), "%s", dialog_error);
        update_window_title(window, config, state);
        return;
    }

    load_media(window, renderer, config, state, path);
    free(path);
}

int app_run(const AppConfig *config) {
    av_log_set_level(AV_LOG_ERROR);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        config->window_title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        config->window_width,
        config->window_height,
        SDL_WINDOW_RESIZABLE
    );
    if (window == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (renderer == NULL) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    AppState state;
    TimelineInteractionState timeline_interaction;
    PreviewInteractionState preview_interaction;
    memset(&state, 0, sizeof(state));
    state.export_profile = EXPORT_PROFILE_CPU_FAST;
    project_init(&state.project);
    transport_init(&state.transport);
    audio_output_init(&state.audio_output);
    audio_output_init(&state.solo_audio_output);
    if (audio_stream_init(&state.audio_stream, state.last_error, (int)sizeof(state.last_error)) != 0) {
        fprintf(stderr, "audio_stream_init failed: %s\n", state.last_error);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (audio_stream_init(&state.solo_audio_stream, state.last_error, (int)sizeof(state.last_error)) != 0) {
        fprintf(stderr, "solo audio_stream_init failed: %s\n", state.last_error);
        audio_stream_shutdown(&state.audio_stream);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (video_stream_init(&state.video_stream, state.last_error, (int)sizeof(state.last_error)) != 0) {
        fprintf(stderr, "video_stream_init failed: %s\n", state.last_error);
        audio_stream_shutdown(&state.solo_audio_stream);
        audio_stream_shutdown(&state.audio_stream);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (export_worker_init(&state.export_worker, state.last_error, (int)sizeof(state.last_error)) != 0) {
        fprintf(stderr, "export_worker_init failed: %s\n", state.last_error);
        video_stream_shutdown(&state.video_stream);
        audio_stream_shutdown(&state.solo_audio_stream);
        audio_stream_shutdown(&state.audio_stream);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    timeline_view_reset_interaction(&timeline_interaction);
    preview_view_reset_interaction(&preview_interaction);
    update_window_title(window, config, &state);

    if (config->startup_media_path != NULL && config->startup_media_path[0] != '\0') {
        load_media(window, renderer, config, &state, config->startup_media_path);
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                if (export_worker_is_running(&state.export_worker)) {
                    snprintf(
                        state.last_error,
                        sizeof(state.last_error),
                        "%s",
                        "Export running; wait for it to finish before closing"
                    );
                    update_window_title(window, config, &state);
                } else {
                    running = false;
                }
            } else if (state.export_filename_prompt_active) {
                if (event.type == SDL_TEXTINPUT) {
                    append_export_filename_prompt_text(&state, event.text.text);
                } else if (event.type == SDL_KEYDOWN) {
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        stop_export_filename_prompt(&state);
                        update_window_title(window, config, &state);
                    } else if (event.key.keysym.sym == SDLK_BACKSPACE) {
                        backspace_export_filename_prompt(&state);
                    } else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) {
                        if (submit_export_filename_prompt(&state) == 0) {
                            update_window_title(window, config, &state);
                        }
                    }
                }
            } else if (event.type == SDL_DROPFILE) {
                load_media(window, renderer, config, &state, event.drop.file);
                SDL_free(event.drop.file);
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_o) {
                open_media_from_dialog(window, renderer, config, &state);
            } else if (event.type == SDL_KEYDOWN && project_has_media(&state.project)) {
                const double now_sec = (double)SDL_GetTicks64() / 1000.0;
                if (event.key.keysym.sym == SDLK_SPACE) {
                    audio_stream_stop(&state.solo_audio_stream);
                    state.solo_audio_active = false;
                    if (state.transport.playback_state == PLAYBACK_PLAYING) {
                        transport_pause(&state.transport);
                        audio_stream_stop(&state.audio_stream);
                    } else {
                        if (start_audio_playback(&state) == 0) {
                            video_stream_request_frame(
                                &state.video_stream,
                                state.project.source_path,
                                state.transport.playhead_sec
                            );
                            transport_play(&state.transport, now_sec);
                        }
                    }
                    transport_apply_to_project(&state.transport, &state.project);
                    update_window_title(window, config, &state);
                } else if (event.key.keysym.sym == SDLK_s) {
                    transport_stop(&state.transport);
                    audio_stream_stop(&state.audio_stream);
                    audio_stream_stop(&state.solo_audio_stream);
                    state.solo_audio_active = false;
                    transport_apply_to_project(&state.transport, &state.project);
                    update_window_title(window, config, &state);
                } else if (event.key.keysym.sym == SDLK_m) {
                    state.project.collapse_audio = !state.project.collapse_audio;
                    update_window_title(window, config, &state);
                } else if (event.key.keysym.sym == SDLK_p) {
                    cycle_export_profile(&state);
                    update_window_title(window, config, &state);
                } else if (event.key.keysym.sym == SDLK_e) {
                    if (export_worker_is_running(&state.export_worker)) {
                        snprintf(state.last_error, sizeof(state.last_error), "%s", "Export already running");
                    } else {
                        start_export_filename_prompt(&state, window);
                    }
                    update_window_title(window, config, &state);
                } else if (event.key.keysym.sym == SDLK_j) {
                    if ((event.key.keysym.mod & KMOD_CTRL) != 0) {
                        reset_trim_start(&state);
                    } else {
                        snap_trim_start_to_playhead(&state);
                    }
                    refresh_after_trim_shortcut(&state, window, config);
                } else if (event.key.keysym.sym == SDLK_k) {
                    if ((event.key.keysym.mod & KMOD_CTRL) != 0) {
                        reset_trim_end(&state);
                    } else {
                        snap_trim_end_to_playhead(&state);
                    }
                    refresh_after_trim_shortcut(&state, window, config);
                } else if (event.key.keysym.sym == SDLK_z && (event.key.keysym.mod & KMOD_CTRL) != 0) {
                    if (project_undo_last_cut(&state.project)) {
                        transport_sync_from_project(&state.transport, &state.project);
                        clear_mixed_audio(&state);
                        update_window_title(window, config, &state);
                    }
                } else if (event.key.keysym.sym == SDLK_LEFT) {
                    transport_seek(&state.transport, state.transport.playhead_sec - 1.0);
                    if (state.transport.playback_state == PLAYBACK_PLAYING) {
                        start_audio_playback(&state);
                        request_preview_frame(&state);
                    } else {
                        sync_audio_on_transport_change(&state);
                        request_preview_frame(&state);
                    }
                    transport_apply_to_project(&state.transport, &state.project);
                    update_window_title(window, config, &state);
                } else if (event.key.keysym.sym == SDLK_RIGHT) {
                    transport_seek(&state.transport, state.transport.playhead_sec + 1.0);
                    if (state.transport.playback_state == PLAYBACK_PLAYING) {
                        start_audio_playback(&state);
                        request_preview_frame(&state);
                    } else {
                        sync_audio_on_transport_change(&state);
                        request_preview_frame(&state);
                    }
                    transport_apply_to_project(&state.transport, &state.project);
                    update_window_title(window, config, &state);
                }
            } else {
                int width = 0;
                int height = 0;
                SDL_GetWindowSize(window, &width, &height);
                const SDL_Rect preview = compute_preview_rect(&state.project, width, height);
                const SDL_Rect timeline = timeline_view_rect(width, height);
                size_t mp3_track_index = 0;
                const SDL_Rect sidebar = compute_sidebar_rect(width, height);
                const SDL_Rect audio_controls = controls_audio_rect(sidebar);
                const int max_audio_scroll = audio_tracks_view_max_scroll(
                    audio_controls,
                    &state.project,
                    state.audio_tracks_collapsed
                );
                if (state.audio_tracks_scroll_y > max_audio_scroll) {
                    state.audio_tracks_scroll_y = max_audio_scroll;
                }
                const bool left_mouse_down = event.type == SDL_MOUSEBUTTONDOWN &&
                    event.button.button == SDL_BUTTON_LEFT;
                if (event.type == SDL_MOUSEBUTTONDOWN &&
                    event.button.button == SDL_BUTTON_LEFT &&
                    !project_has_media(&state.project) &&
                    point_in_rect(event.button.x, event.button.y, empty_open_button_rect(preview))) {
                    open_media_from_dialog(window, renderer, config, &state);
                } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                    event.button.button == SDL_BUTTON_LEFT &&
                    project_has_media(&state.project) &&
                    point_in_rect(event.button.x, event.button.y, sidebar_open_button_rect(sidebar))) {
                    open_media_from_dialog(window, renderer, config, &state);
                } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                    event.button.button == SDL_BUTTON_LEFT &&
                    project_has_media(&state.project) &&
                    point_in_rect(event.button.x, event.button.y, sidebar_profile_button_rect(sidebar))) {
                    cycle_export_profile(&state);
                    update_window_title(window, config, &state);
                } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                    event.button.button == SDL_BUTTON_LEFT &&
                    project_has_media(&state.project) &&
                    point_in_rect(event.button.x, event.button.y, sidebar_export_button_rect(sidebar))) {
                    if (export_worker_is_running(&state.export_worker)) {
                        snprintf(state.last_error, sizeof(state.last_error), "%s", "Export already running");
                    } else {
                        start_export_filename_prompt(&state, window);
                    }
                    update_window_title(window, config, &state);
                } else if (left_mouse_down) {
                    const AudioTracksViewAction audio_action = audio_tracks_view_action_at_point(
                        audio_controls,
                        &state.project,
                        state.audio_tracks_collapsed,
                        state.audio_tracks_scroll_y,
                        state.solo_audio_active,
                        state.solo_audio_track_index,
                        event.button.x,
                        event.button.y
                    );
                    if (audio_action.kind == AUDIO_TRACKS_VIEW_ACTION_TOGGLE) {
                        state.project.audio_tracks[audio_action.track_index].enabled =
                            !state.project.audio_tracks[audio_action.track_index].enabled;
                        invalidate_trim_dependent_state(&state);
                        restart_audio_playback_if_needed(&state, window, config);
                        update_window_title(window, config, &state);
                    } else if (audio_action.kind == AUDIO_TRACKS_VIEW_ACTION_EXTRACT) {
                        if (export_worker_is_running(&state.export_worker)) {
                            snprintf(state.last_error, sizeof(state.last_error), "%s", "Export already running");
                        } else {
                            transport_pause(&state.transport);
                            audio_stream_stop(&state.audio_stream);
                            transport_apply_to_project(&state.transport, &state.project);
                            (void)run_mp3_extract(&state, audio_action.track_index);
                        }
                        update_window_title(window, config, &state);
                    } else if (audio_action.kind == AUDIO_TRACKS_VIEW_ACTION_PREVIEW_TOGGLE) {
                        transport_pause(&state.transport);
                        transport_apply_to_project(&state.transport, &state.project);
                        if (state.solo_audio_active && state.solo_audio_track_index == audio_action.track_index) {
                            pause_solo_audio_preview(&state);
                        } else {
                            double start_sec = state.project.trim_start_sec;
                            if (state.solo_audio_has_position &&
                                state.solo_audio_track_index == audio_action.track_index) {
                                start_sec = state.solo_audio_start_sec;
                            }
                            (void)start_solo_audio_preview(
                                &state,
                                audio_action.track_index,
                                start_sec
                            );
                        }
                        update_window_title(window, config, &state);
                    } else if (audio_action.kind == AUDIO_TRACKS_VIEW_ACTION_PREVIEW_SEEK) {
                        transport_pause(&state.transport);
                        transport_apply_to_project(&state.transport, &state.project);
                        state.solo_audio_scrubbing = true;
                        (void)start_solo_audio_preview(&state, audio_action.track_index, audio_action.seek_sec);
                        update_window_title(window, config, &state);
                    } else if (preview_view_handle_event(
                            &preview_interaction,
                            &event,
                            preview,
                            &state.project
                        )) {
                        state.transport.dragging_crop = preview_interaction.mode != PREVIEW_DRAG_NONE;
                        update_window_title(window, config, &state);
                    } else if (audio_tracks_view_handle_event(
                            &event,
                            audio_controls,
                            &state.project,
                            &state.audio_tracks_collapsed,
                            state.audio_tracks_scroll_y
                        )) {
                        invalidate_trim_dependent_state(&state);
                        restart_audio_playback_if_needed(&state, window, config);
                        update_window_title(window, config, &state);
                    } else {
                        const TimelineDragMode previous_timeline_mode = timeline_interaction.mode;
                        if (timeline_view_handle_event(
                            &timeline_interaction,
                            &event,
                            timeline,
                            &state.project,
                            &state.transport
                        )) {
                            handle_timeline_dependent_update(
                                &state,
                                window,
                                config,
                                &event,
                                previous_timeline_mode
                            );
                        }
                    }
                } else if (event.type == SDL_MOUSEWHEEL) {
                    int mouse_x = 0;
                    int mouse_y = 0;
                    SDL_GetMouseState(&mouse_x, &mouse_y);
                    if (point_in_rect(mouse_x, mouse_y, audio_controls) && max_audio_scroll > 0) {
                        state.audio_tracks_scroll_y -= event.wheel.y * 42;
                        if (state.audio_tracks_scroll_y < 0) {
                            state.audio_tracks_scroll_y = 0;
                        }
                        if (state.audio_tracks_scroll_y > max_audio_scroll) {
                            state.audio_tracks_scroll_y = max_audio_scroll;
                        }
                    }
                } else if (event.type == SDL_MOUSEMOTION && state.solo_audio_scrubbing) {
                    double seek_sec = 0.0;
                    if (audio_tracks_view_scrub_time_for_track(
                            audio_controls,
                            &state.project,
                            state.solo_audio_track_index,
                            state.audio_tracks_scroll_y,
                            event.motion.x,
                            &seek_sec
                        )) {
                        (void)start_solo_audio_preview(&state, state.solo_audio_track_index, seek_sec);
                        update_window_title(window, config, &state);
                    }
                } else if (event.type == SDL_MOUSEBUTTONUP &&
                    event.button.button == SDL_BUTTON_LEFT &&
                    state.solo_audio_scrubbing) {
                    state.solo_audio_scrubbing = false;
                } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                    event.button.button == SDL_BUTTON_RIGHT &&
                    audio_tracks_view_track_at_point(
                        audio_controls,
                        &state.project,
                        state.audio_tracks_collapsed,
                        state.audio_tracks_scroll_y,
                        event.button.x,
                        event.button.y,
                        &mp3_track_index
                    )) {
                    if (export_worker_is_running(&state.export_worker)) {
                        snprintf(state.last_error, sizeof(state.last_error), "%s", "Export already running");
                    } else {
                        transport_pause(&state.transport);
                        audio_stream_stop(&state.audio_stream);
                        transport_apply_to_project(&state.transport, &state.project);
                        (void)run_mp3_extract(&state, mp3_track_index);
                    }
                    update_window_title(window, config, &state);
                } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                    event.button.button == SDL_BUTTON_MIDDLE &&
                    audio_tracks_view_track_at_point(
                        audio_controls,
                        &state.project,
                        state.audio_tracks_collapsed,
                        state.audio_tracks_scroll_y,
                        event.button.x,
                        event.button.y,
                        &mp3_track_index
                    )) {
                    transport_pause(&state.transport);
                    transport_apply_to_project(&state.transport, &state.project);
                    if (state.solo_audio_active && state.solo_audio_track_index == mp3_track_index) {
                        pause_solo_audio_preview(&state);
                    } else {
                        double start_sec = state.project.trim_start_sec;
                        if (state.solo_audio_has_position && state.solo_audio_track_index == mp3_track_index) {
                            start_sec = state.solo_audio_start_sec;
                        }
                        (void)start_solo_audio_preview(&state, mp3_track_index, start_sec);
                    }
                    update_window_title(window, config, &state);
                } else if (preview_view_handle_event(
                        &preview_interaction,
                        &event,
                        preview,
                        &state.project
                    )) {
                    state.transport.dragging_crop = preview_interaction.mode != PREVIEW_DRAG_NONE;
                    update_window_title(window, config, &state);
                } else if (audio_tracks_view_handle_event(
                        &event,
                        audio_controls,
                        &state.project,
                        &state.audio_tracks_collapsed,
                        state.audio_tracks_scroll_y
                    )) {
                    invalidate_trim_dependent_state(&state);
                    restart_audio_playback_if_needed(&state, window, config);
                    update_window_title(window, config, &state);
                } else {
                    const TimelineDragMode previous_timeline_mode = timeline_interaction.mode;
                    if (timeline_view_handle_event(
                        &timeline_interaction,
                        &event,
                        timeline,
                        &state.project,
                        &state.transport
                    )) {
                        handle_timeline_dependent_update(
                            &state,
                            window,
                            config,
                            &event,
                            previous_timeline_mode
                        );
                    }
                }
            }
        }

        const double now_sec = (double)SDL_GetTicks64() / 1000.0;
        if (audio_stream_take_error(&state.audio_stream, state.last_error, (int)sizeof(state.last_error))) {
            transport_pause(&state.transport);
            transport_apply_to_project(&state.transport, &state.project);
            update_window_title(window, config, &state);
        }
        if (audio_stream_take_error(&state.solo_audio_stream, state.last_error, (int)sizeof(state.last_error))) {
            state.solo_audio_start_sec = solo_audio_preview_sec(&state);
            state.solo_audio_has_position = true;
            state.solo_audio_active = false;
            update_window_title(window, config, &state);
        }
        if (state.solo_audio_active && !audio_output_is_playing(&state.solo_audio_output)) {
            state.solo_audio_start_sec = solo_audio_preview_sec(&state);
            state.solo_audio_has_position = true;
            state.solo_audio_active = false;
            audio_stream_stop(&state.solo_audio_stream);
            update_window_title(window, config, &state);
        }
        if (video_stream_take_error(&state.video_stream, state.last_error, (int)sizeof(state.last_error))) {
            update_window_title(window, config, &state);
        }
        ExportWorkerKind finished_kind = EXPORT_WORKER_KIND_EXPORT;
        bool export_success = false;
        int export_exit_code = -1;
        char export_output_path[1024] = {0};
        char export_error[256] = {0};
        if (export_worker_take_finished(
                &state.export_worker,
                &finished_kind,
                &export_success,
                &export_exit_code,
                export_output_path,
                (int)sizeof(export_output_path),
                export_error,
                (int)sizeof(export_error)
            )) {
            if (export_success) {
                fprintf(
                    stderr,
                    "%s complete: %s\n",
                    finished_kind == EXPORT_WORKER_KIND_MP3 ? "MP3 extract" : "Export",
                    export_output_path
                );
                state.last_error[0] = '\0';
            } else {
                const char *kind_label = finished_kind == EXPORT_WORKER_KIND_MP3 ? "MP3 extract" : "Export";
                const int prefix_written = snprintf(
                    state.last_error,
                    sizeof(state.last_error),
                    "%s failed: ",
                    kind_label
                );
                if (prefix_written >= 0 && prefix_written < (int)sizeof(state.last_error)) {
                    snprintf(
                        state.last_error + prefix_written,
                        sizeof(state.last_error) - (size_t)prefix_written,
                        "%s",
                        export_error
                    );
                }
            }
            update_window_title(window, config, &state);
        }
        if (state.transport.playback_state == PLAYBACK_PLAYING &&
            app_enabled_audio_track_count(&state.project) == 0) {
            if (transport_tick(&state.transport, now_sec)) {
                (void)skip_cut_at_playhead(&state);
                transport_apply_to_project(&state.transport, &state.project);
                (void)refresh_preview_if_due(renderer, &state);
                update_window_title_if_due(window, config, &state, now_sec);
            }
        } else if (state.transport.playback_state == PLAYBACK_PLAYING && !audio_output_is_playing(&state.audio_output)) {
            state.transport.playhead_sec = state.transport.trim_end_sec;
            transport_pause(&state.transport);
            transport_apply_to_project(&state.transport, &state.project);
            update_window_title(window, config, &state);
        } else if (state.transport.playback_state == PLAYBACK_PLAYING) {
            state.transport.last_tick_sec = now_sec;
            sync_transport_to_audio_clock(&state);
            if (skip_cut_at_playhead(&state)) {
                (void)start_audio_playback(&state);
                request_preview_frame(&state);
            }
            transport_apply_to_project(&state.transport, &state.project);
            (void)refresh_preview_if_due(renderer, &state);
            if (state.transport.playback_state != PLAYBACK_PLAYING) {
                audio_stream_stop(&state.audio_stream);
            }
            update_window_title_if_due(window, config, &state, now_sec);
        }
        (void)apply_latest_video_frame(renderer, &state);

        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        draw_placeholder(renderer, &state, &timeline_interaction, width, height);
    }

    video_stream_shutdown(&state.video_stream);
    export_worker_shutdown(&state.export_worker);
    audio_stream_shutdown(&state.solo_audio_stream);
    audio_stream_shutdown(&state.audio_stream);
    audio_output_close(&state.solo_audio_output);
    audio_output_close(&state.audio_output);
    clear_mixed_audio(&state);
    clear_preview(&state, renderer);
    project_reset(&state.project);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
