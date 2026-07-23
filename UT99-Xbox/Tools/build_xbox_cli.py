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

import stage_jailbreak


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
XBOX_DIR = os.path.dirname(SCRIPT_DIR)
ROOT_DIR = os.path.dirname(XBOX_DIR)

# Active toolchain is XDK 5558 (CL/Lib/Link/imagebld). 5849 stays in the
# picture as a header + lib fallback for files 5558 lacks (e.g. s3tc.lib,
# stdint.h, winsock2.h). The "v1.0.5849" stamps in the final XBE come from
# patchxbe.py injecting library-version metadata into the XBE descriptor
# table — that's what the kernel checks, independent of the compiler used.
# OpenJKDF2 uses this exact layered setup; we adopt it here because 5849's
# d3d8 libs ship debug validators (DbgPrint+int3 on "Invalid flags passed
# to Clear") that 5558's d3d8.lib (2.1 MB full retail static) does not.
XDK_DIR          = r"C:\XDK_5558\XDK"
XDK_FALLBACK_DIR = r"C:\XDK"

XDK_BIN  = os.path.join(XDK_DIR, "xbox", "bin")
XDK_VC71 = os.path.join(XDK_BIN, "vc71")
CL   = os.path.join(XDK_VC71, "CL.Exe")
LIB  = os.path.join(XDK_VC71, "Lib.Exe")
LINK = os.path.join(XDK_VC71, "Link.Exe")


PROJECTS = [
    ("UT99Core", os.path.join(ROOT_DIR, "Core", "UT99Core.vcproj"), "lib"),
    ("UT99Engine", os.path.join(ROOT_DIR, "Engine", "UT99Engine.vcproj"), "lib"),
    ("UT99Render", os.path.join(ROOT_DIR, "Render", "UT99Render.vcproj"), "lib"),
    ("Fire", os.path.join(ROOT_DIR, "Fire", "Fire.vcproj"), "lib"),
    ("XboxDrv", os.path.join(XBOX_DIR, "XboxDrv", "XboxDrv.vcproj"), "lib"),
    ("XboxRender", os.path.join(XBOX_DIR, "XboxRender", "XboxRender.vcproj"), "lib"),
    ("XboxAudio", os.path.join(XBOX_DIR, "XboxAudio", "XboxAudio.vcproj"), "lib"),
    ("XboxStubs", os.path.join(XBOX_DIR, "XboxStubs", "XboxStubs.vcproj"), "lib"),
    ("XboxLaunch", os.path.join(XBOX_DIR, "XboxLaunch", "XboxLaunch.vcproj"), "exe"),
]

