# UT99 Xbox Render Attempts - 2026-05-16

Purpose: running ledger of attempted fixes, evidence produced, and what each attempt did or did not rule out. Reference this before every new render/crash change.

## Baseline Context

- Target: original Xbox/XDK 5849 build, currently tested in CXBX-R from `C:\Games\Emulators\CXBX\UT99x\default.xbe`.
- Log checked first each time at `C:\Games\Emulators\CXBX\UT99x\ut99.log`.
- Known-good reference projects audited: OpenJKDF2ogx, UC2004, RM4/Jade, rvsource, Mercenaries/RetroStrike.
- Current failure pattern: rendering starts, often runs roughly 1-6 seconds, then crashes or freezes. Timing varies enough that one successful longer run is not proof of a fix.

## Attempts So Far

### 1. Basic D3D Present/Clear Path

Change/evidence:

- Moved to a conventional Xbox D3D frame shape: clear target/z/stencil before `BeginScene`, draw, `EndScene`, then `Present(NULL,NULL,NULL,NULL)`.
- Previous logs showed `Clear`, `BeginScene`, `EndScene`, and `Present` returning success on completed frames.

Result:

- Rendering became visible.
- Not sufficient: crashes/freezes continued after several rendered seconds.

Status:

- Keep this structure. Current logs do not implicate clear/present HRESULT failures.

### 2. Texture Upload/Reuse Resource Safety

Change/evidence:

- Added texture cache reuse logging and memory accounting.
- Texture release/reuse now unbinds texture stages and uses `BlockUntilNotBusy()` before release.
- Texture upload path splits the active scene around texture creation/locking/upload.
- Texture memory stayed low in logs; available physical memory stayed high.

Result:

- Textures became visible.
- Not sufficient: crashes/freezes continued and were not backed by memory exhaustion evidence.

Status:

- Texture lifetime remains a plausible suspect, but not proven as the only issue.

### 3. Texture/UV Warping Investigation

Change/evidence:

- Added validation around transformed/lit vertices and texture dimensions.
- Logs repeatedly showed `badDraw=0`, `badVert=0`, and low `maxPoly` values near crashes.
- Warping was treated separately from the crash because geometry validation did not show invalid TL vertices.

Result:

- Visual correctness improved compared with magenta/wireframe/no-texture states, but Steve still observed warping in later builds.

Status:

- Keep UV/format suspicion open for visual correctness. Current crash evidence is stronger around timing/resource/tick boundaries than invalid vertex data.

### 4. Frame Limiter

Change/evidence:

- Added a 60 Hz cap/yield in `MainLoop`.
- This was based on the fact that uncapped UT can hammer `Tick->Draw->Present`.

Result:

- One run reached frame 227/normal rendering and generally improved behavior.
- Not sufficient: repeated runs still crashed/freezed quickly.

Status:

- Keep limiter. Treat it as reducing pressure, not as a root fix.

### 5. Whole-Scene `DrawPrimitiveUP` Replacement

Change/evidence:

- Replaced the main transient scene draw wrapper with a persistent 512 KB Xbox vertex buffer.
- Uses append-style locks with `D3DLOCK_NOOVERWRITE`, wraps with `BlockUntilNotBusy()`, unbinds stream 0 before present/release.
- Added per-frame VB telemetry: `vbLocks`, `vbWraps`, `vbKB`, totals, and failure logging.
- This was based on UC2004/RM4/Mercenaries/rvsource using dynamic/static VBs for core rendering rather than relying on `DrawPrimitiveUP` for every scene primitive.

Result from latest log:

- Frame 251 completed successfully:
  - `present=0x00000000`
  - `badDraw=0`, `badVert=0`, `totalBad=0`
  - `vbLocks=162`, `vbWraps=0`, `vbKB=29`
  - `liveKB=644`, `availKB=81652`
- Last line was `MainLoop: pre-tick 252 dt=0.053`.
- No `RBEGIN`/`XCLIENT` marker for tick 252 appeared afterward.

Status:

- Dynamic VB path works mechanically but did not solve the root issue.
- Latest evidence moves the immediate failure window to early `UGameEngine::Tick` before the next client/render pass is reached.

## Current Evidence-Based Next Step

The latest log does not show failure in the new VB draw, VB wrap, texture memory exhaustion, or `Present`. It shows the previous rendered frame completed and the next engine tick started. The next build should:

- Reduce hot-frame per-draw logging noise.
- Add coarse `UGameEngine::Tick` phase markers around the known crash window.
- Add coarse `ULevel::Tick` phase markers in the same window.
- Avoid another broad render rewrite until the new markers identify whether the hang/crash is in static tick/cache, level tick/actor tick, travel/pending, or client/render.

### 6. Engine/Level Phase Logging Window

Change/evidence:

- Reduced per-draw boundary logging and added coarse `UGameEngine::Tick` / `ULevel::Tick` markers.
- Latest crash log reached `MainLoop: pre-tick 225 dt=0.026` and stopped there.
- Tick 224 completed through `XCLIENT tick=224 end` and `MainLoop: post-tick 224`.
- No `XCLIENT tick=225 begin` appeared, so the crash is before the next viewport/client draw.
- The first phase-window build accidentally covered engine ticks 240-280, while this run died at engine tick 225.

Result:

- Stronger evidence that the immediate failure is pre-render on this run.
- Not enough granularity yet because engine tick 225 was outside the logged engine phase window.

Status:

- Expand `UGameEngine::Tick` phase logging to ticks 180-280.
- Expand `ULevel::Tick` phase logging to the corresponding 360-560 range.
- Add throttled actor progress breadcrumbs for level ticks 420-500 so an actor-tick crash can be localized without restoring the massive render log.

### 7. Smoke Actor Tick/Physics Burst

Change/evidence:

- Latest crash reached `XTICK tick=218 level-begin`, then `XLEVEL tick=435 actors-begin`.
- The last completed actor breadcrumb was actor index 288, class `SmokeGenerator`, name `SmokeGenerator16`.
- The level had roughly 724 live actors, mostly `UT_SpriteSmokePuff` transient effects.
- The crash occurred before the next actor breadcrumb at index 320 and before returning to client/render.
- `SmokeGenerator`, `UT_SpriteSmokePuff`, and the relevant tick/physics/lifetime code match `!ORIGINAL_SRC`, so this does not look like a source merge typo in those classes.

Result:

- Evidence now points to transient smoke actor pressure during level tick, not render `Present`, texture upload, VB wrap, or client draw.

Status:

- Add an Xbox-side throttle for smoke puff classes at spawn:
  - force smoke puffs to `PHYS_None`;
  - disable collision/network role;
  - shorten lifespan to 0.45s;
  - when more than 128 smoke puffs are live, hide new puffs and expire them in 0.05s.
- Keep `XSMOKE` logs sparse so we can confirm the cap without restoring massive per-draw logging.

### 8. Smoke Cap Destruction Burst Correction

Change/evidence:

- Latest log after the first smoke throttle still crashed in the same level actor band:
  - frame/tick 220 completed client/render cleanly;
  - tick 221 entered `XLEVEL tick=441`;
  - actor index 288, `SmokeGenerator16`, completed;
  - no actor 320 breadcrumb appeared.
- The throttle reduced the live actor count from roughly 724 to roughly 435, so it reduced pressure but did not remove the failure.
- The previous throttle gave excess smoke puffs a `LifeSpan` of 0.05. That can batch many `DestroyActor` calls into the same actor tick, exactly in the failing smoke-puff range.

Result:

- Treat the first throttle as partially useful but too destructive.

Status:

- Revised the cap so excess smoke puffs become inert instead of expiring immediately:
  - `Role=ROLE_None`;
  - `bHidden=1`;
  - `DrawType=DT_None`;
  - `LifeSpan=0.0`;
  - physics/collision/network remain disabled.
- Added fine actor breadcrumbs for indices 288-324 during level ticks 435-455. If this still fails in the same window, the log should identify the exact actor index rather than only the nearest 32-actor boundary.

### 9. Multi-Log Smoke Source Cap And Realtime Texture Fence

Change/evidence:

- Steve kept three logs from the same build.
- Two longer logs reached frames 525 and 535. Both completed the previous `RFRAME` cleanly and stopped immediately after `RTEXUP prelock` on the next frame.
- The shorter log died during `XLEVEL tick=437`, after actor index 309 began.
- The inert smoke cap reduced immediate destruction pressure but created a new problem: `XSMOKE` showed live smoke climbing into the thousands (`live=2816`), because hidden inert puffs had `LifeSpan=0.0` and never left the actor array.
- Memory and geometry telemetry still did not indicate exhaustion or bad vertices:
  - available physical memory stayed around 79 MB in the longer runs;
  - `badDraw=0`, `badVert=0`;
  - `Present` succeeded on the last completed frames.

Result:

- Smoke is still a pressure source, but the right fix is source-side suppression, not making spawned puffs immortal.
- The two longer runs identify realtime texture locking as another plausible crash/hang point.

Status:

- Added Xbox smoke-generator timer suppression in `AActor::Tick`: once 128 smoke puffs are live, `SmokeGenerator` / `ShortSmokeGen` / `SmokeHose` timer callbacks are skipped and logged as `XSMOKEGEN`.
- Allowed smoke puffs now expire normally, capped at 0.75 seconds.
- Excess puffs that still slip through are hidden/`DT_None`/non-networked and expire after 0.25 seconds, avoiding both immortality and same-tick delete storms.
- Added `BlockUntilNotBusy()` before realtime texture `LockRect` updates after unbinding texture stages, matching the safer resource-fence pattern already used when releasing busy textures.

### 10. Post-300 Crash Window And Smoke Log Noise Correction

Change/evidence:

- Latest log reached frame 303, so the realtime texture fence got past the previous frame 525/535-style `prelock` suspicion in this shorter run.
- Smoke no longer ballooned into the thousands; live smoke stayed around 122-128.
- The log stopped after `XSMOKE spawn=1000 live=128`, but this was partly self-inflicted noise: the `XSMOKE` condition logged every spawn because `SmokeCullCount <= 16` is always true while no culling happens.
- Engine and level phase logging were mostly quiet after engine tick 300 / level tick 600, so the exact post-300 stop point was not visible.

Result:

- Source-side smoke cap is working directionally, but the cap is still too high/noisy for a stable candidate.
- Need less smoke churn and better breadcrumbs in the actual observed window.

Status:

- Lowered smoke-generator suppression from 128 live puffs to 64.
- Lowered fallback excess-puff cap from 128 to 80.
- Fixed `XSMOKE` logging so it only logs the first 16 spawns, the first 16 actual culls, and every 128th spawn.
- Extended `MainLoop`, `UGameEngine::Tick`, and `ULevel::Tick` diagnostics through the post-300 engine tick / 600-720 level tick window.

### 11. Disable Intro Smoke Generators Instead Of Repeated Suppression

Change/evidence:

- Latest run got shorter, with the last completed render at frame 98.
- The smoke source cap worked numerically: `XSMOKEGEN` showed live smoke held at 64, and puffs did not balloon.
- However, the tail still sat in repeated `XSMOKEGEN suppress` messages. That means the generators' timers were still waking constantly and being suppressed over and over, which keeps the hot path alive even though visual smoke is capped.
- The crash landed before the earlier 180-280 / 300-360 engine diagnostic windows, so the active missing window is now engine tick 80-140.

Result:

- Smoke remains a strong stressor, but repeated suppression is not a clean enough mitigation.

Status:

- Changed Xbox smoke-generator suppression to disable the generator after its first suppressed timer:
  - `TimerCounter=0`;
  - `TimerRate=0`;
  - hidden / `DT_None`;
  - `RemoteRole=ROLE_None`.
- Lowered fallback excess smoke puff cap from 80 to 32 live puffs.
- Reduced smoke logs further: first 8 generator suppressions, first 8 puff spawns/culls only.
- Added `MainLoop`, `UGameEngine::Tick`, and `ULevel::Tick` diagnostics for engine ticks 80-140 and level ticks 160-280.

### 12. Actor Tick Phase Evidence In Current Failure Window

Change/evidence:

- Latest log stopped after `MainLoop: pre-tick 114` and inside `XTICK tick=114 level-begin`.
- `RFRAME f=98 end` had already completed cleanly with no bad draw/vertex counts and successful present.
- Smoke generator suppression now fires once per generator and disables the source; live smoke was `0` in the logged suppressions, so this run is no longer explained by puff buildup.
- The crash landed in `XLEVEL tick=227` after actor index 288 (`SmokeGenerator16`) completed and before actor index 320 (`BigRock10`) began.
- Earlier fine breadcrumbs identify the unlogged actor band as `SmokeGenerator17-20`, `Fighter2`, `Fighter`, `Pylon`, `InterpolationPoint`, `Trigger12`, `UTIntro1`, `Mutator1`, `GameReplicationInfo1`, `TMale2`, and `PlayerReplicationInfo0`.
- `UTIntro.uc`, `Fighter.uc`, `Fighter2.uc`, and `Pylon.uc` match `!ORIGINAL_SRC` with no script diffs.

Result:

- Treat the current failure as native actor tick/physics/state execution in actor indices 289-319, not a renderer present/texture upload failure and not an obvious modified-script regression.

Status:

