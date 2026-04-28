#!/usr/bin/env python
"""Command-line Xbox build for the UT99 port.

This mirrors the existing VS2005 Release Xbox projects but calls the XDK 5849
VC71 tools directly, so Codex can compile without launching Visual Studio.
"""

from __future__ import print_function

import argparse
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
XBOX_DIR = os.path.dirname(SCRIPT_DIR)
ROOT_DIR = os.path.dirname(XBOX_DIR)

XDK_DIR = r"C:\XDK"
XDK_BIN = os.path.join(XDK_DIR, "xbox", "bin")
XDK_VC71 = os.path.join(XDK_BIN, "vc71")
CL = os.path.join(XDK_VC71, "CL.Exe")
LIB = os.path.join(XDK_VC71, "Lib.Exe")
LINK = os.path.join(XDK_VC71, "Link.Exe")


PROJECTS = [
    ("UT99Core", os.path.join(ROOT_DIR, "Core", "UT99Core.vcproj"), "lib"),
    ("UT99Engine", os.path.join(ROOT_DIR, "Engine", "UT99Engine.vcproj"), "lib"),
    ("UT99Render", os.path.join(ROOT_DIR, "Render", "UT99Render.vcproj"), "lib"),
    ("XboxDrv", os.path.join(XBOX_DIR, "XboxDrv", "XboxDrv.vcproj"), "lib"),
    ("XboxRender", os.path.join(XBOX_DIR, "XboxRender", "XboxRender.vcproj"), "lib"),
    ("XboxStubs", os.path.join(XBOX_DIR, "XboxStubs", "XboxStubs.vcproj"), "lib"),
    ("XboxLaunch", os.path.join(XBOX_DIR, "XboxLaunch", "XboxLaunch.vcproj"), "exe"),
]


def fail(message):
    print("ERROR: " + message)
    sys.exit(1)


def check_tools():
    for path in (CL, LIB, LINK, os.path.join(XDK_BIN, "imagebld.exe")):
        if not os.path.isfile(path):
            fail("Missing required XDK tool: " + path)


def split_semicolon(value):
    if not value:
        return []
    return [item for item in value.split(";") if item]


def split_options(value):
    if not value:
        return []
    # Project options are simple slash switches today. Keep this deliberately
    # small rather than trying to emulate cmd.exe quoting.
    result = []
    for item in value.split():
        # XDK VC71 predates /GS, so /GS- is parsed as an unknown /G switch and
        # produces noisy D4002 warnings. The XDK compiler has no stack cookie
        # instrumentation to disable, so dropping it is equivalent here.
        if item.upper() == "/GS-":
            continue
        result.append(item)
    return result


def expand_macros(value, project_dir, out_dir=None, int_dir=None):
    if value is None:
        return ""
    result = value
    solution_dir = XBOX_DIR + os.sep
    replacements = {
        "$(SolutionDir)": solution_dir,
        "$(ProjectDir)": project_dir + os.sep,
        "$(ConfigurationName)": "Release",
    }
    if out_dir:
        replacements["$(OutDir)"] = out_dir
    if int_dir:
        replacements["$(IntDir)"] = int_dir
    for key, replacement in replacements.items():
        result = result.replace(key, replacement)
    return result


def normalize_path(path, base_dir):
    path = path.replace("/", os.sep)
    if os.path.isabs(path):
        return os.path.normpath(path)
    return os.path.normpath(os.path.join(base_dir, path))


def find_config(root, config_name):
    wanted = config_name + "|Win32"
    for config in root.findall("./Configurations/Configuration"):
        if config.get("Name") == wanted:
            return config
    fail("Configuration not found: " + wanted)


def find_tool(config, tool_name):
    for tool in config.findall("./Tool"):
        if tool.get("Name") == tool_name:
            return tool
    return None


def iter_source_files(root):
    for node in root.iter("File"):
        rel = node.get("RelativePath")
        if not rel:
            continue
        ext = os.path.splitext(rel)[1].lower()
        if ext in (".cpp", ".c", ".cc", ".cxx"):
            yield node, rel