XMP_DIR = os.path.join(XBOX_DIR, "ThirdParty", "libxmp")
KNOWN_GOOD_SYSTEM_DIR = r"C:\Games\Emulators\CXBX\UT99x\System"
KNOWN_GOOD_SYSTEM_FILES = (
    "AdvancedModelSupport.int",
    "AdvancedModelSupport.u",
    "BossSkins.int",
    "Botpack.int",
    "Botpack.u",
    "CommandoSkins.int",
    "Core.int",
    "Core.u",
    "DamienPS2.int",
    "de.int",
    "de.u",
    "Editor.int",
    "Editor.u",
    "Engine.int",
    "Engine.u",
    "FCommandoSkins.int",
    "Fire.u",
    "HaloMasterChief.int",
    "HaloMasterChief.u",
    "HaloMasterChiefSkins.int",
    "HaloMasterChiefSkins.utx",
    "IpDrv.int",
    "IpDrv.u",
    "IpServer.int",
    "IpServer.u",
    "multimesh.int",
    "multimesh.u",
    "PS2WarMachineM.int",
    "QUAKE3c.utx",
    "relics.int",
    "relics.u",
    "relicsbindings.int",
    "relicsbindings.u",
    "SGirlSkins.int",
    "SkeletalChars.u",
    "SoldierSkins.int",
    "Startup.int",
    "tcowmeshskins.int",
    "tnalimeshskins.int",
    "tskmskins.int",
    "UBrowser.int",
    "UBrowser.u",
    "UMenu.int",
    "UMenu.u",
    "UnrealI.int",
    "UnrealI.u",
    "UnrealShare.int",
    "UnrealShare.u",
    "UnrealTournament.int",
    "UTBrowser.u",
    "UTDMT.u",
    "UTDMT.utx",
    "UTMenu.int",
    "UTMenu.u",
    "UTPS2Characters.int",
    "UTPS2Characters.u",
    "UTPS2CharactersSkins.int",
    "UTPS2CharactersSkins.utx",
    "UTServerAdmin.int",
    "UTServerAdmin.u",
    "UWeb.int",
    "UWeb.u",
    "UWindow.u",
)


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
    # 5558-first / 5849-fallback layering: anywhere a .vcproj references
    # the 5849 include dir (C:\XDK\xbox\include), substitute the 5558 one
    # in its place and append 5849 as a fallback. 5558 has Xbox-correct
    # D3D8 enums; 5849 covers files 5558 lacks (stdint.h, winsock2.h).
    expanded_5558 = os.path.join(XDK_DIR, "xbox", "include")
    expanded_5849 = os.path.join(XDK_FALLBACK_DIR, "xbox", "include")
    saw_xdk_include = False
    for include in includes:
        expanded = expand_macros(include, project_dir, out_dir, int_dir)
        if os.path.normcase(os.path.normpath(expanded)) == os.path.normcase(expanded_5849):
            flags.append('/I' + expanded_5558)
            saw_xdk_include = True
            continue
        flags.append('/I' + normalize_path(expanded, project_dir))
    if saw_xdk_include:
        flags.append('/I' + expanded_5849)

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
        safe_obj = rel.replace("\\", "_").replace("/", "_").replace(".", "_")
        obj = os.path.join(obj_dir, safe_obj + ".obj")
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


def iter_xmp_sources():
    makefile = os.path.join(XMP_DIR, "Makefile.vc")
    if not os.path.isfile(makefile):
        fail("Missing libxmp Makefile.vc: " + makefile)

    in_objs = False
    with open(makefile, "r") as f:
        for raw in f:
            line = raw.strip()
            if line.startswith("OBJS"):
                in_objs = True
                line = line.split("=", 1)[1].strip()
            elif in_objs and (not line or line.startswith("PROWIZ_OBJS")):
                break
            if not in_objs:
                continue
            line = line.rstrip("\\").strip()
            if not line:
                continue
            for item in line.split():
                if not item.lower().endswith(".obj"):
                    continue
                rel = item[:-4] + ".c"
                # UMX wraps ordinary tracker modules; the extra packer probes
                # are intentionally disabled for Xbox memory and startup cost.
                if "\\prowizard\\" in rel.lower() or "\\depackers\\" in rel.lower():
                    continue
                if rel.lower() in ("src\\filetype.c", "src\\tempfile.c", "src\\mkstemp.c", "src\\win32.c"):
                    continue
                yield rel
    yield "xbox_shims.c"


