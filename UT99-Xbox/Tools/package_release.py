#!/usr/bin/env python
"""Assemble and validate a complete UT99 Xbox public release archive."""

from __future__ import print_function

import argparse
import hashlib
import os
import shutil
import tempfile
import zipfile

import stage_jailbreak


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
XBOX_DIR = os.path.dirname(SCRIPT_DIR)
ROOT_DIR = os.path.dirname(XBOX_DIR)

RUNTIME_DIRS = (
    "Maps",
    "MenuAssets",
    "Music",
    "MusicXbox",
    "Sounds",
    "System",
    "Textures",
)
RUNTIME_ROOT_FILES = (
    "CONTENT_CREDITS.txt",
    "default.xbe",
    "SaveImage.xbx",
    "TitleImage.xbx",
    "TitleMeta.xbx",
)
REQUIRED_FILES = (
    "default.xbe",
    "TitleImage.xbx",
    "SaveImage.xbx",
    "TitleMeta.xbx",
    "CONTENT_CREDITS.txt",
    "Docs/INSTALLATION_1.1.txt",
    "Docs/RELEASE_NOTES_1.1.txt",
    "Docs/Console_Map_Pack_README.txt",
    "Docs/JailbreakIII/JailbreakIII-Gold.txt",
    "Maps/CTF-Titania.unr",
    "Maps/DM-HangEmHigh.unr",
    "Maps/DM-Halo-Derelict.unr",
    "Maps/JB-Alcatraz.unr",
    "Music/5-2SA1.umx",
    "Music/halochant.umx",
    "Sounds/JBAudio.uax",
    "System/AdvancedModelSupport.u",
    "System/ChaosUT.u",
    "System/HaloMasterChief.u",
    "System/HaloMasterChief.int",
    "System/HaloMasterChiefSkins.utx",
    "System/JailBreak.u",
    "System/OLweapons.u",
    "System/RocketArena.u",
    "System/UTPS2Characters.u",
    "Textures/ScriptedScreen.utx",
)
RETIRED_FILES = (
    "System/BossVoiceFix313.u",
    "System/epiccustommodels.u",
    "System/SkeletalCharsFix313.u",
    "System/UTPS2Baked.u",
)
FORBIDDEN_SUFFIXES = (
    ".exp",
    ".ilk",
    ".lib",
    ".log",
    ".map",
    ".obj",
    ".pdb",
    ".tmp",
)


def fail(message):
    raise RuntimeError(message)


def ensure_file(path, label):
    if not os.path.isfile(path):
        fail("Missing {}: {}".format(label, path))


def ensure_dir(path, label):
    if not os.path.isdir(path):
        fail("Missing {}: {}".format(label, path))


def copy_tree(source, target):
    if os.path.isdir(target):
        shutil.rmtree(target)
    shutil.copytree(source, target)


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest().upper()


def parse_console_maps(readme):
    names = []
    with open(readme, "r") as stream:
        for raw_line in stream:
            line = raw_line.strip()
            if " | " not in line:
                continue
            name = line.split("|", 1)[0].strip()
            if name.startswith(("CTF-", "DM-", "DOM-")):
                names.append(name + ".unr")
    if len(names) != 39 or len(set(name.lower() for name in names)) != 39:
        fail("Expected 39 unique console maps in {}, found {}".format(readme, len(names)))
    return names


def copy_console_maps(rc1_root, package_root, docs_root):
    readme = os.path.join(rc1_root, "Docs", "Console_Map_Pack_README.txt")
    ensure_file(readme, "console map readme")
    shutil.copy2(readme, os.path.join(docs_root, "Console_Map_Pack_README.txt"))

    maps_dst = os.path.join(package_root, "Maps")
    if not os.path.isdir(maps_dst):
        os.makedirs(maps_dst)
    for name in parse_console_maps(readme):
        source = os.path.join(rc1_root, "Maps", name)
        ensure_file(source, "console map")
        shutil.copy2(source, os.path.join(maps_dst, name))


def copy_jailbreak_docs(archive, docs_root):
    archive = stage_jailbreak.ensure_archive(
        os.path.join(XBOX_DIR, "build_cli", "downloads"),
        archive,
    )
    temp_root = tempfile.mkdtemp(prefix="ut99-release-jailbreak-")
    try:
        stage_jailbreak.extract_archive(archive, temp_root)
        help_root = os.path.join(temp_root, "Help")
        ensure_dir(help_root, "Jailbreak Help directory")
        copy_tree(help_root, os.path.join(docs_root, "JailbreakIII"))
    finally:
        shutil.rmtree(temp_root)


def iter_files(root):
    for current, dirs, files in os.walk(root):
        dirs.sort()
        files.sort()
        for name in files:
            yield os.path.join(current, name)


