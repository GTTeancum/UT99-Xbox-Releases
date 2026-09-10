# UT99 Xbox - Open Work Only

Last updated: 2026-09-10 EDT
Status: ACTIVE

This tracker contains only open or explicitly deferred work. Remove an entry
when it is accepted or closed; completed work, evidence history, and superseded
attempts do not belong here.

## Open items at a glance

This is one line per major open deliverable or defect. Detailed implementation
and acceptance work remains below.

1. **DEFERRED** - Fix inconsistent controller sensitivity between single-player and split-screen (#9).
2. **DEFERRED** - Keep split-screen player settings independent (#9).
3. **DEFERRED** - Fix interface clipping at the top and bottom of CRT displays (#9).
4. **PENDING REVIEW** - Verify the split-screen exit fix through repeated sessions and hardware testing (#9).
5. **DEFERRED** - Persist match settings across game restarts (#9).
6. **PARTIAL / PENDING REVIEW** - Finish widescreen HUD and safe-area presentation, then obtain real-Xbox/TV sign-off.

## GitHub issue register

The remaining deferred defects below come from the [bug report in issue #9](https://github.com/GTTeancum/UT99-Xbox-Releases/issues/9#issuecomment-5577635096).

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

### UT99-OPEN-005 - Split-screen exit qualification (GitHub #9, PENDING REVIEW)

- [ ] Verify repeated split-screen entry/exit and subsequent single-player use
      in the same session on real Xbox, including menu ownership and input.
- [ ] Obtain sign-off on the frontend exit fix.

### UT99-OPEN-006 - Match settings reset on startup (GitHub #9, DEFERRED)

- [ ] Reproduce a Deathmatch time limit set to None reverting to 10 minutes
      after closing and restarting the game.
- [ ] Save and restore match settings across fresh launches, not just between
      matches in the same session.

## Presentation and hardware qualification

### UT99-OPEN-010 - Widescreen presentation (PARTIAL / PENDING REVIEW)

- [ ] Correct the horizontally stretched 2D HUD in 16:9.
- [ ] Resolve the safe-area behavior that currently shrinks the whole scene.
- [ ] Qualify dashboard-controlled 480p/480i output, Hor+ single-player projection,
      and physical 4:3 pillarboxing for two or more local players on hardware.
- [ ] Recheck menus, single-player HUD, and two-player split-screen with explicit
      4:3 and 16:9 emulator host aspect ratios.
- [ ] Finish four-player presentation checks on hardware.
- [ ] Obtain final real-Xbox/TV sign-off.

## Proof and acceptance rules

- [ ] Use real in-game proofs and visual evidence where applicable.
- [ ] Obtain Steve's sign-off on each individual item.
- [ ] Use the target application's native capture facility; do not use desktop
      capture or host keyboard/mouse input.
- [ ] Close the test emulator after automated proof work.
