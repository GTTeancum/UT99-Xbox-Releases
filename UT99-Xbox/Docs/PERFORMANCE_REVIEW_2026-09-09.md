# Performance review — 2026-09-09

This is an ongoing engineering review, not closure of OPEN-008 or hardware
qualification. The canonical output is `build/default.xbe`.

## Implemented changes

- `Render/Src/UnMeshRnLOD.h`: face lists and collapsed wedges now use the
  existing GMem scratch stack instead of allocating/growing/freeing TArrays
  for every actor/view. Full-detail meshes read their original wedges directly.
  Face capacity is bounded by Mesh->Faces.Num(); each face is appended at most
  once. Scratch lifetime is the existing DrawLodMesh mark, including recursive
  rendering. The no-environment-texture return now unlocks textures and pops
  that mark. Geometry, lighting and LOD selection remain the same.
- `XboxRender.cpp`: common triangle submissions use eight inline vertices
  rather than a 512-vertex stack array. Larger clipped fans allocate their
  actual count. Texture binding now precedes UV conversion, fixing stale scales
  on the first polygon after material changes. Saturated nonnegative RGB
  conversion uses truncation instead of three out-of-line floor calls per vertex.
- `XboxRender.cpp`: BSP base/lightmap/macro/fog passes reuse one surface-local
  scratch buffer. Up to 16 vertices stay inline; larger polygons reserve their
  actual maximum once outside all polygon loops. The 512-vertex limit, pass
  order and vertex values remain unchanged. Native compiler output confirms
  the fixed stack reservation drops from 16,416 bytes plus a stack probe to
  540 bytes. DrawPrimitiveUP consumes the data during the call; the VB fallback
  copies it before returning, so the next polygon can safely reuse the buffer.
- `XboxAudio.cpp`: persistent ambient voices cache their last successfully
  submitted volume and playback rate. Unchanged volume avoids the logarithm
  and DirectSound call; failed submissions are retried. Slot reset clears the
  cached settings, and changes in global volume still propagate.
- `FFileManagerXbox.h`: in-buffer seeks retain already-read package bytes.
  Precache retains that buffer and Serialize refills on exhaustion. Direct
  reads invalidate the consumed buffer so later cached seeks cannot read stale
  data. This addresses nested export loading without increasing buffer size.
- Diagnostic marker presence and absence are cached at startup in the game,
  renderer and controller paths. Previously the shared helper cached positive
  results but retried two filesystem paths for every missing marker on every
  call. Test markers must be staged before launch; the test tools already do so.

Earlier work in HALOUT_AND_PERFORMANCE.md covers final-frame display correction,
presented-frame FPS accounting, skeletal pose reuse and secondary console setup.

## Idle rendering microbenchmark

The table below has zero bots and neutral player input. It is not combat FPS.

`run_xemu_stress_matrix.py --frontend-loading-proof split-smoke
--split-benchmark --split-players 4` creates real local players with neutral
process-local input. It avoids the controls proof's weapon switching, menus and
pauses. `XSPLIT PERF` now averages draw time over a window, and `XFRAME` averages
engine work separately from the frame limiter. No host input or capture is used.

`summarize_frame_benchmark.py` reports matching guest-time windows. Final
comparisons use 40,000–75,000 ms inside 60-second live soaks, excluding boot.
The baseline XBE and matching linker map are retained together under
`build_cli/perf_20260909_baseline`. Never pair a different XBE with that map.

The capture trigger is resolved before launching Xemu. Baseline and updated
builds alternate for four, three and two players, then repeat the four-player
pair in the same order. Each reported FPS/work value averages seven five-second
measurement windows. Draw time averages 17–18 shorter windows.

| Players/run | Baseline FPS | Updated FPS | Baseline work ms | Updated work ms | Baseline KB/tick | Updated KB/tick |
|---|---:|---:|---:|---:|---:|---:|
| 2 | 57.726 | 58.771 | 15.183 | 12.872 | 22.814 | 0.971 |
| 3 | 38.347 | 43.020 | 24.028 | 21.190 | 35.050 | 1.454 |
| 4, first pair | 27.407 | 33.901 | 34.825 | 27.470 | 43.108 | 1.937 |
| 4, repeated pair | 27.834 | 32.659 | 33.996 | 28.652 | 43.118 | 1.938 |

Evidence directories are `build_cli/perf_controlled_{baseline,optimized}{2,3,4}`
and the two `4_repeat` directories. The combined machine-readable summary is
`build_cli/perf_controlled_comparison.json`. All eight soaks passed; there were
no fatal errors, missing-base-texture warnings or steady texture uploads.

