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
static int contains_path_separator(const char *path) {
    return path != NULL && (strchr(path, '\\') != NULL || strchr(path, '/') != NULL || strchr(path, ':') != NULL);
}

static int utf8_to_wide(const char *src, wchar_t **out) {
    const int required = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
    if (required <= 0) {
        return -1;
    }

    wchar_t *buffer = calloc((size_t)required, sizeof(*buffer));
    if (buffer == NULL) {
        return -1;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, src, -1, buffer, required) <= 0) {
        free(buffer);
        return -1;
    }

    *out = buffer;
    return 0;
}

static int build_adjacent_executable_path(const char *exe_name, wchar_t *buffer, size_t buffer_size) {
    wchar_t module_path[MAX_PATH * 4] = {0};
    const size_t module_capacity = sizeof(module_path) / sizeof(module_path[0]);
    const DWORD written = GetModuleFileNameW(NULL, module_path, (DWORD)module_capacity);
    if (written == 0 || written >= module_capacity) {
        return -1;
    }

    wchar_t *last_sep = wcsrchr(module_path, L'\\');
    wchar_t *last_fwd = wcsrchr(module_path, L'/');
    if (last_fwd != NULL && (last_sep == NULL || last_fwd > last_sep)) {
        last_sep = last_fwd;
    }
    if (last_sep == NULL) {
        return -1;
    }
    *(last_sep + 1) = '\0';

    wchar_t *wide_exe = NULL;
    if (utf8_to_wide(exe_name, &wide_exe) != 0) {
        return -1;
    }

    const int out_written = swprintf(buffer, buffer_size, L"%ls%ls", module_path, wide_exe);
    free(wide_exe);
    return out_written > 0 && out_written < (int)buffer_size ? 0 : -1;
}

static int resolve_windows_command_path(
    const ExportCommand *command,
    wchar_t *buffer,
    size_t buffer_size,
    int *use_path_search,
    char *error_message,
    int error_message_size
) {
    if (command == NULL || command->argc <= 0 || command->argv == NULL || command->argv[0] == NULL) {
        set_error(error_message, error_message_size, "Missing executable to run");
        return -1;
    }

    if (contains_path_separator(command->argv[0])) {
        wchar_t *wide_path = NULL;
        if (utf8_to_wide(command->argv[0], &wide_path) != 0) {
            set_error(error_message, error_message_size, "Executable path was too long");
            return -1;
        }
        const int written = swprintf(buffer, buffer_size, L"%ls", wide_path);
        free(wide_path);
        if (written < 0 || written >= (int)buffer_size) {
            set_error(error_message, error_message_size, "Executable path was too long");
            return -1;
        }
        if (use_path_search != NULL) {
            *use_path_search = 0;
        }
        return 0;
    }

    const char *exe_name = command->argv[0];
    char exe_with_ext[512] = {0};
    if (strchr(exe_name, '.') == NULL) {
        const int written = snprintf(exe_with_ext, sizeof(exe_with_ext), "%s.exe", exe_name);
        if (written < 0 || written >= (int)sizeof(exe_with_ext)) {
            set_error(error_message, error_message_size, "Executable name was too long");
            return -1;
        }
        exe_name = exe_with_ext;
    }

    if (build_adjacent_executable_path(exe_name, buffer, buffer_size) == 0 &&
        GetFileAttributesW(buffer) != INVALID_FILE_ATTRIBUTES) {
        if (use_path_search != NULL) {
            *use_path_search = 0;
        }
        return 0;
    }

    wchar_t *wide_name = NULL;
    if (utf8_to_wide(command->argv[0], &wide_name) != 0) {
        set_error(error_message, error_message_size, "Executable name was too long");
        return -1;
    }
    const int written = swprintf(buffer, buffer_size, L"%ls", wide_name);
    free(wide_name);
    if (written < 0 || written >= (int)buffer_size) {
        set_error(error_message, error_message_size, "Executable name was too long");
        return -1;
    }
    if (use_path_search != NULL) {
        *use_path_search = 1;
    }
    return 0;
}