- Added fine actor breadcrumbs for actor indices 288-324 during level ticks 220-235, matching the new crash window.
- Actor begin logs now include physics, roles, timer, lifespan, draw type, and hidden state.
- Added `XACTORSTEP` phase logging inside `AActor::Tick` for the fine actor window:
  - tick begin;
  - animation end;
  - script event tick begin/end;
  - state processing begin/end;
  - timer begin/end;
  - lifespan begin/end;
  - physics begin/end;
  - pawn postwork begin/end;
  - return.

### 13. Later Actor Tick Window And Interpolation End Guard

Change/evidence:

- Latest run got much farther than Attempt 12:
  - last completed render was `RFRAME f=268`, with successful present and no bad draw/vertex counts;
  - memory was still healthy (`availKB` around 82 MB);
  - crash/freeze occurred after `XTICK tick=269 level-begin`, inside `XLEVEL tick=537 actors-begin`.
- The previous fine actor range at `XLEVEL tick=455` completed, including `TMale0` with `PHYS_Interpolating`.
- Actor diagnostics stopped at level tick 500, so tick 537 had level breadcrumbs but no actor breadcrumbs.
- Audited `InterpolationPoint.uc`, `UnScript.cpp`, and `UnPhysic.cpp` against `!ORIGINAL_SRC`; all match original.
- `physPathing` has a native unsafe path even in original source: it checks `Dest && Dest->Next` for the rate modifier, then immediately dereferences `Dest` and `Dest->Next` for time dilation/FOV/flash. A path-end or invalid target can therefore hard-crash during `PHYS_Interpolating`.

Result:

- The current crash is still level actor tick, later than the previous smoke/actor window.
- The strongest non-render candidate is now interpolation/pathing state, especially the intro pawn/camera path.

Status:

- Extended actor diagnostics through level ticks 520-545.
- Added fine actor phase breadcrumbs for the first dynamic actor block, indices 178-224, during level ticks 530-545.
- Added an Xbox defensive guard in `AActor::physPathing`: if `Target` is not an `InterpolationPoint` or the destination has no `Next`, log `XPATH invalid-target`, stop interpolation, set `PHYS_None`, and avoid the null dereference.

### 14. PlayerReplicationInfo Timer Net-Stat Command Crash

Change/evidence:

- Latest run returned to the earlier level tick window and stopped at an exact actor phase:
  - `XLEVEL tick=223 actor-begin i=309 class=PlayerReplicationInfo name=PlayerReplicationInfo0`;
  - `XACTORSTEP ... phase=timer-begin`;
  - no `timer-end` and no actor-end.
- `PlayerReplicationInfo.uc` matches `!ORIGINAL_SRC`.
- `PlayerReplicationInfo.Timer()` only does location scanning, then randomly queries:
  - `PlayerPawn(Owner).ConsoleCommand("GETPING")`;
  - `PlayerPawn(Owner).ConsoleCommand("GETLOSS")`.
- The preceding `GameReplicationInfo` timer completed cleanly. The following `PlayerReplicationInfo` timer did not.
- Native command audit:
  - `UNetConnection::Exec` handles both `GETPING` and `GETLOSS`;
  - `UViewport::Exec` handles `GETPING`, but not `GETLOSS`;
  - this intro run is standalone/local viewport, so there is no useful net connection data to fetch anyway.

Result:

- Current strongest crash candidate is the local player console command path for PRI net-stat polling, especially unsupported `GETLOSS` through the viewport command chain.

Status:

- Added an Xbox native short-circuit in `APlayerPawn::execConsoleCommand`:
  - `GETPING` returns `"0"`;
  - `GETLOSS` returns `"0"`;
  - first 8 calls log `XCMD netstat`.
- This preserves the script timer behavior while avoiding unnecessary local command-chain traversal for net stats that are meaningless in standalone intro playback.

### 15. Tall Texture UV Scale Mismatch

Change/evidence:

- Steve validated Attempt 14 as stable: the intro map rendered through completely and restarted as expected.
- The remaining visible issue is UV stretching/tearing, most obvious on distant building/window facades in the intro screenshot.
- Audited `UXboxRenderDevice::SetTextureD3D` and `DrawComplexSurface` against the stock `D3DDrv` paths.
- Stock D3D intentionally handles tall textures by storing them transposed in video memory:
  - it chooses a pool by `Max(USize,VSize)+Min(USize,VSize)*65543`, so a 64x256 source lands in a 256x64 surface;
  - it sets `UIndex=1`, `VIndex=0`;
  - but it keeps `UScale=1/(source USize * mip * Info.UScale)` and `VScale=1/(source VSize * mip * Info.VScale)`.
- The Xbox renderer was transposing the pixels and swapping `UIndex/VIndex`, but it computed `UScale/VScale` from the physical destination dimensions. For a 64x256 facade texture this made U divide by 256 and V divide by 64, exactly the kind of axis-stretch that would tear tall building textures.
- Stock D3D also explicitly sets texture addressing to wrap; the Xbox renderer relied on default stage state.

Result:

- Treat this as a root-cause visual fix, not a probe: our math differed from stock D3D in a way that directly matches the screenshot.

Status:

- Changed `SetTextureD3D` so cache `UScale/VScale` use the source mip dimensions (`SrcUSize/SrcVSize`) even when the physical upload is transposed.
- Explicitly set `D3DTSS_ADDRESSU/V` to `D3DTADDRESS_WRAP` for stages 0 and 1 during renderer state initialization.

### 16. Disable BSP Multitexture Stage For Lightmaps

Change/evidence:

- Steve's screenshots after Attempt 15 still show heavy tearing/banding on world surfaces.
- The base render is stable and `RFRAME` reports no failed draws or invalid vertices, but the log shows very high churn in stage-1 tiny RGBA7 lightmap textures.
- Stock D3D has two supported BSP paths:
  - a multitexture path using stage 0 for base texture and stage 1 for lightmap;
  - a fallback path that draws base, macro, lightmap, and fog as separate stage-0 passes.
- Since the artifacts look like world-surface lighting/combiner tears rather than simple diffuse upload corruption, the least speculative isolation/fix is to use the stock single-stage fallback on Xbox. That removes stage-1 FVF/combiner/coordinate risk while keeping lightmaps enabled.

Result:

- This should preserve stable rendering and keep lightmaps, but avoid the Xbox-specific stage-1 multitexture path until it can be audited separately.

Status:

- Added `GUseXboxBspMultitexture=0`.
- `DrawComplexSurface` now routes BSP lightmaps through the stock multi-pass fallback instead of the stage-1 multitexture path.

### 17. Remove PC Tall-Texture Transpose On Xbox

Change/evidence:

- Steve validated that Attempt 16 was not a lightmap/stage-1 issue: the same UV tearing persisted with BSP multitexture disabled.
- That moves the visual bug back to the shared base texture path.
- The remaining Xbox-specific behavior common to the torn BSP textures is the inherited PC D3D7 tall-texture transpose:
  - PC D3D7 pools tall textures as wide surfaces and swaps `UIndex/VIndex`.
  - OpenJKDF2's Xbox FakeGL path creates textures using the real source `width,height` and calls `XGSwizzleRect` with those real dimensions.
  - Our textures are power-of-two, so Xbox swizzled A8R8G8B8 textures do not need the PC D3D7 aspect workaround.
- Keeping a physical transpose means the upload path, cache key, and UV axis swap are all special-cased only for tall base textures. The screenshots show exactly those world/facade textures tearing while the scene otherwise renders.

Result:

- Treat the tall-texture transpose itself as the next root suspect, not the scale math around it.

Status:

- Disabled `bSwapUV` on Xbox so non-DXT textures are created and uploaded in source orientation.
- Restored `GUseXboxBspMultitexture=1` because Attempt 16 disproved the lightmap isolation path and we want the renderer back on the normal BSP route for this test.

### 18. Log Noise Cleanup For UV Testing

Change/evidence:

- Steve asked to clean up the log before the next run.
- The current `ut99.log` tail is dominated by old actor diagnostics (`XTICK`, `XLEVEL`, `XACTORSTEP`) from the crash hunt.
- Those breadcrumbs were useful for the now-fixed intro crash, but they obscure the render and texture evidence needed for the UV issue.

Result:

- Keep startup, render summaries, warnings, errors, and defensive crash guards.
- Disable the broad actor/engine tick phase spam and remove harmless local net-stat success logs.

Status:

- Added disabled compile-time switches around `UGameEngine::Tick` and `ULevel::Tick` verbose diagnostics.
- Removed `XCMD netstat` success logging while preserving the `GETPING`/`GETLOSS` Xbox short-circuit.
- Reduced `RFRAME` logging to first frames, every 60th frame, failures, bad draws, or skipped textures instead of every texture-create frame.

### 19. Texture Cache Thrash And Upload Log Cleanup

Change/evidence:

- Steve reported continued stretching plus occasional surface flicker.
- The fresh log is useful:
  - no `RDRAW FAILED`, no bad TL vertices, no texture rejects, and present succeeds;
  - memory remains stable around 81 MB available;
  - by frame ~1720 the renderer has created over 2300 textures while the resident pool is fixed at 128 entries;
  - repeated `RTEX reuse` and stage-1 lightmap recreation continue during normal rendering.
- The flicker symptom matches texture eviction/recreation churn better than UV math.
- The log cleanup from Attempt 18 was incomplete because `RenderTextureHotTrace` stayed true forever once total texture creates exceeded the resident threshold, so upload internals still spammed the log.

Result:

- Treat flicker as a cache residency problem first.
- Keep the UV investigation separate: unchanged stretching means the tall-texture transpose path was not the root visual cause.

Status:

- Increased the Xbox resident texture cache from 128 to 768 entries while keeping the 12 MB live-texture safety flush.
- Changed hot texture tracing to trigger only near the resident pool limit, not forever after total creates pass the limit.
- Throttled cache reuse logs and guarded mip `prelock` logs behind the hot-trace budget.
- Explicitly disabled texture transform flags on stages 0 and 1 during frame state setup, matching the direct-coordinate usage expected by the renderer.

### 20. Clamp-Fill Texture Padding During Xbox Swizzle

Change/evidence:

- Steve's latest log still shows no failed draws, no bad transformed vertices, no texture rejects, and stable memory/cache behavior, while the screenshot still shows smeared/tearing surfaces.
- Audited the D3D primitive count suspicion against `C:\XDK_5558\XDK\xbox\include\D3D8.h` and OpenJKDF2:
  - Xbox `IDirect3DDevice8::DrawPrimitive` takes standard primitive count and internally calls `D3DVERTEXCOUNT`;
  - OpenJKDF2's C++ wrapper also passes standard primitive count;
  - therefore our `DrawPrimitiveVB(..., Poly->NumPts - 2)` call is not the root of the visual tearing.
- The next verified mismatch is texture composition before swizzling:
  - UT textures often have a valid `UClamp/VClamp` rectangle inside a power-of-two mip allocation;
  - the stock/OpenGL path clamps samples to that valid rectangle when composing BGRA/RGBA textures;
  - the Xbox upload path either copied the whole power-of-two allocation or zero-filled outside clamp for RGBA7, leaving padding visible under wrap/filtering.
- The log already shows many lightmap/realtime textures such as `256x16 clamp=160x14`, which means padding is absolutely present in rendered texture data.

Result:

- Treat the remaining smear/tear as a texture-composition mismatch rather than a D3D draw failure.
- Edge-fill every uploaded A8R8G8B8 scratch image from the mip-scaled `UClamp/VClamp` rectangle before calling `XGSwizzleRect`.

Status:

- Added `RenderMipClampSize`.
- Updated P8, RGBA7, and RGBA/RGBA8 conversion to sample with clamped source coordinates across the full destination mip.
- Added sparse `RTEX clamp-pad` diagnostics for the first 32 padded uploads.

### 21. GPU Projection For Perspective-Correct 3D UVs

Change/evidence:

- Steve pointed out OpenJKDF2 had a similar "texture projection" issue.
- Verified against `C:\Programming\GitHub\OpenJKDF2ogx` commit `b31c660d`:
  - the commit message identifies CPU pre-projection as the root cause of affine/swimming UVs;
  - the fix keeps view-space vertices through the engine and lets the Xbox GPU projection matrix provide real W for perspective-correct interpolation.
- Verified UT's original PC D3D7 renderer still used pretransformed vertices, but explicitly enabled `D3DRENDERSTATE_TEXTUREPERSPECTIVE`.
- Verified Xbox D3D8 headers do not expose `D3DRS_TEXTUREPERSPECTIVE`, so our `D3DFVF_XYZRHW` world path had no equivalent switch to recover perspective-correct UVs.
- The UT Xbox screenshots match that failure mode: geometry is stable enough to render, but large/slanted world surfaces smear and tear as if UVs are affine-interpolated after projection.

Result:

- Treat projection as the primary root suspect, ahead of lightmaps, texture padding, or primitive counts.
- Keep 2D/HUD/fullscreen flash paths on `XYZRHW`.
- Move 3D textured BSP and Gouraud/actor rendering to camera-space `XYZ` vertices so the NV2A performs the perspective divide.

Status:

- Added camera-space world vertex formats (`FXboxWorldVertex`, `FXboxWorldVertex2`) and UV helpers.
- `SetSceneNode` now installs identity world/view transforms plus a UT-matched perspective projection matrix, with `RPROJ gpu` startup diagnostics.
- `DrawComplexSurface` now uses world vertices for base, multitexture, macrotexture, lightmap, and fog passes.
- `DrawGouraudPolygon` now uses world vertices for mesh/actor polygons.
- `DrawTile`, 2D line/point, and `EndFlash` remain pretransformed.

