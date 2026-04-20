# Install And Build Guide

## Runtime Requirements

ClipCut links against FFmpeg libraries for probe, preview, and playback. Export and audio extraction also launch the `ffmpeg` CLI, so `ffmpeg` must be available on `PATH` at runtime.

## Linux

### Arch Linux

```bash
sudo pacman -S --needed cmake pkgconf sdl2 ffmpeg
```

Build from this repository root:

```bash
cmake -S . -B build
cmake --build build
```

Run:

```bash
./build/clipcut
```

Open a file directly:

```bash
./build/clipcut /path/to/video.mp4
```

Optional file picker helpers:

```bash
sudo pacman -S --needed zenity
```

or:

```bash
sudo pacman -S --needed kdialog
```

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y cmake pkg-config libsdl2-dev \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
  ffmpeg zenity
```

Build:

```bash
cmake -S . -B build
cmake --build build
```

Run:

```bash
./build/clipcut
```

## Windows 10

The supported Windows path is CMake + Visual Studio Build Tools + vcpkg.

### Prerequisites

- Windows 10 x64
- Visual Studio 2022 Build Tools with the C++ desktop workload
- CMake 3.20 or newer
- Git
- vcpkg
- `ffmpeg.exe` on `PATH`

### Install Dependencies

From Developer PowerShell:

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install sdl2 ffmpeg --triplet x64-windows
```

### Build

From this repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_windows.ps1 -VcpkgRoot C:\vcpkg
```

Manual equivalent:

```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows

cmake --build build-win --config Release
```

Run:

```powershell
.\build-win\Release\clipcut.exe
```

If Windows cannot locate SDL2 or FFmpeg DLLs, either run from a shell with the vcpkg binary directory on `PATH`, or copy the required DLLs next to `clipcut.exe`.

## Smoke Checks

The build creates several small smoke-test binaries. Useful local checks:

```bash
./build/clipcut_transport_smoke
./build/clipcut_timeline_view_smoke
./build/clipcut_audio_tracks_view_smoke
./build/clipcut_ffmpeg_export_smoke
./build/clipcut_process_smoke
./build/clipcut_export_worker_smoke
```

Media-dependent checks require a local media file:

```bash
./build/clipcut_probe_smoke /path/to/video.mp4
./build/clipcut_preview_smoke /path/to/video.mp4
./build/clipcut_audio_preview_smoke /path/to/video.mp4
```

## Hardware Export Notes

Hardware profiles require FFmpeg support and matching drivers:

- Linux Intel: `INTEL VAAPI`, usually `/dev/dri/renderD128`.
- Windows Intel: `INTEL QSV`.
- Windows/Linux NVIDIA: `NVIDIA NVENC`, requires NVIDIA drivers and FFmpeg built with NVENC.

If a hardware profile fails, switch to `CPU FAST` or `CPU BAL`.
