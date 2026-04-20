#ifndef CLIPCUT_APP_H
#define CLIPCUT_APP_H

typedef struct AppConfig {
    int window_width;
    int window_height;
    const char *window_title;
    const char *startup_media_path;
} AppConfig;

int app_run(const AppConfig *config);

#endif
