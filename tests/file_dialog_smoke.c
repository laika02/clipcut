#include "platform/file_dialog.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    char error[256] = {0};
    char *path = file_dialog_open_media(error, (int)sizeof(error));
    if (path != NULL) {
        printf("file_dialog_smoke=selected:%s\n", path);
        free(path);
        return 0;
    }

    printf("file_dialog_smoke=no-selection:%s\n", error);
    return 0;
}
