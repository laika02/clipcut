#ifndef CLIPCUT_EXPORT_WORKER_H
#define CLIPCUT_EXPORT_WORKER_H

#include <stdbool.h>

#include <SDL.h>

#include "export/ffmpeg_export.h"

typedef enum ExportWorkerKind {
    EXPORT_WORKER_KIND_EXPORT = 0,
    EXPORT_WORKER_KIND_MP3
} ExportWorkerKind;

typedef enum ExportWorkerStatus {
    EXPORT_WORKER_IDLE = 0,
    EXPORT_WORKER_RUNNING,
    EXPORT_WORKER_SUCCEEDED,
    EXPORT_WORKER_FAILED
} ExportWorkerStatus;

typedef struct ExportWorkerState {
    SDL_Thread *thread;
    SDL_mutex *mutex;
    ExportCommand command;
    ExportWorkerKind kind;
    ExportWorkerStatus status;
    int exit_code;
    char output_path[1024];
    char error[256];
} ExportWorkerState;

int export_worker_init(ExportWorkerState *worker, char *error_message, int error_message_size);
void export_worker_shutdown(ExportWorkerState *worker);
bool export_worker_is_running(ExportWorkerState *worker);
int export_worker_start(
    ExportWorkerState *worker,
    const ExportCommand *command,
    ExportWorkerKind kind,
    const char *output_path,
    char *error_message,
    int error_message_size
);
bool export_worker_take_finished(
    ExportWorkerState *worker,
    ExportWorkerKind *kind,
    bool *success,
    int *exit_code,
    char *output_path,
    int output_path_size,
    char *error_message,
    int error_message_size
);

#endif
