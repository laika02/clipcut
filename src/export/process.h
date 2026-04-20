#ifndef CLIPCUT_EXPORT_PROCESS_H
#define CLIPCUT_EXPORT_PROCESS_H

#include "export/ffmpeg_export.h"

typedef struct ProcessResult {
    int exit_code;
} ProcessResult;

int export_run_command_sync(
    const ExportCommand *command,
    ProcessResult *result,
    char *error_message,
    int error_message_size
);

#endif
