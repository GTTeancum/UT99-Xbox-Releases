# UT99 Xbox - Open Work Only

Last updated: 2026-09-09 EDT
Status: ACTIVE

This tracker contains only open or explicitly deferred work. Remove an entry
when it is accepted or closed; completed work, evidence history, and superseded
attempts do not belong here.

## Open items at a glance

This is one line per major open deliverable or defect. Detailed implementation
and acceptance work remains below. `User` marks user-requested or user-reported
work; `Codex` marks verification follow-ups.

1. **User — IN PROGRESS** - Fix split-screen frame-rate degradation (GitHub #9), alongside the efficiency review.
2. **User — DEFERRED** - Fix inconsistent controller sensitivity between single-player and split-screen (#9).
3. **User — DEFERRED** - Keep split-screen player settings independent (#9).
4. **User — DEFERRED** - Fix interface clipping at the top and bottom of CRT displays (#9).
5. **User — DEFERRED** - Fix the broken frontend after exiting split-screen (#9).
6. **User — DEFERRED** - Persist match settings across game restarts (#9).
8. **User — IN PROGRESS** - Review all code with a fine-tooth comb for efficiency, performance, and speed.
9. **User** - Implement Co-op Tournament, using LAN co-op on ModDB as a possible template.
10. **User — PARTIAL / PENDING REVIEW** - Finish widescreen HUD and safe-area presentation, then obtain real-Xbox/TV sign-off.
11. **Codex — PENDING HARDWARE REVIEW** - Qualify System Link on two or more real Xboxes.
12. **Codex** - Audit long-play memory and resource lifetime across repeated transitions.
13. **Codex — PENDING HARDWARE REVIEW** - Run the Tournament ladder regression pass on hardware.
14. **Codex — PENDING HARDWARE REVIEW** - Validate split-screen with one to four controllers and combined System Link play.
15. **Codex — PENDING AUDIBLE REVIEW** - Run the music and SFX regression pass across all play modes.
16. **Codex** - Stress larger DM, CTF, and Assault maps for memory use and performance.
17. **Codex — PENDING HARDWARE REVIEW / RELEASE** - Verify and release the local repairs for GitHub #8, #10, #11, and #12.

## GitHub issue register

The six defects below come from the [bug report in issue #9](https://github.com/GTTeancum/UT99-Xbox-Releases/issues/9#issuecomment-5577635096).
The performance defect is being addressed alongside the efficiency review;
the other five defects remain deferred.

### UT99-OPEN-001 - Split-screen performance (GitHub #9, IN PROGRESS)

- [ ] Reproduce the reported loss of smoothness compared with single-player.
- [ ] Identify and repair frame-time bottlenecks, then verify representative
      maps with two, three, and four local players on Xbox hardware.

### UT99-OPEN-002 - Controller sensitivity consistency (GitHub #9, DEFERRED)

- [ ] Reproduce sensitivity 100 in split-screen feeling like sensitivity 40
      in single-player.
- [ ] Make a player's sensitivity setting behave consistently across player
      counts and frame rates.

### UT99-OPEN-003 - Independent player settings (GitHub #9, DEFERRED)

- [ ] Reproduce one split-screen player's settings changes applying to everyone.
- [ ] Scope player-specific settings to the owning player/profile and verify
      changing one player's settings does not alter another's.

### UT99-OPEN-004 - CRT interface clipping (GitHub #9, DEFERRED)

- [ ] Reproduce top and bottom interface clipping on a CRT.
- [ ] Keep required HUD and menu content inside the visible safe area.
- [ ] Verify single-player and split-screen on a real Xbox/CRT, alongside the
      widescreen work below.

### UT99-OPEN-005 - Broken menu after split-screen exit (GitHub #9, DEFERRED)

- [ ] Reproduce exiting split-screen leaving the frontend unusable until reset.
- [ ] Restore the correct frontend viewport, menu ownership, and input state.
- [ ] Verify repeated split-screen entry/exit and subsequent single-player use
      without restarting the game.

### UT99-OPEN-006 - Match settings reset on startup (GitHub #9, DEFERRED)

- [ ] Reproduce a Deathmatch time limit set to None reverting to 10 minutes
      after closing and restarting the game.
- [ ] Save and restore match settings across fresh launches, not just between
      matches in the same session.

### UT99-OPEN-017 - Issue-fix hardware acceptance and release

These reports remain open on GitHub. Only acceptance and release work remains
here; implementation details are in the [issue-fix report](ISSUE_FIXES_2026-09-08.md).

- [ ] Verify all nine crosshair previews match gameplay on real Xbox hardware
      ([#8 — Minor cosmetic bug](https://github.com/GTTeancum/UT99-Xbox-Releases/issues/8)).
- [ ] Verify restored Phalanx textures on hardware
      ([#10 — Texture problem on the CTF-Phalanx map](https://github.com/GTTeancum/UT99-Xbox-Releases/issues/10)).
- [ ] Verify Sundial textures and sustained bot play without navigation crashes
      ([#11 — Problem with the CTF-Sundial map](https://github.com/GTTeancum/UT99-Xbox-Releases/issues/11)).
- [ ] Verify affected custom characters load and play with the restored dependency
      ([#12 — EpicCustomModels.u is missing from release](https://github.com/GTTeancum/UT99-Xbox-Releases/issues/12)).
- [ ] After acceptance, publish the verified fixes and resolve the corresponding
      GitHub reports.

## User-entered priorities

### UT99-OPEN-008 - Full code efficiency review

- [ ] Go over all code with a fine-tooth comb for efficiency, performance,
      and speed.
- [ ] Measure changes against the original Xbox's hardware and memory limits.
- [ ] Verify optimizations preserve gameplay, rendering, audio, and multiplayer.

### UT99-OPEN-009 - Co-op Tournament

- [ ] Implement cooperative Tournament play.
- [ ] Evaluate LAN co-op on ModDB as a possible template.
- [ ] Define shared progression, match results, and integration with existing
      multiplayer flows.
- [ ] Qualify rendering and networking together, including repeated matches.

## Presentation and hardware qualification

### UT99-OPEN-010 - Widescreen presentation (PARTIAL / PENDING REVIEW)

- [ ] Correct the horizontally stretched 2D HUD in 16:9.
- [ ] Resolve the safe-area behavior that currently shrinks the whole scene.
- [ ] Preserve dashboard-controlled widescreen and correct Hor+ world projection.
- [ ] Recheck menus, single-player HUD, and two-player split-screen with explicit
      4:3 and 16:9 emulator host aspect ratios.
- [ ] Finish four-player presentation checks on hardware.
- [ ] Obtain final real-Xbox/TV sign-off.

### UT99-OPEN-011 - System Link hardware qualification

- [ ] Test discovery, join, ready-up, map travel, and match start on two or more
      real Xboxes.
- [ ] Cover failed join, pending back-out, lobby re-entry, second join, client
      leave, reconnect, host loss, and a subsequent offline match.
- [ ] Verify no stale socket, session key, or translated address survives
      teardown, and no flow exits to the dashboard.
- [ ] Confirm genuine connection loss still raises the warning icon.

### UT99-OPEN-012 - Long-play memory and resource audit

- [ ] Prove repeated frontend/gameplay transitions do not grow resident resources.
- [ ] Track render/menu textures, audio buffers, viewport count, and System Link
      sockets/peers at transition boundaries.
- [ ] Use the [hardware resource audit](../../xbox_hardware_resource_audit.md)
      as the detailed checklist.

### UT99-OPEN-013 - Tournament hardware regression

- [ ] Confirm ladder flow on real hardware.
- [ ] Verify enemy spawns, trigger/fire/start behavior, pause, and return to
      the main menu without lockups.

### UT99-OPEN-014 - Split-screen hardware validation

- [ ] Test one to four real controllers.
- [ ] Verify join, leave, lock, unlock, B/back behavior, and return to one
      frontend viewport outside live split-screen gameplay.
- [ ] Stress split-screen and System Link together.

### UT99-OPEN-015 - Audio regression (PENDING AUDIBLE REVIEW)

- [ ] Verify music and SFX across boot frontend, Instant Action, Tournament,
      split-screen, and System Link.
- [ ] Confirm music changes and level transitions do not grow resident memory.

### UT99-OPEN-016 - Larger-map stress testing

- [ ] Run memory and performance checks on larger maps, including
      `CTF-Darji16` and other maps that have approached resource limits.
- [ ] Cover multiple DM, CTF, and Assault maps.

## Proof and acceptance rules

- [ ] Use real in-game proofs and visual evidence where applicable.
- [ ] Obtain Steve's sign-off on each individual item.
- [ ] Use the target application's native capture facility; do not use desktop
      capture or host keyboard/mouse input.
- [ ] Close the test emulator after automated proof work.
