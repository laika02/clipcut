#!/usr/bin/env bash
set -euo pipefail

build_dir="${BUILD_DIR:-build-win-cross-release}"
triplet="${VCPKG_TARGET_TRIPLET:-x64-mingw-dynamic}"
vcpkg_root="${VCPKG_ROOT:-$HOME/vcpkg}"
package_dir="${PACKAGE_DIR:-dist/windows}"
mingw_bin="${MINGW_BIN:-/usr/x86_64-w64-mingw32/bin}"

mkdir -p "$package_dir"
cp "$build_dir/clipcut.exe" "$package_dir/"
shopt -s nullglob
for dll in "$vcpkg_root/installed/$triplet/bin"/*.dll; do
    cp "$dll" "$package_dir/"
done
for dll in "$mingw_bin"/libwinpthread-1.dll "$mingw_bin"/libgcc_s_seh-1.dll "$mingw_bin"/libstdc++-6.dll; do
    if [[ -f "$dll" ]]; then
        cp "$dll" "$package_dir/"
    fi
done
for exe in "$vcpkg_root/installed/$triplet/bin"/*.exe; do
    cp "$exe" "$package_dir/"
done
shopt -u nullglob

echo "Packaged into $package_dir"
