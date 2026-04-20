# Cross-Compile Windows From Linux

This path builds a Windows 10 executable from Linux using MinGW-w64, CMake, Ninja, and vcpkg-provided Windows libraries.

## Required Tools

On Arch Linux:

```bash
sudo pacman -S --needed \
  mingw-w64-gcc \
  mingw-w64-binutils \
  mingw-w64-headers \
  mingw-w64-crt \
  mingw-w64-winpthreads \
  cmake ninja wine
```

Install vcpkg:

```bash
git clone https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

Install Windows-target dependencies:

```bash
~/vcpkg/vcpkg install sdl2 ffmpeg --triplet x64-mingw-dynamic
```

## Build

From this repository root:

```bash
VCPKG_ROOT=$HOME/vcpkg ./tools/build_windows_cross.sh
```

The expected output is:

```text
build-win-cross-release/clipcut.exe
```

## Run With Wine

The dynamic vcpkg triplet requires DLLs from the vcpkg install directory, plus the MinGW runtime DLLs from `/usr/x86_64-w64-mingw32/bin`. One quick test path:

```bash
export WINEPATH="$HOME/vcpkg/installed/x64-mingw-dynamic/bin"
wine build-win-cross-release/clipcut.exe
```

For export jobs, `ffmpeg.exe` must also be available to the Windows process. Use either:

```bash
export WINEPATH="$HOME/vcpkg/installed/x64-mingw-dynamic/bin;$WINEPATH"
```

or copy a Windows `ffmpeg.exe` next to `clipcut.exe`.

## Packaging Notes

For a portable folder, use:

```bash
./tools/package_windows_cross.sh
```

That stages:

- `build-win-cross-release/clipcut.exe`
- SDL2 DLLs from `~/vcpkg/installed/x64-mingw-dynamic/bin`
- FFmpeg DLLs from `~/vcpkg/installed/x64-mingw-dynamic/bin`
- MinGW runtime DLLs from `/usr/x86_64-w64-mingw32/bin`
- FFmpeg tools from `~/vcpkg/installed/x64-mingw-dynamic/bin`

Static linking may be possible with `x64-mingw-static`, but dynamic is the simpler first target because FFmpeg static dependency closure can be large and brittle.
