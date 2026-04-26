# New Legends XBE — Phase 1 Symbol Map
# Generated: 2026-04-25
# Source: XboxNewLegends/default.xbe (read-only reference)

## XBE Image Facts

| Field               | Value                                         |
|---------------------|-----------------------------------------------|
| File size           | 2,940,928 bytes (0x002CE000) on disc          |
| Virtual image size  | 4,810,880 bytes (0x00496880)                  |
| Base address        | 0x00010000                                    |
| Entry point (retail)| 0x00180547 (CRT startup, NOT main)            |
| Kernel thunk (retail)| 0x002293C0 (125 kernel imports)              |
| Build timestamp     | 2002-01-20 02:17:10 UTC                       |
| Debug EXE path      | E:\depot\gow\main\XBoxLaunch\Retail\XBoxLaunch.exe |
| XDK version         | 4039 (our port targets 5849)                  |
| Title               | "New Legends"                                 |
| Title ID            | 0x54510001                                    |
| UE1 package version | 83 (licensee 25) — UT99 uses version 69       |

## Section Table

| # | Name        | VA         | VSize      | Flags | Contents                            |
|---|-------------|------------|------------|-------|-------------------------------------|
| 0 | .text       | 0x00011000 | 0x0019DDA0 | WR    | Game+engine code (no exec flag set) |
| 1 | D3D         | 0x001AEDA0 | 0x00011B38 | WXR   | D3D8 library (linked statically)    |
| 2 | D3DX        | 0x001C08E0 | 0x0001D468 | WXR   | D3DX utility library                |
| 3 | XGRPH       | 0x001DDD60 | 0x00004EF8 | WXR   | XGraphics library                   |
| 4 | DSOUND      | 0x001E2C60 | 0x0000BC74 | WXR   | DirectSound library                 |
| 5 | WMADECXM    | 0x001EE8E0 | 0x00001310 | WXR   | WMA decoder (Xbox media)            |
| 6 | WMADEC      | 0x001EFC00 | 0x00018D80 | WXR   | WMA decoder (full)                  |
| 7 | BINK        | 0x00208980 | 0x00011FB0 | WR    | Bink video codec data               |
| 8 | BINK32      | 0x0021A940 | 0x00001265 | WR    | Bink 32-bit blitter                 |
|19 | XPP         | 0x00221CA0 | 0x0000771C | WXR   | Xbox C++ runtime                    |
|20 | .rdata      | 0x002293C0 | 0x0006C3F8 | WR    | Read-only data (starts at kernel thunk) |
|21 | .data       | 0x002957C0 | 0x0020289C | WXR   | Read-write data / BSS               |
|22 | DOLBY       | 0x00498060 | 0x00006D98 | WR    | Dolby audio data                    |

## Library Versions (from XBE header)

| Library  | Version       | Notes                          |
|----------|---------------|--------------------------------|
| XAPILIB  | 1.0.4039.1    | Xbox API library (XDK 4039)    |
| D3D8     | 1.0.4039.2    | Direct3D 8 for Xbox            |
| D3DX8    | 1.0.4039.1    | D3DX utilities                 |
| XGRAPHC  | 1.0.4039.1    | XGraphics                      |
| DSOUND   | 1.0.4039.1    | DirectSound                    |
| XBOXKRNL | 1.0.4039.1    | Xbox kernel                    |
| LIBC     | 1.0.4039.1    | C runtime library              |
| LIBCPMT  | 1.0.4039.1    | C++ STL runtime                |

## Kernel Imports (selected, 125 total at 0x002293C0)

Key imports relevant to our port (full list in parse_xbe.py output):

| VA         | Ordinal | Name                              |
|------------|---------|-----------------------------------|
| 0x002293C0 | 128     | MmCreateKernelStack               |
| 0x002293E0 | 184     | NtSetEvent                        |
| 0x002294D0 | 143     | NtAllocateVirtualMemory           |
| 0x00229480 | 149     | NtCreateFile                      |
| 0x002294A0 | 178     | NtReadFile                        |
| 0x00229560 | 279     | RtlZeroMemory                     |
| 0x00229584 | 8       | DbgPrint                          |
| 0x0022957C | 3       | AvSetDisplayMode                  |
| 0x00229578 | 2       | AvSendTVEncoderOption             |
| 0x00229574 | 1       | AvGetSavedDataAddress             |
| 0x00229580 | 4       | AvSetSavedDataAddress             |
| 0x00229598 | 139     | MmQueryAllocationSize             |
| 0x0022958C | 125     | MmAllocateContiguousMemorySpecifyCache |
| 0x002295A4 | 137     | MmPersistContiguousMemory         |
| 0x00229550 | 109     | KeSetTimer                        |
| 0x002295AC | 312     | XboxLANKey                        |
| 0x00229590 | 87      | KeQueryPerformanceFrequency       |

