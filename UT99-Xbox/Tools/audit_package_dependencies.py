"""Validate class/mesh package dependencies in the shipped System directory.

PC texture, sound and music packages are supplied by the player. Classes and
character meshes, including indirect dependencies, must ship with the port.
"""
import argparse
from pathlib import Path
import struct

import convert_seekfree_console_maps as package


def required_imports(path):
    data = Path(path).read_bytes()
    header = struct.unpack_from("<9I", data)
    if header[0] != 0x9E2A83C1:
        raise ValueError("Not an Unreal package: " + str(path))
    names, _ = package.read_names(data, header[3], header[4])
    imports, _ = package.read_imports(data, header[7], header[8])
    result = set()
    for row in imports:
        if names[row[3]].lower() not in ("class", "mesh", "lodmesh", "skeletalmesh"):
            continue
        root = row
        visited = set()
        while root[4] < 0:
            index = -root[4] - 1
            if index >= len(imports) or index in visited:
                raise ValueError("Invalid import outer in " + str(path))
            visited.add(index)
            root = imports[index]
        if root[4] != 0 or names[root[3]].lower() != "package":
            raise ValueError("Invalid package root in " + str(path))
        result.add(names[root[5]])
    return result


def validate_system_dependencies(system_dir):
    files = {p.stem.lower(): p for p in Path(system_dir).iterdir()
             if p.is_file() and p.suffix.lower() == ".u"}
    missing = []
    for path in sorted(files.values()):
        for dependency in sorted(required_imports(path)):
            if dependency.lower() not in files:
                missing.append("{} -> {}.u".format(path.name, dependency))
    if missing:
        raise ValueError("Missing class/mesh dependencies: " + "; ".join(missing))
    return len(files)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("system_dir")
    args = parser.parse_args()
    print("Validated dependencies of {} packages".format(
        validate_system_dependencies(args.system_dir)))
