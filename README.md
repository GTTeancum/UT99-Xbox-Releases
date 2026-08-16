# Unreal Tournament for Original Xbox

A native port of Unreal Tournament 1999 Game of the Year Edition for modded original Xbox hardware. The game runs as an Xbox executable and uses legally owned PC GOTY assets; it is not an emulator wrapper or a patched PC binary.

## Current status

Version **1.1.9b** is the public test build for the upcoming 1.2 release. It is playable on original Xbox hardware and in Xemu, with the complete frontend, controller input, hardware-accelerated rendering, music and sound, local multiplayer, Tournament, and Xbox networking in place.

The current public test includes:

- Instant Action, Tournament, Split Screen, and experimental System Link
- Last Man Standing in Instant Action, Split Screen, and System Link
- All fourteen standard mutators, plus AgentX Arena and Akimbo Arena
- Fixed OldSkool Weapons minigun replacement and corrected its slow, low-pitched looping audio
- Automatic widescreen detection from the Xbox dashboard setting with a wider gameplay view
- Texture, lighting, and UV fixes, including stability while changing brightness, contrast, and gamma
- Reduced intermittent texture pop-in and gameplay hitches caused by premature texture-cache recycling
- Faster menu transitions without freezing the music or CityIntro
- Fixed startup failures in CTF-Phalanx, CTF-Spirito, CTF-Stormfront, CTF-Sundial, DM-CanyonFear, DM-Halberd, DM-Hood, and DM-Pantheon
- Tournament first-match and ladder-advance fixes across all five ladders
- More reliable System Link joining, reconnecting, map travel, lobby return, and subsequent matches

Known limitations in the public test:

- System Link remains experimental pending broader real-hardware testing
- The HUD and menus remain horizontally stretched in widescreen
- Safe Area Size currently scales the full picture instead of moving only the HUD
- Co-op Tournament is not included and is deferred until after 1.2

Public downloads, installation instructions, release notes, and issue reporting are maintained in the [UT99 Xbox Releases repository](https://github.com/GTTeancum/UT99-Xbox-Releases).

## Installing the public test

The public release is a complete Xbox-side package, not a patch.

1. Create a clean `UnrealTournament` folder on the Xbox
2. Copy the complete release package into it
3. From a legally owned PC GOTY installation, merge the `Maps`, `Music`, `Sounds`, and `Textures` folders into the Xbox folder
4. Launch `default.xbe`

Do not copy the PC `System` folder or an older release's configuration files over the public test. The release includes the matching Xbox System files, configuration, menus, community packages, and executable.

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
