# UT99-Xbox: Unreal Tournament 1999 for Original Xbox

A native source port of **Unreal Tournament 1999 (Game of the Year edition)** to original Xbox hardware.

This is a from-source port — UT99's public engine source compiled with the Xbox XDK, running on retail/softmodded Xbox hardware. Not an emulator wrapper, not a patched PC binary.

## Status

Boot pipeline implemented end-to-end. Engine subsystems initialized, package format upgraded to v469 (matching OldUnreal patches), most class layouts aligned to the v469 binary distributions. Game has not yet rendered a frame; current frontier is `UTexture` struct alignment with v469's binary layout.

See [ROADMAP.md](ROADMAP.md) for the phased plan and [CLAUDE.md](CLAUDE.md) for project conventions and standing rules.

## Repository layout

```
Core/                       UT99 v400 (v432) public source — Core lib (Xbox patches inline)
Engine/                     UT99 Engine — class definitions adapted to v469 layout
Render/                     UT99 Render — software renderer
UT99-Xbox/                  Xbox-specific code (replaces Win32-only modules)
  XboxLaunch/                 main() entry, FXboxLogger, FFileManagerXbox, GPackage globals,
                              EngineForceLinks (pragma /include directives), V469VirtualStubs
  XboxDrv/                    UViewport / UClient implementations (controller input)
  XboxRender/                 D3D8 render device (Xbox NV2A hardware-accelerated)
  XboxAudio/                  Audio stub (will use XAudio/XACT, not DirectSound)
  XboxNet/                    Network stub
  XboxStubs/                  CRT intrinsics missing from XDK 5849 (_ftol2_sse,
                              _alloca_probe_16, __CxxFrameHandler3)
  Tools/
    build_xbox_cli.py         Command-line build (vs2003 XDK toolchain, no IDE)
    patchxbe.py               Post-build PE→XBE conversion + D3D8/XGRAPHC injection
    parse_uclass.py           Parse a .u package's UClass property chain
    dump_all_classes.py       Dump all UClass property chains from one or more .u files
    parse_classes_header.py   Parse C++ class definitions from EngineClasses.h-style headers
    compare_layouts.py        Cross-reference binary .u, v469 SDK, and v400 source layouts
build_xbox.bat              Wrapper for build_xbox_cli.py
```

Not in the repository (ignored):
- `v469_SDK/` — OldUnreal v469 SDK, used as a layout reference (their licensed material)
- `XboxNewLegends/` — reverse-engineering data from MGS *New Legends*, used during D3D8 spike work; not part of the port itself
- `_v400_backup/` — transient migration backup
- Build outputs (`UT99-Xbox/build_cli/`)

## Toolchain

- **Compiler:** Visual Studio 2005 IDE optional, but builds run from CLI through the XDK's vc71 (VS2003) toolchain at `C:\XDK\xbox\bin\vc71`
- **XDK:** Microsoft Xbox XDK 5849 (installed at `C:\XDK`, non-standard path)
- **Architecture:** All modules compile as static libraries linked into a single XBE (no DLL loading on Xbox)
- **Forced includes:** `CoreXboxCompat.h` for Core/Engine, `XboxLaunchPrivate.h` / `XboxDrvPrivate.h` / `XboxRenderPrivate.h` for the Xbox modules. These pre-empty `DLL_EXPORT` / `CORE_API` / `ENGINE_API`, kill XDK macro collisions (`Top`, `MAKEFOURCC`), and route `GPackage` per library.
- **No IDE builds.** Always build via `build_xbox.bat` or `python UT99-Xbox\Tools\build_xbox_cli.py`. The script reads each module's `.vcproj` to enumerate sources, compiles, links, and runs `patchxbe.py` to produce `default.xbe`.

## Building

```
> build_xbox.bat
```