### 22. Disable Stat Logs And Remove Performance-Distorting Diagnostics

Change/evidence:

- Steve asked about `D:\Logs`; the directory is produced by UT's built-in ngStats/stat logger, not by `ut99.log`.
- The live `UnrealTournament.ini` had `bLocalLog=True` and `bWorldLog=True`, with `[Engine.StatLog]` writing to `../Logs` and `../NetGamesUSA.com/ngWorldStats/logs`.
- The current `ut99.log` also showed leftover crash-hunt noise during active gameplay:
  - repeated `StaticLoadObject` traces for weapon loads;
  - render summaries every 60 frames;
  - repeated `RDRAW FAILED op=DT` summaries every frame once the timedemo/HUD path hit unsupported tile draws.
- Performance evidence from the latest log points first at CPU/driver overhead rather than texture upload:
  - texture creation/upload drops after warmup;
  - gameplay frames still issue hundreds to ~1500 primitives and nearly one dynamic vertex-buffer lock per primitive;
  - synchronous debug logging was happening in those same hot paths.

Result:

- Stop user-facing stat log files from accumulating on Xbox storage.
- Remove diagnostic logging cost before judging FPS.
- Keep sparse error diagnostics and startup logs, but do not let a known repeated DrawTile failure flood every frame.

Status:

- Set `bLocalLog=False` and `bWorldLog=False` in repo defaults, live CXBX defaults, and live `UnrealTournament.ini`.
- Changed `GameInfo.uc` default properties to disabled for source correctness.
- Gated `StaticLoadObject` tracing behind a disabled local switch.
- Added a disabled verbose render-performance switch and reduced render summaries/splits/wraps to error-only by default.
- Rate-limited `RDRAW FAILED` logs and removed per-bad-frame summary spam from `Unlock`.

### 23. Native FPS Overlay And First Perf Baseline

Change/evidence:

- Steve requires FPS to always be visible.
- The built-in `TIMEDEMO 1` display uses Console/Canvas text, which adds texture-backed `DrawTile` traffic and was mixed into the thing we were trying to measure.
- Replaced automatic timedemo activation with a render-device overlay made from simple line geometry.
- First test showed a renderer-state regression: textures became flat/untextured after the overlay because the overlay changed texture-stage args to diffuse-only and did not restore them.
- The same test gave a useful baseline:
  - intro/menu is near 58 FPS;
  - once gameplay starts, render time jumps as high as 90-124 ms;
  - hot frames show `DGP` actor/mesh polygons around 800-1268 and total VB locks around 1300-1700 per frame;
  - present time is tiny, so this is not primarily vsync/present;
  - repeated `DrawTile` failures show negative RHW from invalid/behind-camera tile Z values.

Result:

- Treat the next major optimization as render submission/VB locking, especially actor/mesh `DrawGouraudPolygon`, not present/vsync.
- Keep the always-visible native FPS overlay, but make it restore D3D texture-stage state correctly.
- Cull invalid-Z `DrawTile` calls before they hit D3D.

Status:

- Restored stage-0 and stage-1 texture args after the FPS overlay.
- Added early invalid-Z rejection in `DrawTile` with sparse diagnostics.

### 24. Deterministic Texture-Stage Restore After Flicker Report

Change/evidence:

- Steve reported distant walls flickering between what looked like lit and unlit.
- Latest log showed textures are still bound and uploads are low during gameplay, so this is not the earlier "textures disappeared" overlay regression.
- The same log still showed BSP lightmaps active on stage 1, while several 2D paths (`Draw2DLine`, `Draw2DPoint`, FPS overlay, `EndFlash`) changed fixed-function texture-stage state and only restored part of it.
- The BSP multitexture path was relying partly on `CurrentPolyFlags` to decide whether stage 1 lightmap ops were already correct. That is fragile after any overlay/HUD/flash path touches the same D3D stages.

Result:

- Treat lit/unlit flicker as a plausible stage-state leak before assuming bad lightmap data.
- Make the BSP lightmap pass explicitly program stage 0 and stage 1 every time it uses base texture + lightmap.
- Add one renderer helper to restore normal textured defaults after untextured 2D/overlay/flash draws.
- Disable old crash-window tick logs so future `ut99.log` runs are dominated by `PERF` and sparse render diagnostics.

Status:

- Added `RestoreDefaultTextureStages()`.
- FPS overlay, `Draw2DLine`, `Draw2DPoint`, and `EndFlash` now restore full stage state instead of only color/alpha ops.
- BSP multitexture explicitly sets stage-1 `COLOROP=MODULATE`, `ALPHAOP=SELECTARG2`, args, filtering, and texcoord index before drawing.
- Disabled `XCLIENT` and `MainLoop` boundary logging windows.

### 25. Config Overwrite Found After Flicker/Perf Log

Change/evidence:

- The next run's log was much cleaner and had no render failures, but FPS still dropped hard during active gameplay.
- `PERF` showed present time at 1-4 ms, while render time jumped to 40-142 ms with roughly 1,100-1,700 primitive submissions / VB locks per gameplay frame.
- The live generated `UnrealTournament.ini` still had desktop-style client settings:
  - `ScreenFlashes=True`
  - `Decals=True`
  - `NoDynamicLights=False`
  - `MinDesiredFrameRate=30.0`
- `UXboxClient::Init` was setting Xbox-safe defaults before `LoadConfig()`, so a stale or desktop-derived ini could immediately overwrite the intended Xbox settings.
- This is directly relevant to the reported far-wall lit/unlit flicker because dynamic lighting was still enabled in the effective config.

Result:

- Treat the current run as not yet representative of the intended Xbox render profile.
- Preserve controller config loading, but force the Xbox render-cost defaults after `LoadConfig()`.
- Add a single startup log line with the effective client settings so future logs prove whether these settings stuck.

Status:

- `UXboxClient::Init` now loads config, then enforces `ScreenFlashes=0`, `Decals=0`, `NoDynamicLights=1`, `MinDesiredFrameRate=20`, and low texture LODs.
- Added explicit Xbox client entries for those settings in repo defaults and the live CXBX `Default.ini` / `UnrealTournament.ini`.

### 26. First High-Payoff FPS Pass: DGP Batching

Change/evidence:

- Reordered the FPS work by expected gain in `FPS_OPTIMIZATION_PLAN.md`.
- Current `PERF` logs identify actor/mesh `DrawGouraudPolygon` as the largest hot-path contributor:
  - active gameplay commonly has 650-1,180 `DGP` calls per logged frame;
  - total primitive submissions / VB locks can hit 1,100-1,700 per frame;
  - present time remains low, so reducing submission/lock overhead is the first target.

Result:

- Add a conservative batcher for compatible `DGP` calls.
- Batch only when texture `CacheID`, normalized `PolyFlags`, and realtime texture status are safe.
- Convert each fan into triangles in a shared per-frame triangle-list buffer.
- Flush before BSP, tile, 2D line/point, clear-Z, FPS overlay, flash, texture-cache flush, scene-node changes, and frame end.
- Keep immediate fallback for realtime texture changes, oversized batches, or incompatible state.

Status:

- Added `FlushDGPBatch()`.
- Added `dgpBatch=batches/polys` to `PERF` logs so the next run can prove whether the hot mesh path is actually being collapsed.

### 27. Second FPS Pass: DrawTile Batching

Change/evidence:

- Steve reported a large improvement from DGP batching.
- The confirming log showed gameplay frames improving into roughly 29-58 FPS ranges, with `DGP` calls collapsed into tens of batches:
  - example: `DGP=904 dgpBatch=30/904 vbLocks=327`;
  - example: `DGP=1549 dgpBatch=39/1548 vbLocks=297`.
- Remaining `DT` traffic is still visible, commonly 60-300 tile draws per frame, and light/HUD frames can still have hundreds of VB locks.

Result:

- Add a conservative `DrawTile` batcher.
- Batch only same texture `CacheID` and same normalized `PolyFlags`.
- Convert each quad to two triangles in a shared TL-vertex triangle-list buffer.
- Flush before mesh/world/2D/flash/FPS/clear/scene changes and frame end.
- Keep immediate fallback for realtime texture updates or incompatible state.

Status:

- Added `FlushDTBatch()`.
- Added `dtBatch=batches/tiles` to `PERF` logs.

### 28. Third FPS Pass: Make Tile Batches Actually Submit

Change/evidence:

- Steve's follow-up log showed the first tile-batching build improved some frames, but also exposed a direct bug in the new batcher:
  - repeated `RDRAW VB reject op=DT-batch ... prim=226 verts=678 stride=28`;
  - `DrawPrimitiveVB()` rejects anything above `XBOX_MAX_VERTS=512`;
  - the new `DrawTile` batch buffer allowed up to 8190 TL vertices, so large batches were counted in `dtBatch` but rejected before D3D submission.
- Gameplay is now commonly around the mid-20s to mid-30s FPS, with low-scene frames still reaching about 59 FPS.

Result:

- Cap triangle-list batch buffers to the renderer's real per-draw vertex limit, rounded down to a multiple of three.
- Apply the same cap to `DGP` and `DT` batches so future compatible batches cannot silently exceed the shared dynamic draw path.
- Make vertex-count rejection logging sparse instead of allowing one bad batch to flood the whole log.
- Cache the dynamic VB stream-source stride so repeated draws with the same stream do not call `SetStreamSource()` every time.

Status:

- Implemented in `XboxRender.cpp`.
- Next log should show no repeated `RDRAW VB reject op=DT-batch`, with `dtBatch` still present and `vbLocks` staying low.

### 29. Deep FPS Pass: Scene Splits, Batch Size, And Detail Gating

Change/evidence:

- Steve's next log still showed active gameplay commonly in the 25-35 FPS range, with low-activity frames reaching about 56-59 FPS.
- The largest remaining patterns were:
  - `texUp=1 splits=1` on almost every hot gameplay frame, meaning the renderer was ending/restarting the scene for a tiny realtime texture update during drawing;
  - `vbLocks` still in the 200-440 range because DGP/DT batches were capped to the old 512-vertex per-draw guard;
  - `MinDesiredFrameRate=20`, so UT's script-level `bDropDetail` and `bAggressiveLOD` paths stayed off while we were below the desired 60 FPS target;
  - BSP multitexture surfaces enabled stage 1, then disabled it after each surface, then re-enabled it for the next lightmapped surface.

Result:

- Added `XBOX_MAX_DRAW_VERTS=2048` for dynamic draw submissions while keeping `XBOX_MAX_VERTS=512` for per-polygon stack buffers.
- DGP/DT triangle-list batch caps now use the larger draw limit.
- Existing cached realtime texture entries are reused instead of forcing a mid-scene upload; `PERF` now reports `texDef`.
- Xbox client now forces `MinDesiredFrameRate=60.0` so UT's own low-detail spawn/detail gates engage below target.
- Stage 1 lightmap state is kept across BSP multitexture surfaces and disabled only when leaving that path.

Status:

- Implemented as one combined high-upside candidate.
- Next log should show lower `vbLocks`, near-zero `splits` after warmup, nonzero `texDef`, and fewer high-detail transient effects during combat.

### 30. Autonomous FPS Smoke Harness

Change/evidence:

- Steve asked for unattended CXBX-R smoke passes that can start a match, respawn, and keep firing for about 90 seconds before reassessing `ut99.log`.
- `UXboxViewport::PollController()` runs every viewport input update and is reached before gameplay rendering; placing the harness there means the test is not dependent on a physical gamepad being present in CXBX-R.

Result:

- Added a gated test-only input pulse:
  - if `D:\XboxAutoFireSmoke.ini` exists, pulse `IK_LeftMouse` on/off every few frames;
  - if the file is absent, boot and controls behave normally;
  - when disabled after being active, release `IK_LeftMouse` once.

Status:

- Implemented in `XboxViewport.cpp`.
- Autonomous runs create the marker file beside `default.xbe`, launch CXBX-R, wait, close CXBX-R, remove the marker, then inspect the new `PERF` lines.

### 31. Fourth FPS Pass: D3D State Cache

Change/evidence:

- The first unattended smoke pass entered gameplay and fired repeatedly.
- `PERF` showed texture-upload splits were mostly solved (`splits=0`, `texDef=1-2`), but active gameplay still hovered around 21-36 FPS.
- Hot frames still had `DCS=150-183`, `DGP=298-1067`, `DT=55-381`, and `vbLocks=270-372`.
- Source audit found `DrawComplexSurface()` issuing the same fixed-function texture-stage setup for every BSP surface.
- OpenJKDF2's Xbox renderer uses dirty/cached state application instead of calling D3D for every repeated state transition.

Result:

- Added cached wrappers for `SetRenderState`, `SetTextureStageState`, and `SetVertexShader`.
- Routed XboxRender state changes through those wrappers so identical state values skip the D3D call.
- Added `state=sets/skips` to `PERF` lines for direct evidence on the next smoke run.

Status:

- Implemented in `XboxRender.cpp` / `XboxRender.h`.
- Next run should show high `state` skip counts if the BSP path is avoiding redundant D3D state traffic.

### 32. Fifth FPS Pass: DrawPrimitiveUP Probe

Change/evidence:

