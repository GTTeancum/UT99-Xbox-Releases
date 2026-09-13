# Halo presentation validation — September 9, 2026

Supersedes the paused presentation checkpoint and the initial menu integration report. The complete normal Xemu disc is `C:/Games/Emulators/Xemu/UT99Test/ut99_halo_final_20260909.iso`; `xemu_elite_approved.toml` points to it. Canonical build output remains `build/`.

## Final behavior

- Actual-model Elite portrait faces the camera and uses blue, matching the forced default menu team. Native Player Setup capture confirms HALO ELITE and TEAM COLOR BLUE.
- HaloUT offers the original three playable guns: Assault Rifle, Pistol and Plasma Rifle, one non-reload set.
- Plasma Rifle framing was subsequently approved by the user: scale 0.4, offset (26, -14, -20). It sits closer than the original view, toward the right edge; the barrel remains aligned with camera forward. Elite and stock Soldier idle/primary/alternate proofs passed (`halo_plasma_close_final_matrix.log`).
- AR view scale is 0.30, offset (8, -4.5, -5.6), with its separate view orientation. The reduced model sits farther right and retains an opaque dark backing behind the compass and ammo digits.
- AR first-person root recoil is 6.2% of the imported motion, up 55% from the preceding 4% tuning. Position and quaternion deltas are scaled around Idle; child bones, sequence timing and third-person animation remain intact. Camera shake is unchanged at magnitude 10, time 0.04, vertical 0.1.
- Loading stops music and effects immediately, suppresses playback during loading, and restores deferred music afterward.

## Recoil reference

Inspected the AR firing sequence around 1:50 in [Nephi Johnson's original Xbox Pillar of Autumn run](https://archive.org/details/goingnowherefast_pillarofautumn), archived from the 2004 Going Nowhere Fast competition. Consecutive frames show small repeated kicks and rapid recovery rather than a large rising motion. This is a qualitative reference: player movement and the low-resolution recording prevent a calibrated recoil measurement. The final tuning follows the user's requested 50–60% increase; it does not claim to reproduce Halo's exact animation.

Local comparison evidence: `build_cli/halo1_poa_burst.mkv`, `halo1_poa_burst_frames.png`, and `halo_rifle_recoil_audit.json`. The latter checks all 12 compiled Fire frames geometrically.

## Verification

- Release executable built successfully. Compiled package animation, first-person projection and weapon attachment audits passed.
- Eight native Xemu cases passed: portrait; all three Halo weapons on Elite; all three on stock Soldier; standard Enforcer on Elite. Idle, primary and alternate captures were inspected. The two AR cases were rerun after the final recoil change.
- All seven gameplay cases logged zero active music/effect buffers during loading and subsequent music restart. No playback-start events occurred within the silenced interval.
- Final normal disc boot reached CityIntro, with advancing heartbeat and a native screenshot. No proof markers or simulated inputs are present in this disc. An initial boot-check criterion incorrectly required music before CityIntro had selected a song; the corrected check verifies level identity and running frames.
- All 642 files extracted from the final ISO match the staging SHA256 manifest. Runtime UnrealTournament.ini matches Default.ini and is included on the read-only disc.
- This validates Xemu; original Xbox hardware has not been tested in this pass.

Evidence under `UT99-Xbox/build_cli/`: `halo_presentation_harness/summary.json`, `halo_recoil_062_matrix.log`, `halo_final_disc_verification.json`, and `halo_final_manual_boot/result.json` plus `normal_boot.png`.

Final executable SHA256: `e1f9631fe0a2541d2b9e2e35b79fe0efcc3ca2be33b827e865a5b82995cbe7ee`.

Final HaloUTXbox.u SHA256 after approved Plasma Rifle framing: `df0158d418a517890065da1f91106825c5f7b2f4797dbb6a35de532d9122a883`. The normal ISO was repackaged and all 642 extracted files matched again.
