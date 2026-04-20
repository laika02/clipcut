#include "export/export_worker.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_arg(const char *arg) {
    const size_t len = strlen(arg) + 1;
    char *copy = malloc(len);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, arg, len);
    return copy;
}

static void free_stack_command_args(char **argv, int argc) {
    for (int i = 0; i < argc; ++i) {
        free(argv[i]);
    }
}

static const char *temp_output_path(void) {
#ifdef _WIN32
    const char *temp = getenv("TEMP");
    if (temp != NULL && temp[0] != '\0') {
        static char path[1024];
        snprintf(path, sizeof(path), "%s\\clipcut-unused.mp4", temp);
        return path;
    }
    return "clipcut-unused.mp4";
#else
    return "/tmp/unused.mp4";
#endif
}

int main(void) {
    if (SDL_Init(SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    ExportWorkerState worker;
    char error[256] = {0};
    if (export_worker_init(&worker, error, (int)sizeof(error)) != 0) {
        fprintf(stderr, "export_worker_init failed: %s\n", error);
        SDL_Quit();
        return 1;
    }

    char *argv[3] = {0};
    argv[0] = dup_arg("ffmpeg");
    argv[1] = dup_arg("-version");
    if (argv[0] == NULL || argv[1] == NULL) {
        fprintf(stderr, "failed to allocate command args\n");
        free_stack_command_args(argv, 2);
        export_worker_shutdown(&worker);
        SDL_Quit();
        return 1;
    }

    ExportCommand command = {.argv = argv, .argc = 2};
    if (export_worker_start(
            &worker,
            &command,
            EXPORT_WORKER_KIND_EXPORT,
            temp_output_path(),
            error,
            (int)sizeof(error)
        ) != 0) {
        fprintf(stderr, "export_worker_start failed: %s\n", error);
        free_stack_command_args(argv, 2);
        export_worker_shutdown(&worker);
        SDL_Quit();
        return 1;
    }
    free_stack_command_args(argv, 2);

    const Uint32 started = SDL_GetTicks();
    while (true) {
        ExportWorkerKind kind = EXPORT_WORKER_KIND_EXPORT;
        bool success = false;
        int exit_code = -1;
        char output_path[1024] = {0};
        char worker_error[256] = {0};

        if (export_worker_take_finished(
                &worker,
                &kind,
                &success,
                &exit_code,
                output_path,
                (int)sizeof(output_path),
                worker_error,
                (int)sizeof(worker_error)
            )) {
            if (!success || exit_code != 0 || kind != EXPORT_WORKER_KIND_EXPORT) {
                fprintf(stderr, "worker failed: success=%d exit=%d error=%s\n", success, exit_code, worker_error);
                export_worker_shutdown(&worker);
                SDL_Quit();
                return 1;
            }
            printf("export_worker_smoke=ok output:%s\n", output_path);
            break;
        }

        if ((SDL_GetTicks() - started) > 3000) {
            fprintf(stderr, "export worker smoke timed out\n");
            export_worker_shutdown(&worker);
            SDL_Quit();
            return 1;
        }
        SDL_Delay(10);
    }

    export_worker_shutdown(&worker);
    SDL_Quit();
    return 0;
}
