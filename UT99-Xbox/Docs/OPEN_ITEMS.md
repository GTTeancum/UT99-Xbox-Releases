# UT99 Xbox Open Items

Last updated: 2026-07-19

This is the quick-access tracker for active UT99 Xbox work. It preserves the
older project backlog and adds the current visual-signoff queue so the project
does not depend on scattered chat context.

## Open Items

### Current Queue

3. Audio item
   - Paused by Steve.
   - Needs Steve's ears before proceeding.

8. Tournament progress saving
   - Added by Steve after post-match work.
   - Verify tournament progress is saved/restored correctly.

12. Splitscreen menu fidelity pass
    - Perform another visual and interaction fidelity pass across all
      splitscreen menus.
    - Verify alignment, navigation, prompts, player-slot states, and consistency
      with the approved single-player menu theme.

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

1. Controls menu
   - Signed off by Steve.
   - Keep the release-quality footer/button-glyph/controller-image standard.

2. Tournament post-match screen
   - Signed off by Steve.

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