- The state-cache smoke pass improved active gameplay from the previous 21-36 FPS band to roughly 38-55 FPS.
- The same run proved the state cache was real work, with sampled frames commonly showing about 1,600-3,000 skipped redundant D3D state calls.
- Remaining hot counter: `vbLocks` still ranges from about 186-398 per sampled frame because every BSP/mesh/tile draw locks and unlocks the shared dynamic VB.
- OpenJKDF2's Xbox renderer uses `DrawPrimitiveUP` for immediate dynamic primitives, avoiding explicit dynamic VB lock/unlock churn in the caller.

Result:

- Added a guarded `DrawPrimitiveUP` path before the dynamic-VB fallback.
- If `DrawPrimitiveUP` succeeds, it avoids the VB lock/unlock path entirely.
- If it fails, it logs sparsely and falls back to the existing dynamic VB path.
- Added `up=draws/fails` to `PERF` lines.

Status:

- Implemented in `XboxRender.cpp`.
- Next smoke run should prove whether Xbox D3D8/CXBX-R accepts UP draws and whether eliminating VB locks improves gameplay FPS.

Observed:

- The smoke run proved the UP path is accepted:
  - `up=336/0` with `vbLocks=0` on a gameplay sample;
  - no `RUP draw-failed` lines;
  - active gameplay stayed around 58-59 FPS even with `DGP` above 1,500 and `DCS` around 150-180.
- This is the first automated run to hold gameplay near the display target under constant fire/respawn input.

Follow-up:

- Investigate whether the remaining 58-59 FPS ceiling is intentional engine/display pacing rather than render throughput.

### 33. Log Cleanup After Stable Smoke

Change/evidence:

- Current smoke logs prove the renderer is reaching the 60 Hz main-loop limiter.
- The log file is still much larger than needed because earlier crash-hunt diagnostics are still enabled:
  - `LINK[...]` one line per class;
  - `[ClsSer]` phase beacons for class serialization;
  - routine `ULinkerLoad` package-open phase logs;
  - file-manager `RESOLVE`, `CreateFileReader`, and `FileSize` success-path logs.

Result:

- Removed routine `LINK[...]` and `[ClsSer]` success-path spam while keeping the actual `UStruct::Link BREAK` error log and out-of-bounds class-default warning.
- Gated routine package-loader success logs behind `bVerbosePackageLoad = 0`.
- Disabled routine file-manager path/file-size/read success logging; failures still log.

Status:

- Implemented after the 58-59 FPS smoke pass.
- Next build/run should produce a much smaller `ut99.log` while preserving `PERF`, init, and failure diagnostics.

### 34. Autonomous Smoke Confirmation: Stable FPS Candidate

Change/evidence:

- Built from command line after the log cleanup and deployed the resulting `default.xbe` to the CXBX-R test folder.
- Ran the auto-fire smoke harness for roughly 70 seconds so the game starts, respawns, and fires without manual input.
- Fresh `ut99.log` size dropped to 68,268 bytes for the smoke run.
- Log health checks:
  - `Error=0`, `Critical=0`, `Warning=0`;
  - `RUP draw-failed=0`;
  - `ULinkerLoad=0`, `LINK[=0`, routine file-manager success spam still absent;
  - retained one `[ClsSer] Camera skipped 16 out-of-bounds prop(s)` warning-style diagnostic.
- `PERF` samples:
  - 31 samples;
  - min 57.9 FPS, max 58.8 FPS, avg 58.7 FPS;
  - `vbLocks=0` throughout;
  - `up=.../0` throughout;
  - redundant state skips remain high, commonly about 2,500-2,900 per active frame.

Result:

- Current build is a strong gameplay candidate for Steve's visual/hardware test.
- The remaining 58-59 FPS ceiling matches the current 60 Hz main-loop limiter, not the earlier render bottleneck.

Status:

- Built and deployed to `C:\Games\Emulators\CXBX\UT99x\default.xbe`.
- No GitHub commit made; waiting for Steve's test confirmation.

### 35. Xbox Menu Text First-Glyph Corruption Audit

Change/evidence:

- The `SELECT` first-character corruption moved to `INSTANT ACTION` after a prompt-text texture experiment, proving the defect was not specific to one string, one font glyph, or the A-button image.
- Removed the generated prompt texture path and restored live Canvas text for button prompts.
- Audited the Canvas text path against `UXboxRenderDevice::SetBlending()`. `SetBlending()` only changes `D3DRS_ALPHATESTENABLE` when `CurrentPolyFlags` says the `PF_Masked` bit changed.
- The Xbox menu helper primitives draw custom 2D rectangles/images by manually changing D3D state, including disabling alpha test. If those helpers leave `CurrentPolyFlags` claiming masked text state is still active, the next Canvas text call can skip re-enabling alpha test. That matches the observed "first drawn menu text after a helper primitive loses/corrupts its leading glyph" behavior.

Result:

- Every menu helper path that manually disables alpha/blend/depth state now invalidates `CurrentPolyFlags` before returning to normal Canvas text.
- Menu rect drawing now explicitly disables alpha test before drawing non-textured quads and explicitly disables blending for fully opaque rects.
- `XboxRenderDrawMenuTexture()` no longer returns before its `unguard`.
- Confirmed no `prompt_select.xui` or `prompt_back.xui` assets remain in source or deployed `MenuAssets`.

Status:

- Command-line build succeeded.
- Deployed `default.xbe` and current `MenuAssets` to `C:\Games\Emulators\CXBX\UT99x`.

Follow-up evidence plan:

- The menu-text fast-path isolation is not yet visually proven.
- Added bounded `MTEXT begin/tile/end` logging to prove which text string owns the first glyph, how many glyph tiles were emitted, and that menu text is using the non-batched/non-`DrawPrimitiveUP` path (`batch=0 up=0`) while gameplay keeps the optimized path.
- Rebuilt and redeployed the diagnostic candidate.

### 36. Menu Regression Follow-up: Backdrop and First Glyph

Change/evidence:

- Steve reported two regressions from the previous candidate:
  - the map was no longer visible behind the main menu;
  - `SELECT` still had a broken first character.
- Backdrop cause was confirmed in code: `XboxMenuDrawChrome()` drew a full-screen rect with the helper default alpha of `1.0f`. Once rect render state stopped depending on previous leaked blending, that full-screen rect became honestly opaque.
- First-glyph cause was re-audited through `UCanvas::WrappedPrintf()` -> `DrawString()` -> `RenDev->DrawTile()`. Each menu text string starts a fresh glyph `DrawTile` sequence, and the first broken character is the first quad in that fresh DT batch. That points at the menu text interaction with the gameplay DT batching/`DrawPrimitiveUP` fast path, not at the A-button image or the string contents.

Result:

- Menu backdrop alpha is now explicit: translucent full-screen darkener and translucent top/bottom bars.
- Menu text now runs in a scoped `GRD_MenuTextMode` that disables DT batching and `DrawPrimitiveUP` only while Canvas is drawing menu text. Gameplay keeps the optimized batch/UP path.

Status:

- Command-line build succeeded.
- Deployed `default.xbe` and current `MenuAssets` to `C:\Games\Emulators\CXBX\UT99x`.

### 37. Hardware Dashboard Kick: Stale PC Audio Driver Guard

Change/evidence:

- Held menu-text work after Steve reported a real Xbox dashboard kick.
- The CXBX folder log did not contain a fatal Unreal shutdown; it ended mid-render, so it was not enough by itself.
- A newer log in `UT99-Xbox/build_cli/release/ut99.log` did contain the concrete failure path: `InitAudio` tried `Galaxy.GalaxyAudioSubsystem`, failed to find `Galaxy`, asserted on an empty class, then `main()` caught an unknown C++ exception.
- Repo/CXBX `Default.ini` and `UnrealTournament.ini` already point to `XboxAudio.XboxAudioDevice`, so the likely hardware-specific trigger is a stale generated `System\UnrealTournament.ini` on the Xbox drive.

Result:

- `UEngine::InitAudio()` now forces Xbox builds back to `XboxAudio.XboxAudioDevice` before resolving `ini:Engine.Engine.AudioDevice`, preventing a stale PC Galaxy audio class from reaching `StaticLoadClass`.
- This is independent of the menu first-glyph investigation, which remains paused.

### 38. Hardware Controls Missing: Stale User.ini Input Bind Guard

Change/evidence:

- Steve reported the hardware build loads but has no controls.
- The available log shows `XboxClient`, `XboxViewport`, and controller polling are alive: `PollController` opened port 0 and `Input system initialized for XboxViewport0` is present.
- That points away from `ViewportManager` and toward stale/missing `User.ini` bindings. Xbox input sends essential buttons as keyboard/mouse keys and sticks as `IK_JoyX/Y/U/V`; those only affect gameplay if `User.ini` contains the expected aliases and raw key binds.

Result:

- `UInput::StaticInitInput()` now forces the Xbox gameplay aliases and key/stick bindings immediately after loading `User.ini`, then calls `SaveConfig()` so the generated `User.ini` is repaired on first run.
- This should make controls resilient to stale PC or older Xbox `User.ini` files on real hardware.

### 39. Hardware Controls Missing: Direct Gameplay Input Fallback

Change/evidence:

- Steve reported controls were still absent on hardware.
- The available log proves controller hardware and polling are not dead: port 0 opens successfully and Start reaches `XMENU opened`.
- That leaves the binding translation layer as the weak point: `UXboxViewport::ProcessControllerInput()` was still depending on `UInput::InputEvent()` plus `User.ini` bindings for movement, look, fire, alt-fire, crouch, and jump.
- `UInput::Exec()` was traced: axis binds write to `APlayerPawn::aStrafe/aBaseY/aTurn/aLookUp`, and button binds write to `bFire/bAltFire/bDuck`; `PlayerPawn.uc` consumes those fields directly in `PlayerInput()`.

Result:

- Core gameplay input now writes those same `APlayerPawn` fields directly after menu handling:
  - left stick -> `aStrafe` / `aBaseY`;
  - right stick -> `aTurn` / `aLookUp`;
  - right trigger -> `bFire`;
  - left trigger/B -> `bAltFire`;
  - left stick click -> `bDuck`;
  - A press -> toggles `bJumpStatus`.
- Fire/alt-fire also set `bReadyToPlay`, covering match-start waiting states without relying on the `Fire` exec binding.
- Utility actions still use `InputEvent()` for D-pad weapon/inventory-style bindings, Back scoreboard, X use, Y next weapon, and Black/White inventory/weapon cycling.
- Added bounded `XINPUT direct` logging so the next hardware log can prove whether non-menu gameplay input reaches a live player actor without flooding the log.

### 40. OGX360 Adapter Input: Port Scan and XDK Polling Parameters

Change/evidence:

- Steve confirmed the A/SELECT first-glyph issue does not occur on real hardware, so that is now classified as a CXBX-R rendering artifact rather than a UT gameplay blocker.
- Steve also reported an OGX360 adapter works in other Xbox games but not in UT.
- UT's Xbox input path only opened one hard-coded controller port (`ControllerPort = 0`) and passed `NULL` polling parameters to `XInputOpen`.
- XDK sample code (`XBInput_CreateGamepads`) opens every connected gamepad port and uses `XINPUT_POLLING_PARAMETERS` with auto-poll/interrupt-out enabled. Other Xbox codebases audited earlier also handle insertion/removal across all four ports.

Result:

- `UXboxViewport::OpenWindow()` and `PollController()` now scan all four gamepad ports and select the first openable gamepad instead of assuming port 0.
- XInput handles are opened with sample-style polling parameters.
- `PollController()` now calls `XGetDeviceChanges()` and closes/reopens handles on insertion/removal.
- Added bounded `XINPUT state` logging for selected port, packet number, buttons, analog buttons, triggers, and sticks.
- Disabled the obsolete `MTEXT` menu glyph diagnostics now that the glyph corruption is confirmed CXBX-R-only.

### 41. OGX360 Adapter Input: Align Open Path with OpenJKDF2

Change/evidence:

- Steve reported OGX360 still has no controls, while OpenJKDF2 recognizes it.
- The provided UT log did not include the new `XINPUT open` or `XINPUT state` markers, so it was produced by an older XBE or a different install path. The deployed CXBX XBE does contain those strings.
- OpenJKDF2's known-good path opens controllers with `XInputOpen(XDEVICE_TYPE_GAMEPAD, XDEVICE_PORT0 + port, XDEVICE_NO_SLOT, NULL)` and does not call `XInputPoll()`.
- UT's previous candidate diverged by passing sample polling parameters and explicitly calling `XInputPoll()` before `XInputGetState()`.

Result:

- Changed UT's controller open helper to match OpenJKDF2: `XDEVICE_PORT0 + Port`, slot `XDEVICE_NO_SLOT`, `NULL` polling parameters.
- Removed the explicit `XInputPoll()` call while keeping the `poll=0` field in diagnostics so logs remain easy to compare.
- Initialized viewport controller fields immediately in `UXboxClient::NewViewport()` because the first `PollController()` can occur before `OpenWindow()`.
- `OpenWindow()` now preserves an already-open controller handle instead of blindly resetting it.

### 42. Retail Xbox Controller Audit: Enumeration Wait and Port Count

Change/evidence:

