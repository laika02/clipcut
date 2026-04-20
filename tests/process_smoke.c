#include "export/process.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static char *dup_arg(const char *arg) {
    const size_t len = strlen(arg) + 1;
    char *copy = malloc(len);
    assert(copy != NULL);
    memcpy(copy, arg, len);
    return copy;
}

int main(void) {
    ExportCommand command = {0};
    char *argv[3] = {0};
    ProcessResult result = {0};
    char error[256] = {0};

    argv[0] = dup_arg("ffmpeg");
    argv[1] = dup_arg("-version");
    command.argv = argv;
    command.argc = 2;

    assert(export_run_command_sync(&command, &result, error, (int)sizeof(error)) == 0);
    assert(result.exit_code == 0);

    free(argv[0]);
    free(argv[1]);
    return 0;
}