Outputs land in `UT99-Xbox\build_cli\release\`:
- `UnrealTournament.exe` — PE produced by the linker (intermediate)
- `default.xbe` — final XBE (subsystem patched 1→14, D3D8/XGRAPHC library entries injected)

## Game data setup

The port loads UT99 GOTY game data plus the **OldUnreal v469 patcher** applied to it. Required runtime layout under `D:\` on the Xbox (or `D:\` mapped via CXBX-R for desktop testing):

```
D:\
  default.xbe              ← built by us
  ut99.log                 ← runtime log (FXboxLogger writes here)
  System\
    *.u, *.int, *.ini      ← game packages, localization, config (from GOTY + v469 patcher)
  Maps\        *.unr
  Textures\    *.utx
  Sounds\      *.uax
  Music\       *.umx
  Voice\       *.uax
```

User flow for end-users:
1. Install UT99 GOTY (GoG, Steam, or disc)
2. Apply [OldUnreal v469 patcher](https://github.com/OldUnreal/UnrealTournamentPatches/releases) to that install — overwrites `System/*.u` with v469-format packages
3. FTP `System/`, `Maps/`, `Textures/`, `Sounds/`, `Music/` from the patched install to the Xbox
4. Drop `default.xbe` from this build into the same directory and launch

The v469 patch step is required — our C++ struct layouts match v469's class layouts. Loading raw GOTY (~v436) `.u` files would mismatch.

## Testing

- **Real hardware:** softmodded retail Xbox with UnleashX. FTP `default.xbe` and pull `D:\ut99.log` after each run for diagnostics.
- **CXBX-R:** earlier development used `C:\Games\Emulators\CXBX\UT99x\` as a copy of the hardware setup. Hardware is now the primary target.
- Logs are written via `FXboxLogger` to `D:\ut99.log` from main() entry, before any UT99 code runs.

## Key engine adaptations from v400 source to v469 binary

UT99's last public C++ source release is v432 (~v400 in our build's version constant). Modern UT99 — including GoG GOTY + v469 patches — has different class layouts. We adapted v400 source to read v469 packages:

- **Class layouts:** Engine class definitions (`EngineClasses.h`, per-class `A*.h`) use v469 SDK ordering — extra fields, reordered booleans, additional native script natives. See commit `9bf1b7e` for the migration.
- **`UPointerProperty`:** New property type added in v451+. Stub class implemented in `Core/Inc/UnType.h` with proper `Link()` setting `ElementSize=sizeof(void*)`.
- **`UAnimation`:** Native class referenced by v469 Engine.u but absent from v400 source. Minimal stub in `Engine/Src/UnAnimationStub.cpp`.
- **DLL handle marker:** v469 native packages need `DllHandle != NULL` so `GetPackageLinker` recognizes them as native. Patched into `UObject::Register()` under `#if TARGET_XBOX`.
- **Native virtual stubs:** `Engine/Src/V469VirtualStubs.cpp` provides empty/default implementations for ~25 virtual methods declared by v469 headers but not implemented in v400 source.
- **Force-link:** `UT99-Xbox/XboxLaunch/Src/EngineForceLinks.cpp` uses `#pragma comment(linker,"/include:_autoclassXxx")` to defeat VS2003's static-lib dead-code elimination for Engine classes that nothing else references directly.

## Standing conventions

- **No empirical iterations.** Every fix gets a code trace or a parsed-binary fact behind it before a build runs. See [CLAUDE.md](CLAUDE.md).
- **Always full file replacements**, never patch-format diffs.
- **Build is always run after a code change** before reporting done.
- **Real Xbox hardware is the test target.** CXBX-R is used only as a copy of the hardware filesystem layout for tooling work.

## License / attribution

Project code is original. The Core/Engine/Render directories contain Epic Games' UT99 v432 public C++ headers/source under whatever terms Epic released those (no formal license attached). The OldUnreal v469 SDK is used as a *layout reference only* and is not redistributed in this repository — end-users obtain it directly from OldUnreal.

This is **not** an official Epic Games or OldUnreal project.
