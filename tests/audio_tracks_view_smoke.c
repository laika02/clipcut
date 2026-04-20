#include "ui/audio_tracks_view.h"

#include <assert.h>
#include <string.h>

static SDL_Event click_at(int x, int y) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    return event;
}

int main(void) {
    AudioTrack tracks[2] = {
        {.enabled = true},
        {.enabled = true},
    };
    ProjectState project = {
        .source_path = "fixture.mp4",
        .duration_sec = 10.0,
        .width = 640,
        .height = 360,
        .trim_start_sec = 1.0,
        .trim_end_sec = 9.0,
        .audio_tracks = tracks,
        .audio_track_count = 2,
        .collapse_audio = false,
    };
    bool collapsed = false;
    const SDL_Rect sidebar = {.x = 824, .y = 40, .w = 300, .h = 640};

    SDL_Rect header = audio_tracks_view_header_rect(sidebar);
    SDL_Event event = click_at(header.x + 4, header.y + 4);
    assert(audio_tracks_view_handle_event(&event, sidebar, &project, &collapsed, 0));
    assert(collapsed);
    assert(!project.collapse_audio);

    event = click_at(header.x + 4, header.y + 4);
    assert(audio_tracks_view_handle_event(&event, sidebar, &project, &collapsed, 0));
    assert(!collapsed);

    SDL_Rect row0 = audio_tracks_view_row_rect(sidebar, 0);
    AudioTracksViewAction action = audio_tracks_view_action_at_point(
        sidebar,
        &project,
        collapsed,
        0,
        false,
        0,
        row0.x + row0.w - 100,
        row0.y + 12
    );
    assert(action.kind == AUDIO_TRACKS_VIEW_ACTION_EXTRACT);
    assert(action.track_index == 0);

    action = audio_tracks_view_action_at_point(
        sidebar,
        &project,
        collapsed,
        0,
        false,
        0,
        row0.x + row0.w - 24,
        row0.y + 12
    );
    assert(action.kind == AUDIO_TRACKS_VIEW_ACTION_PREVIEW_TOGGLE);
    assert(action.track_index == 0);
    assert(action.seek_sec == project.trim_start_sec);

    action = audio_tracks_view_action_at_point(
        sidebar,
        &project,
        collapsed,
        0,
        false,
        0,
        row0.x + (row0.w / 2),
        row0.y + 48
    );
    assert(action.kind == AUDIO_TRACKS_VIEW_ACTION_PREVIEW_SEEK);
    assert(action.track_index == 0);
    assert(action.seek_sec > 4.0 && action.seek_sec < 6.0);

    action = audio_tracks_view_action_at_point(
        sidebar,
        &project,
        collapsed,
        0,
        false,
        0,
        row0.x + 12,
        row0.y + 12
    );
    assert(action.kind == AUDIO_TRACKS_VIEW_ACTION_TOGGLE);
    assert(action.track_index == 0);

    event = click_at(row0.x + 4, row0.y + 4);
    assert(audio_tracks_view_handle_event(&event, sidebar, &project, &collapsed, 0));
    assert(!project.audio_tracks[0].enabled);
    assert(project.audio_tracks[1].enabled);

    SDL_Rect row1 = audio_tracks_view_row_rect(sidebar, 1);
    event = click_at(row1.x + 4, row1.y + 4);
    assert(audio_tracks_view_handle_event(&event, sidebar, &project, &collapsed, 0));
    assert(!project.audio_tracks[1].enabled);

    event = click_at(row1.x + 4, row1.y + 4);
    assert(audio_tracks_view_handle_event(&event, sidebar, &project, &collapsed, 0));
    assert(project.audio_tracks[1].enabled);

    event = click_at(row0.x + 4, row0.y + 4);
    assert(audio_tracks_view_handle_event(&event, sidebar, &project, &collapsed, 0));
    assert(project.audio_tracks[0].enabled);

    event = click_at(row1.x + 4, row1.y + 4);
    assert(audio_tracks_view_handle_event(&event, sidebar, &project, &collapsed, 0));
    assert(!project.audio_tracks[1].enabled);

    double seek_sec = -1.0;
    assert(audio_tracks_view_scrub_time_for_track(sidebar, &project, 0, 0, row0.x + (row0.w / 2), &seek_sec));
    assert(seek_sec > 4.0 && seek_sec < 6.0);

    size_t hit_index = 99;
    assert(audio_tracks_view_track_at_point(sidebar, &project, collapsed, 0, row1.x + 4, row1.y + 4, &hit_index));
    assert(hit_index == 1);

    AudioTrack many_tracks[4] = {
        {.enabled = true},
        {.enabled = true},
        {.enabled = true},
        {.enabled = true},
    };
    ProjectState many_project = project;
    many_project.audio_tracks = many_tracks;
    many_project.audio_track_count = 4;
    const SDL_Rect short_sidebar = {.x = 20, .y = 20, .w = 320, .h = 190};
    assert(audio_tracks_view_max_scroll(short_sidebar, &many_project, false) > 0);

    const SDL_Rect row2 = audio_tracks_view_row_rect(short_sidebar, 2);
    const int scroll_y = row2.y - (audio_tracks_view_header_rect(short_sidebar).y + 36);
    action = audio_tracks_view_action_at_point(
        short_sidebar,
        &many_project,
        false,
        scroll_y,
        false,
        0,
        row2.x + 12,
        row2.y - scroll_y + 12
    );
    assert(action.kind == AUDIO_TRACKS_VIEW_ACTION_TOGGLE);
    assert(action.track_index == 2);

    return 0;
}