- Steve asked to prioritize retail controller setups over our own source ports.
- Audited three retail/shipped-style references:
  - Mercenaries `xboxPblJoystick.cpp`: calls `XInitDevices()`, waits while `XGetDeviceEnumerationStatus() == XDEVICE_ENUMERATION_BUSY`, opens gamepads with `XInputOpen(XDEVICE_TYPE_GAMEPAD, iPadNum, XDEVICE_NO_SLOT, NULL)`, and handles insert/remove with `XGetDeviceChanges()`.
  - Raven `XBInput.cpp` / `gamepad.cpp`: uses `XInitDevices(0,NULL)` or pre-init elsewhere, loops `XGetPortCount()`, opens with `XInputOpen(..., i, XDEVICE_NO_SLOT, NULL)`, uses `XGetDeviceChanges()`, then `XInputGetState()`.
  - UC2004 `Launch.cpp` / `XboxClient.cpp`: preallocates gamepads, opens all four with `XInputOpen(..., Index, XDEVICE_NO_SLOT, NULL)`, and refreshes insertion/removal in the client.
- UT had two differences from this retail pattern:
  - it logged the initial mask immediately after `XInitDevices()` without waiting for enumeration to settle;
  - its scan helper used a hardcoded `4` instead of `XGetPortCount()`.

Result:

- `main()` now waits for Xbox device enumeration to complete after `XInitDevices()`, with bounded logging if it stays busy.
- Controller scans now use `XGetPortCount()` and pass the port index directly to `XInputOpen()`, matching Mercenaries/Raven/UC2004.

### 43. Instant Action Menu: First Functional Option Pass

Change/evidence:

- Steve is away from hardware and asked for emulator-testable Instant Action functionality.
- The previous Instant Action screen displayed fixed labels and only started one hardcoded match (`DM-Turbine`, deathmatch, 4 minimum players, 15 frags, 10 minutes).

Result:

- Added persistent Instant Action menu state for game type, arena, bot count, skill, frag limit, time limit, and mutator.
- D-pad/left-stick left and right now adjust the selected Instant Action option.
- Game type changes constrain the arena list to that mode: Deathmatch, CTF, Domination, or Assault.
- `BEGIN MATCH` now builds the travel URL from the selected options and logs it before calling `SetClientTravel()`.
- Command-line build succeeded and the candidate was deployed to the CXBX test folder.

### 44. Instant Action Menu: Map Previews and Multi-Select Mutators

Change/evidence:

- Steve noted mutators need multi-select behavior, so simple left/right value selection was not enough.
- UT's existing PC menu code loads screenshots via `DynamicLoadObject(MapName$".Screenshot", class'Texture')`, so the Xbox menu can use the same embedded map-package texture path instead of baking separate preview files.

Result:

- Instant Action now loads the selected map's embedded `Screenshot` texture at runtime and draws it in the preview panel.
- Preview textures are cached and rooted while active so Unreal's GC will not collect them out from under the menu.
- Mutators are now multi-select:
  - left/right on the Mutators row chooses which mutator is being targeted;
  - A toggles that mutator on/off;
  - the display shows `NONE`, a single selected mutator name, or `N SELECTED`;
  - match launch emits a comma-separated `?Mutator=` URL list.
- Command-line build succeeded and the candidate was deployed to the CXBX test folder.

### 45. Instant Action Menu: Mutator Overlay

Change/evidence:

- Steve tested the first mutator pass and found the row interaction was wrong: after toggling a mutator, the row still looked like a single left/right option and gave no clear selected-state feedback.
- The intended console-style interaction is a modal list: open mutators, move vertically, toggle entries, then back out.

Result:

- Pressing A on the Instant Action `MUTATORS` row now opens a mutator overlay instead of toggling immediately.
- The overlay lists mutators vertically with `[ ]` / `[X]` indicators.
- D-pad up/down moves through the list, A toggles the focused mutator, and B exits the overlay back to Instant Action.
- The Instant Action row now shows a summary (`NONE`, one mutator name, or `N SELECTED`) and no longer shows misleading left/right arrows for mutators.
- Command-line build succeeded and the candidate was deployed to the CXBX test folder.

### 46. Menu Font/Prompt Standard and Scalable Mutator Overlay

Change/evidence:

- Steve set a blanket UI rule: the main menu font is the gold standard, and contextual footer prompts should use the green A / red B button images.
- Steve also noted the mutator overlay must be scrollable because modded installs can contain 100+ mutators.

Result:

- Menu prompt rendering now goes through one shared helper using the main menu font and the loose `button_a.xui` / `button_b.xui` images.
- Instant Action, Coming Soon, and the mutator overlay now use the same menu font family instead of mixing large/small fonts.
- The mutator overlay now draws a fixed visible row window around the focused mutator and shows `MORE ^` / `MORE v` indicators when entries exist above or below.
- Mutator selection storage was widened from one 32-bit mask to four 32-bit words, enough for 128 mutator toggles when the dynamic mod-mutator list is wired in.
- Command-line build succeeded and the candidate was deployed to the CXBX test folder.

### 47. Gameplay Menu Pause: Match-Only Freeze With Music Pause

Change/evidence:

- Steve wants Start to open the Xbox menu during gameplay, but single-player matches should freeze while the menu is up.
- The engine already has a first-class pause path: `LevelInfo.Pauser`. `ULevel::Tick` skips normal actor ticking while `Pauser` is non-empty but still permits input/menu drawing, matching the needed behavior.
- `Botpack.UTIntro` explicitly rejects pause and is the intro level class, so the Xbox menu must not apply the match pause there.

Result:

- Opening the Xbox menu now sets `LevelInfo.Pauser` only for standalone, non-`UTIntro` games.
- Closing the menu clears only the pause state that the Xbox menu itself applied.
- Added `XAUDIOPAUSEMUSIC` to the Xbox audio device; menu-applied match pause now stops the current music buffer and resumes it when the menu closes.
- Intro `.unr` menu behavior remains unpaused so the attract/menu background can keep running.

### 48. Dedicated In-Match Pause Menu

Change/evidence:

- Steve confirmed pause works, but pressing Start again was activating the main menu selection instead of unpausing.
- The root cause is interaction design, not the pause primitive: the main menu is an action menu, while an in-match pause layer needs Start/B to mean resume.

Result:

- Start now opens a dedicated `PAUSED` screen when the Xbox menu pause path applies to a standalone match.
- The pause screen defaults to `RESUME` and offers `MAIN MENU`, `INSTANT ACTION`, and `SETTINGS`.
- While a match pause is active, pressing Start from the menu stack resumes instead of activating the focused item.
- B also resumes from the pause screen.
- Intro `.unr` still opens the normal main menu and does not pause.

Follow-up:

- Trimmed the pause menu to the intended focused set: `RESUME`, `MAIN MENU`, and `SETTINGS`.
- `RESUME` remains the default selection.
- Selecting `MAIN MENU` now releases the match pause, travels to `CityIntro.unr`, and leaves the Xbox main menu active over the intro city world.
### 62. Xbox Settings Menu: Controls, Audio, HUD, and Gameplay Toggles
- Added a real `SETTINGS` screen reachable from the main menu and pause menu.
- Exposed live Xbox control tuning for look sensitivity, movement sensitivity, invert-Y, dead zone, and button layout. Button layout now supports default, southpaw sticks, and a face-button-fire variant.
- Added native Xbox audio volume config to `XboxAudio.XboxAudioDevice` and menu commands for music and sound volumes, applied immediately through the audio device.
- Added user-config persistence for announcer volume, crosshair index, HUD color, crosshair color, HUD opacity, weapon hand, auto weapon switching, and mature-language filtering.
- Avoided relying on UnrealScript-only `ConsoleCommand` menu calls for the settings implementation. Native player/HUD fields are updated directly where exposed; Botpack script-only globals are written through `User.ini`.
- Built successfully via `UT99-Xbox\Tools\build_xbox_cli.py`.

### 63. Settings Menu Presentation Pass
- Converted look sensitivity, move sensitivity, stick deadzone, music volume, and sound volume rows from plain numeric values to slider controls with the numeric value retained at the right.
- Added a right-side preview pane for crosshair shape/color and a compact HUD element preview using the selected HUD color and opacity.
- Renamed `AUTO-SWITCH` to `WEAPON AUTO-SWITCH` for clearer context.
- Moved the DPAD left/right helper text lower so it has spacing from the final option row.

### 64. Announcer Volume Slider
- Converted the `ANNOUNCER` setting to the same slider presentation as the other volume-style settings while preserving UT99's native `0-4` announcer volume range.
- Renamed the row to `ANNOUNCER VOLUME` for clarity.

### 65. Rocket Arena and ChaosUT Menu Exposure Attempt Rejected
- Initial pass hardcoded the installed Rocket Arena and ChaosUT classes/maps into the Xbox menu.
- Steve rejected this correctly: mod visibility must come from Unreal's registry/config mechanism, not project-specific special cases.
- The hardcoded approach was removed before test.

### 66. Proper Mod Discovery Through Unreal `.int` Registry
- Audited the PC path instead of guessing:
  - `UObject::CacheDrivers()` scans `GSys->Paths`, converts package globs to `*.int`, and parses `[Public] Object=(Name=...,Class=...,MetaClass=...,Description=...)`.
  - `AActor::GetNextInt` / `GetNextIntDesc` call `UObject::GetRegistryObjects()`.
  - The PC start-match UI discovers game types with `GetNextInt("TournamentGameInfo", ...)`.
  - The PC mutator UI discovers mutators with `GetNextIntDesc("Engine.Mutator", ...)`.
  - Map discovery uses the selected game class default `MapPrefix` through the same `GetMapName` search pattern.
- Reworked the Xbox Instant Action data source to follow that path:
  - Game types are discovered from `.int` registry entries whose metaclass is `Botpack.TournamentGameInfo`, then loaded for `GameName` and `MapPrefix`.
  - Mutators are discovered from `.int` registry entries whose metaclass is `Engine.Mutator`, with display names parsed from `Description`.
  - Arena/map choices are discovered by scanning `GSys->Paths` for maps matching the selected game type's `MapPrefix`.
- Result: Rocket Arena, ChaosUT, and future installed mods should appear because their `.int` files advertise them, not because the Xbox menu knows their names.

### 67. Player Setup Menu Via UT Registry Defaults
- Added a `PLAYER SETUP` entry to the Xbox main menu.
- Mirrored the PC player setup data path instead of hardcoding:
  - Player classes are discovered from `.int` registry entries with metaclass `Botpack.TournamentPlayer`.
  - Skins and faces are discovered from registered texture entries using the selected class mesh name and the same multiskin naming rules used by `UMenuPlayerSetupClient`.
  - Voice packs are discovered from the selected player class default `VoicePackMetaClass`.
- Saves `Class`, `Skin`, `Face`, `Voice`, and `Team` to `[DefaultPlayer]` in `User.ini`.
- Instant Action now appends the selected player options to the travel URL so new matches use the current player setup immediately.
- Built successfully via `UT99-Xbox\Tools\build_xbox_cli.py` and deployed `default.xbe` to the CXBX test install.

### 77. Split Screen Movement Deep Dive
- Steve reported P1 could only swap weapons and show the scoreboard, then asked for a deeper investigation rather than another blind iteration.
- Checked `ut99.log` first. The log proved button events were reaching P1 after split activation, but movement/look/fire could not be trusted from the old generic input logs because their counters were exhausted before split gameplay.
- Source audit findings:
  - button actions go through `UEngine::InputEvent`, while Xbox movement/look/fire are written directly to `APlayerPawn` input fields;
  - `Engine\Classes\PlayerPawn.uc::PlayerInput()` clears `aStrafe`, `aTurn`, `aForward`, and `aLookUp` whenever `bShowMenu && myHUD != None`;
  - split activation happens immediately after an Xbox-menu driven travel, but the split handoff did not explicitly clear legacy `bShowMenu`/`bSpecialMenu` state on the newly spawned match pawns;
  - `Engine\Src\UnGame.cpp` always called `Viewport->Actor->eventPostRender()`, and `PlayerPawn.PostRender()` respawns `myHUD` when it is `None`, so dummy viewports were recreating `ChallengeHUD` every frame despite the C++ code clearing `myHUD`.
- Fixed the structural split handoff:
  - added a split-player preparation helper that clears legacy menu state, marks the pawn ready, forces `PlayerWalking`, and keeps dummy HUDs null;
  - blocked player HUD/console post-render for dummy split viewports in the C++ draw path, so dummy players do not recreate `ChallengeHUD`;
  - split gameplay input now clears any unexpected legacy pause/menu state before applying controller fields.
- Added focused split-only input evidence logging:
  - raw XInput sticks;
  - computed movement/look axes;
  - actor pointer, state, physics, `bShowMenu`, HUD pointer;
  - input fields before/after controller application.
- Built successfully via `UT99-Xbox\Tools\build_xbox_cli.py`.

### 68. Player Setup 3D Preview and Voice Test
- Added the required live right-side player preview to `PLAYER SETUP`.
- Followed the PC menu reference instead of a static thumbnail:
  - `UMenuPlayerSetupClient` changes the mesh to the class default `SelectionMesh` and applies the selected skin/face/team.
  - `UMenuPlayerMeshClient` renders a temporary `MeshActor` through `Canvas.DrawClippedActor`.
  - The Xbox menu now spawns a hidden preview actor, loads the selected class `SelectionMesh`, applies UT's multiskin naming/defaults, and draws it into the right panel with its own clipped scene bounds.
