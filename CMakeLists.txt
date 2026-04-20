cmake_minimum_required(VERSION 3.20)

project(clipcut_c_rewrite VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(CLIPCUT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

find_package(PkgConfig QUIET)

find_package(SDL2 CONFIG QUIET)
if(TARGET SDL2::SDL2)
    set(CLIPCUT_SDL2_TARGET SDL2::SDL2)
elseif(TARGET SDL2::SDL2-static)
    set(CLIPCUT_SDL2_TARGET SDL2::SDL2-static)
elseif(PkgConfig_FOUND)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    set(CLIPCUT_SDL2_TARGET PkgConfig::SDL2)
else()
    message(FATAL_ERROR "SDL2 not found. Install SDL2 or configure with a vcpkg toolchain.")
endif()

find_package(unofficial-ffmpeg CONFIG QUIET)
if(TARGET unofficial::ffmpeg::avformat)
    set(CLIPCUT_FFMPEG_TARGETS
        unofficial::ffmpeg::avformat
        unofficial::ffmpeg::avcodec
        unofficial::ffmpeg::avutil
        unofficial::ffmpeg::swscale
        unofficial::ffmpeg::swresample
    )
elseif(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat
        libavcodec
        libavutil
        libswscale
        libswresample
    )
    set(CLIPCUT_FFMPEG_TARGETS PkgConfig::FFMPEG)
else()
    message(FATAL_ERROR "FFmpeg libraries not found. Install FFmpeg dev packages or configure with a vcpkg toolchain.")
endif()

set(CLIPCUT_APP_SOURCES
    src/app/main.c
    src/app/app.c
    src/export/export_worker.c
    src/export/ffmpeg_export.c
    src/export/process.c
    src/model/project.c
    src/media/probe.c
    src/platform/file_dialog.c
    src/playback/audio_output.c
    src/playback/audio_source.c
    src/playback/audio_source_ffmpeg.c
    src/playback/audio_source_mixed_buffer.c
    src/playback/audio_stream.c
    src/playback/audio_preview.c
    src/playback/transport.c
    src/playback/video_preview.c
    src/playback/video_stream.c
    src/ui/bitmap_text.c
    src/ui/audio_tracks_view.c
    src/ui/preview_view.c
    src/ui/timeline_view.c
)

function(clipcut_configure_target target)
    target_include_directories(${target}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_definitions(${target} PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(${target} PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
        $<$<C_COMPILER_ID:MSVC>:/W4>
        $<$<AND:$<BOOL:${CLIPCUT_WARNINGS_AS_ERRORS}>,$<C_COMPILER_ID:GNU,Clang>>:-Werror>
        $<$<AND:$<BOOL:${CLIPCUT_WARNINGS_AS_ERRORS}>,$<C_COMPILER_ID:MSVC>>:/WX>
    )
endfunction()

function(clipcut_link_math target)
    if(NOT MSVC)
        target_link_libraries(${target} PRIVATE m)
    endif()
endfunction()

function(clipcut_link_windows_dialog target)
    if(WIN32)
        target_link_libraries(${target} PRIVATE comdlg32)
    endif()
endfunction()

add_executable(clipcut ${CLIPCUT_APP_SOURCES})
clipcut_configure_target(clipcut)
target_link_libraries(clipcut PRIVATE ${CLIPCUT_SDL2_TARGET} ${CLIPCUT_FFMPEG_TARGETS})
clipcut_link_math(clipcut)
clipcut_link_windows_dialog(clipcut)

add_executable(clipcut_probe_smoke
    tests/probe_smoke.c
    src/model/project.c
    src/media/probe.c
)
clipcut_configure_target(clipcut_probe_smoke)
target_link_libraries(clipcut_probe_smoke PRIVATE ${CLIPCUT_FFMPEG_TARGETS})

add_executable(clipcut_preview_smoke
    tests/preview_decode_smoke.c
    src/playback/video_preview.c
)
clipcut_configure_target(clipcut_preview_smoke)
target_link_libraries(clipcut_preview_smoke PRIVATE ${CLIPCUT_FFMPEG_TARGETS})
clipcut_link_math(clipcut_preview_smoke)

add_executable(clipcut_audio_preview_smoke
    tests/audio_preview_smoke.c
    src/model/project.c
    src/media/probe.c
    src/playback/audio_preview.c
)
clipcut_configure_target(clipcut_audio_preview_smoke)
target_link_libraries(clipcut_audio_preview_smoke PRIVATE ${CLIPCUT_FFMPEG_TARGETS})
clipcut_link_math(clipcut_audio_preview_smoke)

add_executable(clipcut_transport_smoke
    tests/transport_smoke.c
    src/model/project.c
    src/playback/transport.c
)
clipcut_configure_target(clipcut_transport_smoke)

add_executable(clipcut_audio_stream_smoke
    tests/audio_stream_smoke.c
    src/playback/audio_output.c
    src/playback/audio_source.c
    src/playback/audio_source_mixed_buffer.c
    src/playback/audio_stream.c
)
clipcut_configure_target(clipcut_audio_stream_smoke)
target_link_libraries(clipcut_audio_stream_smoke PRIVATE ${CLIPCUT_SDL2_TARGET})
clipcut_link_math(clipcut_audio_stream_smoke)

add_executable(clipcut_audio_output_clock_smoke
    tests/audio_output_clock_smoke.c
    src/playback/audio_output.c
)
clipcut_configure_target(clipcut_audio_output_clock_smoke)
target_link_libraries(clipcut_audio_output_clock_smoke PRIVATE ${CLIPCUT_SDL2_TARGET})
clipcut_link_math(clipcut_audio_output_clock_smoke)

add_executable(clipcut_audio_source_ffmpeg_smoke
    tests/audio_source_ffmpeg_smoke.c
    src/model/project.c
    src/media/probe.c
    src/playback/audio_source.c
    src/playback/audio_source_ffmpeg.c
)
clipcut_configure_target(clipcut_audio_source_ffmpeg_smoke)
target_link_libraries(clipcut_audio_source_ffmpeg_smoke PRIVATE ${CLIPCUT_FFMPEG_TARGETS})
clipcut_link_math(clipcut_audio_source_ffmpeg_smoke)

add_executable(clipcut_video_stream_smoke
    tests/video_stream_smoke.c
    src/playback/video_preview.c
    src/playback/video_stream.c
)
clipcut_configure_target(clipcut_video_stream_smoke)
target_link_libraries(clipcut_video_stream_smoke PRIVATE ${CLIPCUT_SDL2_TARGET} ${CLIPCUT_FFMPEG_TARGETS})
clipcut_link_math(clipcut_video_stream_smoke)

add_executable(clipcut_preview_view_smoke
    tests/preview_view_smoke.c
    src/model/project.c
    src/ui/bitmap_text.c
    src/ui/preview_view.c
)
clipcut_configure_target(clipcut_preview_view_smoke)
target_link_libraries(clipcut_preview_view_smoke PRIVATE ${CLIPCUT_SDL2_TARGET})

add_executable(clipcut_timeline_view_smoke
    tests/timeline_view_smoke.c
    src/model/project.c
    src/playback/transport.c
    src/ui/timeline_view.c
)
clipcut_configure_target(clipcut_timeline_view_smoke)
target_link_libraries(clipcut_timeline_view_smoke PRIVATE ${CLIPCUT_SDL2_TARGET})
clipcut_link_math(clipcut_timeline_view_smoke)

add_executable(clipcut_audio_tracks_view_smoke
    tests/audio_tracks_view_smoke.c
    src/model/project.c
    src/ui/bitmap_text.c
    src/ui/audio_tracks_view.c
)
clipcut_configure_target(clipcut_audio_tracks_view_smoke)
target_link_libraries(clipcut_audio_tracks_view_smoke PRIVATE ${CLIPCUT_SDL2_TARGET})

add_executable(clipcut_ffmpeg_export_smoke
    tests/ffmpeg_export_smoke.c
    src/export/ffmpeg_export.c
    src/model/project.c
)
clipcut_configure_target(clipcut_ffmpeg_export_smoke)

add_executable(clipcut_process_smoke
    tests/process_smoke.c
    src/export/process.c
)
clipcut_configure_target(clipcut_process_smoke)

add_executable(clipcut_ffmpeg_export_process_smoke
    tests/ffmpeg_export_process_smoke.c
    src/export/ffmpeg_export.c
    src/export/process.c
    src/model/project.c
)
clipcut_configure_target(clipcut_ffmpeg_export_process_smoke)

add_executable(clipcut_export_worker_smoke
    tests/export_worker_smoke.c
    src/export/export_worker.c
    src/export/ffmpeg_export.c
    src/export/process.c
    src/model/project.c
)
clipcut_configure_target(clipcut_export_worker_smoke)
target_link_libraries(clipcut_export_worker_smoke PRIVATE ${CLIPCUT_SDL2_TARGET})

add_executable(clipcut_file_dialog_smoke
    tests/file_dialog_smoke.c
    src/platform/file_dialog.c
)
clipcut_configure_target(clipcut_file_dialog_smoke)
clipcut_link_windows_dialog(clipcut_file_dialog_smoke)
