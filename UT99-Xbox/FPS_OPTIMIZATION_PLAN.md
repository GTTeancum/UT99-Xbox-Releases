# UT99 Xbox FPS Optimization Plan

Last updated: 2026-05-17

This plan is ordered by expected performance gain, based on current `PERF` logs.

## Current Evidence

- Menu / low-scene frames can reach about 57-59 FPS, so present/swap is not the main limiter.
- Gameplay frames show `presentMS` around 1-4 ms but `renderMS` as high as 40-142 ms.
- Hot gameplay frames issue roughly 1,100-1,700 primitive submissions and nearly the same number of dynamic vertex-buffer locks.
- `DGP` actor/mesh polygons are often the largest single count, commonly 650-1,180 per hot gameplay frame.
- BSP `DCS` is comparatively stable around 150-180 surfaces in active frames.
- Texture uploads are low after warmup, usually 0-2 per logged frame, so texture creation/upload is not the primary FPS bottleneck right now.
- Generated config was still enabling dynamic lights/decals/screen flashes; this has been corrected and must be verified in the next log.

## 1. Batch Actor/Mesh Rendering (`DrawGouraudPolygon`)

Expected gain: largest.

Status: first conservative implementation added 2026-05-17; awaiting runtime log verification.

Current problem:

- Actor/mesh rendering is submitting hundreds to more than a thousand tiny `DGP` triangle fans per gameplay frame.
- Each call currently pays fixed D3D state/stream/lock/draw overhead.

Plan:

- Build a per-frame mesh batcher for compatible `DGP` calls.
- Batch by texture, poly flags, draw state, and vertex format.
- Convert compatible fans to triangle lists in one shared buffer.
- Flush on texture/state changes, frame end, or buffer full.
- Keep the old immediate path for unusual flags until proven safe.

Why first:

- It attacks both high `DGP` counts and high `vbLocks`, which are the strongest logged costs.

## 2. Batch HUD/Tile Rendering (`DrawTile`)

Expected gain: high, especially during UI-heavy frames.

Current problem:

- Even non-gameplay frames show 200-300 `DT` calls.
- These are small quads that should be cheap if batched, but expensive as individual draw submissions.

Plan:

- Batch compatible tiles by texture and blend flags.
- Emit quads as triangle lists into a 2D dynamic buffer.
- Flush before world state changes, flash overlays, or frame end.
- Keep invalid-Z rejection.

Why second:

- This reduces overhead everywhere, including menus and HUD-heavy gameplay. It is less likely than `DGP` to solve the 8-18 FPS gameplay frames alone, but it is straightforward and broadly useful.

## 3. Reduce Per-Draw Texture/State Churn

Expected gain: medium-high.

Current problem:

- Hot gameplay frames show 200+ texture binds, even when texture uploads are near zero.
- `SetBlending`, texture-stage setup, and stream setup are being touched often.

Plan:

- Add stricter state shadowing for render states, texture-stage states, vertex shader/FVF, stream source, and bound textures.
- Avoid redundant `SetTextureStageState`, `SetRenderState`, and `SetVertexShader` calls.
- Log a compact state-change count in `PERF` once implemented.

Why third:

- Batching reduces call count first. State shadowing then makes remaining calls cheaper.

## 4. BSP Surface Batching / Static World Cache

Expected gain: medium, potentially high on larger maps.

Current problem:

- `DCS` count is not as explosive as `DGP`, but world BSP is stable and repeatedly rebuilt/submitted each frame.

Plan:

- Start with per-frame batching of compatible BSP polygons by base texture/lightmap state.
- Longer term, evaluate static/indexed caches for level BSP surfaces.
- Do not modify map files. Any cache must be runtime-only and rebuildable.

Why fourth:

- The current test map's worst frames are dominated more by actors/meshes than BSP. BSP caching becomes more important after `DGP` and `DT` overhead are reduced.

## 5. Verify And Keep Expensive Visual Features Off

Expected gain: medium, plus visual stability.

Current problem:

- Config load was overriding Xbox defaults, enabling dynamic lights, decals, and screen flashes.
- Dynamic lights are a plausible source of distant lit/unlit flicker.

Plan:

- Verify next log shows `XboxClient::Init: settings flashes=0 decals=0 dynLights=1`.
- Keep `NoDynamicLights=True`, `Decals=False`, `ScreenFlashes=False` for baseline performance.
- Only re-enable one feature at a time after the renderer is fast enough.

Why fifth:

- This is important but already mostly patched. It should be verified before judging the next optimization pass.

## 6. Texture Cache And Lightmap Cleanup

Expected gain: low-medium for FPS, medium for stability.

Current problem:

- Texture upload is not the main FPS cost now, but stage-1 lightmaps and tiny realtime textures still add occasional scene splits.

Plan:

- Reduce scene splits during texture upload where safe.
- Consider prewarming or deferring tiny realtime lightmap uploads.
- Keep deterministic texture-stage restore.

Why sixth:

- Logs show uploads/splits are present but not the main bottleneck compared with draw submission.

## 7. Depth/Flicker Investigation

Expected gain: visual correctness more than FPS.

Current problem:

- If flicker remains with dynamic lights off, it is likely depth precision/z-fighting or pass ordering.

Plan:

- Verify dynamic lights are actually off first.
- If flicker remains, inspect far-plane/near-plane precision and masked/multipass `ZFUNC=EQUAL` behavior.
- Add targeted one-line diagnostics only if needed.

Why seventh:

- It may fix visible flicker, but current FPS evidence points elsewhere for performance.

## Execution Order

1. Verify config enforcement in the next log.
2. Implement `DGP` batching.
3. Implement `DT` batching.
4. Add state-shadow counters and remove redundant state calls.
5. Reassess `PERF`; only then decide whether BSP runtime caching is worth the complexity.
