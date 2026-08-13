# UT99 Xbox Open Items

Last updated: 2026-08-13

This is the quick-access tracker for active UT99 Xbox work. It preserves the
older project backlog and adds the current visual-signoff queue so the project
does not depend on scattered chat context.

## 1.2 Release Scope

- Widescreen: decision pending. Steve is not sold on changing the current
  presentation, so do not commit to a widescreen implementation until the
  intended field of view, HUD/menu treatment, and real-hardware output have
  been reviewed together.
- Flicker and UV fixes: complete and signed off by Steve on 2026-08-13.
- Mutators: fix missing entries and broaden the shipped set. Audit discovery,
  packaging, class loading, compatibility, and launch behavior; investigate
  Agent X and Akimbo as named candidates.
- Last Man Standing: expose and qualify the stock
  `Botpack.LastManStanding` game type in the Xbox flows.
- System Link fixes: failed-join/re-entry implementation and two-xemu lifecycle
  qualification are complete. Real-Xbox multi-machine qualification remains.
- Tournament: investigate the v1.1 reports that the first match of every ladder
  crashes, despite the earlier internal ladder progress qualification.
- Tournament co-op: assess local split screen, System Link, and their combined
  flow. Treat UT99 LAN Coop V1 as reference material until its contents and
  integration requirements are understood.

## Recently Completed Items

