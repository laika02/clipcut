# ClipCut runtime notices

ClipCut's portable bundles include Qt, GStreamer, FFmpeg, and other runtime libraries. Bundled license texts are collected in `licenses/`. The Windows bundle includes `dependencies.lock.json`, which records the exact MSYS2 package filenames and SHA-256 hashes.

- Qt publishes source archives and license information. [Qt Downloads](https://download.qt.io/official_releases/qt/) · [Qt Licensing](https://www.qt.io/licensing/)
- The build uses matching GStreamer sources for the Qt video sink. The Windows dependency lock pins GStreamer 1.28.8. [GStreamer source archives](https://gstreamer.freedesktop.org/src/)
- The Windows dependency lock pins FFmpeg 9.0.2. The bundled Windows build enables GPL components. [FFmpeg source archive](https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz) · [FFmpeg license information](https://ffmpeg.org/legal.html)
- MSYS2 publishes Windows dependency build recipes and source references. [MSYS2 MinGW package repository](https://github.com/msys2/MINGW-packages)

The bundle layout includes ClipCut source and build scripts under `source/`. Before distributing binaries, finalize the ClipCut application license and provide the corresponding dependency source and notices required by the bundled components' licenses.