static int append_quoted_warg(wchar_t *buffer, size_t buffer_size, size_t *offset, const wchar_t *arg) {
    if (*offset > 0) {
        if (*offset + 1 >= buffer_size) {
            return -1;
        }
        buffer[(*offset)++] = L' ';
    }
    if (*offset + 2 >= buffer_size) {
        return -1;
    }
    buffer[(*offset)++] = L'"';

    size_t backslashes = 0;
    for (size_t i = 0; arg[i] != L'\0'; ++i) {
        if (arg[i] == L'\\') {
            backslashes++;
            continue;
        }

        if (arg[i] == L'"') {
            if (*offset + (backslashes * 2u) + 2u >= buffer_size) {
                return -1;
            }
            for (size_t slash = 0; slash < (backslashes * 2u) + 1u; ++slash) {
                buffer[(*offset)++] = L'\\';
            }
            buffer[(*offset)++] = L'"';
            backslashes = 0;
            continue;
        }

        if (*offset + backslashes + 2u >= buffer_size) {
            return -1;
        }
        for (size_t slash = 0; slash < backslashes; ++slash) {
            buffer[(*offset)++] = L'\\';
        }
        buffer[(*offset)++] = arg[i];
        backslashes = 0;
    }

    if (*offset + (backslashes * 2u) + 2u >= buffer_size) {
        return -1;
    }
    for (size_t slash = 0; slash < backslashes * 2u; ++slash) {
        buffer[(*offset)++] = L'\\';
    }
    buffer[(*offset)++] = L'"';
    buffer[*offset] = L'\0';
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
    wchar_t executable_path[MAX_PATH * 4] = {0};
    const size_t executable_capacity = sizeof(executable_path) / sizeof(executable_path[0]);
    int use_path_search = 0;
    if (resolve_windows_command_path(command, executable_path, executable_capacity, &use_path_search, error_message, error_message_size) != 0) {
        return -1;
    }

    wchar_t **wide_argv = calloc((size_t)command->argc + 1u, sizeof(*wide_argv));
    if (wide_argv == NULL) {
        set_error(error_message, error_message_size, "Failed to allocate process argv");
        return -1;
    }
    for (int i = 0; i < command->argc; ++i) {
        if (utf8_to_wide(command->argv[i], &wide_argv[i]) != 0) {
            for (int j = 0; j < i; ++j) {
                free(wide_argv[j]);
            }
            free(wide_argv);
            set_error(error_message, error_message_size, "Failed to allocate process argv");
            return -1;
        }
    }

    size_t cmd_capacity = 4096;
    wchar_t *command_line = calloc(cmd_capacity, sizeof(*command_line));
    if (command_line == NULL) {
        for (int j = 0; j < command->argc; ++j) {
            free(wide_argv[j]);
        }
        free(wide_argv);
        set_error(error_message, error_message_size, "Failed to allocate process command line");
        return -1;
    }
    const int start_index = 0;
    size_t cmd_offset = 0;
    for (int i = start_index; i < command->argc; ++i) {
        if (append_quoted_warg(command_line, cmd_capacity, &cmd_offset, wide_argv[i]) != 0) {
            free(command_line);
            for (int j = 0; j < command->argc; ++j) {
                free(wide_argv[j]);
            }
            free(wide_argv);
            set_error(error_message, error_message_size, "Failed to grow process command line");
            return -1;
        }
    }

    STARTUPINFOW startup_info;
    PROCESS_INFORMATION process_info;
    memset(&startup_info, 0, sizeof(startup_info));
    memset(&process_info, 0, sizeof(process_info));
    startup_info.cb = sizeof(startup_info);

    BOOL ok = CreateProcessW(
        use_path_search ? NULL : executable_path,
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
    for (int j = 0; j < command->argc; ++j) {
        free(wide_argv[j]);
    }
    free(wide_argv);
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
