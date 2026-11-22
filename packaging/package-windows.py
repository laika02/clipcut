#!/usr/bin/env python3
"""Assemble a portable Windows runtime and its PE dependency closure."""
import pathlib, shutil, subprocess, re, zipfile
from bundle_notices import bundle_notices
root = pathlib.Path(__file__).resolve().parents[1]
prefix = root / '.deps/windows/mingw64'
dest = root / 'dist/ClipCut-0.1-windows-x64'
dest.mkdir(parents=True, exist_ok=True)
def copy(src, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, target)
for name in ['clipcut.exe', 'theme_test.exe', 'mute_test.exe']:
    copy(root / 'build-windows' / name, dest / name)
for name in ['ffmpeg.exe', 'ffprobe.exe']:
    copy(prefix / 'bin' / name, dest / name)
for plugin in ['coreelements','playback','matroska','isomp4','audioconvert','audioresample','audiomixer','volume','typefindfunctions','libav','d3d11','wasapi2','autodetect']:
    copy(prefix / 'lib/gstreamer-1.0' / ('libgst'+plugin+'.dll'), dest / 'gstreamer' / ('libgst'+plugin+'.dll'))
copy(root / 'build-windows/libgstqt6d3d11.dll', dest / 'gstreamer/libgstqt6d3d11.dll')
copy(prefix / 'libexec/gstreamer-1.0/gst-plugin-scanner.exe', dest / 'gst-plugin-scanner.exe')
copy(prefix / 'share/qt6/plugins/platforms/qwindows.dll', dest / 'plugins/platforms/qwindows.dll')
for name in ['QtQuick', 'QtQml', 'Qt']:
    src = prefix / 'share/qt6/qml' / name
    if src.exists(): shutil.copytree(src, dest / 'qml' / name, dirs_exist_ok=True)
(dest / 'qt.conf').write_text('[Paths]\nPlugins=plugins\nQmlImports=qml\n')
providers = {p.name.lower():p for p in (prefix / 'bin').glob('*.dll')}
seen = set()
while True:
    pending = [p for p in dest.rglob('*') if p.suffix.lower() in ['.exe','.dll'] and p not in seen]
    if not pending: break
    for p in pending:
        seen.add(p)
        info = subprocess.check_output(['x86_64-w64-mingw32-objdump','-p',str(p)],text=True)
        for dll in re.findall(r'DLL Name: (\S+)',info):
            src = providers.get(dll.lower())
            if src and not (dest / src.name).exists(): copy(src, dest / src.name)
licenses = prefix / 'share/licenses'
if licenses.exists(): shutil.copytree(licenses, dest / 'licenses', dirs_exist_ok=True)
copy(root / 'packaging/windows-dependencies.lock.json', dest / 'dependencies.lock.json')
bundle_notices(root,dest)
zip_path = pathlib.Path(str(dest)+'.zip')
with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED,compresslevel=5) as archive:
    for p in dest.rglob('*'):
        if p.is_file(): archive.write(p,p.relative_to(dest.parent))
print(zip_path)