def file_extra_options(file_node, config_name):
    wanted = config_name + "|Win32"
    for file_config in file_node.findall("./FileConfiguration"):
        if file_config.get("Name") != wanted:
            continue
        tool = find_tool(file_config, "VCCLCompilerTool")
        if tool is not None:
            return split_options(tool.get("AdditionalOptions"))
    return []


def common_cl_flags(project_dir, cl_tool, out_dir, int_dir):
    flags = ["/nologo", "/c", "/TP"]

    if cl_tool.get("Optimization") == "0":
        flags.append("/Od")
    else:
        flags.append("/O2")

    includes = split_semicolon(cl_tool.get("AdditionalIncludeDirectories"))
    for include in includes:
        expanded = expand_macros(include, project_dir, out_dir, int_dir)
        flags.append('/I' + normalize_path(expanded, project_dir))

    for define in split_semicolon(cl_tool.get("PreprocessorDefinitions")):
        flags.append("/D" + define)

    warning = cl_tool.get("WarningLevel")
    if warning:
        flags.append("/W" + warning)

    forced = split_semicolon(cl_tool.get("ForcedIncludeFiles"))
    for item in forced:
        expanded = expand_macros(item, project_dir, out_dir, int_dir)
        # VS passes relative forced includes through include search paths.
        # Resolving CoreXboxCompat.h against the project root would point at
        # Core\CoreXboxCompat.h instead of Core\Inc\CoreXboxCompat.h.
        if os.path.isabs(expanded):
            flags.append("/FI" + os.path.normpath(expanded))
        else:
            flags.append("/FI" + expanded)

    for warning_id in split_semicolon(cl_tool.get("DisableSpecificWarnings")):
        flags.append("/wd" + warning_id)

    flags.extend(split_options(cl_tool.get("AdditionalOptions")))
    flags.append("/EHsc")
    flags.append("/MT")
    return flags


def run_command(args, cwd):
    print(" ".join(args))
    return subprocess.call(args, cwd=cwd)


def build_library(name, vcproj, config_name, build_root):
    project_dir = os.path.dirname(vcproj)
    tree = ET.parse(vcproj)
    root = tree.getroot()
    config = find_config(root, config_name)
    cl_tool = find_tool(config, "VCCLCompilerTool")
    if cl_tool is None:
        fail("No VCCLCompilerTool in " + vcproj)

    obj_dir = os.path.join(build_root, "obj", name)
    lib_dir = os.path.join(build_root, "lib")
    out_lib = os.path.join(lib_dir, name + ".lib")
    if name == "UT99Core":
        out_lib = os.path.join(lib_dir, "UT99Core.lib")
    elif name == "UT99Engine":
        out_lib = os.path.join(lib_dir, "UT99Engine.lib")
    elif name == "UT99Render":
        out_lib = os.path.join(lib_dir, "UT99Render.lib")

    if not os.path.isdir(obj_dir):
        os.makedirs(obj_dir)
    if not os.path.isdir(lib_dir):
        os.makedirs(lib_dir)

    base_flags = common_cl_flags(project_dir, cl_tool, lib_dir, obj_dir)
    objects = []

    print("")
    print("== Building " + name + " ==")
    for file_node, rel in iter_source_files(root):
        src = normalize_path(rel, project_dir)
        obj = os.path.join(obj_dir, os.path.splitext(os.path.basename(rel))[0] + ".obj")
        cmd = [CL] + base_flags + file_extra_options(file_node, config_name) + ["/Fo" + obj, src]
        if run_command(cmd, project_dir) != 0:
            fail("Compile failed: " + src)
        objects.append(obj)

    rsp = os.path.join(obj_dir, "lib.rsp")
    with open(rsp, "w") as f:
        f.write('/OUT:"{}"\n'.format(out_lib))
        for obj in objects:
            f.write('"{}"\n'.format(obj))

    if run_command([LIB, "@" + rsp], project_dir) != 0:
        fail("Library failed: " + out_lib)

    return out_lib


