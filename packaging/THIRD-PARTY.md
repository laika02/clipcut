# Runtime components

ClipCut's local test bundles contain Qt, GStreamer, FFmpeg, and their runtime dependencies. Individual notices and license texts are in `licenses/`. The Windows package records exact MSYS2 package versions and SHA-256 hashes in `dependencies.lock.json`.

- Qt: https://www.qt.io/licensing/ ; sources https://download.qt.io/official_releases/qt/
- GStreamer 1.28.8: https://gstreamer.freedesktop.org/src/ ; matching Qt sink source is built by this workspace.
- FFmpeg 9.0.2, with GPL-enabled codecs: https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz ; https://ffmpeg.org/legal.html
- MSYS2 build recipes and dependency source references: https://github.com/msys2/MINGW-packages

ClipCut sources/build scripts are included in `source/` for inspecting the prototype. These local testing archives are not a completed public release; dependency source distribution and the application's license must be finalized before publishing one.
