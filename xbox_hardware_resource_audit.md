# UT99 Xbox Hardware Resource Audit

This document is a UT99 Xbox-specific checklist for long-play stability on real
hardware. The game now runs smoothly, so the next risk is not raw frame time; it
is stale state surviving too long: GPU textures that outlive their owner,
DirectSound buffers that duplicate package data, menu caches that never reset,
split-screen viewports that survive travel, and small side tables keyed by object
pointers.

The rule for this pass is simple: every Xbox-only cache needs a named lifetime
and a visible purge point. Unreal's normal `LoadMap -> ResetLoaders ->
CollectGarbage` path cleans up many engine objects, but it does not automatically
know about every native cache we added around the engine.

## Resource Lifetimes

Use these lifetimes when auditing or changing code:

- **Process lifetime**: device objects and data that intentionally live until
  shutdown, such as the D3D device, DirectSound object, logger, and static build
  settings.
- **Frontend lifetime**: main menu assets, UI button textures, logo textures,
  menu discovery lists, and player-preview helper actor.
- **Map lifetime**: world textures, lightmaps, realtime textures, actor audio,
  player actors, split-screen dummy actors, and map package objects.
- **Transient draw lifetime**: batched draw vertices, current bound texture IDs,
  render-state cache, and scratch upload buffers.
- **Network/probe lifetime**: System Link discovery socket, peer list, counters,
  and last errors.

Anything that does not fit one of those buckets is a stale-data candidate.

## High Priority Findings

### Texture PolyFlag Side Table

File: `Engine/Src/UnTex.cpp`

Current behavior:

- `UTexture::PolyFlags()` uses a side table keyed by `UTexture*`.
- `UTexture::ClearAllPolyFlags()` can empty the whole table.
- The comment says stale entries are cleared from `Destroy()` / `FlushAll`.
- `UTexture::Destroy()` currently calls only `Super::Destroy()`.

Risk:

- If a `UTexture*` is destroyed and a later texture reuses the same address, the
  old side-table flags can be applied to the new texture.
- That can produce wrong masking, translucency, no-smooth, unlit, portal, or
  waving flags after long play or repeated map changes.

Target:

- Remove the texture's side-table entry in `UTexture::Destroy()`.
- Keep `ClearAllPolyFlags()` for global shutdown or emergency full reset.
- Add a tiny debug counter for side-table size at map transitions until proven
  stable.

### Xbox Render Texture Cache

Files:

- `UT99-Xbox/XboxRender/inc/XboxRender.h`
- `UT99-Xbox/XboxRender/src/XboxRender.cpp`

Current behavior:

- `UXboxRenderDevice::Flush()` calls `FlushTexCache()`.
- `FlushTexCache()` unbinds stages, releases cached D3D textures, clears
  `BoundCacheID`, zeros `TexPool`, and resets `TexLiveBytes`.
- `Lock()` also flushes when `TexLiveBytes > 12 MB` or the resident texture pool
  crosses its limit.
- Texture entries now include `MaskedAlpha` so P8 masked and non-masked uploads
  do not share the wrong alpha variant.

Risk:

- This cache is mostly well-contained, but it depends on engine calls to
  `RenDev->Flush()` at the right moments.
- `UGameEngine::Browse()` has a comment near garbage collection noting texture
  corruption unless the renderer is flushed. That is a hint that render flush
  ordering is important.
- If a future map/menu transition bypasses `Engine->Flush(0)`, stale D3D
  textures can pin memory even after Unreal objects are garbage-collected.

Target:

- Make every map travel path explicitly flush the render device before
  `CollectGarbage()`.
- Log one resource snapshot after flush: `TexLiveBytes`, `TexPoolNext`,
  available physical memory, and current map/menu phase.
- Keep the existing emergency texture-cache flush, but treat it as a warning,
  not the normal steady-state cleanup.

### Static Menu XUI Texture Cache

File: `UT99-Xbox/XboxRender/src/XboxRender.cpp`

Current behavior:

- `GXboxMenuTextures[32]` caches loose `D:\MenuAssets\*.xui` textures.
- `XboxRenderDrawMenuTexture()` loads into the first empty slot.
- There is no obvious release/clear function for this static cache.

Risk:

- The cache is bounded, but it is process-lifetime by accident rather than by
  design.
- A future UI pass can silently fill the 32 slots, after which new assets fail
  to load.
- These D3D textures remain resident even if the frontend is left for a long
  gameplay session.

Target:

- Decide intentionally: either mark these assets process-lifetime and keep the
  set small, or add `XboxRenderFlushMenuTextures()` and call it when leaving the
  frontend.
- Log the menu texture count and total estimated bytes when entering/leaving the
  frontend.
- If the cache stays process-lifetime, convert the current "cache full" log into
  a hard development warning.

### Sound Buffers And Raw Sound Data

File: `UT99-Xbox/XboxAudio/src/XboxAudio.cpp`

Current behavior:

- `RegisterSound()` loads `USound::Data`, creates a DirectSound buffer, copies
  wave bytes, and stores the buffer in `Sound->Handle`.
- `UnregisterSound()` stops/releases the DirectSound buffer.
- `PlaySound()` calls `Sound->Data.Load()` again to read the sample rate for
  pitch calculation.

Risk:

- A registered sound can hold both the DirectSound buffer and the loaded raw
  package data.
- Frequently used weapon/voice/UI sounds may accumulate more resident memory
  than expected during long sessions.
- If `UnregisterSound()` is only reached when the Unreal `USound` object is
  destroyed, sounds from packages that remain loaded can keep buffers for the
  whole process.

Target:

- After successful `RegisterSound()`, unload `Sound->Data` unless the engine
  requires it to remain resident.
- Store the base sample rate beside the DirectSound buffer so `PlaySound()` does
  not need to reload raw data.
- Add audio snapshot fields: registered buffer count, estimated buffer bytes,
  current music name, music buffer state, and failed sound count.

### Music Decode State

File: `UT99-Xbox/XboxAudio/src/XboxAudio.cpp`

Current behavior:

- `StartMusic()` calls `StopMusic()` first.
- Music data is loaded, padded with `Music->Data.Add(1024)`, passed to libxmp,
  then unloaded after playback starts.
- `StopMusic()` releases the DirectSound streaming buffer and releases the xmp
  module if music was playing.

Risk:

- The 1024-byte padding must not grow persistent package data across repeated
  starts of the same song.
- If `xmp_load_module_from_memory()` succeeds but a later step fails, cleanup
  must release the xmp module in every failure branch.
- Pause/resume leaves the xmp state alive by design, so long pause sessions
  should be checked for steady memory.

Target:

- Audit whether `Music->Data.Add(1024)` mutates persistent bulk data or only a
  loaded transient copy. If persistent, replace it with a scratch copy.
- Verify every `StartMusic()` failure branch releases any xmp module already
  loaded.
- Log one transition-only music snapshot when tracks start/stop, not per frame.

### Split-Screen Runtime

File: `UT99-Xbox/XboxDrv/src/XboxViewport.cpp`

Current behavior:

- Split-screen uses four viewports.
- `XboxSplitResetRuntime()` destroys dummy split viewports, destroys dummy
  actors when needed, restores the primary viewport, and clears split globals.
- Non-split travels call the reset path.

Risk:

- Split-screen is deliberately outside the original UT99 single-viewport happy
  path.
- A failed travel, pause-menu return, or frontend transition can leave extra
  viewport state if it misses the reset path.
- Dummy pawn respawn state is static and must be reset when split-screen ends.

Target:

- Keep one public reset path and call it before every non-split travel.
- Add a split snapshot: viewport count, dummy flags, controller ports, active
  flag, pending flag, and dummy death timers.
- Assert in development builds that ordinary frontend/main-menu state has one
  viewport unless split-screen is explicitly active.

### Player Preview Actor

File: `UT99-Xbox/XboxDrv/src/XboxViewport.cpp`

Current behavior:

- The player setup menu creates a preview actor.
- `XboxMenuDestroyPlayerPreview()` destroys it on menu exits and some screen
  changes.

Risk:

- Preview actors can be easy to leak because they are not part of normal
  gameplay ownership.
- Preview mesh/skin/voice class loads can keep packages resident if the actor or
  menu arrays retain references.

Target:

- Call `XboxMenuDestroyPlayerPreview()` on every transition out of player setup,
  on frontend exit, and before map travel.
- Clear any cached preview class/skin/face pointers if they are later added.
- Include preview actor existence in frontend snapshots.

### System Link Probe

File: `UT99-Xbox/XboxDrv/src/XboxViewport.cpp`

Current behavior:

- The System Link screen starts a UDP discovery socket.
- It keeps a peer array, local ID, counters, and last error.

Risk:

- The probe is not final gameplay networking, but stale sockets are still a
  hardware risk.
- Leaving the screen or returning to frontend should close the socket and empty
  peers.

Target:

- Add an explicit `XboxSystemLinkStopProbe()` if not already present.
- Call it when leaving the System Link screen, before map travel, and during
  client destruction.
- Log one stop snapshot: socket valid, peer count, sent packets, last error.

### Registry And Menu Discovery Arrays

Files:

- `Core/Src/UnObj.cpp`
- `UT99-Xbox/XboxDrv/src/XboxViewport.cpp`

Current behavior:

- UT's `UObject::CacheDrivers()` caches `.int` registry objects in
  `UObject::GObjDrivers`.
- Xbox menu code also keeps arrays for game types, maps, mutators, players,
  skins, faces, and voices.
- Some arrays are intentionally cached for menu responsiveness.

Risk:

- User-installed packages and `.int` files can change over time.
- A force-refresh path that is missed can leave stale menu entries.
- Voice/player skin arrays can keep references to classes/textures longer than
  needed.

Target:

- Keep process-lifetime registry caching for normal play, but expose one
  frontend refresh path for development.
- Empty dependent player lists when class changes; this is already partly done
  with `GXboxPlayerVoicesClass`.
- Add counts to the frontend snapshot: game types, maps, mutators, player
  classes, skins, faces, voices.

## Medium Priority Findings

### Config Flushes

File: `UT99-Xbox/XboxDrv/src/XboxViewport.cpp`

Current behavior:

- Settings/player changes call `GConfig->Flush(0, "User.ini")`.

Risk:

- Immediate disk writes from slider changes or repeated option edits can cause
  unnecessary I/O.
- On hardware, this is less about memory and more about stalls and storage
  wear/noise.

Target:

- Dirty settings in memory while the menu is open.
- Flush once when leaving Settings or Player Setup.
- Log only the final flush result.

### Logger

File: `UT99-Xbox/XboxLaunch/inc/FXboxLogger.h`

Current behavior:

- The logger writes to `D:\ut99.log`.
- Explicit `FlushFileBuffers()` was removed from the hot path, but `Flush()`
  still exists.

Risk:

- Verbose transition/hot-path logging can still dominate slow storage on real
  hardware.

Target:

- Keep hot-path logs behind compile-time or config gates.
- Preserve transition snapshots and fatal breadcrumbs.
- Do not log per-frame resource details during normal long-play tests.

### Dynamic / Realtime Textures

Files:

- `Engine/Src/UnTex.cpp`
- `UT99-Xbox/XboxRender/src/XboxRender.cpp`

Current behavior:

- Realtime textures mark `bRealtimeChanged`.
- The render cache updates existing D3D textures when realtime contents change.

Risk:

- Realtime texture uploads split the scene and can retain stale texture variants
  if cache keys miss a state bit.
- Fire/water/scripted textures historically touched several crash paths.

Target:

- Keep cache key fields explicit: source texture, LOD, format, dimensions,
  source UV orientation, and masked-alpha requirement.
- Add a development-only realtime texture counter per frame.
- Ensure realtime textures are unbound before lock/update, which the current
  upload path already tries to do.

## Transition Checklist

Every major transition should leave the process in a known state.

### Enter Frontend

- Release match pause and resume/stop music according to frontend rules.
- Destroy player preview actor.
- Reset split-screen runtime unless the frontend is specifically showing a
  split-screen flow.
- Stop System Link probe.
- Flush render texture cache if coming from gameplay.
- Snapshot: free memory, object count if cheap, render texture bytes, menu
  texture count, audio buffer count, viewport count.

### Leave Frontend For Match

- Stop System Link probe.
- Destroy player preview actor.
- Flush pending config once.
- Reset menu input edge states.
- Decide whether menu XUI textures remain process-lifetime or are flushed.
- Snapshot: selected map/game/mutators/player, menu cache counts.

### Map Travel / Restart

- Flush render device before garbage collection.
- Reset split-screen state unless launching split-screen.
- Stop transient menu sounds and voice sample state.
- Let `LoadMap()` dissociate/destroy old actors and run `CollectGarbage()`.
- Snapshot after load: map name, viewport count, render cache bytes, audio
  buffer count, available memory.

### Leave Match To Main Menu

- Release pause and unpause music before travel.
- Reset split-screen runtime.
- Flush render device.
- Stop System Link probe.
- Browse to `CityIntro.unr` and open the main menu.
- Snapshot after frontend is visible.

### Shutdown

- Stop music.
- Release DirectSound buffers.
- Flush render texture cache.
- Release menu texture cache if it remains static.
- Close System Link socket.
- Destroy/clear texture polyflag side table.

## Recommended Instrumentation

Add one low-noise line at each transition:

```text
XRES phase=<name> map=<map> availKB=<n> texKB=<n> texSlots=<n> menuTex=<n> snd=<n> sndKB=<n> viewports=<n> split=<0/1> xslPeers=<n>
```

Avoid per-frame resource logs for long-play tests. The goal is to compare the
same transition after 5, 30, and 60 minutes and see whether counts ratchet up.

## Priority Fix Plan

1. Remove stale `UTexture*` entries from the polyflag side table in
   `UTexture::Destroy()`.
2. Add explicit menu texture cache ownership: either process-lifetime by design
   with counts, or a real release path.
3. Add transition resource snapshots with texture, menu texture, audio, viewport,
   and System Link counts.
4. Audit audio data duplication: unload raw `USound::Data` after buffer upload
   and avoid reloading it just for pitch.
5. Verify music padding and libxmp cleanup on every failure path.
6. Make settings/player changes flush once per menu exit, not repeatedly while
   editing.
7. Keep split-screen and System Link cleanup on every travel/exit path.

## Definition Of Done

- A 60-minute hardware session with multiple map changes does not show rising
  texture, menu texture, audio, viewport, or System Link counts.
- Returning to the frontend from gameplay leaves one viewport unless
  split-screen is active.
- Entering and leaving Player Setup repeatedly does not increase preview actor
  or audio buffer counts.
- Starting/stopping System Link repeatedly closes sockets and clears peers.
- Repeated music changes do not grow resident memory.
- No hot-path logging is needed to understand a long-play failure; transition
  snapshots tell the story.
