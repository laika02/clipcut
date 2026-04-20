#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform/file_dialog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

static void set_error(char *dst, int dst_size, const char *message) {
    if (dst != NULL && dst_size > 0) {
        snprintf(dst, (size_t)dst_size, "%s", message);
    }
}

#ifndef _WIN32
static char *dup_string(const char *src) {
    const size_t len = strlen(src) + 1;
    char *copy = malloc(len);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, src, len);
    return copy;
}

static void trim_trailing_newline(char *value) {
    if (value == NULL) {
        return;
    }

    size_t len = strlen(value);
    while (len > 0 && (value[len - 1] == '\n' || value[len - 1] == '\r')) {
        value[len - 1] = '\0';
        len--;
    }
}

static char *run_picker_command(const char *command) {
    FILE *pipe = popen(command, "r");
    if (pipe == NULL) {
        return NULL;
    }

    char buffer[4096] = {0};
    char *result = fgets(buffer, sizeof(buffer), pipe);
    const int rc = pclose(pipe);
    if (result == NULL || rc != 0) {
        return NULL;
    }

    trim_trailing_newline(buffer);
    if (buffer[0] == '\0') {
        return NULL;
    }
    return dup_string(buffer);
}
#endif

char *file_dialog_open_media(char *error_message, int error_message_size) {
#ifdef _WIN32
    char path[MAX_PATH] = {0};
    OPENFILENAMEA dialog;
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = path;
    dialog.nMaxFile = sizeof(path);
    dialog.lpstrTitle = "Open media";
    dialog.lpstrFilter =
        "Media Files\0*.mp4;*.mov;*.mkv;*.avi;*.webm;*.m4v;*.mp3;*.wav;*.aac\0"
        "All Files\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameA(&dialog)) {
        set_error(error_message, error_message_size, "No media file selected");
        return NULL;
    }

    const size_t len = strlen(path) + 1;
    char *copy = malloc(len);
    if (copy == NULL) {
        set_error(error_message, error_message_size, "Failed to allocate selected path");
        return NULL;
    }
    memcpy(copy, path, len);
    if (error_message != NULL && error_message_size > 0) {
        error_message[0] = '\0';
    }
    return copy;
#else
    char *path = run_picker_command(
        "zenity --file-selection --title='Open media' "
        "--file-filter='Media files | *.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mp3 *.wav *.aac' "
        "--file-filter='All files | *' 2>/dev/null"
    );
    if (path == NULL) {
        path = run_picker_command(
            "kdialog --getopenfilename . "
            "'*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mp3 *.wav *.aac|Media files' 2>/dev/null"
        );
    }
    if (path == NULL) {
        set_error(error_message, error_message_size, "No file picker available or no media selected");
        return NULL;
    }

    if (error_message != NULL && error_message_size > 0) {
        error_message[0] = '\0';
    }
    return path;
#endif
}
