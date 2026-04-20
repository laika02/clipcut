# Building

The main build and install guide now lives at [../INSTALL.md](../INSTALL.md).

Quick Linux build:

```bash
cmake -S . -B build
cmake --build build
./build/clipcut
```

Quick Windows build:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_windows.ps1 -VcpkgRoot C:\vcpkg
.\build-win\Release\clipcut.exe
```
