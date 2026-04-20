#include "export/process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
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
static int append_quoted_arg(char *buffer, size_t buffer_size, size_t *offset, const char *arg) {
    if (*offset + 3 >= buffer_size) {
        return -1;
    }

    if (*offset > 0) {
        buffer[(*offset)++] = ' ';
    }
    buffer[(*offset)++] = '"';

    size_t backslashes = 0;
    for (size_t i = 0; arg[i] != '\0'; ++i) {
        if (arg[i] == '\\') {
            backslashes++;
            continue;
        }

        if (arg[i] == '"') {
            if (*offset + (backslashes * 2u) + 2u >= buffer_size) {
                return -1;
            }
            for (size_t slash = 0; slash < (backslashes * 2u) + 1u; ++slash) {
                buffer[(*offset)++] = '\\';
            }
            buffer[(*offset)++] = '"';
            backslashes = 0;
            continue;
        }

        if (*offset + backslashes + 2u >= buffer_size) {
            return -1;
        }
        for (size_t slash = 0; slash < backslashes; ++slash) {
            buffer[(*offset)++] = '\\';
        }
        buffer[(*offset)++] = arg[i];
        backslashes = 0;
    }

    if (*offset + (backslashes * 2u) + 2u >= buffer_size) {
        return -1;
    }
    for (size_t slash = 0; slash < backslashes * 2u; ++slash) {
        buffer[(*offset)++] = '\\';
    }
    buffer[(*offset)++] = '"';
    buffer[*offset] = '\0';
    return 0;
}

static int build_windows_command_line(const ExportCommand *command, char **out, char *error_message, int error_message_size) {
    size_t capacity = 256;
    char *buffer = calloc(capacity, 1);
    if (buffer == NULL) {
        set_error(error_message, error_message_size, "Failed to allocate process command line");
        return -1;
    }

    size_t offset = 0;
    for (int i = 0; i < command->argc; ++i) {
        while (append_quoted_arg(buffer, capacity, &offset, command->argv[i]) != 0) {
            capacity *= 2;
            char *resized = realloc(buffer, capacity);
            if (resized == NULL) {
                free(buffer);
                set_error(error_message, error_message_size, "Failed to grow process command line");
                return -1;
            }
            buffer = resized;
            memset(buffer + offset, 0, capacity - offset);
        }
    }

    *out = buffer;
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
    char *command_line = NULL;
    if (build_windows_command_line(command, &command_line, error_message, error_message_size) != 0) {
        return -1;
    }

    STARTUPINFOA startup_info;
    PROCESS_INFORMATION process_info;
    memset(&startup_info, 0, sizeof(startup_info));
    memset(&process_info, 0, sizeof(process_info));
    startup_info.cb = sizeof(startup_info);

    BOOL ok = CreateProcessA(
        NULL,
        command_line,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &startup_info,
        &process_info
    );
    free(command_line);
    if (!ok) {
        set_error(error_message, error_message_size, "CreateProcess failed");
        return -1;
    }

    WaitForSingleObject(process_info.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
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
