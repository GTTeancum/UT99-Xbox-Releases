# Unreal Tournament for Original Xbox

A native port of Unreal Tournament 1999 Game of the Year Edition for modded original Xbox hardware. The game runs as an Xbox executable and uses legally owned PC GOTY assets; it is not an emulator wrapper or a patched PC binary.

## Current status

Version **1.2** is available from the [UT99 Xbox Releases repository](https://github.com/GTTeancum/UT99-Xbox-Releases/releases/tag/1.2).

This release adds Halo Elite and the three-weapon HaloUT mutator, improves split-screen performance, and fixes profile preferences, match-settings persistence, controller sensitivity, safe margins and widescreen menus.

Widescreen follows the Xbox dashboard: 480p when available, otherwise 480i. Single-player gets the wider view; split-screen uses 4:3 with side borders. All 39 converted PS2/Dreamcast maps and the previous release's content are included.

System Link remains experimental. Co-op Tournament and skeletal LOD improvements are deferred. Video output is limited to 480i/480p.

## Installing v1.2

The public release is a complete Xbox-side package, not a patch.

1. Create a clean `UnrealTournament` folder on the Xbox
2. Copy the complete release package into it
3. From a legally owned PC GOTY installation, merge the `Maps`, `Music`, `Sounds`, and `Textures` folders into the Xbox folder
4. Launch `default.xbe`

Do not copy the PC `System` folder or an older release's configuration files over this release. The release includes the matching Xbox System files, configuration, menus, community packages, and executable.

## Reporting problems

Open an issue in the [public issue tracker](https://github.com/GTTeancum/UT99-Xbox-Releases/issues) and include the map, game mode, player or bot count, display settings, and what happened immediately before the problem.

Please also copy `ut99.log` from beside `default.xbe` on the Xbox. In the GitHub issue editor, drag the log from File Explorer into the large description box, wait for GitHub to insert the uploaded file link, and then submit the issue.

## Repository layout

```text
Core/                       UT99 core source with Xbox adaptations
Engine/                     UT99 engine source with Xbox adaptations
IpDrv/                      Network driver source
Render/                     Shared rendering source
UT99-Xbox/
  XboxLaunch/               Xbox entry point and platform services
  XboxDrv/                  Viewport and controller input
  XboxRender/               Xbox D3D8 render device
  XboxAudio/                Xbox audio device
  XboxNet/                  XNet socket and System Link support
  XboxStubs/                XDK/CRT compatibility code
  Docs/                     Installation, release, and engineering notes
  Tools/                    Build, packaging, conversion, and test tools
build/                      Canonical packaged build output
build_xbox.bat              Command-line build entry point
```

## Building

The project targets the original Xbox with the Microsoft Xbox XDK toolchain. Run the command-line build from the repository root:

```bat
build_xbox.bat
```

The canonical runtime output is written to `build\`. Use `build_xbox.bat jailbreak` when explicitly building with the optional Jailbreak III content described in [UT99-Xbox/Docs/JailbreakIII.md](UT99-Xbox/Docs/JailbreakIII.md).

Runtime files are not committed. A test installation needs the generated `default.xbe`, the matching Xbox System content, and the required game assets in the standard `Maps`, `Music`, `Sounds`, and `Textures` folders.

## Testing

Xemu is used for repeatable development tests, including two-instance System Link sessions and long-running bot matches. A modded retail Xbox remains the final compatibility target.

Runtime diagnostics are written to `ut99.log` beside `default.xbe` from the start of the boot process.

## Legal

This is an unofficial community project and is not affiliated with Epic Games. Unreal Tournament is copyright Epic Games. Users must supply assets from their own legally obtained Unreal Tournament GOTY installation. No retail game assets are stored in this source repository.
