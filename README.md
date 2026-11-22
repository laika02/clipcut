# ClipCut 0.1

Native, session-only video surgery for Windows 10 x64 and Artix Linux on Wayland. C++20 / Qt Quick; GStreamer preview; FFmpeg exports. Compact charcoal and rose interface.

Open a video, drag its crop corners or edges, trim its ends, and add interior ripple deletes. Crop side edges change only width; top/bottom edges change only height. The opposite edge stays fixed. **Ctrl-drag on the main timeline creates a ripple delete; Ctrl-drag on an audio waveform creates a mute for that track.** Drag either region in its middle to move it without changing its length, or drag its edges to resize it. Mute drags stay attached until release, including above/below the waveform; the window keeps the edit active if the local waveform loses its grab. Touchpad scrolling pauses during a held edit and resumes on release. Overlapping or touching deletes combine on release; mutes combine within their own audio track. Right-click a region or its edge to remove it; selecting it and pressing Delete also works. Each creation, move, resize, merge, or removal can be undone/redone as one action.

Every audio track has an inclusion toggle, synchronized solo preview, waveform, and extraction. Mute regions silence only that track, preserving timing, and apply to solo/mixed preview, edited extraction and export. Edited tracks offer a small Original track / Edited track popup beside Extract. Original extraction preserves the source audio. Selected audio collapses to one track by default. Audio rows scroll independently when the window is small; keyboard focus reveals rows.

Export copies eligible video/audio streams. Exact temporal edits, crops, mixing, and audio mutes encode the affected streams. A mute alone keeps video eligible for copy; with collapse off, unaffected audio tracks remain eligible for copy independently. Settings holds cut policy, force encode, CPU/GPU profiles, keyboard bindings, and diagnostics. GPU profiles require a successful trial encode. Fast copy snaps H.264 cuts and verifies IDR starts and decoded joins; the export sheet shows effective ranges. Compression defaults to 20 MB and uses CPU H.264 two pass, the script's scaling / 30 fps / decimal MB choices, a 3% reserve, and 128 kb/s for each remaining output audio track. Audio mixing averages selected tracks for headroom, consistently in preview and export.

Export asks only for a filename and saves beside the imported clip. Leave it blank and press Enter for `<stem>_edited.mp4`, `<stem>_compressed.mp4`, or the track extraction default. Existing files still require replacement confirmation. A persistent status strip shows exporting/progress, completion, cancellation, or errors; successful exports have an Open folder button.

Edits are lost on exit. Unfinished edits trigger a warning; preferences and shortcut bindings persist. Export stages temporary files beside the destination and verifies the result before replacing an existing output. Leave enough disk space for intermediate encoded video and PCM audio.

## Run

Linux: extract the tar and run `./clipcut` or `./clipcut.sh`, optionally followed by a recording path. Either can be symlinked into a directory on PATH; the executable resolves its real location and loads its private libraries/plugins. The bundle includes its tested Qt/GStreamer pair and media tools; glibc, compositor, graphics drivers, and sound service come from the host. It targets current Artix x64, rather than claiming compatibility with every Linux distribution. Wayland is the initial platform; native X11 remains a future milestone.

Windows: extract the ZIP and run `clipcut.exe`. Drop a recording onto the executable or its shortcut to open it, or run `clipcut.exe "C:\Videos\recording.mkv"`. Keep the runtime folders and DLLs together. FFmpeg and ffprobe are included. The app uses D3D11 for Qt and video preview, with software decoding when hardware decoding is unavailable.

## Appearance

Settings → Appearance switches themes live. **Graphite · Alacritty / SwayFX** matches the local desktop palette: `#191a1c` backgrounds, `#f1f2f3` text, silver accents, JetBrains Mono, and 80% background opacity. **Dark Rose** preserves the original palette. Each theme remembers its own background and dialog/menu opacity; Reset opacity restores the preset defaults. Fonts fall back to the platform font when unavailable.

Transparency applies to background fills only. Text, icons, waveforms, and video retain their own opaque colors; whole-window opacity stays at 100%. Layered panels can appear denser than the empty window background. SwayFX supplies blur through the existing compositor configuration; ClipCut does not change that configuration. Windows uses a translucent frameless surface with move, resize, minimize/maximize, and close controls; drag the recording name to move it.

Import a JSON theme, save the current palette with **Save as…**, and use **Reload** after editing the imported copy in the theme directory shown in Appearance. See [themes/graphite.json](themes/graphite.json) for a complete template. Theme files use version 1, a name, optional `fontFamily`, `backgroundOpacity` and `popupOpacity` numbers from 0 to 1, and a `colors` object. Colors must be opaque `#RRGGBB`; missing roles inherit Dark Rose defaults. Invalid imports/reloads leave the current theme intact. Imported themes and preferences persist across exits.