Two-player FPS is limited by the existing 60 Hz cap. Allocation traffic is
bytes requested from the heap per engine tick, not resident memory. These
tests have neutral local players and no bots; they isolate rendering cost
and do not represent a combat workload. Baseline and updated logs had no
observed ScriptWarnings in this timing window. All three updated midpoint
captures were inspected for world, weapon and HUD rendering.

This is emulator evidence, not a prediction of Xbox hardware FPS. Host load
still varies, so the observed range is more useful than an exact gain percentage.
Earlier `perf_20260909_*` timing runs are retained as exploratory evidence;
they predate the final BSP/capture fixes and are not the headline comparison.

## Checks and review coverage

The later `build_cli/combat_pilot4` run used four moving local players and eight
active bots. Its 121.5-second live soak passed movement, firing, death and bot-count
checks, averaging 29.625 steady FPS in Xemu. The final counters recorded 58 deaths
and movement from all four local players. This is a workload-validation pilot;
there is not yet a matching baseline with the same combat harness, so it does not
establish a combat performance gain. Native recording runs are excluded from
performance comparisons because capture adds host work.

- Release/XBE builds passed, including the final BSP scratch-buffer change.
- `test_xbox_archive_reader.py` compiles the actual reader class against counted
  in-memory I/O. Cached nested seeks perform no extra I/O; buffered/direct read
  transitions, 10,000 randomized seek/read operations and EOF detection pass.
- The Jailbreak travel harness now removes inherited character/state and
  split-control proof markers. The source runtime carried a character marker
  that would otherwise make its bots invulnerable during match completion.
- Rapid native capture was verified with three successive screenshots: two
  used the same Xemu filename within one second. The helper now detects file
  rewrites and requires the PNG signature and final IEND chunk before copying.
  The initial character-state run was stopped after this capture-tool failure;
  it is not counted as a gameplay or visual pass.
- Traced renderer allocation ownership, texture-batch transitions, stage scale
  ordering, retained scratch chunks and all DrawLodMesh return paths.
- Traced ambient voice creation/reset, volume changes, music packet servicing
  and primary-view-only audio update. Audio already updates once per composite
  frame; no additional skip was introduced.
- Inspected package lookup, export seek/precache/restore, archive buffering,
  allocator accounting, main-loop limiter and production diagnostic gates.
- Inspected nonblocking gameplay UDP receive and menu-only discovery loops.
  No networking optimization is claimed or shipped in this pass.
- Inspected actor ticking, collision link allocation/recycling, point/radius/
  line queries and physics substeps. Collision links already use a bulk free
  list and query results use scratch memory. Actor tick ownership and physics
  cadence are preserved. Long diagonal line queries still traverse the entire
  bounding box; profiling and a collision-equivalence test are needed before
  replacing that traversal.
- Reviewed existing texture hash lookup, resident eviction, render-state
  caches and native music packet refill. Those existing optimizations remain;
  no extra state cache or audio quality reduction was added.
- Release modules already compile with optimization enabled; changing the
  toolchain or globally weakening floating-point semantics is unnecessary.

## Still open

Repeat and broaden timing; stress moving stock and skeletal actors; validate
reader changes during map loads/travel; verify sound on hardware; review the
remaining engine/script/build-tool paths and long-play resource pressure.
Skeletal LOD is currently deliberately disabled by an existing compatibility
guard. Re-enabling it requires repairing/qualifying collapse data, not removing
the guard for an easy FPS claim. No visual quality reduction is part of this pass.

## Build identity

SHA-256 for this test cohort (XBE and map must remain paired):

| File | SHA-256 |
|---|---|
| Baseline `default.xbe` | `5BC07DC9F67793538ACECE460B83140144B74E30F726B4E3950B183777A9C916` |
| Baseline `UnrealTournament.map` | `944889B8AF5436F8C1C72742611C2765ECB57CD2170C9F294FB0B530435DB574` |
| Before BSP edit `perf_20260909_pre_bsp/default.xbe` | `365E137AE0F9D1CD7AC2E21AB4CD430A9DFB59BF5038515BB8BC0E08FA726B90` |
| Before BSP edit `perf_20260909_pre_bsp/UnrealTournament.map` | `B222BACF16CB41F91E099B8F7DB07C0AA7B9E8FD07FEEC4D6AF40F173B00A762` |
| Final `build/default.xbe` | `201E4ABFAAFAFF96DC1156335CE4CD3AF9EE0AF2C6F3930CC216CB098810B4B3` |
| Final `build/UnrealTournament.map` | `DA0D53E7EBABE6EC1FED408DAFA0AEEF26A92A797567C050A70DA57704580845` |
