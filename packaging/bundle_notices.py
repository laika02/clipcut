"""Copy local prototype docs/source and missing generic runtime license texts."""
from pathlib import Path
import shutil

def bundle_notices(root,dest):
    shutil.copytree(root/'themes',dest/'themes',dirs_exist_ok=True)
    for name in ['README.md','TESTING.md','PLAN.md','CMakeLists.txt']:
        shutil.copy2(root/name,dest/name)
    shutil.copy2(root/'packaging/THIRD-PARTY.md',dest/'THIRD-PARTY.md')
    for name in ['src','qml','tests','scripts','packaging','themes','resources']:
        # Omit generated test recordings, while retaining the src/media code.
        ignored=['__pycache__']+(['media'] if name=='tests' else [])
        shutil.copytree(root/name,dest/'source'/name,dirs_exist_ok=True,ignore=shutil.ignore_patterns(*ignored))
    for name in ['CMakeLists.txt','compress-video.sh']:
        shutil.copy2(root/name,dest/'source'/name)
    common=dest/'licenses/common';common.mkdir(parents=True,exist_ok=True)
    for name,alternative in [('GPL-3.0-only.txt','GPL3'),('LGPL-3.0-only.txt','LGPL3'),('LGPL-2.1-or-later.txt','LGPL2.1')]:
        candidates=[root/'.deps/windows/mingw64/share/licenses/qt6-base'/name,Path('/usr/share/licenses/common')/alternative]
        for path in candidates:
            if path.is_file():shutil.copy2(path,common/name);break
        else:raise RuntimeError('Missing license text: '+name)
