# UT99 Xbox Render Crash Audit - 2026-05-16

Purpose: collect evidence for all plausible early-game render crash causes before more test/fix iterations. This is a working audit log, not a patch plan. Each suspect is ranked by current evidence against UT logs and known-good Xbox renderers/projects:

- OpenJKDF2ogx: `C:\Programming\GitHub\OpenJKDF2ogx`
- Mercenaries / RetroStrike: `C:\Programming\GitHub\Mercenaries source code\Final_Editor_And_Projects_Folders\Projects`
- RM4 + Jade GX8: `C:\Programming\GitHub\RM4+JadeSrc`
- UC2004: `C:\Programming\GitHub\UC2004`
- rvsource Xbox: `C:\Programming\GitHub\rvsource`

## Current UT Evidence

Recent UT logs show:

- Geometry validation stayed clean: `badDraw=0`, `badVert=0`, `totalBad=0`.
- Crashes are timing-variable: roughly 1-6 seconds, with some runs reaching frame 225/318.
- Memory exhaustion is not supported by logs: available memory remained high and texture live memory stayed small.
- Longest runs reached texture pool reuse and frequent texture upload/reuse activity.
- Failures did not prove texture as the only cause; they only made texture/resource lifetime the leading suspect.

## Suspect Matrix

| Rank | Suspect | Current Evidence | Reference Evidence | Status |
| --- | --- | --- | --- | --- |
| 1 | Resource lifetime while GPU still references textures or vertex buffers | Strong | UC2004, RM4/Jade, rvsource, Mercenaries all clear or fence resources | Partially patched for textures; vertex side still under audit |
| 2 | Immediate `DrawPrimitiveUP` as the whole-scene draw path | Medium-strong | UT uses it for everything; UC2004/RM4/Mercenaries mainly use VB paths for main geometry | Needs deliberate fix/test after audit |
| 3 | Scene splitting around texture/surface locks | Strong | OpenJKDF2 ends pending drawing before texture updates; RM4/Jade ends scene before surface readback | Partially patched for texture uploads |
| 4 | Stream/texture state not fully unbound after use | Strong | UC2004, RM4/Jade, Mercenaries, rvsource repeatedly clear streams/textures | Texture unbind patched; stream unbind incomplete |
| 5 | Pushbuffer pressure or lack of explicit GPU sync | Medium | OpenJKDF2 sets pushbuffer size; UC2004 can `BlockUntilIdle`; rvsource debug path kicks/block-idles | Not patched |
| 6 | Present/vsync/frame pacing | Medium | rvsource/Mercenaries commonly present at controlled cadence; UT limiter helped but randomness remains | Frame limiter present; not root-proven |
| 7 | Clear/depth/stencil ordering or flags | Low-medium | References clear before or inside known scene phases; UT now follows clear-before-begin | Seems mostly handled |
| 8 | Render-state leakage across multi-pass lightmap/macro/fog/detail paths | Medium | Reference engines reset texture stages/streams aggressively after passes | UT has some resets but not a full state cache |
| 9 | Texture format/swizzle/pitch/UV transform bug | Medium for visual bugs, lower for crash | rvsource/RM4 have explicit swizzle helpers; UC2004 uses Xbox texture headers/managed resource logic | Texture warping was a separate symptom; keep watching |
| 10 | Stack/local vertex arrays or max vertex fan assumptions | Low-medium | UT validates max 512 vertices; logs do not show bad vertices | Guarded but not eliminated |
| 11 | Early non-render systems triggering render work later | Low-medium | Not directly comparable in reference renderers | Needs logging only if render suspects fail |

## Detailed Findings

### 1. Resource Lifetime While GPU Still References Data

UT risk:

- Texture cache eviction/reuse previously released textures without a guaranteed GPU fence.
- The current texture patch unbinds texture stages and calls `BlockUntilNotBusy()` before texture release.
- Vertex buffers were not previously used for scene draws, so this risk shifts if dynamic VBs are introduced.

Reference facts:

- UC2004 `D3DResource.cpp`: Xbox texture and vertex buffer delete paths call `BlockUntilNotBusy()` before freeing/releasing Xbox resources.
- RM4/Jade `GX8VertexBuffer.c`: `Gx8_VertexBuffer_Free` calls `IDirect3DResource8_BlockUntilNotBusy`.
- RM4/Jade `GX8tex.c`: palette/resource cleanup uses `IDirect3DResource8_BlockUntilNotBusy`.
- rvsource `XBMediaDebug.cpp`: debug present path calls `D3DDevice_KickPushBuffer()` then `D3DDevice_BlockUntilIdle()`.

Conclusion:

This remains a root-level suspect, not a symptom. Any release/reuse of GPU-visible memory should either be unbound and fenced or moved to a ring/buffer scheme that avoids overwriting busy data.

