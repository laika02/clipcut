# ClipCut C Rewrite

ClipCut is a native SDL2 + FFmpeg video clipping tool focused on fast timeline edits, crop selection, audio-track control, and simple exports without a heavyweight project format.

This directory is the standalone C rewrite and should be treated as the repository root.

## Current Features

- SDL2 desktop UI with a dark, compact editor layout.
- FFmpeg-backed media probe, preview decode, audio playback, and export.
- Open media from a native file picker, drag/drop, or command-line path.
- Trim start/end by dragging timeline handles.
- Move the current trim window with Shift-drag on the timeline.
- Snap trim start/end to scrubber with `J` / `K`.
- Reset trim start/end with `Ctrl+J` / `Ctrl+K`.
- Remove timeline ranges with Ctrl-drag; undo the last cut action with `Ctrl+Z`.
- Crop by dragging preview handles.
- Toggle, preview, scrub, and extract individual audio tracks.
- Collapse enabled audio tracks into one mix, or preserve enabled tracks separately.
- Export profiles for CPU, Intel, and NVIDIA hardware paths.
- Automatic stream copy for simple exports where no video filtering is required.
- Non-overwriting export and extraction filenames.

## Controls

| Input | Action |
| --- | --- |
| `Space` | Play / pause |
| `Left` / `Right` | Seek |
| Timeline click / drag scrubber | Move playhead |
| Drag trim edge | Adjust trim start/end |
| Shift-drag timeline | Move trim selection while preserving duration |
| Ctrl-drag timeline | Mark a range to cut from export/playback |
| `Ctrl+Z` | Undo the last cut operation |
| `J` / `K` | Snap trim start/end to playhead |
| `Ctrl+J` / `Ctrl+K` | Reset trim start/end |
| `M` | Toggle audio collapse/preserve mode |
| `P` | Cycle export profile |
| `E` | Export |

## Export Profiles

- `CPU FAST`: `libx264`, veryfast preset, CRF 23.
- `CPU BAL`: `libx264`, faster preset, CRF 20.
- `INTEL VAAPI`: Linux Intel hardware encode path.
- `INTEL QSV`: Intel Quick Sync path, useful on Windows.
- `NVIDIA NVENC`: NVIDIA hardware encode path, useful for systems such as an RTX 3050 Ti laptop.

Stream copy is used automatically when practical: no cuts, full-frame crop, and no required audio mix. If the edit requires crop/cuts/audio filtering, ClipCut falls back to the selected encode profile.

## Repository Layout

```text
C_Rewrite/
  CMakeLists.txt
  docs/
  include/
  src/
    app/
    export/
    media/
    model/
    platform/
    playback/
    ui/
  tests/
  tools/
```

## Quick Start

Linux:

```bash
cmake -S . -B build
cmake --build build
./build/clipcut
```

Windows 10:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_windows.ps1 -VcpkgRoot C:\vcpkg
.\build-win\Release\clipcut.exe
```

See [INSTALL.md](INSTALL.md) for full dependency and platform setup.

## Status

This is still an active rewrite. The main editing flow works, but expect rough edges in UI polish, packaging, and hardware-encoder compatibility across machines.
