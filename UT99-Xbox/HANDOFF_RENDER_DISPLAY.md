# HANDOFF: UT99 Xbox Port — Black Screen Despite Working Render Pipeline

## TL;DR

The UT99-Xbox port builds, runs, and the render API surface works end-to-end.
The Xbox D3D8 runtime returns S_OK from every `Clear`, `BeginScene`,
`DrawPrimitiveUP`, `EndScene`, and `Present`. The engine ticks through the
intro level for hundreds of frames without any error. **But the CXBX-R host
window stays solid black.** OpenJKDF2 and TheForceEngine, both built with the
same XDK 5558 toolchain and running in the same CXBX-R, display correctly.
A specific structural difference in our XBE is keeping CXBX-R from blitting
our backbuffer to the host display.

The last change I made (untested at handoff time) is:
`Direct3DCreate8(0)` instead of `Direct3DCreate8(D3D_SDK_VERSION)` in
`UT99-Xbox/XboxRender/src/XboxRender.cpp`, motivated by TFE's research note
that the PC `<d3d8.h>` defines `D3D_SDK_VERSION=120` and the Xbox/CXBX-R
HLE expects 0 — passing 120 forces a fallback surface allocator that
returns 480x518 LIN_X8R8G8B8 instead of the requested 640x480. **If that
change makes the warm-up log report `backbuffer 640x480` instead of the
persistent `480x518`, it's likely the missing piece — try that first.**

## Goal

Make UT99 (v1.40 source, v436 .u data) display its CityIntro flyby in
CXBX-R on Steve's test bed. Real Xbox hardware comes after CXBX-R works.

## What works (don't regress these)

- Build via `python UT99-Xbox/Tools/build_xbox_cli.py` (do not use VS IDE).
- Toolchain is **XDK 5558** for CL/Lib/Link/imagebld; **XDK 5849** is on
  the include search path *after* 5558 as a fallback for files 5558 lacks
  (stdint.h, winsock2.h). Set in `build_xbox_cli.py`:
  - `XDK_DIR          = r"C:\XDK_5558\XDK"`
  - `XDK_FALLBACK_DIR = r"C:\XDK"`
- Lib path order: `XDK_DIR\xbox\lib` first, then `XDK_FALLBACK_DIR\xbox\lib`
  (s3tc.lib comes from 5849; everything else from 5558).
- Linked D3D lib: `d3d8.lib` from 5558 (2.1 MB full retail static lib). Do
  not link `d3d8-xbox.lib` from 5849 — it contains the debug-mode
  `DbgPrint("Invalid flags passed to Clear") + int3` validator that traps
  our Clear calls. Confirmed via `strings`.
- Linker: `/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /FIXED:NO`. We strip
  `/MERGE:.CRT=.data` and `/SECTION:.data,RW` from the inherited vcproj
  options (those broke CRT init pointer walking).
- `patchxbe.py` no longer rewrites the library version table. 5558's
  imagebld already produces a complete, correct table inside the XBE
  header region (file offset ~0x540, VA ~0x10540). The prior version
  of patchxbe appended a duplicate table at the end of the file —
  that file offset was past every section's raw range, so the VA it
  pointed at landed in unmapped memory and CXBX-R read garbage.
- Engine progresses fully: `appInit` -> `InitEngine` -> `LoadMap(Entry)`
  -> `Browse(CityIntro.unr)` -> `Possessed PlayerPawn: TMale2 CityIntro.TMale0`
  -> `Viewport->OpenWindow` -> `RenDev->Init(640x480)` succeeds ->
  `MainLoop` ticks (heartbeat every 60 ticks).
- All three engine draw entry points fire and return S_OK:
  `DrawComplexSurface` (BSP), `DrawTile` (HUD), `DrawGouraudPolygon` (mesh).