- Added voice testing on voice selection changes:
  - `UTPlayerSetupClient.VoiceChanged()` plays a random `ChallengeVoicePack.AckSound`.
  - The Xbox menu now loads the selected voice class, reads `NumAcks`/`AckSound`, and plays a random acknowledgement through `SLOT_Interface`.
- Preview actors are destroyed/cleared when the menu closes or travels so they do not survive map changes.
- Built successfully via `UT99-Xbox\Tools\build_xbox_cli.py` and deployed `default.xbe` to the CXBX test install.

### 69. Player Preview Framing and Registry Class Fix
- Steve reported the preview should show the full character, be larger, face the screen, and not spin.
- Matched the PC player preview behavior more closely:
  - Kept the view yaw fixed at `32768`, the same front-facing value initialized by `UMenuPlayerMeshClient`.
  - Removed the per-frame yaw increment.
  - Increased the right-side preview panel height so the full body has room instead of feeling cropped.
- Fixed player class discovery binding:
  - `UObject::GetRegistryObjects()` compares metaclass against the exact loaded class path.
  - The Xbox menu now loads `Botpack.TournamentPlayer` explicitly before querying `.int` registry entries, instead of relying on ambiguous `FindObject("TournamentPlayer")`.
  - This is intended to restore multiple player class options; skin, face, and voice rows depend on the selected class having the correct registry base.
- Built successfully via `UT99-Xbox\Tools\build_xbox_cli.py` and deployed `default.xbe` to the CXBX test install.

### 70. Startup DefaultPlayer Face Sanitizer
- Steve provided a CXBX-R trace that reached `SpawnPlayActor`, logged `Login: Player`, then hit a breakpoint exception.
- The same trace showed the startup URL as `CityIntro.unr?Name=Player?Class=Botpack.TMale2?team=1?skin=SoldierSkins.blkt?Face=?Voice=BotPack.VoiceMaleTwo`.
- Verified the test install had `[DefaultPlayer] Face=` blank in `System\User.ini`, so the default player URL was malformed before the player pawn was spawned.
- Added an Xbox-only sanitizer before `DefaultURL.LoadURLConfig()` to fill any empty `Class`, `Skin`, `Face`, `Voice`, or `Team` entry with known-good UT defaults and flush `User.ini`.
- Changed the player setup face fallback from an empty default face to `SoldierSkins.Othello` so the menu cannot re-save the same malformed face value when face discovery fails.
- Built successfully via `UT99-Xbox\Tools\build_xbox_cli.py` and deployed `default.xbe` to the CXBX test install.

### 71. Player Setup Registry Refresh, Left Stick Menu Nav, and Preview Scale
- Steve confirmed the startup crash was fixed, then reported player setup could only change team color.
- Checked the latest CXBX log and found every player setup list reported exactly one entry:
  - `XMENU discovered 1 player classes from .int registry`
  - `XMENU discovered 1 skins for player=Botpack.TMale2 mesh=Soldier`
  - `XMENU discovered 1 faces for player=Botpack.TMale2 skin=SoldierSkins.blkt`
  - `XMENU discovered 1 voices for player=Botpack.TMale2 meta=BotPack.VoiceMale`
- That matched the menu fallback counts, so the likely root was stale `.int` registry data rather than four separate input bugs.
- Added a one-time public `GetRegistryObjects(..., ForceRefresh=1)` refresh before Xbox menu registry discovery.
- Moved player-class base lookup closer to the PC path by trying `FindObject(..., "TournamentPlayer")` before explicit package loading.
- Added left-stick up/down edge navigation for all active menus, matching the existing left-stick left/right option adjustment path.
- Expanded the player preview panel and increased the preview actor scale by reducing camera distance from `4.0/tan(fov/2)` to `2.8/tan(fov/2)`.
- Follow-up: kept the character scale, but extended the preview frame vertically to `Y=58..422`, matching the 16px spacing below the top header and above the footer, then rendered the actor into the taller inner viewport.
- Follow-up 2: the forced registry refresh still produced fallback-only counts in Steve's log, so added a direct `.int` `[Public] Object=(...)` parser for menu discovery when Unreal's registry query returns suspiciously tiny results.
- The direct parser still uses the proper UT registry files and supports installed mods/skins/voices, but bypasses the broken runtime cache/filter path observed on Xbox.
- Shrunk the player preview by about 10% after the taller viewport made the body too large.
- Shifted the player setup values and arrow toggles left while keeping row labels aligned, and shortened the selected-row highlight so it does not overlap the preview panel.
- Built successfully via `UT99-Xbox\Tools\build_xbox_cli.py` and deployed `default.xbe` to the CXBX test install.

### 72. Player Setup Registry Failure Evidence
- Steve reported no change: player setup still could only cycle team color.
- Checked the updated log first. The one-entry lists were confirmed again, and the direct `.int` scanner returned zero objects for every category:
  - `XMENU direct .int scan class=Class meta=TournamentPlayer count=0`
  - `XMENU direct .int scan class=Texture meta= count=0`
  - `XMENU direct .int scan class=Class meta=BotPack.VoiceMale count=0`
- Because the test install definitely contains `Botpack.int` and `SoldierSkins.int` with `[Public] Object=` entries, this points below menu selection logic: either wildcard enumeration is not returning `.int` files, config section loading cannot open the constructed `.int` path, or the class/metaclass filters are rejecting all parsed rows.
- Added narrow scan diagnostics for the next run: search path and file count for the first searches, `[Public]` load result for the first files, and parsed/class-filtered/meta-filtered totals.
- Follow-up log isolated the root: every scan used PC-style paths like `D:\System\../System/*.int`, and `FindFiles` returned zero files before parsing. The Xbox file manager only normalized `../` for relative paths; absolute `D:\...` paths returned immediately.
- Fixed `FFileManagerXbox::ResolvePath()` so absolute paths are normalized too. This directly supports the unmodified PC registry path in `UObject::CacheDrivers()`, which constructs `appBaseDir() + ../System/*.int`.

### 73. Player Setup Boss Preview Crash Evidence
- Steve reported one successful Player Setup entry followed by a crash, then a later crash while entering menus. Checked `ut99.log` first.
- `System\User.ini` now saves `[DefaultPlayer] Class=BotPack.TBoss`, `skin=BossSkins.Boss`, `Face=BossSkins.Xan`, `Voice=BotPack.VoiceBoss`, `team=255`.
- The log proves that saved state boots and logs in successfully as `TBoss`; the crash occurs after `XMENU screen: Player Setup` and `XMENU player preview actor created`, before the existing player preview mesh/skin summary.
- Audited PC/root script behavior: `UMenuPlayerSetupClient.UseSelected()` calls `NewPlayerClass.static.SetMultiSkin(...)`, and `Botpack.TBoss` overrides `SetMultiSkin()` with Boss-specific slots/names rather than the generic `TournamentPlayer.SetMultiSkin()` path.
- Changed Xbox preview skinning to detect `TBoss`/`TBossBot` and apply the Boss-specific texture slot rules from `Botpack.TBoss.SetMultiSkin()`. Added focused logs around preview mesh load and skin application so the next crash log identifies the exact failing stage instead of only showing actor creation.
- Steve confirmed Player Setup now looks and functions as intended. Moved the main-menu `PLAYER SETUP` item down to sit directly above `SETTINGS`, and updated the activation switch to match the visual order.

### 74. System Link Connectivity Probe
- Steve approved CXBX-R caveats and asked for a simple two-instance communication test before wiring full System Link gameplay.
- Added a menu-local `SYSTEM LINK` probe instead of touching the Unreal net driver yet:
  - initializes XNet before WSAStartup, following the XDK WinsockPeer/UC2004 pattern;
  - binds a UDP socket to the first available port in 9777-9780;
  - broadcasts `UTXSL1` heartbeat packets once per second to all probe ports;
  - records peer IDs/IPs/ports/packet counts on screen and in `ut99.log`.
- Added `UT99-Xbox/Docs/SystemLinkProbe_CXBX.md` with two-instance CXBX-R setup and expected log/menu output.

### 75. Basic Four-Viewport Split Screen Harness
- Added a first-pass `SPLITSCREEN` main-menu path for local test coverage before full System Link gameplay.
- Starts `DM-Deck16][.unr` as `Botpack.DeathMatchPlus` with `MinPlayers=0`, `MaxPlayers=4`, no frag limit, and no time limit.
- Creates four active local viewports and lays them out in a 2x2 grid:
  - player 1 top-left;
  - dummy player 2 top-right;
  - dummy player 3 bottom-left;
  - dummy player 4 bottom-right.
- Shares the primary Xbox render device across the dummy viewports, clears only the first split viewport, and presents only after the fourth viewport has drawn.
- Applies each viewport's quadrant to the scene frame before rendering so the 3D view is clipped to its correct screen region.
- Only viewport 1 polls the controller. Viewports 2-4 spawn as local dummy players with zeroed movement/fire input so Steve can walk around and kill them.
- Added a simple dummy respawn request: dead/hidden dummy players call the stock `ServerReStartPlayer` path after 3 seconds.
- Built successfully via `UT99-Xbox\Tools\build_xbox_cli.py`.
- Follow-up: Steve hit a freeze immediately after the loading screen. The log stopped at `LoadMap: DM-Deck16][...` after the dummy viewports had already been created.
- Root cause: the first pass created viewports 2-4 before travel, so the normal `LoadMap()` viewport-dissociation path saw extra viewports with no valid player actors during the map transition.
- Changed split screen to queue the request, travel with only viewport 1, then activate split mode after the DM map is live:
  - viewport 1 loads the map normally;
  - `UXboxClient::Tick()` calls the split activation hook after travel;
  - the hook creates viewports 2-4 and spawns dummy player actors using the live level's normal `SpawnPlayActor()` path.
- Rebuilt successfully via `UT99-Xbox\Tools\build_xbox_cli.py`.

### 76. Split Screen Input Ownership and Bot Suppression
- Steve tested the queued split flow and reported bots were active and viewport 1 could not move.
- Checked `ut99.log` first. The log showed the DM map loaded, viewport 1 possessed `TBoss0`, split activation created dummy viewports 1-3, and `XSPLIT active viewports=4`.
- The same log also showed three extra `XINPUT open port=0` calls after split activation, followed by direct input being applied to all four pawns. Root cause: dummy viewports were bypassing the client tick guard through `UXboxViewport::UpdateInput()`, which still called `PollController()`.
- Audited `Botpack.DeathMatchPlus`: its default `InitialBots=4` is copied into `RemainingBots` during startup, and standalone games use ready/countdown state. `MinPlayers=0` alone was not enough to make a dummy-only test arena.
- Fixed dummy input ownership:
  - dummy viewports now return immediately from both `PollController()` and `UpdateInput()`;
  - only viewport 1 can sample the physical controller.
- Suppressed stock match automation for the split harness:
  - zeroed `Botpack.DeathMatchPlus` class defaults for `InitialBots` and `MinPlayers` before travel;
  - after map activation, zeroed the live game object's `InitialBots`, `RemainingBots`, and `MinPlayers`;
  - destroyed any stock `Botpack.Bot` pawns already spawned by the game;
  - marked all local split players ready and called the stock `StartMatch()` script function.
- Built successfully via `UT99-Xbox\Tools\build_xbox_cli.py` and deployed `default.xbe` to the CXBX test install.

### 77. Split Screen No-Movement Evidence
- Steve reported that split-screen P1 still had no movement. Checked `ut99.log` first.
- The split harness itself was healthy: the log showed `XSPLIT active viewports=4`, P1 possessed `TBoss0`, all players in `PlayerWalking`, `showMenu=0`, and no `Pauser`.
- The input trace proved controller data was arriving, but the left stick was being mapped into look axes:
  - raw left stick values such as `raw=-32768,0,0,0` produced `axes=0.00,0.00,-100.00,0.00`;
  - raw left stick Y values such as `raw=0,32767,0,0` produced `axes=0.00,0.00,0.00,100.00`.
- Root cause was a persisted `ButtonLayout=1` value. That layout was labeled as a stick option and swapped movement/look sticks inside `ProcessControllerInput()`, even though it lived in the button-layout setting.
- Fixed by removing stick swapping from `ButtonLayout`, limiting the menu to actual button presets (`DEFAULT`, `FACE FIRE`), and migrating old `ButtonLayout=1` saves back to default on client init.

### 78. Split Screen Movement Consumption Evidence
- Steve reported no visible movement after the button-layout fix. Checked `ut99.log` first.
- The new log proved the layout migration worked and left stick now maps to movement axes:
  - `XboxClient::Init: migrating obsolete southpaw ButtonLayout=1 to default button layout`
  - `raw=0,32767,0,0 axes=0.00,100.00,0.00,0.00`
- The same log showed `aBaseY` and `aStrafe` accumulating after split activation instead of being consumed by the stock `PlayerInput()`/`PlayerMove()` path.
- Source trace found a split-specific ordering bug: `UXboxClient::Tick()` was still polling P1 during the render/client phase, after `GLevel->Tick()` had already run `ReadInput -> PlayerInput -> PlayerTick -> ReadInput(-1)`. This wrote movement fields too late for the current frame and polluted the next diagnostic sample.
- Changed split mode so render/client tick no longer polls controllers. Split input now has to flow through `UViewport::ReadInput()` during the level tick, matching the stock UT movement path.
- Expanded split input diagnostics with pawn location, velocity, and acceleration so the next test can prove whether `PlayerMove()` consumes the input and changes movement state.
- Follow-up: Steve tested again; P1 still could not move. Checked `ut99.log` first.
- New evidence:
  - no extra dummy `XINPUT open port=0` lines appeared after split activation, so the previous dummy input ownership bug was fixed;
  - after `XSPLIT active viewports=4`, there were no gameplay `XINPUT direct` lines, meaning controller input was not reaching the pawn path;
  - the log was flooded by `ChallengeHUD` errors from dummy viewport HUDs.
