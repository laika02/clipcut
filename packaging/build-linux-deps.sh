#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=$(pkg-config --modversion gstreamer-1.0)
cache="$root/.deps/sources"
mkdir -p "$cache" "$root/.deps/gstreamer"
for project in gst-plugins-good gst-libav; do
  archive="$cache/$project-$version.tar.xz"
  if [[ ! -f "$archive" ]]; then curl -fL "https://gstreamer.freedesktop.org/src/$project/$project-$version.tar.xz" -o "$archive"; fi
  if [[ ! -d "$cache/$project-$version" ]]; then tar -xJf "$archive" -C "$cache"; fi
  args=(-Dtests=disabled -Ddoc=disabled)
  if [[ "$project" == gst-plugins-good ]]; then args+=(-Dauto_features=disabled -Dqt6=enabled -Dqt-wayland=enabled -Dqt-egl=enabled -Dqt-x11=disabled -Dexamples=disabled -Dnls=disabled); fi
  if [[ ! -f "$cache/$project-build/build.ninja" ]]; then meson setup "$cache/$project-build" "$cache/$project-$version" "${args[@]}"; fi
  meson compile -C "$cache/$project-build" -j "${CLIPCUT_JOBS:-4}"
  if [[ "$project" == gst-plugins-good ]]; then cp "$cache/$project-build/ext/qt6/libgstqml6.so" "$root/.deps/gstreamer/";
  else cp "$cache/$project-build/ext/libav/libgstlibav.so" "$root/.deps/gstreamer/"; fi
done
