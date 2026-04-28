# CLAUDE.md — UT99 Xbox OG Source Port

## Project Overview

Source port of Unreal Tournament 1999 (v1.40) to **original Xbox hardware** using **VS2005 + XDK 5849**. Static lib build (no DLLs). Steve is the project manager, compiler, and hardware tester. Claude is sole programmer.

## Repository Layout

```
D:\Programming\GitHub\UnrealTournament_1.40\
  Core/                  ← UT99 source (upstream, minor Xbox patches)
  Engine/                ← UT99 source (upstream, minor Xbox patches)
  UT99-Xbox/             ← This project (all Xbox-specific code)
    XboxLaunch/          ← Entry point, platform objects (replaces Launch/)
      Inc/               ← FFileManagerXbox.h, FMallocXbox.h, FFeedbackContextXbox.h,
                            FOutputDeviceXboxError.h, FXboxLogger.h
      Src/               ← XboxLaunch.cpp, XboxEngine.cpp, XboxLaunchPrivate.h
    XboxDrv/             ← Viewport, controller input (replaces WinDrv/)
    XboxRender/          ← D3D8 render device (replaces D3DDrv/) — stub
    XboxAudio/           ← Audio — stub
    XboxNet/             ← Networking — stub
    XboxStubs/           ← CRT intrinsics (_ftol2_sse, _alloca_probe_16, __CxxFrameHandler3)
    Tools/
      patchxbe.py        ← PE→XBE converter (subsystem patch + imagebld + D3D8/XGRAPHC injection)
    UT99-Xbox.sln
```

## Build System

- **Toolchain:** Visual Studio 2005 + Xbox XDK 5849 (installed at `C:\XDK`, non-standard)
- **Architecture:** All modules compile as static libraries, linked into a single EXE by XboxLaunch
- **XBE creation:** Post-build step runs `patchxbe.py` which patches PE subsystem 1→14, runs `C:\XDK\xbox\bin\imagebld.exe`, then injects D3D8/XGRAPHC library version entries
- **Forced includes:** `CoreXboxCompat.h` for Core/Engine libs, `XboxLaunchPrivate.h` for XboxLaunch — these include `<xtl.h>` first, kill XDK macro collisions (`Top`, `MAKEFOURCC`), empty `DLL_EXPORT`/`CORE_API`/`ENGINE_API`, set `#pragma conform(forScope, off)` for VC6 compat
- **Key preprocessor defines:** `TARGET_XBOX=1`, `ASM=0`, `ASM3DNOW=0`, `ASMKNI=0`
- **Linker (Release):** `SubSystem="1"` (Console), no `EntryPointSymbol` — lets xapilib CRT startup call `main()`
- **Release libs:** `d3d8-xbox.lib xboxkrnl.lib xgraphics.lib xonline.lib xacteng.lib xnet.lib xapilib.lib s3tc.lib`
- **Manifest embedding:** Disabled or fails harmlessly (mt.exe error 31) — run `patchxbe.py` manually if needed

## Key Modified UT99 Source Files

- `Core/Src/UnXboxWin32.cpp` — Replaces UnVcWin32.cpp. Contains: `IMPLEMENT_CLASS(USystem)`, `USystem::StaticConstructor()`, `appPlatformInit()` (creates GSys + LoadConfig), `appBaseDir()`, `appSeconds()`, and all platform function stubs
- `Core/Src/CoreXbox.cpp` — `IMPLEMENT_PACKAGE(Core)` only
- `Engine/Src/EngineXbox.cpp` — `IMPLEMENT_PACKAGE(Engine)` + `GCache`/`GEngineMem` globals
- `Core/Inc/CoreXboxCompat.h` — Forced include for Core/Engine, kills XDK macro collisions
- `Core/Inc/UnVcWin32.h` — Needs `#ifndef` guard around `IMPLEMENT_PACKAGE_PLATFORM` (patched)

## Current State (as of March 29, 2026)

