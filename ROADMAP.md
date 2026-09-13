# Unreal Tournament 99 Xbox Roadmap (Historical)

**Closed September 12, 2026:** v1.2 is the final release. No further releases are planned. The roadmap below is retained as historical planning, not an active work queue. See [final release status](UT99-Xbox/Docs/FINAL_RELEASE_1.2.md).

This roadmap tracks the original Xbox port from hardware boot to a complete console-quality release. Real Xbox hardware is the source of truth; CXBX-R is useful only when it shortens diagnosis without creating emulator-only assumptions.

## Historical Gate

The current gate is hardware package loading:

- `main()` enters and opens `D:\ut99.log`.
- `appInit()` completes.
- `GSys->Paths` loads from config.
- `InitEngine()` begins loading `Engine.GameEngine`.
- Hardware reaches `Engine.u` and dependency loading for `Core.u`.

Next expected test result: the expanded `ut99.log` should identify whether package loading stops while opening the package file, reading the summary, loading names/imports/exports, or verifying imports.

## Phase 1: Boot And Package Load

Goal: reliably load Unreal packages and resolve `Engine.GameEngine` on hardware.

- Keep `D:\ut99.log` as the first diagnostic channel.
- Make package/file loading failures self-report in the log.
- Finish `Engine.u` and `Core.u` linker load and import verification.
- Confirm config paths, cache path, save path, and `../` resolution on real Xbox.
- Remove or downgrade temporary diagnostics once the package loader is stable.

Exit criteria:

- `StaticLoadClass("Engine.GameEngine")` returns a valid `UGameEngine` class on hardware.
- Package load failures produce enough log context to fix without a debugger.

## Phase 2: Engine Init

Goal: construct and initialize `UGameEngine`.

- Confirm `ConstructObject<UGameEngine>` succeeds.
- Run `UGameEngine::Init()` far enough to create the client and render device.
- Validate Xbox config values for `ViewportManager`, `GameRenderDevice`, audio, networking, and startup map.
- Replace or stub platform services that assume Win32 desktop behavior.
- Ensure fatal errors unwind to the hardware log.

Exit criteria:

- `InitEngine()` returns a valid engine object.
- `UGameEngine::Init()` completes or reaches a clearly logged next subsystem blocker.

## Phase 3: First Frame

Goal: present a visible frame on hardware.

- Create an Xbox viewport through `XboxDrv.XboxClient`.
- Initialize `XboxRender.XboxRenderDevice`.
- Clear and present a 640x480 frame.
- Enter `MainLoop()` and tick several frames.
- Keep controller polling safe even before gameplay is active.

Exit criteria:

- Hardware shows a visible frame.
- The log confirms repeated engine ticks.

## Phase 4: Map Load

Goal: load and enter a real UT99 map.

- Load `Entry.unr` or a small test map.
- Resolve package dependencies for maps, textures, sounds, music, and scripts.
- Fix serialization or alignment assumptions that break on Xbox.
- Validate save/cache directory behavior.
- Track memory pressure during map load.

Exit criteria:

- A map loads on hardware without fatal error.
- Actor ticking begins in the loaded level.

## Phase 5: Playable Loop

Goal: one player can stand in a map, move, look, shoot, and see the HUD.

- Finish gamepad mapping for movement, looking, fire, alt-fire, jump, use, weapon cycling, menu, and scoreboard.
- Make camera, pawn possession, input axes, and sensitivity feel usable on a controller.
- Stabilize world rendering, actor rendering, HUD tiles, fonts, masked textures, and translucent effects.
- Add enough audio for weapon/world feedback if audio is not already working by this phase.
- Keep frame pacing and memory usage within 64MB retail Xbox constraints.

Exit criteria:

- One-player local gameplay is usable on real hardware.
- Common first-map interactions do not crash.

## Phase 6: Console Feature Completion

Goal: make the port feel like an Xbox-native release, not just a PC build that boots.

- Four-player local splitscreen.
- System Link multiplayer.
- Combined splitscreen plus System Link where feasible, such as multiple local players per Xbox across linked consoles.
- Xbox-friendly menus for player count, controller assignment, network setup, map selection, mutators, and match settings.
- Durable save/config handling for controller preferences and player profiles.
- Audio completeness, including music strategy if direct tracker playback is too expensive.
- Renderer correctness pass for lighting, fog, coronas, skyboxes, decals, particles, weapon models, and UI.
- Performance optimization for CPU, GPU, memory, texture cache, and load times.
- Packaging/deployment workflow for retail hardware.

Exit criteria:

- Four-player splitscreen works locally.
- System Link works between consoles.
- A combined local-plus-link scenario is either supported or explicitly scoped with a documented hardware/memory reason.
- The game can be installed, launched, configured, played, and exited repeatedly on real Xbox hardware.

## Ongoing Rules

- Prefer complete code traces before patching engine behavior.
- Treat real Xbox logs as authoritative.
- Keep emulator-specific assumptions out of core fixes.
- Avoid nxdk; this port targets XDK 5849.
- Preserve upstream UT99 code where possible and isolate Xbox-specific behavior under `UT99-Xbox` or guarded compatibility files.
