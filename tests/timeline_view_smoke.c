#include "ui/timeline_view.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static SDL_Event mouse_down(uint8_t button, int x, int y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = button;
    event.button.x = x;
    event.button.y = y;
    return event;
}

static SDL_Event mouse_up(uint8_t button, int x, int y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEBUTTONUP;
    event.button.button = button;
    event.button.x = x;
    event.button.y = y;
    return event;
}

static SDL_Event mouse_motion(int x, int y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEMOTION;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}

static int timeline_x_for_sec(SDL_Rect rect, double duration, double sec) {
    return rect.x + (int)((sec / duration) * rect.w);
}

static bool nearly_equal(double left, double right) {
    return fabs(left - right) < 0.05;
}

int main(void) {
    ProjectState project = {
        .source_path = "fixture.mp4",
        .duration_sec = 10.0,
        .width = 640,
        .height = 360,
        .trim_start_sec = 2.0,
        .trim_end_sec = 8.0,
        .playhead_sec = 3.0,
    };
    TransportState transport;
    transport_init(&transport);
    transport_sync_from_project(&transport, &project);

    TimelineInteractionState interaction;
    timeline_view_reset_interaction(&interaction);

    const SDL_Rect timeline_rect = {.x = 100, .y = 80, .w = 500, .h = 36};
    const int center_y = timeline_rect.y + timeline_rect.h / 2;

    SDL_Event event = mouse_down(SDL_BUTTON_LEFT, timeline_x_for_sec(timeline_rect, project.duration_sec, 6.0), center_y);
    assert(timeline_view_handle_event(&interaction, &event, timeline_rect, &project, &transport));
    assert(interaction.mode == TIMELINE_DRAG_SEEK);
    assert(nearly_equal(transport.playhead_sec, 6.0));
    assert(nearly_equal(project.playhead_sec, 6.0));

    event = mouse_motion(timeline_x_for_sec(timeline_rect, project.duration_sec, 7.0), center_y);
    assert(timeline_view_handle_event(&interaction, &event, timeline_rect, &project, &transport));
    assert(nearly_equal(transport.playhead_sec, 7.0));

    event = mouse_up(SDL_BUTTON_LEFT, timeline_x_for_sec(timeline_rect, project.duration_sec, 7.0), center_y);
    assert(timeline_view_handle_event(&interaction, &event, timeline_rect, &project, &transport));

    const int trim_start_x = timeline_x_for_sec(timeline_rect, project.duration_sec, 2.0);
    event = mouse_down(SDL_BUTTON_LEFT, trim_start_x + 14, center_y);
    assert(timeline_view_handle_event(&interaction, &event, timeline_rect, &project, &transport));
    assert(interaction.mode == TIMELINE_DRAG_TRIM_START);

    event = mouse_motion(timeline_x_for_sec(timeline_rect, project.duration_sec, 3.0), center_y);
    assert(timeline_view_handle_event(&interaction, &event, timeline_rect, &project, &transport));
    assert(nearly_equal(transport.trim_start_sec, 3.0));

    event = mouse_up(SDL_BUTTON_LEFT, timeline_x_for_sec(timeline_rect, project.duration_sec, 3.0), center_y);
    assert(timeline_view_handle_event(&interaction, &event, timeline_rect, &project, &transport));

    project.trim_start_sec = 3.0;
    project.trim_end_sec = 8.0;
    project.playhead_sec = 5.0;
    transport_sync_from_project(&transport, &project);
    timeline_view_reset_interaction(&interaction);

    SDL_SetModState(KMOD_SHIFT);
    event = mouse_down(SDL_BUTTON_LEFT, timeline_x_for_sec(timeline_rect, project.duration_sec, 5.0), center_y);
    assert(timeline_view_handle_event(&interaction, &event, timeline_rect, &project, &transport));
    assert(interaction.mode == TIMELINE_DRAG_TRIM_WINDOW);

    event = mouse_motion(timeline_x_for_sec(timeline_rect, project.duration_sec, 6.0), center_y);
    assert(timeline_view_handle_event(&interaction, &event, timeline_rect, &project, &transport));
    assert(nearly_equal(transport.trim_start_sec, 4.0));
    assert(nearly_equal(transport.trim_end_sec, 9.0));

    event = mouse_up(SDL_BUTTON_LEFT, timeline_x_for_sec(timeline_rect, project.duration_sec, 6.0), center_y);
    assert(timeline_view_handle_event(&interaction, &event, timeline_rect, &project, &transport));
    SDL_SetModState(KMOD_NONE);

    const int playhead_x = timeline_x_for_sec(timeline_rect, project.duration_sec, 7.0);
    event = mouse_down(SDL_BUTTON_LEFT, playhead_x + 13, center_y);
    assert(timeline_view_handle_event(&interaction, &event, timeline_rect, &project, &transport));
    assert(interaction.mode == TIMELINE_DRAG_SEEK);

    event = mouse_motion(timeline_x_for_sec(timeline_rect, project.duration_sec, 5.0), center_y);
    assert(timeline_view_handle_event(&interaction, &event, timeline_rect, &project, &transport));
    assert(nearly_equal(transport.playhead_sec, 5.0));

    SDL_SetModState(KMOD_CTRL);
    event = mouse_down(SDL_BUTTON_LEFT, timeline_x_for_sec(timeline_rect, project.duration_sec, 5.0), center_y);
    assert(timeline_view_handle_event(&interaction, &event, timeline_rect, &project, &transport));
    assert(interaction.mode == TIMELINE_DRAG_CUT_RANGE);

    event = mouse_motion(timeline_x_for_sec(timeline_rect, project.duration_sec, 6.0), center_y);
    assert(timeline_view_handle_event(&interaction, &event, timeline_rect, &project, &transport));
    assert(project.cut_count == 0);

    event = mouse_up(SDL_BUTTON_LEFT, timeline_x_for_sec(timeline_rect, project.duration_sec, 6.0), center_y);
    assert(timeline_view_handle_event(&interaction, &event, timeline_rect, &project, &transport));
    assert(project.cut_count == 1);
    assert(nearly_equal(project.cuts[0].start_sec, 5.0));
    assert(nearly_equal(project.cuts[0].end_sec, 6.0));
    SDL_SetModState(KMOD_NONE);

    return 0;
}