Command line: `./clipcut.sh --theme graphite`, `--theme dark-rose`, or `--theme /path/to/theme.json`. Use `clipcut.exe` on Windows.

## Keyboard

- Space: play/pause; Left/Right: step a frame; Shift+Left/Right: seek five seconds.
- I / O: trim start/end at the playhead; [ / ]: mark delete start/end; Ctrl+D: add a delete.
- Alt+Left/Right: select a delete; Delete: remove the selected delete or mute.
- Ctrl+Shift+M: add a mute at the playhead on the active audio track; Ctrl+[ / Ctrl+]: mark its start/end. Soloing, scrubbing, focusing a track control, or selecting a mute activates that track.
- Ctrl+Z / Ctrl+Shift+Z: undo/redo; Ctrl+R: reset crop; Ctrl+Shift+R: reset temporal edits.
- 1–9: solo audio; Ctrl+1–9: include/exclude; M: preview the selected mix.
- + / − / F: zoom in/out/fit; Ctrl+mouse wheel over a timeline also zooms.
- Alt+Shift+Up/Down: enlarge the preview/audio area; the divider above playback controls also drags.
- Ctrl+O / Ctrl+E / Ctrl+comma: Open / Export / Settings; Ctrl+K: searchable command palette.
- Tab reaches controls and crop/timeline handles. Focused edges use arrows; Shift moves ten frames or ten crop pixels. Text fields and dialogs suppress editing shortcuts.

In file browsers, `/` or Ctrl+F focuses the live filename filter. Search is case insensitive and applies within the current folder. Tab completes a matching filename or folder; repeated Tab cycles matches and Shift+Tab cycles backward. Editing resets the matches. Empty or unmatched searches keep normal Tab focus navigation. Ctrl+L edits the location; arrows and Enter navigate/select; Alt+Up opens the parent. Escape clears a filter, then closes the browser. `/` remains literal text while editing paths or filenames. Output naming still confirms replacement of an existing file.

Bindings can be changed in Settings; conflicting combinations are rejected. Empty disables a binding.

## Profile and test

`./clipcut.sh recording.mkv --profile performance.jsonl`

`clipcut.exe recording.mkv --profile performance.jsonl`

Settings → Diagnostics can start/stop logging. JSONL records startup versions, encoder probes, pipeline caps/decoders, delivered video buffers, sink rendered/dropped counters, UI synchronization intervals, process CPU and RSS, seek completion, and FFmpeg stages/timings. Buffer delivery and seek completion are explicitly separate from compositor presentation; idle or hidden-window gaps are not playback frame times.

Smoke flags: `--smoke-seconds 20`, `--exercise-preview`, `--exercise-audio`, `--screenshot image.png`, `--exercise-editor output-directory`, `--exercise-files directory`, `--exercise-theme output-directory`, `--exercise-crop output-directory`, and `--exercise-mutes output-directory`. Exercise-editor sends real Qt keyboard/pointer events through the UI and exports the edited result. Exercise-crop checks precise corner/edge dragging with a 1920×1080 recording at two window sizes. Exercise-mutes checks Ctrl-drag creation, track-local editing, undo/redo, preview and export with a multi-track recording. `mute_test` verifies sample boundaries, GStreamer seek segments, stream copying, actual exported silence/mixing, extraction and compression. Exercise-theme checks live controls and rendered background/text/video alpha. `theme_test` validates theme files, preferences, and reload behavior. Legacy core/media test targets are available when their source files are present. See [TESTING.md](TESTING.md) for the actual measurements and limitations.

## Build

Install CMake, Ninja, a C++20 compiler, Qt 6.8+ Core/Gui/Quick/QuickControls2/Concurrent/Test, GStreamer 1.24+ development packages, FFmpeg, and ffprobe. Build the Qt sink against the exact Qt shipped with the application.

```
./packaging/build-linux-deps.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j4
ctest --test-dir build --output-on-failure
python packaging/package-linux.py
```

Windows cross-build in this workspace:

```
python packaging/fetch_msys.py
./packaging/fetch-windows-gst-source.sh
PKG_CONFIG_LIBDIR="$PWD/.deps/windows/mingw64/lib/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$PWD/.deps/windows/mingw64" cmake -S . -B build-windows -G Ninja -DCMAKE_TOOLCHAIN_FILE=packaging/mingw-toolchain.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCLIPCUT_GST_BAD_SOURCE="$PWD/.deps/sources/gst-plugins-bad-1.28.8"
cmake --build build-windows -j4
python packaging/package-windows.py
```

The Windows dependency lock records package versions and SHA-256 digests. The QML D3D11 sink is built from matching GStreamer source. Packagers must provide dependency notices and corresponding source as required by their licenses; the included FFmpeg build enables GPL components. This workspace's packages are testing artifacts.