### 2. Immediate `DrawPrimitiveUP` as Whole-Scene Path

UT risk:

- `UXboxRenderDevice::DrawPrimitiveVB` is still historically a wrapper around `Device->DrawPrimitiveUP`.
- It feeds stack/local arrays for BSP, gouraud polygons, tiles, 2D lines, points, and flash overlay.
- This can be timing-sensitive if the runtime internally copies into a limited push/temporary buffer.

Reference facts:

- UC2004 uses dynamic vertex streams and cached vertex buffers for main geometry:
  - `D3DResource.cpp` uses `D3DLOCK_DISCARD` / `D3DLOCK_NOOVERWRITE`.
  - It avoids busy buffers via `IsBusy()` checks.
  - `D3DRenderInterface.cpp` binds `DynamicVertexStream` and calls `DrawPrimitive`.
- RM4/Jade uses a large shared "big vertex buffer" path:
  - `GX8VertexBuffer.c` creates a large singleton vertex buffer.
  - `GX8ShiftDraw.c` uses stream sources and clears them after drawing.
  - `GX8BuildUVs.c` checks `IDirect3DVertexBuffer8_IsBusy`.
- Mercenaries/RetroStrike uses vertex buffers for transient effect geometry and double buffers particles/effects.
- rvsource uses vertex buffers for UI/font paths and checks `m_pVB->IsBusy()` in `XBFont.cpp`.
- OpenJKDF2 does use `DrawPrimitiveUP` in some compatibility paths, so this is not a universal ban. But it also has vertex-buffer paths and internal batching, which UT currently lacks.

Conclusion:

This is a major suspect, but not a guaranteed smoking gun. A controlled dynamic-VB conversion should be done as its own test candidate, with logging for VB lock/wrap/fence behavior.

### 3. Scene Splitting Around Resource Locks

UT risk:

- Texture upload/update can happen during scene rendering.
- Previous logs clustered near texture upload/reuse activity, especially `RTEXUP prelock` style entries.

Reference facts:

- OpenJKDF2 `fakeglx.cpp`: texture subimage/update path calls `internalEnd()` before updating a texture because pending drawing may still reference old texture state.
- RM4/Jade `GX8request.c`: snapshot/readback path ends the current scene before locking a surface, then begins scene again afterward.
- RM4/Jade AfterFX code has many explicit `BeginScene` / `EndScene` pass boundaries.

Conclusion:

The existing UT scene split around texture upload is evidence-supported. If crashes persist, the next log should tell whether the failure moved away from texture locks.

### 4. Stream/Texture State Unbinding

UT risk:

- Texture stage cache only tracked stages 0 and 1; the hardware may keep more stage state.
- Stream source state is not treated as a resource ownership hazard.

Reference facts:

- UC2004 `D3DRenderState.cpp`: reset state clears hardware texture stages and clears stream source when needed.
- UC2004 `D3DResource.cpp`: before vertex buffer release, it unsets any bound stream source.
- RM4/Jade `GX8ShiftDraw.c` clears stream sources 0-3 after draw paths.
- RM4/Jade `GX8init.c` clears streams 0-5 in some geometry paths.
- Mercenaries/RetroStrike clears texture stages 0-3 after pass work and clears streams in effect rendering.
- rvsource clears texture stage 0 after UI draws and clears stages before resource unload/reset paths.

Conclusion:

Texture unbinding is partially handled in UT now. Stream unbinding should be audited and probably added around dynamic VB or shutdown/flush paths.

### 5. Pushbuffer Pressure / GPU Sync

UT risk:

- UT renders many small primitives and state changes.
- The crash timing varies, and the frame limiter helped but did not eliminate risk.
- No current logging directly measures pushbuffer or GPU idle state.

Reference facts:

- OpenJKDF2 sets pushbuffer size before device creation in `fakeglx.cpp`.
- UC2004 Xbox path calls `SetPushBufferSize(1024 * 1024, 32 * 1024)` before device creation.
- rvsource debug path can `KickPushBuffer` and `BlockUntilIdle`.
- OpenJKDF2 research notes call out `D3DDevice_KickPushBuffer`, `BlockUntilIdle`, and `IsBusy` as important Xbox sync primitives.

Conclusion:

This is plausible, especially with many `DrawPrimitiveUP` calls and texture updates. It should not be the first blind patch because earlier pushbuffer experiments caused clear/depth issues, but it should be logged and tested deliberately if resource/VB changes are not enough.

### 6. Present / Vsync / Frame Pacing

UT risk:

- Before the limiter, the game could hammer Tick->Present too fast.
- Limiter helped but did not make the crash deterministic or solved.

Reference facts:

