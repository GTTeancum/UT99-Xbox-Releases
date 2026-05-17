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
