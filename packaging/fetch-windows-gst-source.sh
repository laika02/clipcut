#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=$(sed -n 's/^Version: //p' "$root/.deps/windows/mingw64/lib/pkgconfig/gstreamer-1.0.pc" | tr -d '\r')
cache="$root/.deps/sources"
mkdir -p "$cache"
archive="$cache/gst-plugins-bad-$version.tar.xz"
if [[ ! -f "$archive" ]]; then curl -fL "https://gstreamer.freedesktop.org/src/gst-plugins-bad/gst-plugins-bad-$version.tar.xz" -o "$archive"; fi
if [[ ! -d "$cache/gst-plugins-bad-$version" ]]; then tar -xJf "$archive" -C "$cache"; fi
printf '%s\n' "$cache/gst-plugins-bad-$version"
