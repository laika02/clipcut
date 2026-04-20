#ifndef CLIPCUT_MEDIA_PROBE_H
#define CLIPCUT_MEDIA_PROBE_H

#include <stddef.h>

#include "model/project.h"

typedef struct ProbeError {
    char message[256];
} ProbeError;

int probe_media_file(const char *path, ProjectState *project, ProbeError *error);

#endif