def build_xmp_library(build_root):
    name = "XboxXmp"
    obj_dir = os.path.join(build_root, "obj", name)
    lib_dir = os.path.join(build_root, "lib")
    out_lib = os.path.join(lib_dir, name + ".lib")

    if not os.path.isdir(obj_dir):
        os.makedirs(obj_dir)
    if not os.path.isdir(lib_dir):
        os.makedirs(lib_dir)

    include_5558 = os.path.join(XDK_DIR, "xbox", "include")
    include_5849 = os.path.join(XDK_FALLBACK_DIR, "xbox", "include")
    flags = [
        "/nologo", "/c", "/TC", "/O2", "/W2", "/MT",
        "/I" + include_5558,
        "/I" + include_5849,
        "/I" + os.path.join(XMP_DIR, "include"),
        "/I" + os.path.join(XMP_DIR, "src"),
        "/DWIN32",
        "/DNDEBUG",
        "/DTARGET_XBOX=1",
        "/DLIBXMP_STATIC",
        "/DLIBXMP_NO_PROWIZARD",
        "/DLIBXMP_NO_DEPACKERS",
        "/D_CRT_SECURE_NO_WARNINGS",
        "/D_CRT_NONSTDC_NO_WARNINGS",
        "/D_USE_MATH_DEFINES",
        "/wd4244", "/wd4267", "/wd4996", "/wd4018", "/wd4305",
    ]

    objects = []
    print("")
    print("== Building XboxXmp ==")
    for rel in iter_xmp_sources():
        src = os.path.join(XMP_DIR, rel)
        safe_obj = rel.replace("\\", "_").replace("/", "_").replace(".", "_")
        obj = os.path.join(obj_dir, safe_obj + ".obj")
        cmd = [CL] + flags + ["/Fo" + obj, src]
        if run_command(cmd, XMP_DIR) != 0:
            fail("Compile failed: " + src)
        objects.append(obj)

    rsp = os.path.join(obj_dir, "lib.rsp")
    with open(rsp, "w") as f:
        f.write('/OUT:"{}"\n'.format(out_lib))
        for obj in objects:
            f.write('"{}"\n'.format(obj))

    if run_command([LIB, "@" + rsp], XMP_DIR) != 0:
        fail("Library failed: " + out_lib)

    return out_lib


