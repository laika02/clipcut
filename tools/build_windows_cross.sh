#!/usr/bin/env bash
set -euo pipefail

build_dir="${BUILD_DIR:-build-win-cross}"
triplet="${VCPKG_TARGET_TRIPLET:-x64-mingw-dynamic}"
vcpkg_root="${VCPKG_ROOT:-$HOME/vcpkg}"
toolchain_file="${CMAKE_TOOLCHAIN_FILE:-$vcpkg_root/scripts/buildsystems/vcpkg.cmake}"
cross_file="${CLIPCUT_MINGW_TOOLCHAIN:-cmake/toolchains/mingw-w64-x86_64.cmake}"
bootstrap_vcpkg="${CLIPCUT_BOOTSTRAP_VCPKG:-1}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cross_file_abs="$(realpath "$script_dir/../${cross_file#./}")"

if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    echo "missing x86_64-w64-mingw32-gcc; install mingw-w64-gcc" >&2
    exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
    echo "missing ninja" >&2
    exit 1
fi

if [[ "$bootstrap_vcpkg" == "1" && "$toolchain_file" == "$vcpkg_root/scripts/buildsystems/vcpkg.cmake" ]]; then
    if [[ ! -d "$vcpkg_root/.git" ]]; then
        git clone https://github.com/microsoft/vcpkg "$vcpkg_root"
    fi
    if [[ ! -x "$vcpkg_root/vcpkg" ]]; then
        "$vcpkg_root/bootstrap-vcpkg.sh" -disableMetrics
    fi
fi

if [[ ! -f "$toolchain_file" ]]; then
    echo "missing vcpkg toolchain: $toolchain_file" >&2
    echo "set VCPKG_ROOT or CMAKE_TOOLCHAIN_FILE, then install: vcpkg install sdl2 ffmpeg --triplet $triplet" >&2
    exit 1
fi

if [[ "$toolchain_file" == "$vcpkg_root/scripts/buildsystems/vcpkg.cmake" ]]; then
    "$vcpkg_root/vcpkg" install "sdl2:$triplet" "ffmpeg:$triplet"
fi

cmake -S . -B "$build_dir" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain_file" \
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$cross_file_abs" \
    -DVCPKG_TARGET_TRIPLET="$triplet" \
    -DVCPKG_APPLOCAL_DEPS=OFF

cmake --build "$build_dir"

echo "Built $build_dir/clipcut.exe"