- Applied a split-specific gameplay handoff:
  - split input now hard-closes any stale Xbox menu and bypasses menu input handling while the split harness is active;
  - live `DeathMatchPlus` `bRequireReady` and `CountDown` are cleared along with bot counts;
  - `LevelInfo.Pauser` is cleared during activation;
  - each split player is marked ready and forced into `PlayerWalking` after `StartMatch()`;
  - dummy viewport HUD pointers are cleared so only P1 owns a HUD and the dummy `ChallengeHUD` spam stops.
- Built successfully via `UT99-Xbox\Tools\build_xbox_cli.py` and deployed `default.xbe` to the CXBX test install.
- Follow-up: Steve reported no movement, with scoreboard and weapon swap still working, and pause gone.
- Checked `ut99.log` first. The log showed P1's controller path receiving the left stick and writing movement-like values, but P1's `Location`, `Velocity`, and `Acceleration` stayed unchanged. The old/new axis trace also showed `aBaseY`/`aStrafe` accumulating instead of being consumed.
- Source trace:
  - stock level tick runs `ReadInput -> eventPlayerInput -> eventPlayerTick -> ReadInput(-1)`;
  - `PlayerPawn.PlayerInput()` remaps `aBaseY` into `aForward`;
  - `PlayerWalking.PlayerMove()` consumes only `aForward/aStrafe` to produce `NewAccel`;
  - split input was still not reliably landing at the exact point `PlayerMove()` consumes it.
- Added a split-only native input cache and handoff:
  - `UXboxViewport::ProcessControllerInput()` stores P1 stick values in the split cache instead of directly accumulating `aBaseY`/`aStrafe`;
  - `UnLevTic.cpp` calls `XboxSplitApplyNativePlayerInput()` immediately after `eventPlayerInput()` and before `eventPlayerTick()`, placing movement directly in `aForward/aStrafe` where `PlayerWalking.PlayerMove()` expects it;
  - added focused `XSPLIT tick` and `XSPLIT apply` logs around `ReadInput`, `PlayerInput`, and `PlayerTick` to prove whether velocity/acceleration change after the handoff.
- Restored split pause handling: Start now opens the existing pause menu in split mode instead of the split gameplay path closing/bypassing menu handling entirely.
- Built successfully via `UT99-Xbox\Tools\build_xbox_cli.py`.
- Follow-up: Steve reported still no movement. Checked `ut99.log` first.
- New evidence:
  - controller samples were reaching the split path and the cached handoff was firing;
  - `aForward/aStrafe/aTurn` accumulated into very large values across frames;
  - `Location`, `Velocity`, and `Acceleration` remained zero, so the late cached handoff was bypassing the normal `UInput` scale/reset lifecycle instead of behaving like stock gameplay input.
- Re-audited the stock path:
  - `UViewport::ReadInput()` calls `UpdateInput()` first;
  - platform input is expected to write raw `aBaseY/aStrafe/aTurn/aLookUp` there;
  - `UInput::ReadInput()` then scales those axes;
  - `PlayerPawn.PlayerInput()` remaps `aBaseY` to `aForward`;
  - `PlayerWalking.PlayerMove()` produces `Acceleration`;
  - `ReadInput(-1)` clears the input floats.
- Removed the split-only late cached input injection from `UnLevTic.cpp`.
- Changed split gameplay input to feed the same direct axis fields as non-split gameplay during `UXboxViewport::UpdateInput()`. This is now back on the stock UT input pipeline; render-phase polling remains disabled during split so the old late-write problem should not recur.
- Adjusted split tick diagnostics so idle dummy players do not consume the log budget before real input arrives.

### 79. Split Screen Movement Root Cause Verified By Isolated Smoke Test
- Steve asked whether we could minimize iterations by smoke-testing with the isolated CXBX-R copy at `C:\Programming\GitHub\UnrealTournament_1.40\CXBXR`.
- Added a file-gated smoke harness triggered only by `D:\XboxSplitSmoke.ini`. It queues split-screen on `DM-Deck16][.unr`, disables bots, feeds deterministic P1 forward input, and logs `XSPLIT SELFTEST PASS/FAIL`.
- First smoke iterations proved:
  - the level, split viewports, P1 ownership, `PlayerWalking`, and script `PlayerInput`/`PlayerTick` functions were all present;
  - `UInput` reflected properties existed with `CPF_Input`;
  - direct C++ writes to `APlayerPawn::aBaseY`, `aStrafe`, `bFire`, etc. did not line up with the reflected UnrealScript property offsets that `UInput` and script actually consume.
- Root cause: Xbox gameplay input was writing compiled C++ `APlayerPawn` members directly. For this port, those offsets are not a trustworthy ABI for reflected script input fields.
- Correct fix: route gamepad input through the stock `UInput` command path:
  - axes now use `SetInputAction(IST_Axis)` plus `Input->Exec("AXIS aBaseY SPEED=...")`;
  - buttons now use `SetInputAction(IST_Press/IST_Release)` plus `Input->Exec("BUTTON bFire")`, etc.
- Removed the failed split-only native input cache and the heavy `UnLevTic.cpp` / `UnIn.cpp` diagnostics after proving the cause.
- Build succeeded with `UT99-Xbox\Tools\build_xbox_cli.py`.
- Isolated CXBX-R smoke result:
  - `XSPLIT SELFTEST PASS movement elapsed=0.75 distSq=47622.9 loc=706.5,-1441.7,-724.2 vel=-338.4,213.2,0.0 acc=-723.9,456.1,0.0`
- The isolated emulator settings were restored and the smoke trigger file removed after the test.

### 80. Split Screen Fire / Jump Control Fix
- Steve reported that movement worked but several gameplay controls, including jump and fire, were still missing.
- Checked the latest `ut99.log` first. It showed left-stick movement producing real acceleration, but later `XINPUT state` lines showed `a=255` while the gameplay trace still reported `jump=0`.
- Source audit:
  - movement was fixed because it fed reflected `UInput` axes;
  - fire/alt-fire/duck had been routed only to reflected byte fields, skipping the PC-style aliases that also execute `Fire`, `AltFire`, and `Jump` script commands;
  - `StaticInitInput()` forced Xbox axis binds, but did not force `Joy1`-`Joy4` gameplay button binds, leaving controller button behavior dependent on stale or incomplete user config.
- Fixed the button path to match UT's normal binding model:
  - `Joy1=Fire`
  - `Joy2=Jump`
  - `Joy3=AltFire`
  - `Joy4=Duck`
- Gameplay buttons now emit real `UEngine::InputEvent()` press/release events on those virtual joystick keys instead of directly toggling `APlayerPawn` C++ members.
- Default layout mapping:
  - right trigger -> `Joy1` / Fire
  - A -> `Joy2` / Jump
  - left trigger or B -> `Joy3` / AltFire
  - left stick click -> `Joy4` / Duck
- Face-fire layout mapping remains:
  - A -> Fire
  - right trigger -> Jump
  - left trigger or B -> AltFire
  - left stick click -> Duck
- Build succeeded with `UT99-Xbox\Tools\build_xbox_cli.py`.
- Isolated CXBX-R split smoke still passed after the change:
  - `XSPLIT SELFTEST PASS movement elapsed=1.00 distSq=1338.8 loc=1738.7,1544.2,-660.2 vel=-47.8,-3.8,0.0 acc=-39.9,-3.2,0.0`
- Restored the isolated CXBX-R settings and removed the temporary smoke trigger again.

### 81. Split Screen Control Scheme Audit And Silo Check
- Steve asked for a full control audit before more testing, plus confirmation that split-screen remains siloed from single-player.
- Static code audit findings:
  - `UEngine::InputEvent(Viewport, ...)` routes to the `UInput` instance owned by the exact viewport passed in, so button events are per-viewport as long as the Xbox layer passes `this`;
  - `UXboxViewport::PollController()` already returns immediately for `bXboxSplitDummy`, so current dummy viewports cannot steal P1 input;
  - gameplay axes use the corrected reflected `UInput` axis path (`AXIS aStrafe/aBaseY/aTurn/aLookUp`) and are therefore not dependent on compiled C++ member offsets;
  - face buttons/triggers now route through `Joy1`-`Joy4` so Fire, Jump, AltFire, and Duck execute the same aliases as PC/default joystick input;
  - utility controls route through viewport-local `InputEvent()` as D-pad weapon select, Back scoreboard, X inventory/use, Y next weapon, Black/White inventory previous/next.
- Found one actual control gap: right-stick click was mapped to `IK_Joy6`, but Xbox forced defaults blanked `Joy6`. Added the missing `CenterView` alias and bound `Joy6=CenterView`.
- Found one actual split isolation gap from the engine trace:
  - `UGameEngine::Browse()` calls `MatchViewportsToActors()` for every `Client->Viewports` entry;
  - if four split viewports remain in `Client->Viewports`, a later CityIntro or single-player travel will spawn actors for all of them.
- Added `XboxSplitResetRuntime()` and call it before non-split travels (`InstantAction`, `ReturnToFrontend`) and before starting a new split session. It destroys dummy split viewports, restores the primary viewport to full screen, clears split flags, and logs the reset reason.
- No smoke test run yet for this section per Steve's request to audit first.

### 82. Deterministic Split Controller Ownership
- Steve clarified the hard rule: controller port number must equal viewport number. No first-available controller assignment.
- Changed Xbox controller opening from "first available controller" to deterministic viewport-index ownership:
  - viewport 0 -> physical port 0 / controller 1;
  - viewport 1 -> physical port 1 / controller 2;
  - viewport 2 -> physical port 2 / controller 3;
  - viewport 3 -> physical port 3 / controller 4.
- Split activation now samples the gamepad device mask and marks each viewport as dummy only when its matching controller port is not present.
- `XboxSplitTickDummies()` now re-checks the device mask and only zeroes/respawns players that are actually dummy viewports. Controller-backed viewports 2-4 are left alone so their `UpdateInput()` path can poll their matching ports.
- If a split controller is unplugged, its viewport returns to dummy mode and its handle/state are cleared.
- Single-player also stays deterministic: viewport 1/controller 1 only, instead of silently binding to the first plugged-in controller.
- Built successfully with `UT99-Xbox\Tools\build_xbox_cli.py` and deployed `default.xbe` to `C:\Games\Emulators\CXBX\UT99x\default.xbe`.

### 83. Split Pause/Main Menu Exception Audit
- Latest split-screen return-to-main-menu run failed in `FMallocXbox` while loading `CityIntro.unr`, immediately after split teardown. Cxbx's kernel debug did not add a more useful UT CPU exception; Unreal's own log showed the allocator failure first.
- The log showed available memory falling steadily while the split pause menu was open, then `FMallocXbox: Out of memory reallocating 877440 bytes` during `LoadMap: CityIntro.unr`.
- Code audit found a render-frame pairing bug: `UXboxRenderDevice::Lock()` called `Device->BeginScene()` but did not set `SceneOpen`, while `Unlock()` only calls `EndScene()` when `SceneOpen` is true. This can leave unmatched scene state, multiplied by split-screen's four viewport draws.
- Patched `Lock()` to set `SceneOpen = SUCCEEDED(hrBegin)` so the scene opened for the frame is always eligible to be closed by `Unlock()`.
- Split return-to-frontend teardown now destroys dummy player actors before destroying dummy viewports and flushes the engine render cache before queuing `CityIntro.unr`.
- Added a compact split pause renderer so the full main-menu chrome/footer is not drawn separately into every viewport.

### 84. Weapon Wheel First Pass
- Steve provided the weapon wheel mockup and requested a counter-clockwise wheel with no slot numbers, per-weapon icons, greyed unavailable weapons, and bottom text in the exact format `[weapon name] - ([ammo count])`.
- Source-backed weapon/icon audit used the Botpack `.uc` defaults, not guessed texture names:
  - `ImpactHammer` -> `Botpack.Icons.UseHammer`
  - `Enforcer` -> `Botpack.Icons.UseAutoM`
  - `UT_BioRifle` -> `Botpack.Icons.UseBio`
  - `ShockRifle` -> `Botpack.Icons.UseASMD`
  - `PulseGun` -> `Botpack.Icons.UsePulse`
  - `Ripper` -> `Botpack.Icons.UseRazor`
  - `Minigun2` -> `Botpack.UseMini`
  - `UT_FlakCannon` -> `Botpack.Icons.UseFlak`
  - `UT_Eightball` -> `Botpack.Icons.Use8ball`
  - `SniperRifle` -> `Botpack.UseRifle`
  - `WarHeadLauncher` -> `Botpack.UseWarH`
  - `Translocator` -> `Botpack.Icons.UseTrans`
  - `ChainSaw` -> `Botpack.Icons.UseSaw`
- Added a gameplay-only wheel overlay drawn after the world/HUD, with greyed icon/text state when the weapon is missing or out of ammo.
- Ammo count is read through Unreal reflection (`AmmoAmount`) because generated Xbox C++ headers do not expose `AAmmo::AmmoAmount` directly.
- Selection uses the stock `GetWeapon <class>` command so Enforcer selection follows UT's own single/dual-enforcer behavior.
- White/LB and Black/RB now support tap-or-hold behavior:
  - tap White/LB: previous weapon
  - tap Black/RB: next weapon
  - hold either: open the weapon wheel
