#!/usr/bin/env python3
"""Bundle the current tested Qt/GStreamer pair for Artix x64; keep host drivers."""
import pathlib,shutil,subprocess,re,tarfile
from bundle_notices import bundle_notices
root=pathlib.Path(__file__).resolve().parents[1]
dest=root/'dist/ClipCut-0.1-linux-x64'
dest.mkdir(parents=True,exist_ok=True)
def copy(src,target):
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,target)
for name in ['clipcut','theme_test','mute_test']:
    copy(root/'build'/name,dest/name)
for name in ['ffmpeg','ffprobe']:
    copy(pathlib.Path(shutil.which(name)),dest/name)
for plugin in ['coreelements','playback','matroska','isomp4','audioconvert','audioresample','audiomixer','volume','typefindfunctions','opengl','va','pulseaudio','alsa','pipewire','autodetect','videoconvertscale']:
    src=pathlib.Path('/usr/lib/gstreamer-1.0')/('libgst'+plugin+'.so')
    if src.exists():copy(src,dest/'gstreamer'/src.name)
for name in ['libgstqml6.so','libgstlibav.so']:
    copy(root/'.deps/gstreamer'/name,dest/'gstreamer'/name)
copy(pathlib.Path('/usr/lib/gstreamer-1.0/gst-plugin-scanner'),dest/'gst-plugin-scanner')
plugins=pathlib.Path('/usr/lib/qt6/plugins')
for directory in ['platforms','wayland-graphics-integration-client','wayland-decoration-client','wayland-shell-integration','imageformats']:
    if (plugins/directory).exists():
        for src in (plugins/directory).glob('*.so'):
            if directory=='platforms' and not src.name.startswith('libqwayland'):continue
            copy(src,dest/'plugins'/directory/src.name)
for name in ['QtQuick','QtQml','Qt']:
    src=pathlib.Path('/usr/lib/qt6/qml')/name
    if src.exists():shutil.copytree(src,dest/'qml'/name,dirs_exist_ok=True)
(dest/'qt.conf').write_text('[Paths]\nPlugins=plugins\nQmlImports=qml\n')
seen=set()
while True:
    pending=[p for p in dest.rglob('*') if p.is_file() and p not in seen and ('.so' in p.name or p.name in ['clipcut','theme_test','mute_test','ffmpeg','ffprobe','gst-plugin-scanner'])]
    if not pending:break
    for p in pending:
        seen.add(p)
        info=subprocess.run(['ldd',str(p)],text=True,capture_output=True).stdout
        for path in re.findall(r'=> (/\S+) ',info):
            src=pathlib.Path(path)
            if src.name.startswith(('libc.so','libm.so','libpthread.so','libdl.so','librt.so','libGLX_mesa','libEGL_mesa','libvulkan_')):continue
            target=dest/'lib'/src.name
            if not target.exists():copy(src,target)
launcher=dest/'clipcut.sh'
launcher.write_text('''#!/bin/sh
set -eu
CLIPCUT_LAUNCHER=$(readlink -f -- "$0")
CLIPCUT_DIR=$(CDPATH= cd -- "$(dirname -- "$CLIPCUT_LAUNCHER")" && pwd)
export LD_LIBRARY_PATH="$CLIPCUT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland}"
export GST_PLUGIN_SCANNER="$CLIPCUT_DIR/gst-plugin-scanner"
exec "$CLIPCUT_DIR/clipcut" "$@"
''');launcher.chmod(0o755)
licenses=pathlib.Path('/usr/share/licenses')
for name in ['qt6-base','qt6-declarative','qt6-wayland','gstreamer','gst-plugins-base','gst-plugins-good','gst-plugins-bad','gst-libav','ffmpeg']:
    if (licenses/name).exists():shutil.copytree(licenses/name,dest/'licenses'/name,dirs_exist_ok=True)
bundle_notices(root,dest)
archive=pathlib.Path(str(dest)+'.tar.gz')
with tarfile.open(archive,'w:gz') as tar:tar.add(dest,arcname=dest.name)
print(archive)