- CXBX-R's `libXbSymbolDatabase` recognises our binary and hooks the right
  D3D8 functions. Verified via
  `C:\Games\Emulators\CXBX\SymbolCache\UnrealTournament-<hash>.ini`:
  `[Libs]` lists all six v5558 libs and `[Symbols]` lists all the
  expected `D3DDevice_Clear`, `D3DDevice_Swap`, `D3DDevice_SetRenderTarget`,
  etc. The DSOUND symbols are absent (we don't link dsound.lib) but that
  doesn't affect display.

## What doesn't work

- **Host window stays solid black** despite a forced magenta Clear that
  paints the full backbuffer.
- **Wireframe diagnostic is also invisible.** Lock currently forces
  `D3DRS_FILLMODE = D3DFILL_WIREFRAME`, stage 0 `COLOROP/ALPHAOP =
  SELECTARG2(DIFFUSE)`, and every vertex's diffuse colour to
  `0xFFFFFFFF`. Geometry submits, returns S_OK, never appears.
- **CXBX-R's warm-up GetDesc still reports `480x518 LIN_X8R8G8B8` for the
  backbuffer (and matching depth-stencil)** — not the 640x480 we request.
  TFE running in the same CXBX-R reports `640x480` for the equivalent
  warm-up call. That divergence is the strongest remaining signal that
  something specific to *our* CreateDevice call is taking CXBX-R down a
  different code path. The Direct3DCreate8(0) change targets exactly this.

## The smoking-gun discovery chain (history)

This is the order things were diagnosed and fixed. Don't undo any of these.

### 1. `Invalid flags passed to Clear` + `int 3`
- Symptom: CXBX-R kernel-debug window showed `DEBUG_PRINT: Invalid flags
  passed to Clear` followed by `int 3` breakpoint exceptions, looping
  every frame. Screen was black.
- Cause: We were linking `d3d8-xbox.lib` (and earlier `d3d8d.lib`) from
  XDK 5849. Both contain the debug-mode parameter validator that emits
  this exact message. Verified via `strings` dump comparing the libs.
- Fix: Switched to 5558's `d3d8.lib` (full retail static, 2.1 MB,
  214 D3DDevice exports) — does NOT contain the validator string. To
  use 5558's libs we had to switch the active toolchain to 5558
  (CL/Lib/Link/imagebld). 5849 stays as header + lib fallback only.

### 2. Lib version table in unmapped memory
- Symptom: After 5558 switch, CXBX-R generated a symbol cache showing
  `[Libs] = 0, BuildVersion = 0`. Zero libraries detected. With no
  recognised libraries, `libXbSymbolDatabase` scanned for nothing,
  found nothing, installed zero HLE patches. All D3D calls returned
  S_OK but only because they executed the unhooked lib bytecode
  writing to NV2A registers CXBX-R's emulated GPU never sees.
- Cause: `patchxbe.py` was written for the 5849 imagebld which doesn't
  stamp D3D8/XGRAPHC library entries — patchxbe appended its own table
  to the *end of the XBE file* and rewrote `dwLibraryVersionsAddr` to
  point at it. That file offset was past every section's raw range, so
  the VA wasn't mapped at runtime. CXBX-R read garbage and gave up.
- Fix: Rewrote `patchxbe.py` to leave the lib table alone. 5558's
  imagebld produces a correct table inside the XBE header region
  (file 0x540, VA 0x10540). `patchxbe.py` now just verifies it lives
  inside the mapped header range and aborts patching the table.
- Verification: Symbol cache now lists `XAPILIB = 5558, D3D8 = 5558,
  XGRAPHC = 5558, LIBCMT = 5558, LIBC = 5558, XBOXKRNL = 5558` and a
  populated `[Symbols]` section with all D3D8 hooks.

### 3. Backbuffer dimensions still wrong even with HLE engaged
- Symptom: With HLE now actually hooking our D3D calls, the engine
  runs cleanly for hundreds of frames, every HRESULT is S_OK, but the
  host window is still solid black. Even Clear's magenta isn't visible.
- Concrete evidence: our warm-up reports `backbuffer 480x518 fmt=30`
  (LIN_X8R8G8B8); TFE's warm-up in the same CXBX-R reports
  `back-buffer 640x480 fmt=30`. **Same XDK, same CXBX-R, same exact
  PP struct fields per side-by-side comparison.** CXBX-R is taking a
  different code path for our XBE specifically.
- Current hypothesis (untested at handoff): `Direct3DCreate8` argument.
  TFE's `renderBackend_xbox.cpp:181-186` explicit comment:

  > *"Pass 0, NOT the D3D_SDK_VERSION macro. On this XDK install
  > <d3d8.h> resolves to the PC DirectX 8 header which defines
  > D3D_SDK_VERSION = 120; the Xbox runtime / CXBX-R HLE both expect 0."*

  We were passing the macro. Just changed it to literal 0. Build
  succeeded but not yet run.

