#include "export/process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static void set_error(char *dst, int dst_size, const char *message) {
    if (dst != NULL && dst_size > 0) {
        snprintf(dst, (size_t)dst_size, "%s", message);
    }
}

#ifdef _WIN32
static int contains_path_separator(const char *path) {
    return path != NULL && (strchr(path, '\\') != NULL || strchr(path, '/') != NULL || strchr(path, ':') != NULL);
}

static int build_adjacent_executable_path(const char *exe_name, char *buffer, size_t buffer_size) {
    char module_path[MAX_PATH * 4] = {0};
    const DWORD written = GetModuleFileNameA(NULL, module_path, (DWORD)sizeof(module_path));
    if (written == 0 || written >= sizeof(module_path)) {
        return -1;
    }

    char *last_sep = strrchr(module_path, '\\');
    char *last_fwd = strrchr(module_path, '/');
    if (last_fwd != NULL && (last_sep == NULL || last_fwd > last_sep)) {
        last_sep = last_fwd;
    }
    if (last_sep == NULL) {
        return -1;
    }
    *(last_sep + 1) = '\0';

    const int out_written = snprintf(buffer, buffer_size, "%s%s", module_path, exe_name);
    return out_written > 0 && out_written < (int)buffer_size ? 0 : -1;
}

static int resolve_windows_command_path(
    const ExportCommand *command,
    char *buffer,
    size_t buffer_size,
    char *error_message,
    int error_message_size
) {
    if (command == NULL || command->argc <= 0 || command->argv == NULL || command->argv[0] == NULL) {
        set_error(error_message, error_message_size, "Missing executable to run");
        return -1;
    }

    if (contains_path_separator(command->argv[0])) {
        const int written = snprintf(buffer, buffer_size, "%s", command->argv[0]);
        if (written < 0 || written >= (int)buffer_size) {
            set_error(error_message, error_message_size, "Executable path was too long");
            return -1;
        }
        return 0;
    }

    const char *exe_name = command->argv[0];
    char candidate[512] = {0};
    if (strchr(exe_name, '.') == NULL) {
        const int written = snprintf(candidate, sizeof(candidate), "%s.exe", exe_name);
        if (written < 0 || written >= (int)sizeof(candidate)) {
            set_error(error_message, error_message_size, "Executable name was too long");
            return -1;
        }
        exe_name = candidate;
    }

    if (build_adjacent_executable_path(exe_name, buffer, buffer_size) != 0) {
        set_error(error_message, error_message_size, "Failed to resolve adjacent executable path");
        return -1;
    }
    return 0;
}
#endif

int export_run_command_sync(
    const ExportCommand *command,
    ProcessResult *result,
    char *error_message,
    int error_message_size
) {
    if (result != NULL) {
        result->exit_code = -1;
    }
    if (command == NULL || command->argc <= 0 || command->argv == NULL || command->argv[0] == NULL) {
        set_error(error_message, error_message_size, "Missing command to run");
        return -1;
    }

#ifdef _WIN32
    char executable_path[MAX_PATH * 4] = {0};
    if (resolve_windows_command_path(command, executable_path, sizeof(executable_path), error_message, error_message_size) != 0) {
        return -1;
    }

    char **argv = calloc((size_t)command->argc + 1u, sizeof(*argv));
    if (argv == NULL) {
        set_error(error_message, error_message_size, "Failed to allocate process argv");
        return -1;
    }
    argv[0] = executable_path;
    for (int i = 1; i < command->argc; ++i) {
        argv[i] = command->argv[i];
    }
    argv[command->argc] = NULL;

    const intptr_t child = _spawnv(_P_WAIT, executable_path, (const char * const *)argv);
    free(argv);
    if (child == -1) {
        set_error(error_message, error_message_size, "Failed to spawn process");
        return -1;
    }

    const DWORD exit_code = (DWORD)child;
    if (result != NULL) {
        result->exit_code = (int)exit_code;
    }
    if (exit_code != 0) {
        set_error(error_message, error_message_size, "Process exited with a non-zero status");
        return -1;
    }
    if (error_message != NULL && error_message_size > 0) {
        error_message[0] = '\0';
    }
    return 0;
#else
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(error_message, (size_t)error_message_size, "fork failed: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execvp(command->argv[0], command->argv);
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            snprintf(error_message, (size_t)error_message_size, "waitpid failed: %s", strerror(errno));
            return -1;
        }
    }

    int exit_code = 1;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    }
    if (result != NULL) {
        result->exit_code = exit_code;
    }
    if (!WIFEXITED(status) || exit_code != 0) {
        set_error(error_message, error_message_size, "Process exited with a non-zero status");
        return -1;
    }

    if (error_message != NULL && error_message_size > 0) {
        error_message[0] = '\0';
    }
    return 0;
#endif
}
