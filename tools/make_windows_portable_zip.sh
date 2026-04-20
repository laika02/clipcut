#!/usr/bin/env bash
set -euo pipefail

build_dir="${BUILD_DIR:-build-win-cross-release}"
triplet="${VCPKG_TARGET_TRIPLET:-x64-mingw-dynamic}"
vcpkg_root="${VCPKG_ROOT:-$HOME/vcpkg}"
mingw_bin="${MINGW_BIN:-/usr/x86_64-w64-mingw32/bin}"
package_dir="${PACKAGE_DIR:-dist/windows}"
portable_dir="${PORTABLE_DIR:-dist/clipcut-windows-portable}"
zip_path="${ZIP_PATH:-dist/clipcut-windows-portable.zip}"
ffmpeg_asset="${FFMPEG_ASSET:-ffmpeg-n8.1-latest-win64-lgpl-8.1.zip}"
ffmpeg_root="${ffmpeg_asset%.zip}"

rm -rf "$portable_dir"
mkdir -p "$portable_dir"

"$(dirname "$0")/package_windows_cross.sh"
cp -f "$package_dir"/* "$portable_dir"/

tmpdir="$(mktemp -d)"
cleanup() {
    rm -rf "$tmpdir"
}
trap cleanup EXIT

gh release download latest -R BtbN/FFmpeg-Builds -p "$ffmpeg_asset" -D "$tmpdir"
unzip -j "$tmpdir/$ffmpeg_asset" "$ffmpeg_root/bin/ffmpeg.exe" -d "$portable_dir" >/dev/null
unzip -j "$tmpdir/$ffmpeg_asset" "$ffmpeg_root/LICENSE.txt" -d "$portable_dir" >/dev/null
mv "$portable_dir/LICENSE.txt" "$portable_dir/FFMPEG_LICENSE.txt"

rm -f "$zip_path"
(cd "$(dirname "$portable_dir")" && zip -qr "$(basename "$zip_path")" "$(basename "$portable_dir")")

echo "Wrote $zip_path"