def validate_package(package_root):
    missing = []
    for relative in REQUIRED_FILES:
        path = os.path.join(package_root, relative.replace("/", os.sep))
        if not os.path.isfile(path):
            missing.append(relative)
    if missing:
        fail("Release package is missing required files: " + ", ".join(missing))

    present_retired = []
    for relative in RETIRED_FILES:
        path = os.path.join(package_root, relative.replace("/", os.sep))
        if os.path.isfile(path):
            present_retired.append(relative)
    if present_retired:
        fail("Retired assets are present: " + ", ".join(present_retired))

    forbidden = []
    for path in iter_files(package_root):
        relative = os.path.relpath(path, package_root).replace(os.sep, "/")
        if relative.lower().endswith(FORBIDDEN_SUFFIXES):
            forbidden.append(relative)
    if forbidden:
        fail("Development artifacts are present: " + ", ".join(forbidden))

    for name in ("lib", "obj", "Help", "Web"):
        if os.path.exists(os.path.join(package_root, name)):
            fail("Forbidden release-root directory is present: " + name)

    map_count = len(
        [
            path
            for path in iter_files(os.path.join(package_root, "Maps"))
            if path.lower().endswith(".unr")
        ]
    )
    if map_count != 54:
        fail("Expected 54 bundled maps, found {}".format(map_count))

    default_ini = os.path.join(package_root, "System", "Default.ini")
    with open(default_ini, "r") as stream:
        config = stream.read().lower()
    for package in ("jailbreak", "jbarena", "jbspecials", "screen", "jailfight"):
        if "serverpackages=" + package not in config:
            fail("Default.ini is missing ServerPackages={}".format(package))

    return map_count


def write_manifest(package_root):
    manifest = os.path.join(package_root, "Docs", "FILE_MANIFEST_SHA256.txt")
    rows = []
    for path in iter_files(package_root):
        if os.path.normcase(path) == os.path.normcase(manifest):
            continue
        relative = os.path.relpath(path, package_root).replace(os.sep, "/")
        rows.append((relative, os.path.getsize(path), sha256_file(path)))
    with open(manifest, "w") as stream:
        stream.write("SHA256  BYTES  PATH\n")
        for relative, size, digest in rows:
            stream.write("{}  {}  {}\n".format(digest, size, relative))
    return len(rows)


def write_zip(package_root, archive_path):
    if os.path.isfile(archive_path):
        os.remove(archive_path)
    prefix = os.path.basename(package_root)
    with zipfile.ZipFile(
        archive_path,
        "w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
        allowZip64=True,
    ) as archive:
        for path in iter_files(package_root):
            relative = os.path.relpath(path, package_root).replace(os.sep, "/")
            archive.write(path, prefix + "/" + relative)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default="1.1")
    parser.add_argument("--build", default=os.path.join(ROOT_DIR, "build"))
    parser.add_argument("--rc1-root", required=True)
    parser.add_argument("--jailbreak-archive", required=True)
    parser.add_argument("--out-dir", required=True)
    args = parser.parse_args()

    build_root = os.path.abspath(args.build)
    rc1_root = os.path.abspath(args.rc1_root)
    out_dir = os.path.abspath(args.out_dir)
    package_name = "UT99-Xbox-" + args.version
    package_root = os.path.join(out_dir, package_name)
    archive_path = os.path.join(out_dir, package_name + ".zip")

    ensure_dir(build_root, "canonical build")
    ensure_dir(rc1_root, "RC1 extraction")
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)
    if os.path.isdir(package_root):
        shutil.rmtree(package_root)
    os.makedirs(package_root)

    for name in RUNTIME_ROOT_FILES:
        source = os.path.join(build_root, name)
        ensure_file(source, "runtime root file")
        shutil.copy2(source, os.path.join(package_root, name))
    for name in RUNTIME_DIRS:
        source = os.path.join(build_root, name)
        if os.path.isdir(source):
            copy_tree(source, os.path.join(package_root, name))

    docs_root = os.path.join(package_root, "Docs")
    os.makedirs(docs_root)
    for name in ("INSTALLATION_1.1.txt", "RELEASE_NOTES_1.1.txt"):
        source = os.path.join(XBOX_DIR, "Docs", name)
        ensure_file(source, "release documentation")
        shutil.copy2(source, os.path.join(docs_root, name))

    copy_console_maps(rc1_root, package_root, docs_root)
    copy_jailbreak_docs(args.jailbreak_archive, docs_root)
    map_count = validate_package(package_root)
    manifest_count = write_manifest(package_root)
    write_zip(package_root, archive_path)

    print("Release package: " + package_root)
    print("Bundled maps: {}".format(map_count))
    print("Manifest entries: {}".format(manifest_count))
    print("Archive: " + archive_path)
    print("Archive bytes: {}".format(os.path.getsize(archive_path)))
    print("Archive SHA256: " + sha256_file(archive_path))


if __name__ == "__main__":
    main()