1. Texture/light flickering in maps
   - Complete and signed off by Steve on 2026-08-13.
   - Added by Steve on 2026-08-03 after reports from several users.
   - Root cause found 2026-08-04: display calibration post-process leaked
     texture address CLAMP into world rendering.
     - `XboxRenderApplyDisplayPostProcess` runs from `Unlock` every frame once
       brightness/contrast/gamma are off neutral, and sets stages 0-2 to
       `D3DTADDRESS_CLAMP` with raw `SetTextureStageState` calls.
     - The only code that programs `D3DTADDRESS_WRAP` for stages 0/1 is the
       one-shot `bStateInit` block in `Lock`, which runs on frame 1 only.
       `RestoreDefaultTextureStages` also restores WRAP but is reachable only
       from menus, loading screens, `Draw2DLine`/`Draw2DPoint`, the perf
       overlay, and `EndFlash` - none of which run during ordinary gameplay
       with the forced Xbox defaults (`ScreenFlashes=0`, no overlay).
     - BSP UVs tile far outside 0..1, so under CLAMP every world surface smears
       its edge texel across the whole polygon. This is the "extremely
       stretched textures" in issue 6, and it persists for the rest of the
       session.
     - Explains why only some users see it (neutral calibration early-outs at
       the `Neutral` check), why it appeared in 1.1 (calibration shipped in
       0dfeb25), and why toggling menus can appear to fix it temporarily.
   - Fix applied: `XboxRenderApplyDisplayPostProcess` now calls
     `RestoreDefaultTextureStages()` after invalidating the state caches.
   - Verified on Xemu 2026-08-04 via `run_xemu_stress_matrix.py --lighting-proof`
     on DM-Deck16][ with `Brightness=0.75 Contrast=1.30 Gamma=1.25`. Two builds
     differing only by that one line, compared at the same deterministic
     viewpoint (`slot1_draw120`):
     - Pre-fix: `ADDRTRACE s0u=3 s0v=3 s1u=3` (CLAMP) sustained, and the frame
       is entirely horizontal smear bands with no recoverable geometry detail.
     - Fixed: `ADDRTRACE s0u=1 s0v=1 s1u=1` (WRAP), and the frame renders
       correctly - ceiling light strips, wall panels, floor tiles, pickups.
     - Detail-free pixel fraction falls 59.1% -> 42.9% and 79.9% -> 53.2% on
       the two matched viewpoints.
   - Test procedure note: `build_xbox_cli.py` re-copies the repo System inis
     into `build/System` on every build, so calibration overrides must be
     applied AFTER building or the run silently reverts to neutral and the
     post-process early-outs without reproducing anything.
   - Still untested on real hardware.
   - Permanent diagnostics added: `ADDRTRACE` (sampler address mode every 60th
     frame), `CLAMPTRACE`, and `clampBad`/`clampOk`/`nobase` counters on `PERF`.
   - Second root cause found and fixed 2026-08-13: the full-cache LRU reuse
     path blanket-unbound all four D3D texture stages before releasing an
     unrelated victim. During multitextured BSP rendering, a stage-1 lightmap
     miss could therefore clear the stage-0 base texture that had just been
     bound. The surface was then submitted with only its lightmap, producing a
     transient bright/white texture or lighting flash.
     - The LRU candidate scan already excludes the resources tracked on stages
       0 and 1, and the existing low-memory eviction path releases such an
       unbound victim directly. Slot reuse now follows the same lifetime rule
       and preserves active stage bindings.
     - The permanent `nobase` invariant remains in place to detect any other
       path that loses a BSP base binding before draw submission.
   - Xemu cache-pressure qualification on 2026-08-13 used `DM-HangEmHigh` with
     eight bots and all eight deterministic lighting viewpoints. The resident
     pool reached its 512-entry limit and completed at least 1,024 LRU reuses
     with `nobase=0` in every PERF sample, zero `RTEX nobase` events, zero draw
     failures, and zero fatal errors. All eight captures retained coherent base
     textures and lighting.
     - Evidence: `UT99-Xbox/build_cli/xemu_item1_texture_flicker_20260813`.
     - Qualified XBE SHA-256:
       `19D8FC9BC404348745B498D59A1A1756265F42A6FFDF8523D618DCBFE6991917`.
     - The harness aggregate is false only because lighting-proof mode does not
       collect the generic character-roster audit that the selected stress case
       normally requires; the lighting captures and renderer-specific checks
       completed successfully.
   - Moving temporal qualification completed on Xemu 2026-08-13 after the
     static-view test was rejected as insufficient:
     - The proof camera spectated a live bot in first person, so navigation,
       corners, stairs/lifts, elevation changes, view turns, and combat came
       from the game's bot AI instead of a fixed camera or scripted circle.
     - Tested the actual first Deathmatch Tournament arena (`DM-Oblivion`) plus
       all other specifically reported maps available in the Xbox build:
       `DM-Deck16][`, `DM-Morpheus`, `DM-HangEmHigh`, and
       `DM-Halo-Derelict`.
     - Each map ran for about 82 seconds with four bots while brightness,
       contrast, and gamma cycled through five neutral/moderate/edge profiles.
       The primary matrix recorded 292 movement samples, 84,455 Unreal units
       of cumulative bot travel, 160 consecutive moving captures, 49 display
       calibration changes, and 119 live sampler reads.
     - Every map passed. All sampler reads remained WRAP; there were zero
       `RTEX nobase` events, display post-process failures, draw failures, or
       fatal errors. The texture cache reached its 512-slot ceiling on every
       map and performed up to 2,304 LRU reuses during a run.
     - Automated adjacent-frame luminance review initially flagged two pairs;
       both landed on intentional six-second calibration transitions. Morpheus
       and Halo Derelict were rerun with 48 captures offset from all transition
       boundaries. Both reruns passed with zero flagged frame pairs, and visual
       contact-sheet review found no local texture/light flicker within any
       held profile.
     - Primary evidence:
       `UT99-Xbox/build_cli/xemu_item1_bot_calibration_4bots_20260813`.
       Stable-window reruns:
       `UT99-Xbox/build_cli/xemu_item1_bot_calibration_stable_capture_20260813`.
     - Qualified XBE SHA-256:
       `7B9F006F1D4307FBC532F4031469D9E5B54F93073DF2EA294E42231D401AA1EC`.
   - Steve accepted the completed fix after the moving bot-spectator and
     calibration qualification. Any later affected-user or real-hardware report
     is a new regression, not a blocker on this item.

## Open Items

### Current Queue

2. System Link real-hardware qualification
   - Source: [UT99-Xbox-Releases issue 7](https://github.com/GTTeancum/UT99-Xbox-Releases/issues/7),
     opened 2026-08-09 with a v1.1 `ut99.log` from a four-machine test.
   - The supplied log does not contain a terminal exception or explicit
     dashboard exit. It does prove a failed client join leaves its pending
     `UTcpipConnection` alive after the frontend is reopened and even after an
     unrelated `DM-Tempest` match starts.
   - The secure association initially connects and sends one packet, then
     becomes `XNET_CONNECT_STATUS_LOST`. The retry path repeatedly translates
     the same host into new virtual addresses (`0.138.206.0` through
     `0.138.210.0`), eventually loops on `XNetConnect` result 10022
     (`WSAEINVAL`), and performs a blocking 0.10-second wait on repeated sends.
   - Returning to the System Link frontend unregisters the session/key and
     clears the global secure travel host while that stale connection still
     exists. The old connection then continues using the invalid association.
   - The UC2 Xbox source under `Z:\Programming\UC2004` confirms the lifecycle
     pattern that is missing here:
     - every connection destroy calls `XNetUnregisterInAddr` for its translated
       address;
     - host and remote keys are driver-owned, reference-counted, and released
       during driver teardown;
     - System Link retries are rate-limited and close the pending connection
       after `InitialConnectTimeout` instead of retrying every send forever.
   - Repair implemented 2026-08-13:
     - the Xbox `CityIntro` rewind shortcut now cancels pending network travel
       before returning, restoring the cleanup skipped by that early return;
     - opening a fresh System Link lobby also cancels any stale pending travel
       before replacing the session/key;
     - each secure connection retains its remote `XNADDR`/`XNKID`, unregisters
       its translated address before retry or destruction, and accepted peers
       now retain the same cleanup data;
     - secure association handling no longer sleeps on the gameplay thread;
       retries are limited to once per second and a pending association closes
       after 15 seconds instead of retrying indefinitely.
   - The final cleanup ordering is based on the actual UC2004 Xbox source, not
     an inferred packet-close theory:
     - `UTcpipConnectionXbox::Destroy` unregisters its translated `IN_ADDR`
       before calling the base connection destructor;
     - `UNetDriver::Destroy` deletes all connections before `LowLevelDestroy`;
     - `UTcpNetDriverXbox::LowLevelDestroy` then closes the socket and releases
       host/remote keys. UT99 now mirrors that address-then-driver/session-key
       ownership split.
   - Genuine same-host System Link was established through xemu's pcap backend
     on the physical Ethernet interface. The automated lifecycle run then
     passed on both instances:
     - each deliberately unreachable join created and cancelled a real pending
       level before lobby entry;
     - the first host/client pair reached `NM_ListenServer` / `NM_Client` live
       gameplay;
     - both instances independently backed out, and each reached the frontend
       with session, launch, acknowledgement, pending-travel, peer, started,
       and socket state fully cleared;
     - the second lobby formed a fresh secure association and both instances
       reached a second live gameplay join (`XSL LIFECYCLE PASS`), with no
       timeout, critical error, retry storm, or gameplay-thread wait loop.
   - The red lower-right connection icon seen intermittently on one xemu is not
     evidence of a dead secure association. Runtime logging caught `alert=1`
     with 0% loss and a packet received 0.017 seconds earlier. UC2004 uses the
     same stock UT expression, including the independent `InPackets < 2`
     one-second-sample trigger, so that behavior is intentionally unchanged.
   - Emulator evidence:
     `UT99-Xbox/build_cli/xemu_syslink_lifecycle_ucaligned_20260813`.
   - Canonical `Release` XBE SHA-256:
     `95BCD2904F53C6DBEA041CD7E3EF2E38A118224514BDEE93737AFBF065C7E79C`.
   - Remaining qualification is real-Xbox-only: repeat discovery, failed join,
     pending back-out, lobby re-entry, second join, client leave, host loss,
     and a subsequent offline match on two or more consoles. Verify no stale
     socket/key/address and no dashboard exit. The emulator implementation and
     lifecycle defect are complete; hardware sign-off remains open.

3. Tournament first-match crash regression
   - Sources: [release issue 5](https://github.com/GTTeancum/UT99-Xbox-Releases/issues/5)
     and [release issue 6](https://github.com/GTTeancum/UT99-Xbox-Releases/issues/6)
     both report that selecting the first match in Tournament crashes v1.1.
   - Reconcile the deployed v1.1 file set and fresh-profile boot path with the
     earlier internal ladder progress proof. Do not assume the save/progress
     qualification proves the public release package can launch rung 1.
   - Reproduce from a clean install and newly created profile for Deathmatch,
     Domination, CTF, Assault, and Final Challenge. Capture the final log and
     verify package/map dependencies, player-ready transition, opponent spawn,
     result handling, save, resume, and frontend return.

4. Mutator repair and expansion
   - Audit the `.int` registry, Xbox fallback list, staged packages, forced
     class links, URL construction, and in-match behavior so every displayed
     mutator is present and functional.
   - The current fixed fallback exposes only Low Gravity, Instagib, No
     Powerups, and optionally OldSkool Weapons. No Agent X or Akimbo assets are
     currently present in the repository; establish the exact packages,
     permissions, dependencies, and Xbox compatibility before adding them.
   - Regression-test combinations as well as individual mutators, especially
     mutually exclusive weapon-replacement mutators.

5. Last Man Standing mode
   - The stock implementation already exists as `Botpack.LastManStanding`; the
     remaining work is Xbox discovery/menu exposure, match-option presentation,
     content staging, and gameplay qualification rather than inventing a new
     game mode.
   - Verify lives, elimination/spectating, bot population, win/end conditions,
     restart/rematch, and frontend return in Instant Action, split screen, and
     System Link where supported.

6. Co-op Tournament in existing multiplayer flows
   - Added by Steve on 2026-07-22 as a 1.2 ask.
   - Assess and prototype Tournament mode as multiplayer without adding a new
     main-menu entry or changing the existing menu flows.
   - Silo the option inside the current Split Screen and System Link paths so
     players still join, select profiles, host, join, ready up, and start from
     the same screens they use today.
   - Save and load Tournament ladder progress from P1's profile on the host
     machine only. Other local or System Link players participate in the host
     run without owning ladder advancement.
   - Treat [UT99 LAN Coop V1](https://www.moddb.com/games/unreal-tournament/addons/ut99-lan-coop-v1)
     as reference material until its package contents are inspected; the
     release-quality path should integrate with the existing Xbox Tournament,
     profile, splitscreen, and System Link systems.
   - Proof requirements: validate local splitscreen, System Link host/client,
     and splitscreen plus System Link. Cover ladder load, match start, bot
     population, end-of-match travel, result screen, save/reload, and back-out
     or disconnect behavior.

7. Widescreen decision and prototype gate
   - Keep this as a 1.2 decision item, not an assumed feature commitment.
   - Before implementation, compare the current 4:3-safe presentation against a
     true widescreen camera/FOV prototype on real Xbox output. Include weapon
     view, projection, scope/overlay effects, HUD safe area, menus, loading
     screens, split screen, and performance.
   - Proceed only after Steve approves the visual direction and compatibility
     tradeoff.

### Active Legacy Backlog

1. Hardware System Link alpha validation
   - Test with two or more real Xboxes.
   - Verify lobby discovery, join, ready-up, map travel, match start, exits,
     reconnects, and leave/host-loss edge cases.

2. Long-play memory/resource audit
   - Prove repeated frontend/gameplay transitions do not grow resident resource
     counts.
   - Track render textures, menu textures, audio buffers, viewport count, and
     System Link sockets/peers at transition boundaries.
   - Use the existing resource audit as the detailed implementation checklist.

3. Tournament hardware regression pass
   - Confirm ladder flow on hardware.
   - Verify enemy spawns, trigger/fire/start behavior, pause, and return to
     main menu without lockups.

4. Splitscreen hardware validation
   - Test one to four real controllers.
   - Verify join, leave, lock, unlock, B/back behavior, and return to one
     frontend viewport outside live splitscreen gameplay.
   - Stress splitscreen plus System Link together.

6. Audio regression pass
   - Verify music and SFX across boot frontend, Instant Action, Tournament,
     Splitscreen, and System Link.
   - Confirm no music changes or level transitions grow resident memory.

7. Larger-map stress testing
   - Run memory/performance checks on larger maps, especially maps that have
     looked tight in prior testing such as `CTF-Darji16`.
   - Include multiple DM, CTF, and AS maps.

## Completed And Signed-Off Items

18. Animated loading wheel
   - Completed and signed off by Steve on 2026-07-26.
   - The loading screen now keeps the normal background while the spinner
     animates independently during synchronous map/package loading.
   - The old static spinner is suppressed during the background prepaint; the
     Xbox renderer captures that clean loading frame, replays it during blocking
     load pulses, and draws exactly one animated spinner over it.
   - Xemu-only frontend travel proof:
     `UT99-Xbox/build_cli/xemu_item18_loading_spinner_clean_20260725`.
     The proof captured four clean loading-frame PNGs and 103 successful
     rendered loading frames during `DM-Pantheon.unr`, with no fatal errors.
   - Multi-frame rotation proof:
     `UT99-Xbox/build_cli/xemu_item18_loading_spinner_multiframe_20260725`.
   - Post-load gameplay handoff smoke passed on `DM-Deck16][` with 1 bot,
     300 ticks, and steady FPS around 58.6.

19. Skeletal-model fidelity and animation
   - Completed and signed off by Steve on 2026-07-23.
   - Restored loaded bone-track evaluation, stable skeletal bounds, package face
     winding, and the stock PC render basis across the custom character roster.
   - Steve approved animation, orientation, face winding, model stability, and
     the corrected Skaarj Hybrid combat visual.
   - The Skaarj defect was traced to red human blood billboards intersecting the
     otherwise-correct model. Skaarj Hybrid actors now enable green blood after
     `PostBeginPlay`.
   - Rendered qualification covered all 17 custom character classes in isolated
     matches with 102 captured frames, complete camera coverage, zero
     skeletal-flicker alerts, and no fatal or capture failures.
   - An additional eight-bot custom-roster soak ran 709.7 seconds with zero
     flicker alerts and 15,384 KB minimum free memory. Final WarMachine proof ran
     at 58.35 steady FPS with zero texture uploads after warmup.
   - Expensive pose, render, material, and weapon diagnostics remain gated by
     explicit qualification markers and do not run during normal gameplay.

20. `DOM-Coagulate` lighting investigation
   - Resolved and signed off by Steve on 2026-07-26.
   - The deployed map still matches the previously signed-off rebuilt SHA-256
     `6537E014FCCF782AFEAD235ACBD59DC0379A1CEE29B10C318E8071FB7B7D00B6`.
   - Xemu lighting proof used first-person map-lighting anchors, skipped
     pre-game/logo frames, and physically moved the local pawn through
     `FarMoveActor`/`MoveActor` so the actor hash and zone were updated before
     rendering. All eight viewpoints were inside BSP (`wouldClear=0`), with no
     fatal errors or capture failures.
   - Evidence:
     `C:\Programming\GitHub\UnrealTournament_1.40\UT99-Xbox\build_cli\xemu_items20_21_lighting_proof_20260726_0003_pawnmove\02_DOM-Coagulate\lighting_contact_sheet.png`.

21. `DM-Morpheus` lighting investigation
   - Resolved and signed off by Steve on 2026-07-26.
   - Xemu lighting proof rendered eight pawn-moved first-person viewpoints with
     recognizable stock-map lighting, signs, sky, and weapon/world contrast.
   - The run had no fatal errors, no lighting capture failures, no texture
     upload churn during steady state, and all viewpoints were inside BSP
     (`wouldClear=0`).
   - Evidence:
     `C:\Programming\GitHub\UnrealTournament_1.40\UT99-Xbox\build_cli\xemu_items20_21_lighting_proof_20260726_0003_pawnmove\01_DM-Morpheus\lighting_contact_sheet.png`.

1. Controls menu
   - Signed off by Steve.
   - Keep the release-quality footer/button-glyph/controller-image standard.

2. Tournament post-match screen
   - Signed off by Steve.

3. Weapon audio playback
   - Complete and signed off by Steve on 2026-07-19.
   - Restored Galaxy-style ambient actor servicing for sustained Pulse Gun and
     Minigun fire sounds.
   - Corrected linear-amplitude to DirectSound-decibel conversion, honored
     `LowSoundQuality=False`, and restored ambient brightness/distance updates.
   - Sustained sounds now use dedicated DirectSound voices, preventing Pulse
     projectiles and other users of the same sound asset from interrupting the
     held weapon loop.
   - One-shots restart atomically with `DSBPLAY_FROMSTART`.
   - Final XBE SHA-256:
     `FA3B8882D80D99DC5345239A2C24F4E4A9B1B6133BA0029765B49B6D912019D7`
   - Passing runtime log:
     `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item3-audio-pulse-voice-retest-20260719\runtime-ut99-item3-audio-pass.log`
   - In-game proof:
     `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item3-audio-pulse-voice-retest-20260719\proofs\20260719-210222-deck16-pulse-secondary-dedicated-voice-retest.png`

4. Completed current item
   - Marked complete by Steve before moving to item 5.

5. Instant Action match options and rules
   - Complete.
   - Signed off by Steve.
   - Crash found during validation was resolved by restoring the Xbox object
     cache to the existing 1 MB cap instead of the temporary 256 KB override.

6. Instant Action crash-map proof batch
   - Complete.
   - Signed off by Steve.
   - Source: GitHub issue #2, 2026-07-13 update.
   - Reported maps that crash in Instant Action:
     - `DM-Pantheon`
     - `DM-Halberd`
     - `DM-Hood`
     - `DM-CanyonFear`
   - Proofs produced and accepted by Steve.
   - Cache note: the 1 MB object-cache fix is active in all proof logs. The
     `DM-Hood` crash also required replacing a stale staged map; the old staged
     copy failed with `Bad import index`.
   - Visual proofs:
     - `DM-Pantheon`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item6-fixed-dm-pantheon-mapview-20260717-2153\proofs\20260717-214923-dm-pantheon-mapview-autofire.png`
     - `DM-Halberd`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item6-fixed-dm-halberd\proofs\20260717-214406-dm-halberd-loaded.png`
     - `DM-Hood`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item6-fixed-dm-hood\proofs\20260717-214424-dm-hood-loaded.png`
     - `DM-CanyonFear`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item6-fixed-dm-canyonfear\proofs\20260717-214443-dm-canyonfear-loaded.png`

7. Main menu UI regression
   - Complete.
   - Signed off by Steve.
   - `CONTROLS` now lives under `SETTINGS` beside `AUDIO` and `VIDEO`.
   - Menu rectangles, images, text, highlights, and footer prompts now share
     frame-relative canvas geometry. The light-blue footer stripe is excluded
     from the prompt-centering area.
   - Footer command prompts are compacted from the left using measured label
     widths; contextual scroll controls remain independently right-aligned.
   - Main: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item7-footer-main-20260717-230122\proofs\20260717-230135-main-menu.png`
   - Settings: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item7-footer-settings-20260717-230122\proofs\20260717-230147-settings-hub.png`
   - Audio: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item7-footer-audio-20260717-230122\proofs\20260717-230158-settings-audio.png`
   - Video: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item7-footer-video-20260717-230122\proofs\20260717-230207-settings-video.png`
   - Controls: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item7-footer-controls-20260717\proofs\20260717-230043-settings-controls.png`
   - Mutators: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item7-mutators-20260718-80pct-retry\proofs\20260718-004110-mutators-candidate-5.png`

8. Tournament progress saving
   - Complete and signed off by Steve on 2026-07-21.
   - Implemented profile-owned progress for Deathmatch, Domination, Capture the
     Flag, Assault, and Final Challenge. Each ladder writes its own position to
     the active profile and reads it back on the next launch; legacy global
     progress migrates once to a single owning profile.
   - The normal UnrealScript tournament result path now feeds the native result
     screen and persists its real pending ladder position after the match ends.
   - Qualified on final XBE SHA-256
     `2428FCA9869783B3FF57B516EFAF5272EFD70CB8FE89D19783BA15E4A082855F`.
     Deathmatch, Domination, CTF, Assault, and Final Challenge each completed a
     forced first-rung victory, saved position 2, then restored rung 2 from a
     separate clean emulator launch. All ten mode logs were clear of fatal
     signatures.
   - Profile isolation also passed: profile 1 retained position 2 in all five
     ladders, while profile 2 opened Deathmatch at position 1. Switching back
     restored profile 1 at position 2.
   - CTF qualification exposed a staged-content mismatch: tournament rung 2
     needs stock `CTF-Face.unr`, while the stage only contained the unrelated
     `CTF-Face][.unr`. The canonical stock map is now in the CXBX stage, and map
     previews now verify the package exists before loading so missing content
     produces `NO PREVIEW` rather than taking down the frontend.
   - Win/result proofs:
     - Deathmatch: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item8-qualified-dm-win\proofs\20260721-220551-deathmatch-win-rung02-saved.png`
     - Domination: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item8-qualified-dom-win\proofs\20260721-220704-domination-win-rung02-saved.png`
     - Capture the Flag: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item8-qualified-ctf-win\proofs\20260721-220819-capture-the-flag-win-rung02-saved.png`
     - Assault: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item8-qualified-assault-win\proofs\20260721-220936-assault-win-rung02-saved.png`
     - Final Challenge: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item8-qualified-challenge-win\proofs\20260721-221053-final-challenge-win-rung02-saved.png`
   - Fresh-launch restore proofs:
     - Deathmatch: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item8-qualified-dm-resume\proofs\20260721-220614-deathmatch-fresh-launch-rung02.png`
     - Domination: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item8-qualified-dom-resume\proofs\20260721-220726-domination-fresh-launch-rung02.png`
     - Capture the Flag: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item8-qualified-ctf-resume\proofs\20260721-220843-capture-the-flag-fresh-launch-rung02.png`
     - Assault: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item8-qualified-assault-resume\proofs\20260721-220959-assault-fresh-launch-rung02.png`
     - Final Challenge: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item8-qualified-challenge-resume\proofs\20260721-221117-final-challenge-fresh-launch-rung02.png`
   - Profile-isolation proofs:
     - Profile 2 remains at rung 1: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item8-qualified-profile2-isolation\proofs\20260721-221213-profile2-deathmatch-remains-rung01.png`
     - Profile 1 remains at rung 2: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item8-qualified-profile1-isolation\proofs\20260721-221237-profile1-deathmatch-still-rung02.png`

9. Updated PlayStation 2 character pack
   - Complete and signed off by Steve on 2026-07-18.
   - The staged 3.0 pack exposes 16 player classes and 16 matching bot classes.
   - Four representative PS2 bots were exercised with four concurrent Master
     Chief bots and one local player at difficulty 3:
     - `PS2 Damien` on `CTF-Darji16` for 240 seconds.
     - `PS2 Rampage` on `AS-HiSpeed` for 204 seconds of round time.
     - `PS2 Dominator` on `DM-KGalleon` for 203 seconds.
     - `PS2 Xan` on `DM-HangEmHigh` for 309 seconds.
   - Exact bot classes, meshes, active skins, scores, deaths, and roster counts
     are recorded in the logs below. All four runs retained eight bots and
     showed active score/death progression with no critical errors.
   - The broader five-PS2-class plus three-Master-Chief crash is resolved.
     Generic PS2 skin helpers probe optional textures that are absent from the
     consolidated skin package; Xbox treated those expected misses as fatal
     dynamic loads. Texture probes from the Pawn and generic skin-helper paths
     now return `None` only when the requested export is absent, preserving the
     scripts' normal fallback behavior without weakening other dynamic loads.
   - Xbox also releases skeletal reference-bone and move arrays after load;
     the current renderer uses bind poses but still needs animation sequence
     metadata. This recovered roughly 6 MB during the mixed-roster load.
   - The exact formerly crashing roster completed rendered regression runs on
     `CTF-Darji16` and `DM-HangEmHigh`. Both retained all eight bots, produced
     live score/death changes, and recorded no critical error. The HangEmHigh
     run reached 115 seconds of game time with 9,756 KB free at its last audit.
   - Logs:
     - `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-ctf-darji16-eightbot-soak\ut99.log`
     - `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-as-hispeed-eightbot-soak\ut99.log`
     - `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-dm-kgalleon-eightbot-soak\ut99.log`
     - `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-dm-hangemhigh-five-minute-soak\ut99.log`
     - `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-ctf-darji16-eight-roster-resolved-20260718\logs\ut99-ctf-darji16-eight-roster-resolved.log`
     - `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-dm-hangemhigh-eight-roster-resolved-20260718\logs\ut99-dm-hangemhigh-eight-roster-resolved.log`
     - `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-final-group-aware-ctf-20260718\logs\ut99-final-group-aware-ctf.log`
     - `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-final-group-aware-hangemhigh-20260718\logs\ut99-final-group-aware-hangemhigh.log`
   - Visual proofs:
     - `CTF-Darji16`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-ctf-darji16-eightbot-soak\proofs\20260718-012118-ctf-darji16-final-candidate-2.png`
     - `AS-HiSpeed`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-as-hispeed-eightbot-soak\proofs\20260718-012740-as-hispeed-final-over-180s.png`
     - `DM-KGalleon`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-dm-kgalleon-eightbot-soak\proofs\20260718-013022-dm-kgalleon-mid-soak-1.png`
     - `DM-HangEmHigh`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-dm-hangemhigh-five-minute-soak\proofs\20260718-014356-dm-hangemhigh-five-minute-complete.png`
     - Exact mixed roster on `CTF-Darji16`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-ctf-darji16-eight-roster-resolved-20260718\proofs\20260718-124437-ctf-darji16-eight-bot-live-combat.png`
     - Exact mixed roster scoreboard on `DM-HangEmHigh`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-dm-hangemhigh-eight-roster-resolved-20260718\proofs\20260718-124843-dm-hangemhigh-eight-bot-soak-b.png`
     - Final group-aware XBE on `CTF-Darji16`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-final-group-aware-ctf-20260718\proofs\20260718-125512-final-group-aware-ctf-eight-roster.png`
     - Final group-aware XBE roster board on `DM-HangEmHigh`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-items9-10-final-group-aware-hangemhigh-20260718\proofs\20260718-125654-final-group-aware-hangemhigh-post-death.png`

10. Master Chief character and bot
    - Complete and signed off by Steve on 2026-07-18.
    - Four `HaloMasterChief.HaloMasterChiefBot` instances were run in every
      item 9 stress test, for 16 concurrent-bot placements across four maps.
    - Every audit sample retained the expected
      `HaloMasterChief.HaloMasterChief` mesh and active
      `HaloMasterChiefSkins.chef1T_*` team skin.
    - The 309-second `DM-HangEmHigh` run was the heaviest test: it reached a
      3,064 KB free-memory floor and a brief 36.8 FPS low while keeping all
      eight bots active and completing without a critical error.
    - The final exact mixed-roster regressions added three simultaneous Master
      Chiefs alongside five distinct PS2 bot classes and passed both maps.
    - Uses the logs and visual proofs recorded under item 9.

11. Main-menu return confirmation
    - Complete and signed off by Steve on 2026-07-18 after live emulator
      inspection.
    - `MAIN MENU` from the pause screen now opens a compact centered
      confirmation dialog instead of traveling immediately.
    - The safe default is `NO`. `A` selects, directional input changes the
      choice, and `B`, Back, or Start cancels the dialog.
    - Only an explicit `YES` selection returns to the frontend.
    - Final XBE SHA-256:
      `2CF1A3AAF2139DC75999D4C0CBB5C6D8C28D1C31E396DBD99FA73EED41AEA37C`
    - Runtime log:
      `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item11-pause-confirmation-20260718\logs\ut99-item11-pause-confirmation-pass.log`

12. Splitscreen menu fidelity pass
    - Complete and signed off by Steve on 2026-07-21 after inspecting the full
      rendered CXBX-R menu/gameplay proof matrix.
    - The ready screen keeps four fixed physical-controller slots labeled `P1`
      through `P4`, left to right. Players choose a unique profile inside their
      own slot and cannot choose or renumber the player slot itself.
    - `P1` is active automatically and cannot vacate the primary slot. Sparse
      joins remain sparse in the menu, so a controller in port 3 activates the
      `P3` card while `P2` remains visibly inactive.
    - Gameplay preserves each active physical port for input/profile ownership,
      then assigns a separate compact render order. Two active ports draw two
      full-width horizontal views; three active ports draw three quadrants with
      the lower-right quadrant inactive.
    - All six requested slot combinations passed clean independent launches:
      `P1+P2` (`0x3`), `P1+P3` (`0x5`), `P1+P4` (`0x9`), `P1+P2+P3`
      (`0x7`), `P1+P2+P4` (`0xB`), and `P1+P3+P4` (`0xD`). Logs report the
      physical port, compact render order, profile, joined/dummy state, region,
      active mask, and drawn/skipped viewport count. No accepted proof log has
      a fatal error, assertion failure, or general protection fault.
    - Menu and gameplay proofs:
      - `P1+P2`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item12-p1p2-menu-proof-a1\proofs\20260721-143730-p1p2-menu.png`; `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item12-p1p2-game-proof-a1\proofs\20260721-143743-p1p2-game.png`
      - `P1+P3`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item12-p1p3-menu-proof-a1\proofs\20260721-143757-p1p3-menu.png`; `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item12-p1p3-game-final\proofs\20260721-141448-p1-p3-two-viewports-final.png`
      - `P1+P4`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item12-p1p4-menu-proof-a1\proofs\20260721-143812-p1p4-menu.png`; `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item12-p1p4-game-proof-a1\proofs\20260721-143824-p1p4-game.png`
      - `P1+P2+P3`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item12-p1p2p3-menu-proof-a1\proofs\20260721-143839-p1p2p3-menu.png`; `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item12-p1p2p3-game-proof-a1\proofs\20260721-143851-p1p2p3-game.png`
      - `P1+P2+P4`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item12-p1p2p4-menu-proof-a1\proofs\20260721-143905-p1p2p4-menu.png`; `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item12-p1p2p4-game-proof-a1\proofs\20260721-143917-p1p2p4-game.png`
      - `P1+P3+P4`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item12-p1p3p4-menu-proof-a1\proofs\20260721-143933-p1p3p4-menu.png`; `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item12-p1p3p4-game-proof-a1\proofs\20260721-143946-p1p3p4-game.png`
    - Final XBE SHA-256:
      `71E1AA5B752D7799DD0C7A3033181F3BA8E84E6886B9091D77C7FD96E66ACB6D`

13. Distinct Tournament mode UI
    - Complete and signed off by Steve on 2026-07-18 after final CXBX-R
      framebuffer inspection.
    - Tournament now presents a ladder circuit, visible run progression, locked
      opponents, and a next-match dossier with arena art, rules, and briefing.
    - Its dark panels, blue status rails, and progression language carry the
      approved Tournament post-match flavor without copying Instant Action.
    - Navigation now has three visible states: ladder, next match, and
      difficulty. `A` begins the match from difficulty, while left/right still
      changes the selected difficulty.
    - The duplicate bottom `ENTER ARENA` and rung labels were removed, and the
      ladder-selector arrows are positioned from the measured ladder-name width.
    - Final XBE SHA-256:
      `C5F2C1FDCCE2EF75CED767FADE1EF569547FE07C957390C77634C1847B2D1DB7`
    - Final visual proof:
      `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item13-tournament-clean-footer-final-20260718\proofs\20260718-211506-tournament-clean-footer.png`
    - Post-match continuity proof:
      `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item13-tournament-result-final-20260718\proofs\20260718-210531-tournament-result.png`

14. Video image-adjustment controls
    - Completed and signed off by Steve on 2026-07-19.
    - `VIDEO` now has aligned `BRIGHTNESS`, `CONTRAST`, and `GAMMA` sliders.
      Each adjustment saves `XboxDrv.XboxClient` immediately and applies to the
      composed frame without requiring a restart.
    - Brightness and contrast use exact affine adjustment. Gamma uses a smooth
      NV2A tone curve generated by the XDK 5558 pixel-shader assembler. The
      correction runs as one 640x480 GPU copy/pass only when values are not
      neutral; neutral defaults allocate no post-process resources.
    - Independent clean launches loaded and rendered brightness `75%`, contrast
      `140%`, gamma `175%`, and gamma `50%`. Runtime logs confirm the exact
      persisted values loaded by `UXboxClient` and the active render pass.
    - Final XBE SHA-256:
      `F5558EA8F981E714BF5090AB9F09BB595FEBD669ABCECD1A665A7C2C75543FFD`
    - Visual proofs:
      - Final menu: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item14-video-menu-release-20260719\proofs\20260719-014227-video-menu-release.png`
      - Neutral `50/100/100`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item14-video-neutral-20260719\proofs\20260719-010000-neutral-050-100-100.png`
      - Brightness `75/100/100`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item14-video-brightness-tone-clean-20260719\proofs\20260719-012448-brightness-075-tone-clean.png`
      - Contrast `50/140/100`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item14-video-contrast-tone-clean-20260719\proofs\20260719-012549-contrast-050-140-100.png`
      - Gamma `50/100/175`: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item14-video-gamma-tone-clean-20260719\proofs\20260719-012634-gamma-050-100-175.png`
      - Gamma `50/100/50`, single-player: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item14-video-gamma-final-linear-20260719\proofs\20260719-013430-gamma-050-final-linear.png`
      - Gamma `50/100/50`, final XBE across four split-screen viewports: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item14-video-split-gamma-final-direct-20260719\proofs\20260719-014130-split-four-viewports-gamma-050-direct.png`
    - Final release logs:
      - Menu: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item14-video-menu-release-20260719\runtime-ut99.log`
      - Four-player split-screen: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item14-video-split-gamma-final-direct-20260719\runtime-ut99.log`

15. Create/load player profiles
    - Complete and signed off by Steve on 2026-07-19 after final CXBX-R
      framebuffer inspection.
    - The normal boot flyby remains intact. First Start requires loading or
      creating a profile before entering the main menu, and `X LOAD PROFILE`
      allows switching later.
    - Profiles persist player identity, model, team, controller bindings, stick
      layout, sensitivities, deadzone, invert Y, weapon hand, and auto-switch.
      Player Setup and in-match Controls save immediately to the owning profile.
    - Splitscreen and System Link require unique profiles for every joined local
      player; duplicate assignment is blocked.
    - The final wiring audit passed every profile, menu-command, scrolling,
      persistence, and multiplayer ownership check. The rendered local split
      qualification passed 80/80 controls with zero failures, and the ownership
      save/reload proof confirmed P2 did not modify P1.
    - Final XBE SHA-256:
      `13354B511717571947CFDA898EBC374D8A7F8149D1EB1D951FED8F1451DE58E0`
    - Final Player Setup alignment proof:
      `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item15-player-setup-centered-final-20260719\proofs\20260719-000632-player-setup-values-centered.png`
    - Scrollable selector proof:
      `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item15-profile-scroll-twopane-20260718\proofs\20260718-232655-profile-overflow-two-pane.png`
    - Multiplayer profile proofs:
      - Splitscreen: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item15-split-ready-exact-final-20260718\proofs\20260718-233931-splitscreen-unique-profiles.png`
      - System Link: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item15-system-ready-exact-final-20260718\proofs\20260718-234013-system-link-unique-profiles.png`
      - Runtime log: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item15-four-player-profile-controls-20260718\ut99.log`

16. Alphabetical player-character selection
    - Complete and signed off by Steve on 2026-07-18.
    - The 56-character roster is alphabetized and uses unique explicit
      class, skin, face, voice, team, and portrait mappings.
    - All 57 portrait assets, including the fallback, are 256x512 with a
      normalized 372-pixel depicted-character height and transparent edges.
      There are no exact visual duplicates.
    - Portrait provenance covers 46 fresh Unreal Wiki character cards and 10
      exact local renders for the classic Skaarj, Skaarj Boss, and Master
      Chief assets that required package-accurate replacements.
    - Normal reboot persistence now accepts all playable PS2 classes and the
      intentionally blank face fields used by Damien, Rampage, Xan, and Xan
      PS2 instead of resetting those selections to Othello.
    - Damien passed the final saved-selection qualification and then spawned
      in live `DM-HangEmHigh` gameplay from inherited `User.ini` options with
      the exact `DamienPS2` class and no critical errors.
    - Final XBE SHA-256:
      `518A85CDFD9432A5DCEF354C5DF369FA02FCF5CD5CC8F59B7974494ADE6B8BF3`
    - Runtime logs:
      - Saved selection: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item16-damien-persistence-final-20260718\ut99.log`
      - Spawn qualification: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item16-damien-spawn-final-20260718\ut99.log`
    - Final visual proofs:
      - Damien selection: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item16-damien-persistence-final-20260718\proofs\20260718-204841-damien-saved-selection.png`
      - Damien gameplay: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item16-damien-spawn-final-20260718\proofs\20260718-205040-damien-live-gameplay.png`
      - Classic Skaarj set: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-item16-hires-final-baetal-20260718`, `ut99-item16-hires-final-berserker2-20260718`, `ut99-item16-hires-final-disconnect-20260718`, `ut99-item16-hires-final-dominator-20260718`, `ut99-item16-hires-final-firewall-20260718`, `ut99-item16-hires-final-guardian-20260718`, `ut99-item16-hires-final-pharoh-20260718`, and `ut99-item16-hires-final-skrilax-20260718`.

17. Working XBE game icon
    - Complete and signed off by Steve on original Xbox hardware on 2026-07-21.
    - The Unreal Tournament icon is embedded in the XBE as a retail-format
      128x128 `XPR0` `$$XTIMAGE` section, with matching title/save metadata.
    - The build uses retail Title ID `0x4D4A0008` (`MJ-008`, BlowOut), an
      obscure Xbox FPS assigned to the Xbox 360 `xefu7` profile.
    - The XPR container and Xbox image-project wiring were cross-checked against
      the local Unreal Championship 2 Xbox source before hardware sign-off.

## Completed Runtime Fixes

- `DOM-Coagulate` lighting and Xbox render crash
  - Completed and signed off by Steve on 2026-07-19 after several in-emulator
    screenshots visibly verified working lighting.
  - Replaced the stale staged map with the PC-qualified rebuilt map
    (`6537E014FCCF782AFEAD235ACBD59DC0379A1CEE29B10C318E8071FB7B7D00B6`).
  - The rebuilt map exposed an Xbox-only render assertion from an empty child
    scene model. Empty recursive child frames are now skipped while the master
    world still retains the strict non-empty model assertion.
  - Multiple live combat/respawn captures show the purple source fixtures,
    floor and wall lightmaps, shadowed geometry, and colored weapon/model
    highlights. Black clipped-wall frames were discarded and are not counted
    as proof.
  - Runtime log:
    `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-dom-coagulate-lighting-20260719\runtime-ut99-dom-coagulate.log`
  - Visual proofs:
    - `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-dom-coagulate-lighting-20260719-pass2\proofs\20260719-004631-dom-coagulate-view-01.png`
    - `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-dom-coagulate-lighting-20260719-pass2\proofs\20260719-004850-dom-coagulate-view-04.png`
    - `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-dom-coagulate-lighting-20260719-moving\proofs\20260719-005002-dom-coagulate-moving-view-01.png`

- Weapon cycling and wheel command dispatch
  - Marked complete by Steve on 2026-07-18.
  - Source: GitHub issues #2 and #3. The old Xbox helper called
    `UInput::Exec` directly, which swallowed the self-named
    `NextWeapon`/`PrevWeapon` aliases and never forwarded `GetWeapon` to the
    player.
  - Tap and wheel commands now dispatch through the active player. Runtime
    logs record command handling plus current/pending weapon state before and
    after dispatch.
  - The real White/Black tap-and-hold state machine passed 11/11 checks:
    initial next, initial previous, four repeated next taps, four repeated
    previous taps, and held-wheel selection of the Shock Rifle.
  - Proof log:
    `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-weapon-cycle-proof-20260718-095241\ut99.log`
  - Wheel replay log:
    `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-weapon-cycle-wheel-open-20260718-095522\ut99.log`
  - Visual proofs:
    - Next to Bio Rifle: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-weapon-cycle-proof-20260718-095241\proofs\20260718-095311-tap-next-bio-rifle.png`
    - Previous to Enforcer: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-weapon-cycle-proof-20260718-095241\proofs\20260718-095334-tap-previous-enforcer.png`
    - Wheel open with Shock Rifle focused: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-weapon-cycle-wheel-open-20260718-095522\proofs\20260718-095613-weapon-wheel-open-shock-focused.png`
    - Shock Rifle equipped after release: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-weapon-cycle-proof-20260718-095241\proofs\20260718-095419-held-wheel-shock-rifle-focus.png`

- Four-player split controls and pause ownership
  - Fixed and signed off by Steve on 2026-07-18.
  - All four controller ports resolve to distinct viewports, player actors,
    input objects, controller state, and weapon-wheel state.
  - Local split passed 80/80 checks: four ownership checks plus 19 mapped
    controls per player. The matrix covers both sticks, A/B/X/Y, both
    triggers, White/Black tap cycling and held weapon wheels, Back, right
    thumb, all four D-pad weapon shortcuts, and Start.
  - Every local Start test paused all four viewports, left the invoking player
    as menu owner, and blocked a second player's movement with zero measured
    displacement.
  - Split-over-System-Link passed 8/8 checks: four ownership checks plus Start
    from every player. Start never set the world pauser; match time advanced
    and another local player moved during every menu hold.
  - Final XBE SHA-256:
    `45F1B86AE83F573014FCB74D23208F84C89FF518043D0523E08D17857507652E`
  - Logs:
    - Local: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-split-controls-local-exact-final-20260718-1110\ut99-local-pass.log`
    - Online: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-split-controls-online-pass-20260718-1106\ut99-online-pass.log`
  - Visual proofs:
    - Slot 1 weapon wheel: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-split-controls-local-exact-final-20260718-1110\proofs\20260718-110804-controller-slot-1-weapon-wheel-independent.png`
    - Slot 2 weapon wheel: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-split-controls-local-final-20260718-1110\proofs\20260718-105850-player-2-weapon-wheel-independent.png`
    - Slot 3 weapon wheel: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-split-controls-local-final-20260718-1110\proofs\20260718-105939-player-3-weapon-wheel-independent.png`
    - Slot 4 weapon wheel: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-split-controls-local-final-20260718-1110\proofs\20260718-110025-player-4-weapon-wheel-independent.png`
    - Shared local pause: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-split-controls-local-exact-final-20260718-1110\proofs\20260718-110823-controller-slot-1-shared-local-pause.png`
    - Online owner-only pause: `C:\Games\Emulators\CXBX-CodexCapture\captures\ut99-split-controls-online-pass-20260718-1106\proofs\20260718-110606-player-1-online-pause-gameplay-continues.png`

## Standing Proof Rules

- In-game proofs only. No mockups.
- Use visual proof when applicable.
- Stop for Steve's signoff on each individual item.
- For future screenshot automation, use `C:\Games\Emulators\CXBX-CodexCapture`
  and follow that folder's `AGENTS.md`.
- Do not leave the emulator running after automated proof work.

## Closed Or Deferred Legacy Items

5. Frontend cleanup/resource lifetime
   - Closed per Steve on 2026-06-23.
   - Any future frontend polish should be tracked as a concrete bug or feature,
     not as a broad cleanup bucket.

8. Console map conversion
   - Closed out of the active engine/runtime queue per Steve on 2026-06-23.
   - PS2 and Dreamcast map work remains documented separately in
     `UT99-Xbox/Docs/ConsoleMapConversionNotes.md`.

9. Boot-time/performance polish
   - Closed per Steve on 2026-06-23.
   - Future boot or performance issues should be filed as specific repro cases.

## Reference Docs

- `xbox_hardware_resource_audit.md`
- `HANDOFF_UT99_XBOX_2026-06-09.md`
- `UT99-Xbox/Docs/ConsoleMapConversionNotes.md`
- `UT99-Xbox/Docs/SystemLinkProbe_CXBX.md`