### What Works
- CRT init → `main()` entry point runs
- `FXboxLogger` writes to `D:\ut99.log` (opened before any Unreal code)
- `appInit()` completes: names init, config loaded from `D:\System\UnrealTournament.ini`, UObject subsystem initialized
- `InitEngine()` starts, reads `GameEngine=Engine.GameEngine` from config
- File manager (`FFileManagerXbox`) resolves paths using internal BaseDir tracking, `../` resolution works
- `D:\` drive mapping works on both CXBX-R and real Xbox (maps to XBE parent directory)

### Active Blocker — PackageNotFound for Engine.u

`StaticLoadClass("Engine.GameEngine")` calls `appFindPackageFile("Engine")` which searches `GSys->Paths`. **`GSys->Paths` may still be empty** despite adding `GSys->LoadConfig(1)` in `appPlatformInit()`.

The last build added `LoadConfig(1)` and diagnostic logging (`appPlatformInit: GSys->Paths.Num()=X`) but Steve hasn't tested it yet. **The diagnostic output needs to be checked in CXBX-R's console.**

The config loading chain: `LoadConfig` → `StaticConfigName()` returns `"System"` → `FConfigCacheIni::Find` appends `.ini` → `"System.ini"` → translated to `SystemIni` = `"UnrealTournament.ini"` → reads `[Core.System]` section → should populate `Paths` array.

If `Paths.Num()` is still 0 after `LoadConfig(1)`, the problem is in the property registration. Check that `USystem::StaticConstructor` registers the `"Path"` property (note: property name is `"Path"`, config key is `"Paths"` — this mismatch might be the bug).

### If Paths Load Successfully
The next issue will be that `appFindPackageFile` concatenates `appBaseDir()` + `Paths(i)` giving e.g. `D:\System\../System/*.u`. This raw path with embedded `../` must be resolved by the OS or by `FFileManagerXbox::ResolvePath`. On CXBX-R (Windows host), `FindFirstFileA` handles `../` natively. On real Xbox hardware, it may not — `ResolvePath` handles it.

## File Manager (FFileManagerXbox)

- Tracks `BaseDir` internally as a `TCHAR[1024]` member
- `SetDefaultDirectory(path)` stores path in BaseDir with trailing backslash
- `ResolvePath(filename)` prepends BaseDir to relative paths, resolves `../` by walking up directories, passes absolute paths (X:\) through unchanged
- All file operations (`CreateFileReader`, `FileSize`, `FindFiles`, etc.) call `ResolvePath` then use Win32 ANSI APIs directly (`CreateFileA`, `FindFirstFileA`, etc.)
- `FArchiveFileReader`/`FArchiveFileWriter` are inlined (can't include `FFileManagerWindows.h` which has `SetCurrentDirectoryA`)
- Has diagnostic logging (`RESOLVE:`, `SetDefaultDirectory:`, `FindFiles:` via `OutputDebugStringA`)

## CXBX-R Testing Setup

```
D:\Emulators\CXBX\!GAME BUILD\
  default.xbe                    ← Built XBE
  ut99.log                       ← FXboxLogger output (D:\ maps here)
  System\                        ← Config + .u packages + .int files
    Default.ini                  ← Xbox config (GameRenderDevice=XboxRender.XboxRenderDevice, ViewportManager=XboxDrv.XboxClient)
    UnrealTournament.ini         ← Created from Default.ini at runtime (DELETE before testing config changes)
    Engine.u, Core.u, BotPack.u, etc.
  Maps\, Music\, Sounds\, Textures\  ← Full UT99 game data
```

- CXBX-R version: 9454f34 (Jan 30 2026)
- Must build Release config (CXBX-R has no HLE signatures for debug XDK libs)
- `D:\` in Xbox APIs maps to the XBE's parent directory on both CXBX-R and real Xbox
- `appBaseDir()` currently returns `"D:\System\"` (hardcoded for debugging — will be made runtime-detected later)
- Debug output goes to CXBX-R console window (`OutputDebugStringA` → `DEBUG_PRINT:` lines)

## Real Xbox Hardware

- Softmodded 64MB retail unit with UnleashX dashboard
- Games installed to E:\ or F:\ via FTP
- `D:\` maps to XBE parent directory (same as CXBX-R)
- Log file at `D:\ut99.log` — confirmed working on CXBX-R, untested on hardware since CRT fix
- For hardware deployment: swap `appBaseDir()` back to `"D:\System\"` (already correct) and FTP the build

## Bugs Fixed (chronological)

1. S3TC link error — added `s3tc.lib` to Release config
2. CXBX-R crash on debug XBE — switched to Release (links `XAPILIB` not `XAPILIBD`)
3. NV2A register crash — injected D3D8/XGRAPHC library versions via `patchxbe.py`
4. Xbox hardware hang — removed `EntryPointSymbol="main"`, changed `SubSystem` to `"1"` (Console)
5. NULL GLog crash (EIP=0) — changed 4th param of `appInit` from `NULL` to `&XboxWarn`
6. FFileManagerXbox infinite recursion — rewrote to call Win32 APIs directly instead of `GFileManager->`
7. FArchiveFileReader/Writer not found — inlined from FFileManagerWindows.h
8. SetCurrentDirectoryA not on Xbox — stubbed, then replaced with internal BaseDir tracking
9. MisingIni errors — fixed path resolution: `appBaseDir()` returns `"D:\System\"`, `FFileManagerXbox::ResolvePath` prepends BaseDir
10. UnrealTournament.ini stale — engine reads existing `.ini` not `Default.ini`; must delete to regenerate
11. GSys->Paths empty — added `GSys->LoadConfig(1)` in `appPlatformInit()` (fix pending verification)

## Standing Rules

- Steve uploads source/logs, Claude provides complete replacement files
- All patch theories require full code trace before writing bytes
- Data-only patches to read-only tables are safe
- Never reference Flycast or emulator-ripped assets
- nxdk is permanently off the table as a toolchain option
- Real Xbox hardware is the ultimate test target; CXBX-R is the current debug environment
- Delete `UnrealTournament.ini` before testing any config changes (engine won't re-read `Default.ini` if it exists)
