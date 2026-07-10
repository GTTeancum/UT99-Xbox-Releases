#!/usr/bin/env python
"""Stage Jailbreak III Gold into a UT99 Xbox runtime directory."""

from __future__ import print_function

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
XBOX_DIR = os.path.dirname(SCRIPT_DIR)

ARCHIVE_NAME = "JailbreakIII-Gold-zip.7z"
EXPECTED_SHA1 = "a2f7cb0ff0454a51a516fe76efd732c0cdffafc6"
DOWNLOAD_URLS = [
    "https://unreal-archive-files-na.s3.ca-east-tor.io.cloud.ovh.net/gametypes/Unreal%20Tournament/jailbreak-iii/" + ARCHIVE_NAME,
    "https://unreal-archive-files-eu.s3.de.io.cloud.ovh.net/gametypes/Unreal%20Tournament/jailbreak-iii/" + ARCHIVE_NAME,
]

GAMEPLAY_DIRS = ("System", "Maps", "Textures", "Sounds")
OPTIONAL_DIRS = ("Help", "Web")
RUNTIME_EXTENSIONS = {
    "System": (".u", ".int", ".ini"),
    "Maps": (".unr",),
    "Textures": (".utx",),
    "Sounds": (".uax",),
}
SERVER_PACKAGES = ("JailBreak", "JBArena", "JBSpecials", "Screen", "JailFight")
EDITOR_PACKAGES = ("JailBreak", "JBArena", "JBSpecials", "Screen")
REQUIRED_FILES = (
    "System/JailBreak.u",
    "System/JailBreak.int",
    "System/JBArena.u",
    "System/JBSpecials.u",
    "System/Screen.u",
    "Maps/JB-Raid-Gold.unr",
    "Textures/ScriptedScreen.utx",
    "Sounds/JBAudio.uax",
)


def fail(message):
    print("ERROR: " + message)
    sys.exit(1)


def norm(path):
    return os.path.normpath(os.path.abspath(path))


def ensure_dir(path):
    if not os.path.isdir(path):
        os.makedirs(path)


def sha1_file(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def download_file(url, dst):
    import urllib.request

    ensure_dir(os.path.dirname(dst))
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=120) as response:
        with open(dst, "wb") as out:
            shutil.copyfileobj(response, out)


def ensure_archive(cache_dir, archive_override=None):
    if archive_override:
        archive = norm(archive_override)
        if not os.path.isfile(archive):
            fail("Jailbreak archive not found: " + archive)
    else:
        ensure_dir(cache_dir)
        archive = os.path.join(cache_dir, ARCHIVE_NAME)
        if not os.path.isfile(archive):
            last_error = None
            for url in DOWNLOAD_URLS:
                try:
                    print("Downloading Jailbreak III Gold from " + url)
                    download_file(url, archive)
                    last_error = None
                    break
                except Exception as exc:
                    last_error = exc
                    if os.path.isfile(archive):
                        os.remove(archive)
                    print("Download failed: " + str(exc))
            if last_error is not None:
                fail("Could not download Jailbreak archive")

    digest = sha1_file(archive)
    if digest.lower() != EXPECTED_SHA1:
        fail("Jailbreak archive SHA1 mismatch: {} != {}".format(digest, EXPECTED_SHA1))
    return archive


def find_tar():
    tar = shutil.which("tar")
    if not tar:
        fail("tar.exe not found on PATH; Windows bsdtar is required to extract the .7z archive")
    return tar


def extract_archive(archive, out_dir):
    tar = find_tar()
    ensure_dir(out_dir)
    code = subprocess.call([tar, "-xf", archive, "-C", out_dir])
    if code != 0:
        fail("Failed to extract Jailbreak archive")


def copy_tree(src_root, dst_root, folder, allowed_extensions=None):
    src_dir = os.path.join(src_root, folder)
    if not os.path.isdir(src_dir):
        return 0

    copied = 0
    for current, dirs, files in os.walk(src_dir):
        dirs.sort()
        files.sort()
        rel_dir = os.path.relpath(current, src_root)
        dst_dir = os.path.join(dst_root, rel_dir)
        ensure_dir(dst_dir)
        for name in files:
            if allowed_extensions is not None:
                ext = os.path.splitext(name)[1].lower()
                if ext not in allowed_extensions:
                    continue
            src = os.path.join(current, name)
            dst = os.path.join(dst_dir, name)
            shutil.copy2(src, dst)
            copied += 1
    return copied


