#include "app/app.h"

#include <stddef.h>

int main(int argc, char **argv) {
    const AppConfig config = {
        .window_width = 1440,
        .window_height = 900,
        .window_title = "ClipCut C Rewrite",
        .startup_media_path = argc > 1 ? argv[1] : NULL,
    };

    return app_run(&config);
}
