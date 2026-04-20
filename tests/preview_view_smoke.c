#include "ui/preview_view.h"

#include <assert.h>
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

static int crop_center_preview_x(SDL_Rect preview_rect, const ProjectState *project) {
    return preview_rect.x + (int)(((double)(project->crop.x + (project->crop.width / 2)) / (double)project->width) * preview_rect.w);
}

static int crop_center_preview_y(SDL_Rect preview_rect, const ProjectState *project) {
    return preview_rect.y + (int)(((double)(project->crop.y + (project->crop.height / 2)) / (double)project->height) * preview_rect.h);
}

static void reset_full_crop(ProjectState *project) {
    project->crop = (CropRect){.x = 0, .y = 0, .width = project->width, .height = project->height};
}

static void assert_corner_drag_works(
    ProjectState *project,
    PreviewInteractionState *interaction,
    SDL_Rect preview_rect,
    int down_x,
    int down_y,
    int move_x,
    int move_y,
    PreviewDragMode expected_mode
) {
    reset_full_crop(project);
    preview_view_reset_interaction(interaction);

    SDL_Event event = mouse_down(SDL_BUTTON_LEFT, down_x, down_y);
    assert(preview_view_handle_event(interaction, &event, preview_rect, project));
    assert(interaction->mode == expected_mode);

    event = mouse_motion(move_x, move_y);
    assert(preview_view_handle_event(interaction, &event, preview_rect, project));
    assert(project->crop.width < project->width);
    assert(project->crop.height < project->height);

    event = mouse_up(SDL_BUTTON_LEFT, move_x, move_y);
    assert(preview_view_handle_event(interaction, &event, preview_rect, project));
}

int main(void) {
    ProjectState project = {
        .source_path = "fixture.mp4",
        .duration_sec = 4.0,
        .width = 320,
        .height = 240,
        .crop = {.x = 0, .y = 0, .width = 320, .height = 240},
    };
    PreviewInteractionState interaction;
    const SDL_Rect preview_rect = {.x = 40, .y = 40, .w = 640, .h = 480};

    preview_view_reset_interaction(&interaction);

    assert_corner_drag_works(
        &project,
        &interaction,
        preview_rect,
        preview_rect.x,
        preview_rect.y,
        preview_rect.x + 120,
        preview_rect.y + 120,
        PREVIEW_DRAG_RESIZE_NW
    );
    assert(project.crop.x > 0);
    assert(project.crop.y > 0);

    assert_corner_drag_works(
        &project,
        &interaction,
        preview_rect,
        preview_rect.x + preview_rect.w,
        preview_rect.y,
        preview_rect.x + preview_rect.w - 120,
        preview_rect.y + 120,
        PREVIEW_DRAG_RESIZE_NE
    );
    assert(project.crop.x == 0);
    assert(project.crop.y > 0);

    assert_corner_drag_works(
        &project,
        &interaction,
        preview_rect,
        preview_rect.x,
        preview_rect.y + preview_rect.h,
        preview_rect.x + 120,
        preview_rect.y + preview_rect.h - 120,
        PREVIEW_DRAG_RESIZE_SW
    );
    assert(project.crop.x > 0);
    assert(project.crop.y == 0);

    assert_corner_drag_works(
        &project,
        &interaction,
        preview_rect,
        preview_rect.x + preview_rect.w + 6,
        preview_rect.y + preview_rect.h + 6,
        preview_rect.x + preview_rect.w - 120,
        preview_rect.y + preview_rect.h - 120,
        PREVIEW_DRAG_RESIZE_SE
    );
    assert(project.crop.x == 0);
    assert(project.crop.y == 0);

    reset_full_crop(&project);
    preview_view_reset_interaction(&interaction);

    SDL_Event event = mouse_down(SDL_BUTTON_LEFT, 674, 514);
    assert(preview_view_handle_event(&interaction, &event, preview_rect, &project));

    event = mouse_motion(520, 420);
    assert(preview_view_handle_event(&interaction, &event, preview_rect, &project));
    assert(project.crop.width < 320);
    assert(project.crop.height < 240);

    event = mouse_up(SDL_BUTTON_LEFT, 520, 420);
    assert(preview_view_handle_event(&interaction, &event, preview_rect, &project));

    const CropRect resized_crop = project.crop;
    event = mouse_down(
        SDL_BUTTON_LEFT,
        crop_center_preview_x(preview_rect, &project),
        crop_center_preview_y(preview_rect, &project)
    );
    assert(preview_view_handle_event(&interaction, &event, preview_rect, &project));
    event = mouse_motion(440, 300);
    assert(preview_view_handle_event(&interaction, &event, preview_rect, &project));
    assert(project.crop.x > resized_crop.x);
    assert(project.crop.y > resized_crop.y);

    event = mouse_up(SDL_BUTTON_LEFT, 440, 300);
    assert(preview_view_handle_event(&interaction, &event, preview_rect, &project));

    event = mouse_down(SDL_BUTTON_RIGHT, 300, 200);
    assert(preview_view_handle_event(&interaction, &event, preview_rect, &project));
    assert(project.crop.x == 0);
    assert(project.crop.y == 0);
    assert(project.crop.width == 320);
    assert(project.crop.height == 240);

    return 0;
}