def build_ipdrv_library(build_root):
    name = "IpDrv"
    src_dir = os.path.join(ROOT_DIR, "IpDrv", "Src")
    obj_dir = os.path.join(build_root, "obj", name)
    lib_dir = os.path.join(build_root, "lib")
    out_lib = os.path.join(lib_dir, name + ".lib")

    if not os.path.isdir(obj_dir):
        os.makedirs(obj_dir)
    if not os.path.isdir(lib_dir):
        os.makedirs(lib_dir)

    include_5558 = os.path.join(XDK_DIR, "xbox", "include")
    include_5849 = os.path.join(XDK_FALLBACK_DIR, "xbox", "include")
    sources = [
        "IpDrv.cpp",
        "UnSocket.cpp",
        "InternetLink.cpp",
        "UdpLink.cpp",
        "TcpLink.cpp",
        "TcpNetDriver.cpp",
    ]
    flags = [
        "/nologo", "/c", "/TP", "/O2", "/W2", "/MT", "/EHsc",
        "/I" + include_5558,
        "/I" + include_5849,
        "/I" + os.path.join(ROOT_DIR, "Core", "Inc"),
        "/I" + os.path.join(ROOT_DIR, "Engine", "Inc"),
        "/I" + os.path.join(ROOT_DIR, "IpDrv", "Inc"),
        "/I" + src_dir,
        "/DWIN32",
        "/D_XBOX",
        "/DTARGET_XBOX=1",
        "/DASM=0",
        "/DASM3DNOW=0",
        "/DASMKNI=0",
        "/DIPDRV_API=",
        "/DGPackage=GPackage_IpDrv",
        "/FICoreXboxCompat.h",
        "/wd4005", "/wd4244", "/wd4267", "/wd4996", "/wd4018", "/wd4305",
    ]

    objects = []
    print("")
    print("== Building IpDrv ==")
    for rel in sources:
        src = os.path.join(src_dir, rel)
        obj = os.path.join(obj_dir, rel.replace(".", "_") + ".obj")
        cmd = [CL] + flags + ["/Fo" + obj, src]
        if run_command(cmd, src_dir) != 0:
            fail("Compile failed: " + src)
        objects.append(obj)

    rsp = os.path.join(obj_dir, "lib.rsp")
    with open(rsp, "w") as f:
        f.write('/OUT:"{}"\n'.format(out_lib))
        for obj in objects:
            f.write('"{}"\n'.format(obj))

    if run_command([LIB, "@" + rsp], src_dir) != 0:
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
        safe_obj = rel.replace("\\", "_").replace("/", "_").replace(".", "_")
        obj = os.path.join(obj_dir, safe_obj + ".obj")
        cmd = [CL] + base_flags + file_extra_options(file_node, config_name) + ["/Fo" + obj, src]
        if run_command(cmd, project_dir) != 0:
            fail("Compile failed: " + src)
        objects.append(obj)

    deps = [
        built_libs["XboxStubs"],
        built_libs["UT99Core"],
        built_libs["UT99Engine"],
        built_libs["UT99Render"],
        built_libs["Fire"],
        built_libs["XboxDrv"],
        built_libs["XboxRender"],
        built_libs["XboxAudio"],
        built_libs["IpDrv"],
        built_libs["XboxXmp"],
        # 5558's d3d8.lib is the full 2.1 MB retail static lib (xQuake's
        # link target). Lacks the debug DbgPrint+int3 validator that
        # 5849's d3d8-xbox.lib and d3d8d.lib emit on "Invalid flags passed
        # to Clear". 5558 LIBPATH is searched first via /LIBPATH above.
        "d3d8.lib",
        # d3dx8.lib and dsound.lib — added to match OpenJKDF2's link layout
        # exactly. Linking these creates D3DX and DSOUND named sections in
        # the XBE. CXBX-R's HLE pattern-matcher identifies D3D8 functions
        # by section name; without D3DX/DSOUND sections present, our XBE
        # appears to CXBX-R as a non-canonical D3D8 binary and the host
        # display blit pipeline never engages.  We don't call into these
        # libs ourselves — their presence is the entire point.
        "d3dx8.lib",
        "dsound.lib",
        "xboxkrnl.lib",
        "xgraphics.lib",
        "xonline.lib",
        "xacteng.lib",
        "xnet.lib",
        "xapilib.lib",
        "s3tc.lib",
    ]

    rsp = os.path.join(obj_dir, "link.rsp")
    map_path = out_exe.replace(".exe", ".map")
    with open(rsp, "w") as f:
        f.write('/OUT:"{}"\n'.format(out_exe))
        f.write('/MAP:"{}"\n'.format(map_path))
        # 5558 lib path first (full retail d3d8.lib lives here), 5849 as
        # fallback for libs 5558 doesn't ship (s3tc.lib in particular).
        f.write('/LIBPATH:"{}"\n'.format(os.path.join(XDK_DIR, "xbox", "lib")))
        f.write('/LIBPATH:"{}"\n'.format(os.path.join(XDK_FALLBACK_DIR, "xbox", "lib")))
        f.write('/LIBPATH:"{}"\n'.format(os.path.join(ROOT_DIR, "Engine", "Lib")))
        # WINDOWS subsystem + explicit mainCRTStartup entry — matches OpenJKDF2.
        # Xbox apps are always WINDOWS-subsystem on the PE side; patchxbe.py
        # rewrites to subsystem 14 (Xbox) post-link. CONSOLE subsystem picks a
        # different CRT init glue that may leave HLE-detectable state missing.
        f.write('/SUBSYSTEM:WINDOWS\n')
        f.write('/ENTRY:mainCRTStartup\n')
        # Filter out /MERGE:.CRT=.data and /SECTION:.data,RW — OpenJKDF2 doesn't
        # use these. /MERGE:.CRT=.data folds the C-runtime init pointer table
        # into .data, which on Xbox can prevent the CRT from walking it during
        # startup; if static constructors don't run, D3D8 lib globals may not
        # initialise and CXBX-R won't see a fully-formed app.
        for opt in split_options(link_tool.get("AdditionalOptions")):
            up = opt.upper()
            if up.startswith("/MERGE:") or up.startswith("/SECTION:"):
                continue
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


def remove_stale_runtime_asset_files(build_root, runtime_src, subdirs):
    for subdir in subdirs:
        src_root = os.path.join(runtime_src, subdir)
        if not os.path.isdir(src_root):
            continue

        for dirpath, _, filenames in os.walk(src_root):
            rel_dir = os.path.relpath(dirpath, runtime_src)
            dst_dir = build_root if rel_dir == "." else os.path.join(build_root, rel_dir)
            for name in filenames:
                dst = os.path.join(dst_dir, name)
                if os.path.isfile(dst):
                    os.remove(dst)

        for dirpath, dirnames, filenames in os.walk(os.path.join(build_root, subdir), topdown=False):
            if not dirnames and not filenames:
                os.rmdir(dirpath)


