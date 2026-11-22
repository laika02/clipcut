#!/bin/sh
# Test-only input injector for the isolated 1920x1080 Sway compositor.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-"$root/build"}
mkdir -p "$out"
wayland-scanner client-header "$root/tests/wlr-virtual-pointer-unstable-v1.xml" "$out/virtual-pointer.h"
wayland-scanner private-code "$root/tests/wlr-virtual-pointer-unstable-v1.xml" "$out/virtual-pointer.c"
cc -Wall -Wextra -I"$out" "$root/tests/wayland-pointer.c" "$out/virtual-pointer.c" -o "$out/wayland-pointer" $(pkg-config --cflags --libs wayland-client)
