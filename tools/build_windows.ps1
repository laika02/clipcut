param(
    [string]$VcpkgRoot = "C:\vcpkg",
    [string]$BuildDir = "build-win",
    [string]$Triplet = "x64-windows",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$Toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $Toolchain)) {
    throw "vcpkg toolchain not found at $Toolchain"
}

cmake -S . -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="$Toolchain" `
    -DVCPKG_TARGET_TRIPLET="$Triplet"

cmake --build $BuildDir --config $Config

Write-Host "Built $BuildDir\$Config\clipcut.exe"