def build_launch(vcproj, config_name, build_root, built_libs):
    project_dir = os.path.dirname(vcproj)
    tree = ET.parse(vcproj)
    root = tree.getroot()
    config = find_config(root, config_name)
    cl_tool = find_tool(config, "VCCLCompilerTool")
    link_tool = find_tool(config, "VCLinkerTool")
    if cl_tool is None or link_tool is None:
        fail("Missing compile/link tool in " + vcproj)

    obj_dir = os.path.join(build_root, "obj", "XboxLaunch")
    out_exe = os.path.join(build_root, "UnrealTournament.exe")
    out_xbe = os.path.join(build_root, "default.xbe")
    if not os.path.isdir(obj_dir):
        os.makedirs(obj_dir)
    if not os.path.isdir(build_root):
        os.makedirs(build_root)

    base_flags = common_cl_flags(project_dir, cl_tool, build_root, obj_dir)
    objects = []

    print("")
    print("== Building XboxLaunch ==")
    for file_node, rel in iter_source_files(root):
        src = normalize_path(rel, project_dir)
        obj = os.path.join(obj_dir, os.path.splitext(os.path.basename(rel))[0] + ".obj")
        cmd = [CL] + base_flags + file_extra_options(file_node, config_name) + ["/Fo" + obj, src]
        if run_command(cmd, project_dir) != 0:
            fail("Compile failed: " + src)
        objects.append(obj)

    deps = [
        built_libs["XboxStubs"],
        built_libs["UT99Core"],
        built_libs["UT99Engine"],
        built_libs["UT99Render"],
        built_libs["XboxDrv"],
        built_libs["XboxRender"],
        "d3d8-xbox.lib",
        "xboxkrnl.lib",
        "xgraphics.lib",
        "xonline.lib",
        "xacteng.lib",
        "xnet.lib",
        "xapilib.lib",
        "s3tc.lib",
    ]

    rsp = os.path.join(obj_dir, "link.rsp")
    with open(rsp, "w") as f:
        f.write('/OUT:"{}"\n'.format(out_exe))
        f.write('/LIBPATH:"{}"\n'.format(os.path.join(XDK_DIR, "lib")))
        f.write('/LIBPATH:"{}"\n'.format(os.path.join(ROOT_DIR, "Engine", "Lib")))
        f.write('/SUBSYSTEM:CONSOLE\n')
        for opt in split_options(link_tool.get("AdditionalOptions")):
            f.write(opt + "\n")
        for dep in deps:
            f.write('"{}"\n'.format(dep))
        for obj in objects:
            f.write('"{}"\n'.format(obj))

    print("")
    print("== Linking UnrealTournament.exe ==")
    if run_command([LINK, "@" + rsp], project_dir) != 0:
        fail("Link failed: " + out_exe)

    print("")
    print("== Building default.xbe ==")
    patchxbe = os.path.join(SCRIPT_DIR, "patchxbe.py")
    if run_command([sys.executable, patchxbe, out_exe, out_xbe], XBOX_DIR) != 0:
        fail("XBE generation failed")

    return out_exe, out_xbe


def main():
    parser = argparse.ArgumentParser(description="Build UT99 Xbox from the command line.")
    parser.add_argument("--config", default="Release", choices=("Release", "Debug"))
    parser.add_argument("--clean", action="store_true")
    parser.add_argument(
        "--out-dir",
        default=None,
        help="Override the build output directory. Defaults to UT99-Xbox\\build_cli\\<config>.",
    )
    args = parser.parse_args()

    check_tools()

    build_root = args.out_dir
    if build_root:
        build_root = normalize_path(build_root, ROOT_DIR)
    else:
        build_root = os.path.join(XBOX_DIR, "build_cli", args.config.lower())
    if args.clean:
        if os.path.isdir(build_root):
            shutil.rmtree(build_root)
        print("Cleaned " + build_root)
        return

    if not os.path.isdir(build_root):
        os.makedirs(build_root)

    built_libs = {}
    for name, vcproj, kind in PROJECTS:
        if kind == "lib":
            built_libs[name] = build_library(name, vcproj, args.config, build_root)

    out_exe = out_xbe = None
    for name, vcproj, kind in PROJECTS:
        if kind == "exe":
            out_exe, out_xbe = build_launch(vcproj, args.config, build_root, built_libs)

    print("")
    print("BUILD SUCCEEDED")
    print("EXE: " + out_exe)
    print("XBE: " + out_xbe)


if __name__ == "__main__":
    main()
