#ifndef CLIPCUT_MODEL_PROJECT_H
#define CLIPCUT_MODEL_PROJECT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct CropRect {
    int x;
    int y;
    int width;
    int height;
} CropRect;

typedef struct AudioTrack {
    int stream_index;
    int stream_order_among_audio;
    char codec_name[64];
    int channels;
    int sample_rate;
    char language[32];
    char title[128];
    bool enabled;
} AudioTrack;

typedef struct CutRange {
    double start_sec;
    double end_sec;
} CutRange;

typedef struct ProjectState {
    char *source_path;
    double duration_sec;
    int width;
    int height;
    char video_codec_name[64];
    double trim_start_sec;
    double trim_end_sec;
    double playhead_sec;
    CropRect crop;
    AudioTrack *audio_tracks;
    size_t audio_track_count;
    CutRange *cuts;
    size_t cut_count;
    CutRange *undo_cuts;
    size_t undo_cut_count;
    bool has_cut_undo;
    bool collapse_audio;
} ProjectState;

void project_init(ProjectState *project);
void project_reset(ProjectState *project);
void project_clamp_playhead(ProjectState *project);
bool project_has_media(const ProjectState *project);
bool project_time_is_cut(const ProjectState *project, double time_sec, CutRange *cut);
double project_skip_cut_forward(const ProjectState *project, double time_sec);
int project_add_cut(ProjectState *project, double start_sec, double end_sec);
bool project_undo_last_cut(ProjectState *project);
void project_clear_cuts(ProjectState *project);

#endif