## Hard evidence available

### XBE binary inspection script (one-liners are scattered in chat — preserve them)

```python
# Section dump
python -c "
import struct
with open(path,'rb') as f: d = f.read()
base = struct.unpack_from('<I', d, 0x104)[0]
n = struct.unpack_from('<I', d, 0x11C)[0]
sh = struct.unpack_from('<I', d, 0x120)[0] - base
for i in range(n):
    h = sh + i*0x38
    flags = struct.unpack_from('<I', d, h+0x00)[0]
    va    = struct.unpack_from('<I', d, h+0x04)[0]
    vsize = struct.unpack_from('<I', d, h+0x08)[0]
    raw   = struct.unpack_from('<I', d, h+0x10)[0]
    rsize = struct.unpack_from('<I', d, h+0x0C)[0]
    no = struct.unpack_from('<I', d, h+0x14)[0] - base
    name = d[no:d.find(b'\\x00', no)].decode('ascii')
    print(name, hex(flags), hex(va), hex(vsize), hex(raw), hex(rsize))
"
```

Section diff us vs OpenJKDF2 (verified):
- Ours:      `.text, D3D, XGRPH, XPP, .rdata, .data, $$XTIMAGE` (7)
- OpenJKDF2: `.text, D3D, D3DX, DSOUND, XGRPH, XPP, .rdata, .data, DOLBY, $$XTIMAGE` (10)
- D3DX / DSOUND / DOLBY are absent because we don't reference any
  symbol from d3dx8.lib or dsound.lib so the linker drops them. We
  added the libs to the link line but without `/INCLUDE:<sym>`
  directives no code from them survives. **This is probably NOT the
  blocker** (TFE renders without D3DX too) but if Codex wants to
  rule it out, force-link one D3DX symbol with
  `#pragma comment(linker, "/INCLUDE:_D3DXMatrixIdentity@4")` from
  XboxLaunch.cpp or EngineForceLinks.cpp.

### Library version table inspection
```python
python -c "
import struct
with open(path,'rb') as f: d = f.read()
base = struct.unpack_from('<I', d, 0x104)[0]
n    = struct.unpack_from('<I', d, 0x160)[0]
va   = struct.unpack_from('<I', d, 0x164)[0]
o = va - base
for i in range(n):
    e = d[o+i*16: o+i*16+16]
    name = e[:8].rstrip(b'\\x00').decode('ascii')
    mj,mn,b,fl = struct.unpack('<HHHH', e[8:])
    print(name, b)
"
```
After fix should print all six libs at 5558.

### CXBX-R symbol cache
- Path: `C:\Games\Emulators\CXBX\SymbolCache\UnrealTournament-<hash>.ini`
- Compare to: `C:\Games\Emulators\CXBX\SymbolCache\Star Wars Jedi Knight-<hash>.ini`
- After our fix in step 2, `[Libs]` and `[Symbols]` populate identically
  to OpenJKDF2's (modulo DSOUND symbols).

### App-side log
- `C:\Games\Emulators\CXBX\UT99x\ut99.log`
- Instrumentation in `XboxRender.cpp`:
  - `Lock` logs frame, hrClear, hrBegin for frames 1-30 then every 60th
  - `Unlock` logs frame, hrEnd, hrPresent, Blit for same set
  - `DCS#N`, `DT#N`, `DGP#N` with full sub-step trace for first 30 calls
    each of DrawComplexSurface, DrawTile, DrawGouraudPolygon
  - `SetSceneNode` FIRST CALL logs engine `XB/YB/X/Y` and
    `ActualBackBufferW/H`

### Reference codebases (Steve insists 1:1 alignment to these, not guessing)

