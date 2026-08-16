# UT99-Xbox

An Xbox port of Unreal Tournament 1999 (v1.40) targeting original Xbox hardware.

> **This repository contains only the Xbox-specific code authored for this port.**
> The UT99 v1.40 source code (Epic Games IP) is not included and must be obtained separately.

## Current Release

Version **1.1.9b** is the public test build for the upcoming 1.2 release. The latest replacement package:

- Fixes OldSkool Weapons so its Unreal minigun replaces the standard Unreal Tournament minigun
- Corrects looping minigun audio that could play at half speed and pitch
- Reduces intermittent texture pop-in and gameplay hitches caused by premature texture-cache recycling

Downloads, installation instructions, and public issue reporting are maintained in the [UT99 Xbox Releases repository](https://github.com/GTTeancum/UT99-Xbox-Releases).

---

## Hardware Target

- **Console:** Original Xbox (modded with hard drive)
- **CPU:** 733 MHz Intel Pentium III
- **RAM:** 64MB unified
- **GPU:** NVidia NV2A
- **Storage:** HDD only (E:\)

---

## Requirements

- **Visual Studio 2005**
- **Microsoft Xbox XDK 5849**
- **UT99 v1.40 source code** — obtain from [archive.org](https://archive.org)

---

## Directory Layout

The repository must be placed **alongside** the UT99 source tree:

```
UT99Root/
  Core/               <- UT99 source (obtain separately)
  Engine/             <- UT99 source (obtain separately)
  D3DDrv/             <- UT99 source (obtain separately)
  WinDrv/             <- UT99 source (obtain separately)
  Launch/             <- UT99 source (obtain separately)
  Window/             <- UT99 source (obtain separately)
  ... (other UT99 modules)
  UT99-Xbox/          <- This repository
    XboxLaunch/
    XboxDrv/
    XboxRender/
    XboxAudio/
    XboxNet/
    Docs/
    Tools/
    UT99-Xbox.sln
```

The VS2005 solution uses relative paths (`..\..\Core\Inc` etc.) to reference UT99 source.

---

## Repository Modules

| Module | Replaces | Description |
|---|---|---|
| `XboxLaunch/` | `Launch/` | Xbox entry point, platform object instantiation |
| `XboxDrv/` | `WinDrv/` | Xbox viewport, controller input |
| `XboxRender/` | `D3DDrv/` | Xbox D3D8 render driver |
| `XboxAudio/` | `Galaxy/`/`Cluster/` | Xbox XACT audio driver |
| `XboxNet/` | `IpDrv/` (socket layer) | Xbox XNet UDP socket layer |

---

## Build Instructions

1. Install Visual Studio 2005 and Xbox XDK 5849.
2. Ensure `XEDK` environment variable is set (XDK installer sets this automatically).
3. Place UT99 v1.40 source at the directory layout described above.
4. Open `UT99-Xbox.sln` in Visual Studio 2005.
5. Select **Debug|Xbox** or **Release|Xbox** configuration.
6. Build solution.

---

## XDK Environment Variable

The project files reference `$(XEDK)` for XDK include and library paths.
XDK 5849 sets this automatically during installation. Verify it is set:

```
echo %XEDK%
```

Expected output: `C:\Program Files\Microsoft Xbox SDK` (or similar).

---

## HDD Path Configuration

The Xbox HDD root path defaults to `E:\UT99\`. To change this, edit:

```
XboxLaunch/Inc/FFileManagerXbox.h
```

Look for `GetRootPath()` and update the return value.

---

## Project Status

See `Docs/` for the full design document and current phase status.

---

## Legal

This repository contains no Epic Games source code or assets.
All code in this repository is original work authored for this port.
Unreal Tournament 1999 is copyright Epic Games, Inc.
