#include "export/export_worker.h"

#include "export/process.h"

#include <stdio.h>
#include <string.h>

static void set_error(char *dst, int dst_size, const char *message) {
    if (dst != NULL && dst_size > 0) {
        snprintf(dst, (size_t)dst_size, "%s", message);
    }
}

static int copy_command(
    const ExportCommand *src,
    ExportCommand *dst,
    char *error_message,
    int error_message_size
) {
    export_command_reset(dst);
    for (int i = 0; i < src->argc; ++i) {
        char **next = SDL_realloc(dst->argv, ((size_t)dst->argc + 2u) * sizeof(*dst->argv));
        if (next == NULL) {
            set_error(error_message, error_message_size, "Failed to allocate worker command argv");
            export_command_reset(dst);
            return -1;
        }
        dst->argv = next;

        dst->argv[dst->argc] = SDL_strdup(src->argv[i]);
        if (dst->argv[dst->argc] == NULL) {
            set_error(error_message, error_message_size, "Failed to copy worker command arg");
            export_command_reset(dst);
            return -1;
        }
        dst->argc++;
        dst->argv[dst->argc] = NULL;
    }

    return 0;
}

static int worker_thread_main(void *userdata) {
    ExportWorkerState *worker = userdata;
    ProcessResult result = {0};
    char error[256] = {0};

    const int rc = export_run_command_sync(&worker->command, &result, error, (int)sizeof(error));

    SDL_LockMutex(worker->mutex);
    worker->exit_code = result.exit_code;
    if (rc == 0) {
        worker->status = EXPORT_WORKER_SUCCEEDED;
        worker->error[0] = '\0';
    } else {
        worker->status = EXPORT_WORKER_FAILED;
        snprintf(worker->error, sizeof(worker->error), "%s", error);
    }
    SDL_UnlockMutex(worker->mutex);
    return 0;
}

int export_worker_init(ExportWorkerState *worker, char *error_message, int error_message_size) {
    if (worker == NULL) {
        set_error(error_message, error_message_size, "Missing export worker state");
        return -1;
    }

    memset(worker, 0, sizeof(*worker));
    worker->mutex = SDL_CreateMutex();
    if (worker->mutex == NULL) {
        set_error(error_message, error_message_size, "SDL_CreateMutex failed for export worker");
        return -1;
    }
    worker->status = EXPORT_WORKER_IDLE;
    return 0;
}

void export_worker_shutdown(ExportWorkerState *worker) {
    if (worker == NULL) {
        return;
    }

    if (worker->thread != NULL) {
        SDL_WaitThread(worker->thread, NULL);
        worker->thread = NULL;
    }
    if (worker->mutex != NULL) {
        SDL_DestroyMutex(worker->mutex);
    }
    export_command_reset(&worker->command);
    memset(worker, 0, sizeof(*worker));
}

bool export_worker_is_running(ExportWorkerState *worker) {
    if (worker == NULL || worker->mutex == NULL) {
        return false;
    }

    SDL_LockMutex(worker->mutex);
    const bool running = worker->status == EXPORT_WORKER_RUNNING;
    SDL_UnlockMutex(worker->mutex);
    return running;
}

int export_worker_start(
    ExportWorkerState *worker,
    const ExportCommand *command,
    ExportWorkerKind kind,
    const char *output_path,
    char *error_message,
    int error_message_size
) {
    if (worker == NULL || worker->mutex == NULL || command == NULL || command->argc <= 0) {
        set_error(error_message, error_message_size, "Missing export worker start inputs");
        return -1;
    }

    if (worker->thread != NULL) {
        SDL_WaitThread(worker->thread, NULL);
        worker->thread = NULL;
    }

    SDL_LockMutex(worker->mutex);
    const bool running = worker->status == EXPORT_WORKER_RUNNING;
    SDL_UnlockMutex(worker->mutex);
    if (running) {
        set_error(error_message, error_message_size, "An export job is already running");
        return -1;
    }

    if (copy_command(command, &worker->command, error_message, error_message_size) != 0) {
        return -1;
    }

    SDL_LockMutex(worker->mutex);
    worker->kind = kind;
    worker->status = EXPORT_WORKER_RUNNING;
    worker->exit_code = -1;
    worker->error[0] = '\0';
    snprintf(worker->output_path, sizeof(worker->output_path), "%s", output_path != NULL ? output_path : "");
    SDL_UnlockMutex(worker->mutex);

    worker->thread = SDL_CreateThread(worker_thread_main, "clipcut-export", worker);
    if (worker->thread == NULL) {
        SDL_LockMutex(worker->mutex);
        worker->status = EXPORT_WORKER_FAILED;
        snprintf(worker->error, sizeof(worker->error), "%s", "SDL_CreateThread failed for export worker");
        SDL_UnlockMutex(worker->mutex);
        set_error(error_message, error_message_size, "SDL_CreateThread failed for export worker");
        return -1;
    }

    return 0;
}

bool export_worker_take_finished(
    ExportWorkerState *worker,
    ExportWorkerKind *kind,
    bool *success,
    int *exit_code,
    char *output_path,
    int output_path_size,
    char *error_message,
    int error_message_size
) {
    if (worker == NULL || worker->mutex == NULL) {
        return false;
    }

    SDL_LockMutex(worker->mutex);
    const bool finished =
        worker->status == EXPORT_WORKER_SUCCEEDED ||
        worker->status == EXPORT_WORKER_FAILED;
    if (!finished) {
        SDL_UnlockMutex(worker->mutex);
        return false;
    }

    const ExportWorkerStatus status = worker->status;
    if (kind != NULL) {
        *kind = worker->kind;
    }
    if (success != NULL) {
        *success = status == EXPORT_WORKER_SUCCEEDED;
    }
    if (exit_code != NULL) {
        *exit_code = worker->exit_code;
    }
    if (output_path != NULL && output_path_size > 0) {
        snprintf(output_path, (size_t)output_path_size, "%s", worker->output_path);
    }
    if (error_message != NULL && error_message_size > 0) {
        snprintf(error_message, (size_t)error_message_size, "%s", worker->error);
    }
    worker->status = EXPORT_WORKER_IDLE;
    SDL_UnlockMutex(worker->mutex);

    if (worker->thread != NULL) {
        SDL_WaitThread(worker->thread, NULL);
        worker->thread = NULL;
    }
    export_command_reset(&worker->command);
    return true;
}