| Project | Location | Use for |
|---|---|---|
| xQuake (MS official sample) | `C:\Programming\GitHub\xbox\private\test\games\xquake\gl_fakegl.cpp` | Canonical Xbox D3D8 init / per-frame flow. PRIMARY reference for full GPU pipeline. |
| OpenJKDF2 (we shipped) | `C:\Programming\GitHub\OpenJKDF2ogx\src\Platform\Xbox\` and `build_xbox.bat` | XDK 5558 toolchain setup. Currently runs visibly in same CXBX-R. PRIMARY reference. |
| TheForceEngine (we shipped) | `C:\Programming\GitHub\TheForceEngine-master\TheForceEngine\TFE_RenderBackend\renderBackend_xbox.cpp` | Software-rendered with single quad blit; **only use for init/Clear/Present basics**, not texture pipeline. Their renderBackend_xbox.cpp contains the `Direct3DCreate8(0)` discovery. |
| MS XDK samples | `C:\Programming\GitHub\xbox\private\atg\samples\graphics\` | Various D3D patterns (AlphaFog, PolynomialTextureMaps, etc.). xbapp framework in `samples\common\src\xbapp.cpp`. |
| CXBX-R source | `C:\Programming\GitHub\Cxbx-Reloaded-master\src\core\hle\` | Read this when you need to understand what CXBX-R is actually doing with our binary. `Intercept.cpp` is where HLE detection runs. |

### Reference build configs

OpenJKDF2 link.rsp (working):
```
/nologo
/OUT:openjkdf2_xbox.exe
/MAP:...
/LIBPATH:C:\XDK_5558\XDK\xbox\lib
/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /FIXED:NO
/IGNORE:4254
d3d8.lib d3dx8.lib dsound.lib xboxkrnl.lib xgraphics.lib xonline.lib libc.lib xapilib.lib
<obj files...>
```

TFE present params (640x480):
```cpp
pp.BackBufferWidth              = 640;
pp.BackBufferHeight             = 480;
pp.BackBufferFormat             = D3DFMT_X8R8G8B8;
pp.BackBufferCount              = 1;
pp.Windowed                     = FALSE;
pp.EnableAutoDepthStencil       = TRUE;
pp.AutoDepthStencilFormat       = D3DFMT_D24S8;
pp.SwapEffect                   = D3DSWAPEFFECT_DISCARD;
pp.FullScreen_RefreshRateInHz   = 60;
pp.hDeviceWindow                = NULL;
pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

s_d3d = Direct3DCreate8(0);     // <-- the discovery
s_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, 0,
    D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
    &pp, &s_device);
```

Our PP is byte-for-byte identical except we were calling
`Direct3DCreate8(D3D_SDK_VERSION)`. That is what the latest change
fixes.

## Things to try in order if `Direct3DCreate8(0)` alone isn't enough

If the warm-up STILL reports 480x518 after the Direct3DCreate8(0) change,
the include order is wrong — we're picking up the PC `<d3d8.h>` somewhere
that overrides the Xbox D3D8 types. Check `XboxRender.h` include order
(currently kills NOD3D before xtl.h then includes `<D3D8-Xbox.h>`).

If the warm-up reports 640x480 but display is still black:

1. **Add d3dx8.lib force-link** so the XBE has a D3DX section. Add
   `#pragma comment(linker, "/INCLUDE:_D3DXMatrixIdentity@4")` in
   `XboxLaunch.cpp`. Rebuild. Check sections via the Python dump.
   If section appears, run, see if anything changes.

2. **Try linking dsound.lib** the same way (just for completeness;
   audio works without it via our XboxAudio stub).

3. **Drop our entire render-state init block from Lock**. Just do
   `Clear -> BeginScene -> EndScene -> Present` with the magenta clear
   still forced. If that shows magenta, an SRSt call we make is upsetting
   CXBX-R's HLE. Bisect to find which.

4. **Set the host CXBX-R window into windowed mode**. If the Xbox-side
   `Windowed = FALSE` PP setting interacts badly with CXBX-R window
   management. Try `params.Windowed = TRUE` as a diagnostic.

5. **Read CXBX-R's `D3DDevice_Swap` HLE implementation** at
   `C:\Programming\GitHub\Cxbx-Reloaded-master\src\core\hle\D3D8\Direct3D9\Direct3D9.cpp`
   to understand exactly what it does between our Present call and the
   host window blit. Look for early-out conditions on render-target
   state, fence counters, vertex buffer state.

6. **Run CXBX-R with its kernel debug log redirected to a file** (there
   should be a `--log` or settings flag). The runtime LOG_TEST_CASE
   warnings will name what's getting rejected. Steve can't read logs
   so Codex should grep them. The user's CXBX-R is at
   `C:\Games\Emulators\CXBX\cxbx.exe`.

