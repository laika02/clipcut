#!/usr/bin/env python3
"""Fetch a reproducible, project-local MinGW dependency prefix; never installs globally."""
import concurrent.futures
import hashlib
import io
import json
from pathlib import Path
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
BASE = "https://repo.msys2.org/mingw/mingw64/"
CACHE = ROOT / ".deps/downloads"
PREFIX = ROOT / ".deps/windows"
CACHE.mkdir(parents=True, exist_ok=True)
PREFIX.mkdir(parents=True, exist_ok=True)

def fetch(url, dest):
    if not dest.exists():
        temp = dest.with_suffix(dest.suffix + ".partial")
        with urllib.request.urlopen(url, timeout=90) as response, temp.open("wb") as output:
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
        temp.replace(dest)
    return dest

lock_path = ROOT / "packaging/windows-dependencies.lock.json"
if lock_path.exists():
    pinned = json.loads(lock_path.read_text())
    packages = {name: {"FILENAME": [data["file"]], "SHA256SUM": [data["sha256"]]} for name, data in pinned.items()}
    selected = set(packages)
else:
    db = fetch(BASE + "mingw64.db", CACHE / "mingw64.db")
    packages = {}
    with tarfile.open(db) as archive:
        for entry in archive:
            if entry.name.endswith("/desc"):
                fields = archive.extractfile(entry).read().decode().split("\n\n")
                fields = {block.splitlines()[0].strip("%"): block.splitlines()[1:] for block in fields if block.strip()}
                packages[fields["NAME"][0]] = fields
    
    names = ["qt6-base", "qt6-declarative", "qt6-shadertools", "gstreamer", "gst-plugins-base", "gst-plugins-good", "gst-plugins-bad", "gst-libav", "ffmpeg"]
    selected = set()
    def select(name):
        name = name.split(">=")[0].split("<=")[0].split("=")[0].split(">")[0].split("<")[0]
        if name in selected: return
        if name not in packages:
            matches = [p for p, data in packages.items() if name in [v.split("=")[0] for v in data.get("PROVIDES", [])]]
            if not matches: raise RuntimeError("Unresolved dependency: " + name)
            name = matches[0]
        selected.add(name)
        for dependency in packages[name].get("DEPENDS", []): select(dependency)
    for name in names: select("mingw-w64-x86_64-" + name)
print(f"Fetching {len(selected)} packages into {PREFIX}", flush=True)

def download(name):
    data = packages[name]
    filename = data["FILENAME"][0]
    path = fetch(BASE + filename, CACHE / filename)
    if hashlib.sha256(path.read_bytes()).hexdigest() != data["SHA256SUM"][0]:
        raise RuntimeError("Checksum mismatch: " + filename)
    print(filename, flush=True)
    return name, path

lock = {}
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    for name, path in pool.map(download, sorted(selected)):
        with tarfile.open(path) as archive:
            # Extract only the dependency prefix; pacman metadata is not installed.
            entries = [e for e in archive if e.name.startswith("mingw64/") and ".." not in Path(e.name).parts]
            archive.extractall(PREFIX, members=entries, filter="data")
        lock[name] = {"file": path.name, "sha256": packages[name]["SHA256SUM"][0]}
(ROOT / "packaging/windows-dependencies.lock.json").write_text(json.dumps(lock, indent=2) + "\n")
print("Dependency prefix ready", flush=True)
