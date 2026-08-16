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
    "Docs",
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
    "Docs/AgentX_099/axv099Readme.txt",
    "Docs/AkimboArena/AkimboArena.txt",
    "Docs/AkimboArena/akimboarena.umod",
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
    "System/AgentX.int",
    "System/AgentX.u",
    "System/AkimboArena.int",
    "System/AkimboArena.u",
    "System/ChaosUT.u",
    "System/Default.ini",
    "System/DefUser.ini",
    "System/HaloMasterChief.u",
    "System/HaloMasterChief.int",
    "System/HaloMasterChiefSkins.utx",
    "System/JailBreak.u",
    "System/JBMadmen.ini",
    "System/OLweapons.u",
    "System/RandomRelic.ini",
    "System/RocketArena.u",
    "System/UnrealTournament.ini",
    "System/User.ini",
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
UNREAL_PACKAGE_SUFFIXES = (
    ".u",
    ".uax",
    ".umx",
    ".utx",
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


def validate_build_mirror(build_root, package_root):
    """Prove that every selected canonical-build file reached the package."""
    source_files = []
    for name in RUNTIME_ROOT_FILES:
        source_files.append((name, os.path.join(build_root, name)))
    for name in RUNTIME_DIRS:
        source_root = os.path.join(build_root, name)
        if not os.path.isdir(source_root):
            continue
        for source in iter_files(source_root):
            relative = os.path.relpath(source, build_root).replace(os.sep, "/")
            source_files.append((relative, source))

    missing = []
    mismatched = []
    for relative, source in source_files:
        target = os.path.join(package_root, relative.replace("/", os.sep))
        if not os.path.isfile(target):
            missing.append(relative)
        elif (
            os.path.getsize(source) != os.path.getsize(target)
            or sha256_file(source) != sha256_file(target)
        ):
            mismatched.append(relative)
    if missing:
        fail("Canonical build files omitted from package: " + ", ".join(missing))
    if mismatched:
        fail("Packaged build files differ from source: " + ", ".join(mismatched))
    return len(source_files)


def validate_config_packages(package_root):
    """Resolve network-critical configured packages to shipped Unreal files."""
    available = {}
    for path in iter_files(package_root):
        base, suffix = os.path.splitext(os.path.basename(path))
        if suffix.lower() in UNREAL_PACKAGE_SUFFIXES:
            available.setdefault(base.lower(), []).append(path)

    config_path = os.path.join(package_root, "System", "Default.ini")
    configured = []
    with open(config_path, "r") as stream:
        for raw_line in stream:
            line = raw_line.strip()
            if not line or line.startswith((";", "#")) or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip().lower()
            value = value.strip()
            if key == "serverpackages":
                configured.append(value)
            elif key == "serveractors":
                configured.append(value.split(".", 1)[0])

    unresolved = sorted(
        set(name for name in configured if name and name.lower() not in available),
        key=str.lower,
    )
    if unresolved:
        fail("Configured Unreal packages are not shipped: " + ", ".join(unresolved))
    return len(set(name.lower() for name in configured if name))


def validate_package(package_root, version):
    missing = []
    required_files = REQUIRED_FILES + (
        "Docs/INSTALLATION_{}.txt".format(version),
        "Docs/RELEASE_NOTES_{}.txt".format(version),
    )
    for relative in required_files:
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

    empty = []
    for path in iter_files(package_root):
        if os.path.getsize(path) == 0:
            empty.append(os.path.relpath(path, package_root).replace(os.sep, "/"))
    if empty:
        fail("Zero-byte release files are present: " + ", ".join(empty))

    relative_paths = [
        os.path.relpath(path, package_root).replace(os.sep, "/")
        for path in iter_files(package_root)
    ]
    casefolded_paths = {}
    for relative in relative_paths:
        folded = relative.lower()
        if folded in casefolded_paths:
            fail(
                "Case-colliding release paths: {} and {}".format(
                    casefolded_paths[folded],
                    relative,
                )
            )
        casefolded_paths[folded] = relative

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

    ini_pairs = (
        ("Default.ini", "UnrealTournament.ini"),
        ("DefUser.ini", "User.ini"),
    )
    for template_name, runtime_name in ini_pairs:
        template_path = os.path.join(package_root, "System", template_name)
        runtime_path = os.path.join(package_root, "System", runtime_name)
        if sha256_file(template_path) != sha256_file(runtime_path):
            fail(
                "Fresh runtime ini does not match its template: {} != {}".format(
                    runtime_name,
                    template_name,
                )
            )

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


def validate_manifest(package_root):
    manifest_relative = "Docs/FILE_MANIFEST_SHA256.txt"
    manifest_path = os.path.join(
        package_root,
        manifest_relative.replace("/", os.sep),
    )
    expected = {}
    for path in iter_files(package_root):
        relative = os.path.relpath(path, package_root).replace(os.sep, "/")
        if relative == manifest_relative:
            continue
        expected[relative] = path

    recorded = {}
    with open(manifest_path, "r") as stream:
        header = stream.readline().rstrip("\r\n")
        if header != "SHA256  BYTES  PATH":
            fail("Invalid release manifest header")
        for line_number, raw_line in enumerate(stream, 2):
            fields = raw_line.rstrip("\r\n").split(None, 2)
            if len(fields) != 3:
                fail("Invalid release manifest row {}".format(line_number))
            digest, size_text, relative = fields
            if relative in recorded:
                fail("Duplicate release manifest path: " + relative)
            try:
                size = int(size_text)
            except ValueError:
                fail("Invalid release manifest size for " + relative)
            recorded[relative] = (digest.upper(), size)

    if set(recorded) != set(expected):
        missing = sorted(set(expected) - set(recorded))
        extra = sorted(set(recorded) - set(expected))
        fail(
            "Release manifest coverage mismatch; missing={} extra={}".format(
                ", ".join(missing),
                ", ".join(extra),
            )
        )

    verified = {}
    for relative, path in expected.items():
        actual_size = os.path.getsize(path)
        actual_digest = sha256_file(path)
        expected_digest, expected_size = recorded[relative]
        if actual_size != expected_size or actual_digest != expected_digest:
            fail("Release manifest mismatch for " + relative)
        verified[relative] = actual_digest
    return verified


def validate_archive(package_root, archive_path, payload_hashes):
    prefix = os.path.basename(package_root) + "/"
    manifest_relative = "Docs/FILE_MANIFEST_SHA256.txt"
    expected = dict(payload_hashes)
    expected[manifest_relative] = sha256_file(
        os.path.join(package_root, manifest_relative.replace("/", os.sep))
    )

    with zipfile.ZipFile(archive_path, "r") as archive:
        infos = [info for info in archive.infolist() if not info.filename.endswith("/")]
        archived = {}
        casefolded = {}
        for info in infos:
            if not info.filename.startswith(prefix):
                fail("Archive entry is outside the release root: " + info.filename)
            relative = info.filename[len(prefix):]
            if (
                not relative
                or "\\" in relative
                or relative.startswith("/")
                or ".." in relative.split("/")
            ):
                fail("Unsafe archive entry path: " + info.filename)
            if relative in archived:
                fail("Duplicate archive entry: " + info.filename)
            folded = relative.lower()
            if folded in casefolded:
                fail(
                    "Case-colliding archive entries: {} and {}".format(
                        casefolded[folded],
                        relative,
                    )
                )
            archived[relative] = info
            casefolded[folded] = relative

        if set(archived) != set(expected):
            missing = sorted(set(expected) - set(archived))
            extra = sorted(set(archived) - set(expected))
            fail(
                "Archive payload mismatch; missing={} extra={}".format(
                    ", ".join(missing),
                    ", ".join(extra),
                )
            )

        for relative, expected_digest in expected.items():
            digest = hashlib.sha256()
            with archive.open(archived[relative], "r") as stream:
                while True:
                    chunk = stream.read(1024 * 1024)
                    if not chunk:
                        break
                    digest.update(chunk)
            if digest.hexdigest().upper() != expected_digest:
                fail("Archive content mismatch for " + relative)
    return len(expected)


def validate_fresh_extraction(archive_path, version):
    """Extract the final ZIP and repeat package/manifest checks from disk."""
    temp_root = tempfile.mkdtemp(prefix="ut99-release-extract-audit-")
    try:
        with zipfile.ZipFile(archive_path, "r") as archive:
            corrupt = archive.testzip()
            if corrupt is not None:
                fail("ZIP CRC failure: " + corrupt)
            archive.extractall(temp_root)
        extracted_root = os.path.join(
            temp_root,
            "UT99-Xbox-" + version,
        )
        ensure_dir(extracted_root, "freshly extracted release root")
        validate_package(extracted_root, version)
        validate_config_packages(extracted_root)
        return len(validate_manifest(extracted_root)) + 1
    finally:
        shutil.rmtree(temp_root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default="1.1.9b")
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
    if not os.path.isdir(docs_root):
        os.makedirs(docs_root)
    for name in (
        "INSTALLATION_{}.txt".format(args.version),
        "RELEASE_NOTES_{}.txt".format(args.version),
    ):
        source = os.path.join(XBOX_DIR, "Docs", name)
        ensure_file(source, "release documentation")
        shutil.copy2(source, os.path.join(docs_root, name))

    copy_console_maps(rc1_root, package_root, docs_root)
    copy_jailbreak_docs(args.jailbreak_archive, docs_root)
    mirrored_count = validate_build_mirror(build_root, package_root)
    map_count = validate_package(package_root, args.version)
    configured_package_count = validate_config_packages(package_root)
    manifest_count = write_manifest(package_root)
    payload_hashes = validate_manifest(package_root)
    write_zip(package_root, archive_path)
    archive_count = validate_archive(package_root, archive_path, payload_hashes)
    extracted_count = validate_fresh_extraction(archive_path, args.version)

    print("Release package: " + package_root)
    print("Mirrored canonical-build files: {}".format(mirrored_count))
    print("Bundled maps: {}".format(map_count))
    print("Resolved configured packages: {}".format(configured_package_count))
    print("Manifest entries: {}".format(manifest_count))
    print("Verified archive files: {}".format(archive_count))
    print("Fresh-extraction verified files: {}".format(extracted_count))
    print("Archive: " + archive_path)
    print("Archive bytes: {}".format(os.path.getsize(archive_path)))
    print("Archive SHA256: " + sha256_file(archive_path))


if __name__ == "__main__":
    main()