## Files modified (current state)

All under `C:\Programming\GitHub\UnrealTournament_1.40\`:

- `UT99-Xbox\Tools\build_xbox_cli.py` — toolchain layering, libs, link flags
- `UT99-Xbox\Tools\patchxbe.py` — verify-only (no longer rewrites lib table)
- `UT99-Xbox\XboxRender\src\XboxRender.cpp` — most current render device
- `UT99-Xbox\XboxRender\inc\XboxRender.h` — added ActualBackBufferW/H
- `UT99-Xbox\XboxLaunch\src\EngineForceLinks.cpp` — `/INCLUDE:` directives
  for every native class so static initializers run (UT99 build-time link
  necessity, unrelated to display bug). Has explanatory comments.
- `UT99-Xbox\XboxAudio\` — silent UAudioSubsystem stub. Engine config
  references `XboxAudio.XboxAudioDevice`.
- `Core\Src\UnClass.cpp` — `UClass::Bind` walks parent for ClassConstructor
  when GetDllExport returns NULL (static-lib has no DLL exports)
- `Core\Src\UnCorSc.cpp` — `GFindDynamicNative` registry implementation
- `Core\Inc\UnScript.h` — `FDynamicNativeReg` + `IMPLEMENT_FUNCTION` macro
  expansion that registers each native by name

Diagnostic state currently active in `XboxRender.cpp` (REMOVE before
shipping; keep until display works):
- Lock: `Clear(0xFFFF00FF)` forced magenta (search "magenta")
- Lock: `D3DRS_FILLMODE = D3DFILL_WIREFRAME` + stage 0
  `COLOROP/ALPHAOP = SELECTARG2(DIFFUSE)` to bypass texture sampling
- `DrawGouraudPolygon`, `DrawTile`: vertex `color = 0xFFFFFFFF` forced
- Per-call instrumentation (`DGP#N: ...`, `DCS#N: ...`, `DT#N: ...`)
  for the first 30 calls of each draw function
- Per-frame HRESULT logging in Lock/Unlock for frames 1-30 then every 60th

## Standing rules Steve has emphasised throughout

1. **Don't iterate on guesses.** Read the references. Verify before changing.
2. **Reference projects are authoritative:** xQuake (MS) and OpenJKDF2 are
   primary for full GPU pipeline. **TFE is software-rendered**, only useful
   for init/Clear/Present basics.
3. **Build via `build_xbox_cli.py`, never the VS IDE.**
4. **Snapshot files (`UT99-Xbox/snapshots/`) before non-trivial edits.**
   Has many `.zip` and bare-file snapshots from prior iterations.
5. **Hardware test comes after CXBX-R works.** Steve does the hardware
   FTP. He doesn't read logs — surface findings to him as conclusions.

## Build / run

```
# From repo root:
python UT99-Xbox/Tools/build_xbox_cli.py

# Output:
#   UT99-Xbox/build_cli/release/UnrealTournament.exe
#   UT99-Xbox/build_cli/release/default.xbe

# Steve copies default.xbe to C:\Games\Emulators\CXBX\UT99x\ and
# runs cxbx.exe pointed at it. Logs appear at:
#   C:\Games\Emulators\CXBX\UT99x\ut99.log
# CXBX-R kernel debug window separately (not redirected to file by default)
```

## Quick sanity checklist for the next agent

Before doing anything else, verify:
- Symbol cache at `C:\Games\Emulators\CXBX\SymbolCache\UnrealTournament-*.ini`
  (newest) shows `[Libs]` populated with all six v5558 entries. If `= 0`
  the lib table is in unmapped memory again — check patchxbe.py and the
  build.
- `ut99.log` warm-up: `backbuffer ...`. If 640x480 the Direct3DCreate8(0)
  fix worked; if still 480x518, the include resolution is still picking
  up the PC d3d8.h somewhere.
- All HRESULTs are still `0x00000000`. If anything else, that's the new
  signal worth following.

Good luck. The whole project's been one Russian doll of XDK-build /
linker / CXBX-R HLE gotchas — every fix has uncovered a new one. The
remaining mile feels close: backbuffer size mismatch is the only loud
runtime signal still indicating wrong behaviour at the boundary
between our XBE and CXBX-R's host display.
