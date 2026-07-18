# UT99 Xbox Open Items

Last updated: 2026-07-18

This is the quick-access tracker for active UT99 Xbox work. It preserves the
older project backlog and adds the current visual-signoff queue so the project
does not depend on scattered chat context.

## Current Visual-Signoff Queue

1. Controls menu
   - Signed off by Steve.
   - Keep the release-quality footer/button-glyph/controller-image standard.

2. Tournament post-match screen
   - Signed off by Steve.

3. Audio item
   - Paused by Steve.
   - Needs Steve's ears before proceeding.

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
   - Added by Steve after post-match work.
   - Verify tournament progress is saved/restored correctly.

## Newly Reported Issues

- `DOM-Coagulate` lighting is broken.
  - Needs investigation and visual proof when addressed.

## Standing Proof Rules

- In-game proofs only. No mockups.
- Use visual proof when applicable.
- Stop for Steve's signoff on each individual item.
- For future screenshot automation, use `C:\Games\Emulators\CXBX-CodexCapture`
  and follow that folder's `AGENTS.md`.
- Do not leave the emulator running after automated proof work.

## Active Legacy Backlog

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
