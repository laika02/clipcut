#include "model/project.h"

#include <stdlib.h>
#include <string.h>

static double clamp_double(double value, double min, double max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static int compare_cut_ranges(const void *a, const void *b) {
    const CutRange *left = a;
    const CutRange *right = b;
    if (left->start_sec < right->start_sec) {
        return -1;
    }
    if (left->start_sec > right->start_sec) {
        return 1;
    }
    return 0;
}

void project_init(ProjectState *project) {
    if (project == NULL) {
        return;
    }

    memset(project, 0, sizeof(*project));
}

void project_reset(ProjectState *project) {
    if (project == NULL) {
        return;
    }

    free(project->source_path);
    free(project->audio_tracks);
    free(project->cuts);
    free(project->undo_cuts);
    memset(project, 0, sizeof(*project));
}

bool project_has_media(const ProjectState *project) {
    return project != NULL &&
           project->source_path != NULL &&
           project->duration_sec > 0.0 &&
           project->width > 0 &&
           project->height > 0;
}

void project_clamp_playhead(ProjectState *project) {
    if (!project_has_media(project)) {
        if (project != NULL) {
            project->playhead_sec = 0.0;
        }
        return;
    }

    if (project->playhead_sec < project->trim_start_sec) {
        project->playhead_sec = project->trim_start_sec;
    }
    if (project->playhead_sec > project->trim_end_sec) {
        project->playhead_sec = project->trim_end_sec;
    }
    project->playhead_sec = project_skip_cut_forward(project, project->playhead_sec);
    if (project->playhead_sec > project->trim_end_sec) {
        project->playhead_sec = project->trim_end_sec;
    }
}

bool project_time_is_cut(const ProjectState *project, double time_sec, CutRange *cut) {
    if (project == NULL || project->cuts == NULL) {
        return false;
    }

    for (size_t i = 0; i < project->cut_count; ++i) {
        if (time_sec >= project->cuts[i].start_sec && time_sec < project->cuts[i].end_sec) {
            if (cut != NULL) {
                *cut = project->cuts[i];
            }
            return true;
        }
    }

    return false;
}

double project_skip_cut_forward(const ProjectState *project, double time_sec) {
    CutRange cut = {0};
    double result = time_sec;
    while (project_time_is_cut(project, result, &cut)) {
        result = cut.end_sec;
    }
    return result;
}

int project_add_cut(ProjectState *project, double start_sec, double end_sec) {
    if (!project_has_media(project)) {
        return -1;
    }

    if (end_sec < start_sec) {
        const double tmp = start_sec;
        start_sec = end_sec;
        end_sec = tmp;
    }

    start_sec = clamp_double(start_sec, project->trim_start_sec, project->trim_end_sec);
    end_sec = clamp_double(end_sec, project->trim_start_sec, project->trim_end_sec);
    if ((end_sec - start_sec) < 0.05) {
        return -1;
    }

    CutRange *undo = NULL;
    if (project->cut_count > 0) {
        undo = malloc(project->cut_count * sizeof(*undo));
        if (undo == NULL) {
            return -1;
        }
        memcpy(undo, project->cuts, project->cut_count * sizeof(*undo));
    }

    CutRange *next = realloc(project->cuts, (project->cut_count + 1u) * sizeof(*project->cuts));
    if (next == NULL) {
        free(undo);
        return -1;
    }

    free(project->undo_cuts);
    project->undo_cuts = undo;
    project->undo_cut_count = project->cut_count;
    project->has_cut_undo = true;

    project->cuts = next;
    project->cuts[project->cut_count++] = (CutRange){.start_sec = start_sec, .end_sec = end_sec};
    qsort(project->cuts, project->cut_count, sizeof(*project->cuts), compare_cut_ranges);

    size_t write = 0;
    for (size_t read = 0; read < project->cut_count; ++read) {
        CutRange current = project->cuts[read];
        if (write == 0 || current.start_sec > project->cuts[write - 1u].end_sec) {
            project->cuts[write++] = current;
        } else if (current.end_sec > project->cuts[write - 1u].end_sec) {
            project->cuts[write - 1u].end_sec = current.end_sec;
        }
    }
    project->cut_count = write;
    project_clamp_playhead(project);
    return 0;
}

bool project_undo_last_cut(ProjectState *project) {
    if (project == NULL || !project->has_cut_undo) {
        return false;
    }

    CutRange *restored = NULL;
    if (project->undo_cut_count > 0) {
        restored = malloc(project->undo_cut_count * sizeof(*restored));
        if (restored == NULL) {
            return false;
        }
        memcpy(restored, project->undo_cuts, project->undo_cut_count * sizeof(*restored));
    }

    free(project->cuts);
    project->cuts = restored;
    project->cut_count = project->undo_cut_count;
    free(project->undo_cuts);
    project->undo_cuts = NULL;
    project->undo_cut_count = 0;
    project->has_cut_undo = false;
    project_clamp_playhead(project);
    return true;
}

void project_clear_cuts(ProjectState *project) {
    if (project == NULL) {
        return;
    }

    free(project->cuts);
    free(project->undo_cuts);
    project->cuts = NULL;
    project->cut_count = 0;
    project->undo_cuts = NULL;
    project->undo_cut_count = 0;
    project->has_cut_undo = false;
}