## UE1 Engine Source Layout (from .rdata strings)

Internal depot: `E:\depot\gow\main\`

| Module          | Source path                                              |
|-----------------|----------------------------------------------------------|
| Core            | E:\depot\gow\main\Core\Src\Un*.cpp                       |
| Core Xbox       | E:\depot\gow\main\Core\Src\UnXBox.cpp                    |
| Engine          | E:\depot\gow\main\Engine\Src\Un*.cpp                     |
| XBoxLaunch      | E:\depot\gow\main\XBoxLaunch\XBoxLaunch.cpp              |
| XBoxDrv         | E:\depot\gow\main\XBoxDrv\Src\XboxClient.cpp             |
| XBoxDrv         | E:\depot\gow\main\XBoxDrv\Src\XboxViewport.cpp           |
| ImRenderDrv     | E:\depot\gow\main\ImRenderDrv\Src\ImRenderDevice.cpp     |
| ImAudioDrv      | E:\depot\gow\main\ImAudioDrv\Src\ImAudioDevice.cpp       |
| Render          | E:\depot\gow\main\Render\Src\ImRenderVB.cpp + UnLight.cpp etc |
| GowAI           | E:\depot\gow\main\GowAI\Src\                             |
| ParticleSystems | E:\depot\gow\main\ParticleSystems\Src\                   |

Platform headers (in Core/Inc/):
- `FFileManagerLinear.h`  — Xbox file manager (replaces FFileManagerWindows)
- `FMallocXBox.h`         — Xbox heap allocator
- `FFeedbackContextXBox.h`— Xbox feedback/log context

## Key Symbol Table

All VAs are virtual addresses (base 0x00010000). File offset = VA - 0x00010000 + section.raw_off.

### Startup / Launch

| Symbol                    | VA         | Notes                                    |
|---------------------------|------------|------------------------------------------|
| CRT startup               | 0x00180547 | XBE entry point (retail); calls main()   |
| XBoxLaunch main()         | 0x00174590 | Actual game init; 3 params (argc,argv,?) |

### Platform Core (UnXBox.cpp / XBoxLaunch.cpp)

| Symbol                    | VA         | Notes                                    |
|---------------------------|------------|------------------------------------------|
| appPlatformInit equiv [0] | 0x0004BA40 | Large function; sets up GSys-equiv, calls appInit-chain |
| appPlatformInit equiv [1] | 0x0004BBD0 | Second xref to UnXBox.cpp string         |

### FFileManagerLinear (..\\Core\\Inc\\FFileManagerLinear.h)

| Symbol                     | VA         | Notes                                    |
|----------------------------|------------|------------------------------------------|
| FFileManagerLinear ctor    | 0x001728B0 | Sets vtable=0x002608D0; stores pkg ver 83, 0x190, lic 25; 3-arg ctor |
| Vtable                     | 0x002608D0 | Loaded in ctor at [esi+0]                |
| FFileManagerLinear slot(?)  | 0x001729C0 | Next function after ctor (FileClose equiv) |
| Thunk stubs [slot+0..+5]   | 0x00172A50 | 6 thunks: `mov ecx,[ecx+4]; jmp [vtable+N]` |

### FMallocXBox (..\\Core\\Inc\\FMallocXBox.h)

10 function prologues in 0x00172F00–0x00173F80:

| Symbol              | VA         | Notes                                    |
|---------------------|------------|------------------------------------------|
| FMallocXBox::Malloc | 0x00173040 | First malloc func; large alloc loop      |
| FMallocXBox ctor?   | 0x00173190 | Initializes heap structure at [esi+0x260+] |
| FMallocXBox::Realloc| 0x001732E0 | Uses NtAllocateVirtualMemory pattern      |
| FMallocXBox::Free   | 0x00173570 | Block free + merge                       |
| FMallocXBox helper  | 0x00173700 | Block split / coalesce                   |
| FMallocXBox init    | 0x00173850 | Heap init; calls debugf with size strings|
| FFeedbackContextXBox| 0x00173CA0 | (different file) AddRef pattern           |
| FFileManagerGeneric | 0x00173D40 | FFileManagerGeneric::FindFiles equiv     |
| FFileManagerGeneric2| 0x00173DD0 | Second FindFiles variant                 |
| FFileManagerGeneric3| 0x00173F80 | Path parsing (backslash scan)            |

### XboxDrv — Client / Viewport

| Symbol                  | VA         | Source file             |
|-------------------------|------------|-------------------------|
| XboxClient func [0]     | 0x0016D610 | XboxClient.cpp          |
| XboxClient func [1]     | 0x0016DB3F region | XboxClient.cpp   |
| XboxViewport func [0]   | 0x0016F110 | XboxViewport.cpp        |
| XboxViewport func [1]   | 0x0016F147 region | XboxViewport.cpp |
| XboxViewport func [2-3] | 0x0016F168 region | XboxViewport.cpp |

### ImRenderDevice (D3D8 render device)

| Symbol                  | VA         | Notes                   |
|-------------------------|------------|-------------------------|
| ImRenderDevice func [0] | 0x001258C6 region | ImRenderDevice.cpp |
| ImRenderDevice func [1] | 0x00125F16 | Second method           |
| ImRenderDevice func [2] | 0x00127864 | Third method            |
| ImRenderDevice func [3] | 0x00127EB0 | Fourth method           |
| ImRenderDevice func [4] | 0x0012B054 | Fifth method            |
| Probable vtable         | 0x00255CEC | 138 entries; slot[4]=0x00125400 |

### ImAudioDevice (Xbox audio)

| Symbol                  | VA         | Notes                   |
|-------------------------|------------|-------------------------|
| ImAudioDevice func [0]  | 0x0012F3F5 region | ImAudioDevice.cpp |
| ImAudioDevice func [1]  | 0x0012F440 |                         |
| ImAudioDevice func [2]  | 0x00130E57 |                         |
| ImAudioDevice func [3]  | 0x00131492 |                         |

### Notable Large Vtables (top candidates)

| VA         | Entries | Likely class                    |
|------------|---------|----------------------------------|
| 0x00264B60 | 329     | GNatives / UObject dispatch table (not vtable) |
| 0x0026565C | 267     | Second dispatch table            |
| 0x00242AF0 | 169     | UActor or UPawn vtable           |
| 0x00255CEC | 138     | URenderDevice / ImRenderDevice vtable |
| 0x00267960 | 137     | Major game class                 |
| 0x002552D0 | 136     | Second render class vtable       |
| 0x002472F0 | 120     | UActor subclass                  |
| 0x0022AB58 | 95      | UObject vtable (base; shared entries at [0-2]) |

UObject vtable pattern (entries shared across all derived classes):
- slot[0] = 0x000373F0 (scalar deleting destructor)
- slot[1] = 0x00011160 (shared - likely UObject::~UObject or Delete)
- slot[2] = 0x00011160 (same)
- slot[3] = CLASS_SPECIFIC (first per-class override)

## Critical Divergences from UT99 (v69)

1. **Package version 83 vs 69**: Their .u files are NOT compatible with ours. Export table format,
   name table format, and property serialization all differ. New Legends packages cannot be loaded
   by our UT99 v69 linker code without a version-aware shim.

2. **XDK 4039 vs 5849**: Kernel thunk ordinals are the same (ordinals are stable), but some
   APIs may differ in behavior. Our XDK 5849 build should be a superset of XDK 4039's API surface.

3. **FFileManagerLinear vs FFileManagerWindows**: New Legends replaces the Windows file manager
   with a custom "linear" implementation. Their vtable (10 thunks) maps directly onto the
   FFileManager interface. This is the closest reference to our FFileManagerXbox.

4. **ImRenderDevice vs D3DDrv**: New Legends' ImRenderDevice is a ground-up D3D8 renderer.
   Our XboxRender stub needs to implement the same URenderDevice interface. The vtable at
   0x00255CEC (138 slots) shows the full interface we must satisfy.

5. **ImAudioDevice vs Galaxy**: New Legends uses DirectSound 8 via a custom UAudioSubsystem.
   Our XboxAudio stub needs the same UAudioSubsystem interface.

6. **UE1 build drift**: New Legends' engine is heavily modified from UT99 v436/v400. Classes
   like UImMesh, USkeleton, UWaterTexture, UCutsceneCamera are not in UT99. The core
   ULinkerLoad / UObject / UClass machinery is similar but not identical.

## File Paths Used by New Legends (from strings)

```
d:\Engine.u         — packages on D: drive
z:\Engine.u         — packages also probed on Z: drive (likely save/scratch)
z:\*.lin            — .lin save files on Z:
d:\Textures\        — textures directory
d:\Movies\          — Bink movie files
d:\sounds\dsstdfx.bin — DirectSound effects bank
d:\XboxObjectives.txt — objectives data
d:\NLLauncher.xbe   — launcher XBE
d:\ImRender\        — render scratch path
d:\default.xbe      — self-reference
```

## Phase 3 Findings

### VerifyImport (UnLinker.cpp) — 0x00029400

New Legends' `VerifyImport` is at 0x00029400, 1824 bytes. Assert strings confirm it shares
the same source structure as UT99 v69. Key asserts:
- `Import.ClassName==NAME_Package` at push 0x0002948a (same check as UT99)
- `Import.ClassPackage==NAME_Core` at push 0x000294a6
- `Import.PackageIndex<0` at push 0x00029549
- `Import.SourceLinker` at push 0x00029585

`Export.PackageIndex>0` and `Export.ClassIndex!=0` exist in .rdata but have no
push xrefs in .text — they are in a separate code path (export saving/validation),
not in VerifyImport.

**Fix applied to our UT99 port**: In both hash loop and linear fallback, the check
`if (ParentImport.SourceIndex==INDEX_NONE) { if (Source.PackageIndex!=0) continue; }`
was removed. In UT99 v69 packages, direct children of the root package use
PackageIndex=1 (pointing to the package's own export at index 0), not 0. The old
check incorrectly skipped Class Engine.Font (causing the current port blocker).

### appPlatformInit equivalent — 0x0004BA40

Source: `E:\depot\gow\main\Core\Src\UnXBox.cpp`

Init sequence:
1. `CALL 0x000365f0` — getter for global at `[0x00385068]`
2. Checks/creates `[0x00385198]` via `0x0004b980` — first major global (GMalloc or GLog)
3. `CALL 0x0004af10` — sets up output devices (GLog, GError): manages globals at
   `[0x003850D4]` and `[0x003850BC]`
4. `CALL 0x0003e480` (UnObj.cpp) — object construction
5. Timing calibration loop using `KeQueryPerformanceFrequency`-equivalent,
   `Frequency!=0` assert from UnXBox.cpp
6. Stores computed tick frequency to global `[0x00296440]`

### main() — 0x00174590

Source: `E:\depot\gow\main\XBoxLaunch\XBoxLaunch.cpp`
Key string pushed: `'Engine'` (for engine package load)

Call sequence:
- `0x0016e540` — XboxClient/viewport setup
- `0x0016bb00` — XboxClient method (called twice: init + game loop)
- `0x0016c1e0` — XboxClient method
- `0x00037290` (×2) — appInit-equivalent (called before and inside game loop)
- `0x001724a0` — FFileManagerLinear or file manager setup
- `0x00027420` (×2) — InitEngine-equivalent
- `0x0004b100` — tick/update call
- `0x0004b040` — audio tick?

### Global Pointer Assignments (.data)

| VA         | Set in         | Likely identity                         |
|------------|----------------|-----------------------------------------|
| 0x00385198 | appPlatformInit| First platform global (GMalloc or GLog) |
| 0x002c2f10 | appPlatformInit| Second global (GFileManager candidate)  |
| 0x00296440 | appPlatformInit| Tick frequency / performance counter    |
| 0x002c5180 | appPlatformInit| Init flag (EBX=1 stored here)           |
| 0x0029b600 | main()         | Timing/benchmark result                 |
| 0x0029b51c | appPlatformInit| GIsRunning or equivalent flag           |
| 0x002c512c | appPlatformInit| GLog or timing global                  |

### FMallocXBox

10 functions in 0x00172F00–0x00173F80. The ctor at 0x00173190 does NOT write a
vtable pointer (the vtable is set up by the compiler's generated constructor, not
in this function). The allocator uses NtAllocateVirtualMemory-equivalent for large
blocks (kernel thunk at 0x002294D0).

Realloc (0x001732E0) calls: `0x0017d0ae` (NtAllocateVirtualMemory wrapper).

### URenderDevice vtable — 0x00255CEC

138 slots confirmed. Key slots:
- Slot  24 (0x00125370): `c2 38 00` — RETN 0x38 (stub/thunk)
- Slot  27 (0x00125380): Thunk that pushes 5 args, calls vtable+0x6c
- Slot  38 (0x00083360): Full function, proper prolog 55 8B EC
- Slot  39 (0x00083000): Full function, uses FPU instructions
- Slot  42 (0x00083740): Full function (triangle/polygon rendering)
- Slot  51 (0x00163190): `c2 14 00` — RETN 0x14 (stub)
- Slot  52 (0x00174f20): `b8 01 00 00 00 c2 04 00` — returns 1

## Next Steps (Phase 3 complete → Phase 4)

Phase 3 extraction is done at the identification level. Pending hardware test of
the Font VerifyImport fix. Once that clears:

1. Next expected blocker: URenderDevice init (XboxRender.XboxRenderDevice not found
   or crashes). XboxRenderDevice stub must return non-NULL from Init() or the engine
   skips the render path gracefully.
2. After renderer: UGameEngine::Init completes → first tick → map load attempt.
3. Audio (XboxAudio.XboxAudioDevice) and Net (XboxNet.XboxNetDriver) stubs needed
   before a map can run.

The 138-slot vtable at 0x00255CEC defines the full URenderDevice interface. Our
XboxRenderDevice stub should declare all 138 virtual methods (most as no-ops/stubs).
