# Windows 10 Build

This project builds on Windows 10 with CMake plus vcpkg-managed SDL2 and FFmpeg.

## Prerequisites

- Visual Studio 2022 Build Tools with the C++ desktop workload
- CMake 3.20 or newer
- Git
- vcpkg
- FFmpeg CLI available on `PATH` at runtime for exports

## Install Dependencies

From a Developer PowerShell:

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install sdl2 ffmpeg --triplet x64-windows
```

## Configure And Build

From this `C_Rewrite` directory:

```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake

cmake --build build-win --config Release
```

Or use the repo script:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_windows.ps1 -VcpkgRoot C:\vcpkg
```

The app binary will be under:

```text
build-win\Release\clipcut.exe
```

## Runtime Notes

- Keep `ffmpeg.exe` on `PATH`; the app uses FFmpeg libraries internally but export/extract jobs launch the FFmpeg CLI.
- CPU export profiles work anywhere FFmpeg can encode H.264.
- `NVIDIA NVENC` is the expected hardware profile for an NVIDIA 3050 Ti machine.
- `INTEL QSV` is the Windows Intel hardware path.
- `INTEL VAAPI` is Linux-only and is skipped by the Windows GUI profile cycle.

If Windows cannot find SDL2 or FFmpeg DLLs at launch, copy the required DLLs next to `clipcut.exe` or run from a shell where vcpkg's installed binary directory is on `PATH`.