- rvsource uses `D3DPRESENT_INTERVAL_IMMEDIATE` in some paths, but has a normal game loop/present cadence.
- Mercenaries sample path uses `D3DPRESENT_INTERVAL_ONE_OR_IMMEDIATE`.
- UC2004 Xbox path forces immediate in some Xbox setup but has a mature render pipeline and resource manager.

Conclusion:

Frame pacing is a contributor, not currently the best root cause. Keep limiter; do not use it as a substitute for resource correctness.

### 7. Clear / Depth / Stencil Ordering

UT risk:

- Earlier code had clear/depth/stencil issues.
- Current UT clears target/z/stencil before `BeginScene`.

Reference facts:

- OpenJKDF2 clears through its fake GL layer and begins scene lazily.
- RM4/Jade and rvsource clear target/depth/stencil in standard render phases.
- Mercenaries sample clears target/depth/stencil before begin/draw/end/present.

Conclusion:

Currently lower priority. Leave logging in place for `Clear`/`BeginScene`/`EndScene` failures, but do not chase unless logs show nonzero HRESULTs.

### 8. Render-State Leakage Across Multi-Pass Surface Paths

UT risk:

- `DrawComplexSurface` switches between base texture, lightmap, macro, and fog passes.
- A bad state transition can produce visual corruption or crash only after a specific surface/pass appears.

Reference facts:

- RM4/Jade AfterFX and GX8 paths aggressively clear textures after pass sections.
- Mercenaries clears texture stages after shader/pass work.
- UC2004 has a deferred state manager with explicit reset/clear functions.

Conclusion:

Medium suspect. Current logs should include enough draw op names (`DCS-base`, `DCS-light`, `DCS-macro`, `DCS-fog`, `DGP`, `DT`) to identify if the last successful operation clusters on one pass.

### 9. Texture Format / Swizzle / UV Transform

UT risk:

- Texture warping was visible and separate from the crash.
- Tall/non-square texture handling had custom UV/index swapping.
- DXT/S3TC is disabled for now, reducing but not eliminating texture-format risk.

Reference facts:

- rvsource `XBUtil.cpp` includes explicit swizzle/unswizzle helpers.
- RM4/Jade has texture reload/readback paths and swizzle-style utilities.
- UC2004 uses Xbox-specific texture creation/header paths for console.

Conclusion:

Keep as visual correctness suspect and a secondary crash suspect. It is less supported by current crash logs than resource lifetime or immediate draw path.

### 10. Stack Vertex Arrays / Fan Size

UT risk:

- UT builds `FXboxTLVertex Verts[XBOX_MAX_VERTS]` and similar local arrays for every draw.
- Overflow or bad transformed vertices would be catastrophic.

Current evidence:

- `RenderValidateTLVertices` found no bad TL vertices in multiple crash logs.
- `maxPoly` in logs stayed well below 512.

Conclusion:

Not eliminated, but currently low priority. Keep validation and max-poly logging.

### 11. Early Non-Render Systems Triggering Render Work Later

UT risk:

- Decals, dynamic lights, particles, audio callbacks, or actor tick could start after the first few seconds and call different render paths.

Current evidence:

- UT Xbox client config has `Decals=0` and `NoDynamicLights=1`.
- `SupportsFogMaps=0`, detail textures disabled, high-detail actors disabled.
- The logs still show render activity before the crash, so render/resource remains more likely than pure game logic.

Conclusion:

Do not ignore, but only escalate if render pipeline fixes fail and logs show crash before/without render operations.

## Proposed Order Of Attack

1. Finish the dynamic vertex buffer candidate cleanly, with older-XDK-safe flags and explicit stream unbinding/logging.
2. Add resource-fence logging, not more noisy draw logging: texture release, VB release, stream unbind, scene split, present.
3. If crash remains near texture upload/reuse, audit exact LockRect/swizzle/pitch path against UC2004/RM4/rvsource.
4. If crash moves to draw/VB activity, refine the dynamic VB ring/fencing behavior against UC2004/RM4.
5. If crash remains random with no last-op clustering, add optional `KickPushBuffer`/`BlockUntilIdle` probes at controlled boundaries only.
6. Only after render evidence weakens, instrument early non-render systems.

## Notes On Current Worktree

After the audit, suspect #2 was tackled deliberately: `UXboxRenderDevice::DrawPrimitiveVB` now routes scene draws through an Xbox vertex buffer instead of using `DrawPrimitiveUP` for the whole scene. The implementation is conservative for the 5558 header: create one 512 KB VB, append with `D3DLOCK_NOOVERWRITE`, fence with `BlockUntilNotBusy()` when wrapping, unbind stream 0 before present/release, and log `RVB create`, `RVB wrap`, `RVB lock-failed`, `RVB stream-failed`, `RVB draw`, plus per-frame `vbLocks/vbWraps/vbKB`.
