# ClipCut v0.2

ClipCut is a desktop video editor for quickly cropping, trimming, and sharing a single recording. Remove sections from the middle of a clip, mute individual audio tracks, and export the result without re-encoding streams that can be copied.

v0.2 is a rewrite in C++20 and Qt Quick. It uses GStreamer for playback and FFmpeg for export.

## Run ClipCut

- **Windows 10 x64** — Extract the portable ZIP and run `clipcut.exe`. Keep the executable, DLLs, and runtime folders together. You can drop a recording onto the executable or its shortcut to open it. Preview uses Direct3D 11 and can fall back to software decoding.
- **Linux x64** — Extract the portable tarball and run `./clipcut.sh` from the extracted directory. The bundle requires Wayland, targets Artix Linux, and uses the host's graphics drivers and audio service.

Both bundles include FFmpeg and ffprobe. To open a recording from the command line, pass its path to `clipcut.exe` or `./clipcut.sh`. To compile ClipCut yourself, see [Build from source](#build-from-source).

## Edit a recording

1. Open a video with **Ctrl+O**.
2. Drag the crop edges or corners in the preview. Side edges adjust width, and top and bottom edges adjust height. Drag the trim handles on the timeline or press **I** and **O** at the playhead to set the start and end.
3. **Ctrl-drag on the main timeline** to remove a section and close the gap. **Ctrl-drag on an audio waveform** to silence part of that track while preserving its timing. Drag a region by its middle to move it or by an edge to resize it. Right-click a region or select it and press **Delete** to remove it. Overlapping or touching regions of the same kind merge on release, with mutes merging only within their own track.
4. Open **Export** with **Ctrl+E**. Review the output settings and enter a filename. ClipCut saves the output next to the source recording and asks before replacing an existing file.

Each audio track has a waveform, an inclusion toggle, solo preview, and extraction. Track mutes apply to solo and mixed preview, edited extraction, and export. When offered a choice, use **Original track** to extract the source audio or **Edited track** to apply your edits. Export mixes selected tracks into one by default. Clear **Collapse selected audio into one track** to keep them separate.

Edits are held for the current session. Export the finished clip before closing it. Appearance settings and keyboard bindings persist across sessions.

## Export and compression

- **Frame accurate** is the default cut policy. ClipCut re-encodes streams when edits require it and copies eligible streams. A track mute can leave the video and other audio tracks eligible for copying.
- **Fast copy (snap to keyframes)** supports H.264 cuts. Review **Effective copy ranges** in the export dialog to see where the cuts will land.
- **Compress and re-encode** uses CPU-based, two-pass H.264 encoding with a default target of 20 MB. You can change the target size, reduce the dimensions, and cap frame rates above 30 fps at 30.
- **Ordinary export profile** in **Settings → Editing / export** lets you choose a CPU profile or a GPU profile if the encoder passes a trial encode.

Leave the filename blank to use the default, such as `recording_edited.mp4` or `recording_compressed.mp4`. The status bar shows progress and offers **Open folder** when the export succeeds. Export uses temporary files in the destination folder, so allow space for intermediate video and audio.

## Keyboard shortcuts

| Shortcut | Action |
| --- | --- |
| Space | Play or pause |
| Left / Right | Step one frame |
| Shift+Left / Shift+Right | Seek five seconds |
| I / O | Trim the start or end at the playhead |
| [ / ] | Mark a ripple delete's start or end |
| Ctrl+D | Add a ripple delete at the playhead |
| Alt+Left / Alt+Right | Select the previous or next ripple delete |
| Ctrl+Shift+M | Add a mute at the playhead on the active audio track |
| Ctrl+[ / Ctrl+] | Mark a mute's start or end |
| Delete | Remove the selected ripple delete or mute |
| Ctrl+Z / Ctrl+Shift+Z | Undo or redo |
| Ctrl+R / Ctrl+Shift+R | Reset crop / reset trim and ripple deletes |
| 1–9 / Ctrl+1–9 | Solo an audio track / toggle its inclusion |
| M | Preview the selected audio mix |
| = / − / F | Zoom in, zoom out, or fit the timeline |
| Alt+Shift+Up / Alt+Shift+Down | Give more space to the preview or audio area |
| Ctrl+O / Ctrl+E / Ctrl+, | Open, Export, or Settings |
| Ctrl+K | Open the searchable command palette |

In the editor, use **Tab** to reach controls and editing handles. Use the arrow keys to move a focused handle. Hold **Shift** to move by ten frames or ten crop pixels. To activate an audio track for the mute shortcuts, solo it or focus one of its controls. Change shortcuts under **Settings → Keyboard** or clear a binding to disable it.

In the file browser, press **Ctrl+F** or **/** to filter filenames in the current folder. **Tab** completes or cycles through matching names, and **Shift+Tab** cycles backward. **Ctrl+L** edits the location, **Alt+Up** opens the parent folder, and **Escape** clears the filter before closing the browser.

## Appearance

Use **Settings → Appearance** to switch between **Graphite · Alacritty / SwayFX** and **Dark Rose**, or adjust background and dialog opacity. Text, icons, waveforms, and video stay opaque. On Wayland, background blur depends on the compositor.

Use **Save as…** to export a palette as JSON or import your own theme. After editing the imported copy in the theme directory, choose **Reload**. Use [the Graphite theme](themes/graphite.json) as a template. Theme files use schema version `1`, a name, and a `colors` object with opaque `#RRGGBB` values. Optional settings include `fontFamily`, `backgroundOpacity`, and `popupOpacity`. Opacity values range from 0 to 1, and omitted color roles inherit the Dark Rose defaults.

The command-line option `--theme` accepts `graphite`, `dark-rose`, or a path to a JSON theme.

## Build from source

### Linux

Install CMake 3.24+, Ninja, a C++20 compiler, pkg-config, Qt 6.8+ (Core, Gui, Quick, QuickControls2, Concurrent, and Test), GStreamer 1.24+ development packages, FFmpeg, and ffprobe. Preview needs the GStreamer Qt 6 sink built against the same Qt version as the app. The helper below builds the sink and the GStreamer FFmpeg plugin from matching GStreamer sources. It requires Bash, curl, Meson, and the development libraries for Qt Wayland, EGL, and FFmpeg.

```sh
./packaging/build-linux-deps.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/clipcut recording.mkv
```

### Windows cross-build from Linux

Install the x86_64 MinGW-w64 toolchain, CMake, Ninja, pkg-config, Python with compressed tar support for Zstandard, and native Qt build tools matching the target Qt version. The dependency lock specifies the MSYS2 packages, and the Qt D3D11 sink builds from matching GStreamer source. Set `QT_HOST_PATH` at configure time if the native Qt tools are installed outside `/usr`.

```sh
python3 packaging/fetch_msys.py
gst_bad_source="$(./packaging/fetch-windows-gst-source.sh)"
windows_prefix="$PWD/.deps/windows/mingw64"
PKG_CONFIG_LIBDIR="$windows_prefix/lib/pkgconfig" \
PKG_CONFIG_SYSROOT_DIR="$windows_prefix" \
cmake -S . -B build-windows -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=packaging/mingw-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCLIPCUT_GST_BAD_SOURCE="$gst_bad_source"
cmake --build build-windows --parallel
```

The portable archive scripts in `packaging/` still use v0.1 filenames. Both call a helper that expects `TESTING.md`, `PLAN.md`, and `compress-video.sh`, which are absent from the source tree. See the [runtime notices](packaging/THIRD-PARTY.md) for information about bundled dependencies.

## Diagnostics

Start a performance log under **Settings → Diagnostics**, or pass `--profile performance.jsonl` when launching ClipCut. The JSONL log records playback, seeks, resource use, encoder probes, and export stages. Run `--help` to see the available smoke-test and UI exercise options.