def read_lines(path):
    with open(path, "r") as f:
        return f.read().splitlines()


def write_lines(path, lines):
    with open(path, "w") as f:
        f.write("\n".join(lines))
        f.write("\n")


def find_section_bounds(lines, section_name):
    wanted = "[" + section_name.lower() + "]"
    start = None
    for i, line in enumerate(lines):
        if line.strip().lower() == wanted:
            start = i
            break
    if start is None:
        lines.append("")
        lines.append("[" + section_name + "]")
        return len(lines) - 1, len(lines)

    end = len(lines)
    for i in range(start + 1, len(lines)):
        stripped = lines[i].strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            end = i
            break
    return start, end


def ensure_repeated_values(lines, section_name, key, values):
    start, end = find_section_bounds(lines, section_name)
    existing = set()
    lower_key = key.lower()
    for i in range(start + 1, end):
        line = lines[i].strip()
        if not line or line.startswith(";") or "=" not in line:
            continue
        lhs, rhs = line.split("=", 1)
        if lhs.strip().lower() == lower_key:
            existing.add(rhs.strip().lower())

    insert_at = end
    changed = False
    for value in values:
        if value.lower() in existing:
            continue
        lines.insert(insert_at, key + "=" + value)
        insert_at += 1
        end += 1
        changed = True
    return changed


def patch_ini(path, editor_packages=False):
    if not os.path.isfile(path):
        return False

    lines = read_lines(path)
    changed = ensure_repeated_values(lines, "Engine.GameEngine", "ServerPackages", SERVER_PACKAGES)
    if editor_packages:
        changed = ensure_repeated_values(lines, "Editor.EditorEngine", "EditPackages", EDITOR_PACKAGES) or changed
    if changed:
        write_lines(path, lines)
    return changed


def validate_stage(dst_root):
    missing = []
    for rel in REQUIRED_FILES:
        if not os.path.isfile(os.path.join(dst_root, rel.replace("/", os.sep))):
            missing.append(rel)
    if missing:
        fail("Jailbreak stage is missing required files: " + ", ".join(missing))


def stage_jailbreak(target, archive=None, cache_dir=None, include_docs=False, editor_packages=False):
    target = norm(target)
    if cache_dir is None:
        cache_dir = os.path.join(XBOX_DIR, "build_cli", "downloads")
    cache_dir = norm(cache_dir)

    archive_path = ensure_archive(cache_dir, archive)
    temp_root = tempfile.mkdtemp(prefix="ut99-jailbreak-")
    try:
        extract_archive(archive_path, temp_root)
        folders = list(GAMEPLAY_DIRS)
        if include_docs:
            folders.extend(OPTIONAL_DIRS)

        copied = 0
        ensure_dir(target)
        for folder in folders:
            copied += copy_tree(temp_root, target, folder, RUNTIME_EXTENSIONS.get(folder))

        validate_stage(target)

        system_dir = os.path.join(target, "System")
        patched = []
        for ini_name in ("Default.ini", "UnrealTournament.ini"):
            ini = os.path.join(system_dir, ini_name)
            if patch_ini(ini, editor_packages=editor_packages):
                patched.append(ini_name)

        print("Staged Jailbreak III Gold to " + target)
        print("Copied {} files from {}".format(copied, ", ".join(folders)))
        if patched:
            print("Patched config: " + ", ".join(patched))
        else:
            print("Runtime config already had Jailbreak package entries, or no config was present")
    finally:
        shutil.rmtree(temp_root)


def main():
    parser = argparse.ArgumentParser(description="Stage Jailbreak III Gold into a UT99 Xbox runtime directory.")
    parser.add_argument("--target", required=True, help="Runtime root containing System, Maps, Textures, and Sounds.")
    parser.add_argument("--archive", default=None, help="Use an existing JailbreakIII-Gold-zip.7z instead of downloading.")
    parser.add_argument(
        "--cache-dir",
        default=None,
        help="Download cache directory. Defaults to UT99-Xbox\\build_cli\\downloads.",
    )
    parser.add_argument("--include-docs", action="store_true", help="Also stage Help and Web files.")
    parser.add_argument("--editor-packages", action="store_true", help="Also add editor EditPackages lines.")
    args = parser.parse_args()

    stage_jailbreak(
        target=args.target,
        archive=args.archive,
        cache_dir=args.cache_dir,
        include_docs=args.include_docs,
        editor_packages=args.editor_packages,
    )


if __name__ == "__main__":
    main()
