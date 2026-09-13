# UT99 Xbox FPS Optimization Plan

Last updated: 2026-05-17

This plan is ordered by expected performance gain, based on current `PERF` logs.

## Current Evidence

- Autonomous CXBX-R smoke harness starts/respawns/fires by creating `D:\XboxAutoFireSmoke.ini`; normal boots are unaffected when the file is missing.
- Current gameplay smoke is now held near the 60 Hz Xbox main-loop limiter: sampled active frames are roughly 58-59 FPS.
- The renderer still logs high primitive counts (`DGP` often 900-1500+, `DCS` around 150-180), but render time is now low enough for the 60 FPS target.
- Mid-scene texture upload splits are mostly eliminated after realtime texture deferral (`splits=0` in almost all hot frames).
- State caching skips about 1,600-3,000 redundant D3D state calls per sampled frame.
- `DrawPrimitiveUP` is accepted by CXBX-R/Xbox D3D8 in the current build (`up=.../0`) and eliminates the 180-390 dynamic VB locks per active frame (`vbLocks=0`).
- Remaining 58-59 FPS ceiling is expected: `XboxEngine.cpp` currently caps the main loop at `XboxMaxTickRate = 60.0f`.

## 1. Batch Actor/Mesh Rendering (`DrawGouraudPolygon`)

Expected gain: largest.

Status: implemented and verified. Runtime logs show effective batching, now using a larger draw-call vertex limit while preserving smaller per-polygon stack buffers.

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

Status: implemented and verified. Follow-up fix uses the larger draw-call vertex limit after logs showed oversized tile batches were being rejected by the old guard.

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

Current status:

- Dynamic VB stream-source stride caching added first.
- BSP stage-1 lightmap state is now retained across multitexture BSP surfaces and disabled only when leaving that path.
- Render-state, texture-stage-state, and vertex-shader calls are now cached; current smoke logs show thousands of skipped redundant calls per sampled frame.
- `DrawPrimitiveUP` now bypasses the explicit dynamic-VB lock/unlock path, with VB fallback left in place if UP ever fails.

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
- Force `MinDesiredFrameRate=60` so `bDropDetail` and `bAggressiveLOD` engage whenever the port is below target.
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

Current status:

- Existing cached realtime texture entries are reused instead of forcing a mid-scene upload; first-time texture creation/upload remains intact.
- `PERF` now reports deferred refreshes as `texDef`.

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

1. Steve hardware/visual test of the current candidate.
2. If Steve confirms visual correctness, commit the optimization candidate.
3. If the real Xbox still misses target, profile against hardware-specific costs before adding larger BSP runtime caches.
4. Defer any 120 FPS experiments until timing policy is explicit; current main loop intentionally caps at 60 Hz.