def copy_known_good_system_files(system_dst):
    if not os.path.isdir(KNOWN_GOOD_SYSTEM_DIR):
        print("WARNING: known-good System source not found: " + KNOWN_GOOD_SYSTEM_DIR)
        return

    if not os.path.isdir(system_dst):
        os.makedirs(system_dst)

    copied = 0
    missing = []
    for name in KNOWN_GOOD_SYSTEM_FILES:
        src = os.path.join(KNOWN_GOOD_SYSTEM_DIR, name)
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(system_dst, name))
            copied += 1
        else:
            missing.append(name)

    print("Copied {} known-good v436/GOTY System files to {}".format(copied, system_dst))
    if missing:
        print("WARNING: missing known-good System files: " + ", ".join(missing))


def copy_runtime_assets(build_root):
    dashboard_asset_dir = os.path.join(XBOX_DIR, "XboxAssets")
    dashboard_assets = (
        ("titleimage.xbx", "TitleImage.xbx"),
        ("saveimage.xbx", "SaveImage.xbx"),
        ("TitleMeta.xbx", "TitleMeta.xbx"),
    )
    for source_name, target_name in dashboard_assets:
        source_path = os.path.join(dashboard_asset_dir, source_name)
        if not os.path.isfile(source_path):
            fail("Missing dashboard asset: " + source_path)
        shutil.copy2(source_path, os.path.join(build_root, target_name))
    print("Copied Xbox dashboard metadata beside default.xbe")

    system_src = os.path.join(ROOT_DIR, "System")
    system_dst = os.path.join(build_root, "System")
    copy_known_good_system_files(system_dst)

    default_ini = os.path.join(system_src, "Default.ini")
    if os.path.isfile(default_ini):
        if not os.path.isdir(system_dst):
            os.makedirs(system_dst)
        runtime_ini = os.path.join(system_dst, "UnrealTournament.ini")
        if os.path.isfile(runtime_ini):
            os.remove(runtime_ini)
        shutil.copy2(default_ini, os.path.join(system_dst, "Default.ini"))
        shutil.copy2(default_ini, runtime_ini)
        print("Copied Xbox System ini files to " + system_dst)

    menu_src = os.path.join(XBOX_DIR, "MenuAssets")
    menu_dst = os.path.join(build_root, "MenuAssets")
    if os.path.isdir(menu_src):
        if not os.path.isdir(menu_dst):
            os.makedirs(menu_dst)
        for name in os.listdir(menu_src):
            src = os.path.join(menu_src, name)
            dst = os.path.join(menu_dst, name)
            if os.path.isfile(src):
                shutil.copy2(src, dst)
        print("Copied MenuAssets to " + menu_dst)

    music_src = os.path.join(XBOX_DIR, "MusicXbox")
    music_dst = os.path.join(build_root, "MusicXbox")
    if os.path.isdir(music_src):
        if not os.path.isdir(music_dst):
            os.makedirs(music_dst)
        for name in os.listdir(music_src):
            src = os.path.join(music_src, name)
            dst = os.path.join(music_dst, name)
            if os.path.isfile(src):
                shutil.copy2(src, dst)
        print("Copied MusicXbox to " + music_dst)

    runtime_src = os.path.join(XBOX_DIR, "RuntimeAssets")
    if os.path.isdir(runtime_src):
        copied = 0
        runtime_system_dirs = ("System", "Textures")
        runtime_root_dirs = ("Maps", "Music")
        if not os.path.isdir(system_dst):
            os.makedirs(system_dst)
        for subdir in runtime_system_dirs:
            src_root = os.path.join(runtime_src, subdir)
            if not os.path.isdir(src_root):
                continue
            for dirpath, _, filenames in os.walk(src_root):
                rel_dir = os.path.relpath(dirpath, src_root)
                dst_dir = system_dst if rel_dir == "." else os.path.join(system_dst, rel_dir)
                if not os.path.isdir(dst_dir):
                    os.makedirs(dst_dir)
                for name in filenames:
                    shutil.copy2(os.path.join(dirpath, name), os.path.join(dst_dir, name))
                    copied += 1

        credits = os.path.join(runtime_src, "CONTENT_CREDITS.txt")
        if os.path.isfile(credits):
            shutil.copy2(credits, os.path.join(build_root, "CONTENT_CREDITS.txt"))
            copied += 1

        remove_stale_runtime_asset_files(build_root, runtime_src, ("Maps", "Textures", "Sounds", "Music"))
        for subdir in runtime_root_dirs:
            src_root = os.path.join(runtime_src, subdir)
            if not os.path.isdir(src_root):
                continue
            for dirpath, _, filenames in os.walk(src_root):
                rel_dir = os.path.relpath(dirpath, runtime_src)
                dst_dir = build_root if rel_dir == "." else os.path.join(build_root, rel_dir)
                if not os.path.isdir(dst_dir):
                    os.makedirs(dst_dir)
                for name in filenames:
                    shutil.copy2(os.path.join(dirpath, name), os.path.join(dst_dir, name))
                    copied += 1
        print("Copied {} Xbox RuntimeAssets files to {}".format(copied, build_root))

    stale_debug_udp = os.path.join(build_root, "XboxDebugUDP.ini")
    if os.path.isfile(stale_debug_udp):
        os.remove(stale_debug_udp)

    cityintro = os.path.join(build_root, "Maps", "CityIntro.unr")
    if os.path.isfile(cityintro):
        patcher = os.path.join(SCRIPT_DIR, "patch_cityintro_frontend.py")
        if run_command([sys.executable, patcher, cityintro], ROOT_DIR) != 0:
            fail("CityIntro frontend patch failed: " + cityintro)


