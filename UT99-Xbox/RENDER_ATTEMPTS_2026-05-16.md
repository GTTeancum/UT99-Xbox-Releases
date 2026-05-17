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