- While the wheel is open, only right-stick look is suppressed. Movement, jump, fire, alt-fire, crouch, and use still pass through. Scoreboard/back and pause/start are blocked while the wheel is active.
- Updated face controls for the new scheme:
  - A: jump
  - B: duck/crouch
  - X: use
  - Y + left-stick direction: dodge
  - right trigger: fire
  - left trigger: alt-fire
- Build succeeded with `UT99-Xbox\Tools\build_xbox_cli.py`.

### 85. Weapon Wheel Geometry And Icon Mask Correction
- Steve clarified the mockup should be followed as exact geometry: 16 equal wheel slices with the top 3 missing, no blue background, leaving 13 visible wedge-shaped weapon slots.
- Replaced the first-pass rectangular slot blocks with calculated annular sector slices:
  - 16 equal angular sectors;
  - the 3 centered around 12 o'clock are omitted;
  - the remaining 13 slots are laid out counter-clockwise from the upper-left side of the gap.
- Removed the full-screen blue overlay from the wheel path so only the wheel wedges/icons/label are drawn over gameplay.
- Fixed icon rendering to force masked texture semantics instead of always drawing as translucent. This matches how UT PC draws `Weapon.StatusIcon` via `Canvas.DrawIcon()` and should prevent the square icon background from being treated as the icon itself.
- Build succeeded with `UT99-Xbox\Tools\build_xbox_cli.py`.

### 86. Weapon Wheel Presentation Pass
- Steve tested the wheel and found two presentation failures: wedge fills were grainy, and the weapon art was still showing square HUD boxes instead of isolated weapon silhouettes.
- Source audit confirmed the first pass used `Use*` textures from the weapon defaults, which are the black "selected/use" HUD box assets. PC UT separately imports color weapon silhouettes as `Icon*` textures in the `Botpack.Icons` group.
- Switched the wheel table to the `Icon*` weapon silhouettes:
  - `IconHammer`, `IconAutoM`, `IconBio`, `IconASMD`, `IconPulse`, `IconRazor`, `IconMini`, `IconFlak`, `Icon8ball`, `IconRifle`, `IconWarH`, `IconTrans`, `IconSaw`.
- Added a runtime texture fallback that retries `Botpack.IconName` when the grouped `Botpack.Icons.IconName` lookup fails. This covers older defaults such as `UseMini`, `UseRifle`, and `UseWarH` that prove some Botpack icon references may exist outside the group path.
- Replaced the scanline wedge fill with `XboxRenderDrawMenuRingSlice`, a proper D3D triangle-list annular-sector helper. This removes the horizontal grain caused by drawing every slice as thousands of 2-pixel menu rectangles.
- Added a layered visual treatment: dark wedge underlay for contrast, clean light wedge fill, and a subtle cobalt focus sheen behind the selected weapon icon.

### 87. Weapon Wheel Pickup Mesh Pivot
- Steve confirmed the wheel geometry is much better, but the packaged weapon silhouette icons still are not resolving into the desired art.
- Pivoted from HUD texture icons to actual weapon pickup meshes, using data that already exists on each weapon class:
  - load each `AWeapon` class;
  - read `AInventory::PickupViewMesh`, falling back to `AActor::Mesh`;
  - preserve each class default `PickupViewScale` and pickup `Rotation`.
- Added a separate hidden weapon-wheel preview actor so this does not interfere with the existing Player Setup preview actor.
- The wheel now renders each slot as a tiny 3D pickup model through the same menu mesh-preview path used by Player Setup: temporary clipped canvas frame, small FOV, Z clear for the slot, then `DrawActor`.
- Kept the previous texture icon path only as a fallback if a weapon mesh is missing.
- Unavailable weapons now render dimmed through lower `ScaleGlow`, while available and focused weapons render brighter.

### 88. Weapon Wheel Mesh Material And Mutator Overlay Cleanup
- Steve's log confirmed every weapon-wheel slot resolved to the intended map pickup mesh (`ImpPick`, `MagPick`, `BRifle2Pick`, `ASMD2pick`, `Flak2Pick`, `Eight2Pick`, etc.), so the remaining grey-box problem was in the preview render state, not asset selection.
- Fixed the mesh preview actor to copy the weapon class default visual fields before drawing:
  - `Style`;
  - `Texture`;
  - `Skin`;
  - all 8 `MultiSkins`;
  - `bMeshEnviroMap`;
  - `bMeshCurvy`.
- Enlarged the slot mesh preview frame based on the wheel ring thickness instead of the old HUD-icon size, so pickup models have enough screen area to read as weapons rather than tiny low-detail blocks.
- Reworked the Mutators overlay layout:
  - larger, darker panel;
  - dedicated header, list, and footer regions;
  - five-row scroll window;
  - footer prompts moved below the list with separator lines;
  - `MORE ^/v` indicators moved out of the row text area.
- Build succeeded with `UT99-Xbox\Tools\build_xbox_cli.py` and the XBE was deployed to the CXBX test install.

### 89. Weapon Wheel Pre-Rendered Mesh Sprite Pivot
- Steve's screenshot proved the live HUD previews still were not presenting as weapon meshes in the wheel: the data lookup was correct, but the tiny live actor render path remained too fragile for 13 independent previews in one HUD pass.
- Added `UT99-Xbox\Tools\render_weapon_wheel_assets.py`, a source-asset renderer that reads UT's original `*_a.3D` / `*_d.3D` James mesh files plus PCX skins. The parser follows the PC import path in `Editor\Src\UnMeshEd.cpp` and the packed `FMeshVert` layout in `Engine\Inc\UnMesh.h`.
- Generated transparent loose XUI sprites for all 13 wheel weapons:
  - Impact Hammer, Enforcer, Bio Rifle, Shock Rifle, Pulse Gun, Ripper, Minigun, Flak Cannon, Rocket Launcher, Sniper Rifle, Redeemer, Translocator, Chainsaw.
- Wrote a contact sheet at `UT99-Xbox\MenuAssets\weapon_mesh_contact.png` to verify the source renders show colored weapon/pickup shapes rather than square HUD icon boxes.
- The gameplay wheel now draws these pre-rendered XUI sprites first and only falls back to the live pickup mesh path if a loose asset is missing.
- Kept the renderer scissor / slot-only Z-clear helpers in place as a safer fallback for any future live menu mesh preview use.
- Steve corrected the orientation requirement: these must read in profile, not isometric. Updated the asset renderer to compute a per-mesh profile camera from the weapon's longest principal axis, keeping the weapon long axis horizontal and rendering from the side.
- Regenerated and redeployed all 13 weapon XUI sprites with the profile orientation.
- Steve's next test still showed weapon-wheel junk, so the runtime path was audited instead of changing the images again:
  - generated sprites were valid XUI0 files and the contact sheet proved the source images were not square HUD tiles;
  - `XboxRenderDrawMenuTexture()` loaded loose `D:\MenuAssets\*.xui` into a fixed `GXboxMenuTextures[8]` cache;
  - the wheel requires 13 weapon sprites by itself, before counting button/logo/menu assets;
  - when a sprite failed to load, the wheel still fell back to the older live pickup mesh / HUD icon paths that caused the same grey-box failure.
- Increased the menu texture cache to 32 entries so all weapon-wheel sprites and normal menu assets can stay resident together.
- Removed the weapon wheel's live pickup-mesh fallback from the draw path. If a loose sprite is present, it is used; if it is not, only the old HUD icon fallback can draw. The broken live mini-mesh preview no longer re-enters the wheel.
- Darkened the Mutators overlay's scrolling list region separately from the header/footer so the marked list area stands out.

### 90. Weapon Wheel Runtime Audit And Gotcha Fixes
- Re-audited the wheel after Steve reported the same junk visuals, instead of assuming the cache increase was sufficient.
- Found a second major runtime gotcha: the generated loose weapon sprites were 96x96 XUI textures. That size is legal on modern D3D, but it is a bad fit for the original Xbox swizzled texture path and can fail or sample incorrectly on the XDK D3D8/XG upload path.
- Regenerated every wheel sprite as a 128x128 power-of-two XUI texture and verified every file header from disk:
  - magic `XUI0`;
  - dimensions `128x128`;
  - exact byte length `65548` (`12 + 128 * 128 * 4`).
- Hardened `XboxRenderDrawMenuTexture()` for HUD/menu sprites:
  - explicitly clamps `ADDRESSU`/`ADDRESSV` for every loose menu texture draw;
  - disables texture coordinate transforms;
  - disables mip filtering for one-level UI textures.
- Hardened failure behavior in `XboxWeaponWheelDraw()`:
  - each wheel slot has a `SpriteName`, so the pre-rendered sprite is now the authoritative wheel visual;
  - if the sprite draw fails, the code logs `XWHEEL sprite draw failed` and skips that slot instead of falling into the old grey-box/icon/mesh path;
  - the old live pickup mesh helper functions remain compiled but are no longer reachable from the wheel draw path.
- Added a menu texture cache-full log entry (`XMENU tex cache full...`) so future asset pressure is visible in `ut99.log`.
- Audit conclusion before test: the previously observed grey-box weapon wheel could be reproduced by either exhausting the 8-entry loose texture cache or by failing the fragile 96x96 sprite upload, then falling through to the old broken preview path. The current patch removes all three failure conditions from the active wheel path.
- Build succeeded with `UT99-Xbox\Tools\build_xbox_cli.py`.
- Deployed `default.xbe` plus all 13 verified 128x128 weapon XUI sprites to the CXBX test install at `C:\Games\Emulators\CXBX\UT99x`.

### 91. Weapon Wheel Sprite Scale And Slice Transparency
- Steve confirmed the weapon wheel now works, with only presentation adjustments remaining.
- Added per-slot sprite scaling:
  - Impact Hammer stays at `1.00x`;
  - every other weapon sprite draws at `1.50x`.
- Scaled the sprite draw rectangle at runtime instead of baking larger sprites, preserving the verified 128x128 XUI assets and avoiding accidental source-image cropping.
- Reduced wheel slice alpha so the wedges read as a translucent gameplay overlay:
  - dark underlay alpha lowered from `190/130` to `132/86`;
  - main available slice alpha lowered from `222/182` to `162/122`;
  - unavailable slice alpha lowered from `98` to `66`;
  - focus sheen alpha lowered from `74` to `52`.
- Build succeeded with `UT99-Xbox\Tools\build_xbox_cli.py`.
- Deployed updated `default.xbe` to the CXBX test install.

### 92. Weapon Wheel Larger Non-Hammer Icons
- Steve confirmed the previous scale pass was good and asked for every weapon except Impact Hammer to grow another 30%.
- Bumped non-hammer weapon sprite scale from `1.50x` to `1.95x`.
- Impact Hammer remains at `1.00x`.
- Build succeeded with `UT99-Xbox\Tools\build_xbox_cli.py`.
- Deployed updated `default.xbe` to the CXBX test install.

### 93. Open Item Cleanup
- Steve confirmed the OGX360 adapter/control issue is resolved.
- Closed OGX360 adapter support as an open item.

### 94. Duke Button Prompt Assets
- Replaced the current menu button prompt assets with crops from `C:\Users\smmel\Downloads\xbox_duke_buttons.png`.
- Updated `UT99-Xbox\Tools\build_menu_assets.py` so the asset build now carves the Duke sheet into transparent 64x64 XUI textures.
- Generated a full Duke prompt set for current and future UI use:
  - A, B, X, Y, White, Black, Start, Back;
  - left/right stick, left/right stick move, L/R, LT/RT;
  - D-pad neutral and cardinal directions.
- Verified every generated `button_*.xui` file parses as `64x64` with expected byte size `16396`.
- Deployed all generated button assets to `C:\Games\Emulators\CXBX\UT99x\MenuAssets`.
- No XBE rebuild was needed because the runtime loads these prompt textures as loose menu assets.

### 95. Weapon Wheel Grey Pass And Player Voice Samples
- Steve confirmed the Duke buttons look good and requested:
  - weapon wheel slices should be grey instead of blue-grey;
  - wheel slices should be slightly darker and about 20% more transparent;
  - player setup should play a voice sample when changing voice.
- Updated the weapon wheel RGBA constants:
  - dark wheel underlay changed from blue-tinted `(18,32,58)` to neutral `(10,10,10)`;
  - available slices changed from blue-grey `(214/184,224/218,228/212)` to neutral greys `(176/148)`;
  - slice alphas reduced by about 20% (`132/86 -> 106/69`, `162/122 -> 130/98`, `66 -> 53`, `52 -> 42`).
- Root cause for missing voice sample: `XboxMenuPlayVoiceSample()` already fired when the voice row changed, but `XboxAudio::PlaySound()` suppressed all effects while menus were open.
- Added a narrow bypass for only the explicit player-menu voice sample and only `SLOT_Interface`; normal menu-open effect/VO suppression remains active.
- Added voice sample result logging (`XMENU voice sample ... played=0/1`) plus skip-reason logs for missing class/defaults/sound.
- Build succeeded with `UT99-Xbox\Tools\build_xbox_cli.py`.
- Deployed updated `default.xbe` to the CXBX test install.