def copy_jailbreak_assets(build_root, archive=None, include_docs=False):
    stage_jailbreak.stage_jailbreak(
        target=build_root,
        archive=archive,
        include_docs=include_docs,
    )


def main():
    parser = argparse.ArgumentParser(description="Build UT99 Xbox from the command line.")
    parser.add_argument("--config", default="Release", choices=("Release", "Debug"))
    parser.add_argument("--clean", action="store_true")
    parser.add_argument(
        "--out-dir",
        default=None,
        help="Override the build output directory. Defaults to <repo>\\build.",
    )
    parser.add_argument(
        "--with-jailbreak",
        action="store_true",
        help="Download, verify, and stage Jailbreak III Gold into the build output.",
    )
    parser.add_argument(
        "--jailbreak-archive",
        default=None,
        help="Use an existing JailbreakIII-Gold-zip.7z when --with-jailbreak is set.",
    )
    parser.add_argument(
        "--jailbreak-include-docs",
        action="store_true",
        help="With --with-jailbreak, also stage Help and Web files.",
    )
    args = parser.parse_args()

    check_tools()

    build_root = args.out_dir
    if build_root:
        build_root = normalize_path(build_root, ROOT_DIR)
    else:
        build_root = os.path.join(ROOT_DIR, "build")
    if args.clean:
        if os.path.isdir(build_root):
            shutil.rmtree(build_root)
        print("Cleaned " + build_root)
        return

    if not os.path.isdir(build_root):
        os.makedirs(build_root)

    built_libs = {}
    built_libs["XboxXmp"] = build_xmp_library(build_root)
    built_libs["IpDrv"] = build_ipdrv_library(build_root)
    for name, vcproj, kind in PROJECTS:
        if kind == "lib":
            built_libs[name] = build_library(name, vcproj, args.config, build_root)

    out_exe = out_xbe = None
    for name, vcproj, kind in PROJECTS:
        if kind == "exe":
            out_exe, out_xbe = build_launch(vcproj, args.config, build_root, built_libs)
    copy_runtime_assets(build_root)
    if args.with_jailbreak:
        copy_jailbreak_assets(
            build_root,
            archive=args.jailbreak_archive,
            include_docs=args.jailbreak_include_docs,
        )

    print("")
    print("BUILD SUCCEEDED")
    print("EXE: " + out_exe)
    print("XBE: " + out_xbe)


if __name__ == "__main__":
    main()
