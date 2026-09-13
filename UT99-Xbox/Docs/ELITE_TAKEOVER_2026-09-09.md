# Elite conversion takeover — unresolved

The user rejected the previous animation and weapon evidence: motions looked
slow/incomplete and the weapon pointed downward with an incorrect offset.
Neither prior green soak summaries nor timing ratios establish visual acceptance.
The goal remains active. Do not deliver the old recording as a verified result.

## Recovered requirements

- Use the existing Master Chief/PS2 skeletal runtime architecture.
- Correct animation speed, complete motions, and right-hand weapon placement.
- Deliver 60 seconds of actual bot gameplay from the right-hand-visible side.
- Use active bots for gameplay/performance evidence. Native Xemu capture only;
  no desktop capture, window input, or UI automation.
- The user suggested comparing our working Chief with the UT2004 mod's Chief
  as an oracle. This comparison is now part of the investigation.

## Established trace and changes

The previous conversion exports PSK/PSA with UE Viewer, maps Halo sequence names
to TMale2 names, changes some rates to UT99 cycle durations, compiles with an
isolated 469e UCC, and removes the reserved wedge WORD. It is not a fully
qualified animation/behavior conversion.

1. The proof camera could fall back to the left side and enforce a minimum
   distance past a collision. `Engine/Src/UnGame.cpp` now uses right-side camera
   candidates for right-side proofs, a 6-unit collision extent, no forced
   penetration distance, and a wider body-centred frame.
2. `halout_aim_pose.py` incorrectly used PSA BONENAMES parents. Those are
   placeholders (many are zero). The runtime uses the PSK mesh hierarchy.
   The converter now maps animation tracks to the real PSK parent chain.
   `test_halout_aim_hierarchy.py` verifies +/-45-degree elevation, grip-relative
   transforms, untouched legs/root, and independence from PSA placeholder parents.
3. The three Halo world weapon meshes omitted original MeshOrigin/RotOrigin.
   Local UE Viewer dumps of the original Haloweapons.ukx establish:
   Assault (-5,-20,-5), Pistol (-5,0,3), Plasma (-5,-10,16), yaw -16500 for all.
   Those values are now imported using MESH ORIGIN (yaw -64.453125 * 256).
   This alone did NOT qualify attachment orientation or offset.
4. Native diagnostic build adds XSKELSOCKET camera axes and XSKELMUZZLE root-to-
   flash direction under existing skeletal-audit gating. In the level pose,
   the weapon barrel followed socket X, which was mostly camera depth; the
   character-forward direction corresponds to -socket Y. EliteAssets now has
   a quarter-turn WeaponPosition yaw=64 to convert the authored socket to
   classic +X barrel axes. This newest correction still needs native validation.
   Aim rotation now uses that same authored -Y barrel direction.
5. HumanBotPlus.PlayInAir/FastInAir/PlayDodge/PlayLanded and TournamentPlayer's
   air/landing functions use TweenAnim, sometimes with a two-second tween.
   This only reaches the FIRST pose and cannot play the imported multi-frame
   clips. EliteBot and Elite now override the air/landing paths; EliteBot also
   overrides FastInAir/PlayDodge. These changes compile but need live validation.
   JumpLGFR still maps only JumpF_Mid: takeoff-to-mid sequence composition and
   full behavior/transition coverage remain unresolved. Do not call this done.

## Chief oracle

The working `build/System/HaloMasterChief.u` mesh has 47 bones, 1075 exported
points, and uses AdvancedModelSupport.EpicUTPS2MaleAnimation. Its attachment is
right-hand bone 26 with position approximately (15,0,-6) and a 180-degree roll.
The UT2004 `Animations/HaloMasterChief.ukx` MC mesh has 26 bones, 1325 points,
and a Bone_weapon child of the right hand. It shares 25 case-insensitive bone
names with ours, but it is NOT the same rig or animation package. Use working
Chief to establish runtime conventions; do not transplant bone indices/offsets.
Exports/dumps are in build_cli/chief_oracle_* and halout_source/chief_reference.

## Current evidence

- `elite_takeover_build.log`: successful canonical camera-fix Release build.
- `elite_takeover_direction_build.log`: successful canonical build including
  native direction diagnostics; paired XBE/map hashes below.
- Attachment rotation, proof clock, timed track endpoints tests pass, with their
  original narrow scope. The attachment test mocks the final coordinate product
  and does not prove the complete socket/mesh/pivot transform.
- `elite_takeover_camera_pilot`: four live bots on Morpheus; first three camera
  frames inspected individually. A texture-oscillation alert on AssaultWorld
  stopped the pilot. Dynamic ammo/compass material changes are a possible cause;
  trace before classifying or suppressing it. User rejected weapon/animations.
- `elite_takeover_all_states`: stopped on user correction before useful coverage.
- `elite_takeover_pivot_states`: ten states captured; soak passed. Only run and
  jump frames 1/2 inspected here (four frames). They do NOT prove correct grip.
- `elite_takeover_direction_trace`: completed ten-state soak with diagnostics.
  This predates yaw=64 and playback overrides, and is not final acceptance.
- No new final gameplay recording has been made. The old Deck16 recording starts
  dark and obstructed and is rejected. Full visual review and Chief regressions
  remain necessary after the final repairs.

Canonical diagnostic XBE SHA256:
`F2E6F0C7790131229F91B5CC9122F550EFE8B2BB09A75C22F0FDB0B7A772B041`

Paired map SHA256:
`071B1FEFCF48FF029B0DDF13376C39F4835E5FD98F13A745D5F717EC16641ED8`

Latest package build log is `build_cli/elite_takeover_playback_assets.log`;
on successful compilation it is copied to canonical build/System/HaloUTXbox.u.
Rehash the package for each test cohort; its identity changes independently.

## Repeatable commands

Run `Tools/build_halout.py` with source `build_cli/halout_source/original`,
umodel `build_cli/halout_source/umodel/umodel.exe`, compiler
`build_cli/halout_source/compiler469e`, and runtime `build_cli/issue5_mapfix_stage`.
Copy its successfully normalized System/HaloUTXbox.u to canonical build/System.

The native harness uses `Tools/run_xemu_stress_matrix.py --build-dir build`
with runtime-source `UT99-Xbox/build_cli/issue5_mapfix_stage`, stage-dir
`UT99-Xbox/build_cli/elite_takeover_stage`, xemu-instance
`UT99-Xbox/build_cli/elite_takeover_xemu`, cases DM-Morpheus,
bot-class HaloUTXbox.EliteBot, mutator HaloUTXbox.HaloWeapons,
right-side-proof, poll-interval 0.75, mute-audio, and a NEW evidence directory.
State proof sets skeletal-state-proof; a live recording uses record-gameplay
with multiple bots and no skeletal-state-proof. Recordings are 60 seconds.

Outstanding: trace/render true source and converted motions, complete jump
composition and per-state playback semantics, qualify orientation AND grip
offset, test all three weapons, perform broad animation and live gameplay review,
run Chief regressions, record 60 seconds, and update accurate final evidence.

## Chief animation oracle: key-retention defect isolated

User clarified Chief is an oracle for animation conversion; animation correctness
is the priority. Weapon -90-degree roll remains a hypothesis. Current attachment
yaw is -64 (supersedes the earlier +64 entry); placement is NOT accepted.
JumpLGFR now joins source takeoff and mid-air motions (31 frames, 1.033333 sec).

Exported working AdvancedModelSupport.EpicUTPS2MaleAnimation to PSA and imported
an isolated ChiefConversionOracle package through the same 469e UCC commands.
All 70 frame counts/rates/durations survived, but intermediate motion did not.
ANIMINFO KeyReduction (offset 144) is zero in UEViewer output; IMPORTSEQS uses
that per-sequence value despite COMPRESS=1. UCC logs Keymax 0 and lossy culling
to endpoints. COMPRESS=0 and MAXKEYS=999999 alone do not remedy it. Setting only
the PSA KeyReduction to 1 does: quaternion keys increase from 4305 to 26633.
Original source PSA itself contains the intermediate poses. Elite PSA has the
same zero metadata; e.g. Dead1 imported 48 frames but only two keys per moving
track. This is a demonstrated data-conversion defect, not a playback-speed guess.

Tools/read_native_animation.py parses the serialized Animation schema with export
boundary validation. Tools/audit_animation_import.py compares every source-frame
local bone pose to the imported tracks, including clip timing. Chief before fix:
max angular deviation 178.788 degrees, position 123.923 units. After fix: 0.068315
degrees, 0.0000573 units across 51792 bone poses. These comparisons use exported
PSA coordinate conventions, not uncorrected native-to-ActorX axes.

Tools/build_halout.py now sets full key retention on Elite and all three weapon
PSAs, then audits imports with a 0.1-degree / 0.001-unit maximum error gate.
Corrected Elite: 68 clips, 100360 bone poses, max 0.069449 degrees and 0.0000479
units. Weapon imports also pass. Report: build_cli/animation_import_pose_audit.json.
This is numerical source-frame coverage, not a claim of full visual acceptance.
Corrected package staged in canonical build/System/HaloUTXbox.u. Native state
proof currently running in build_cli/elite_retained_keys_states.

Pending: verify full native motions visually and in live play; reassess earlier
rate overrides now the real intermediate motion is preserved; fix weapon rotation
and grip; complete Chief native regression and final 60-second recording.

Native retained-key state proof completed: ten state categories, 45.4 seconds,
no harness failures, 58.15 steady FPS, minimum available memory 30248 KB. Run
and jump frame pairs were viewed individually and show distinct intermediate
poses; weapon still visibly misaligned. These four images are not full motion
or all-state visual acceptance. The build was rerun successfully with the new
source-pose audit gate enabled, then copied to canonical build/System.

## Standard and Halo weapon isolation

User confirmed animation is MUCH better and requested both standard and Halo guns.
Source Elite.uc recovered using UEViewer -export -uc shows righthand socket on
Bone_weapon with yaw 49152, roll 32768, zero translation. Directly restoring
roll 180 was tested against PlasmaWorld and standard PulseGun; neither constitutes
placement acceptance. Existing test mocked FCoords multiplication; now it extracts
the real operators from Core/Inc/UnMath.h and verifies final transformed vertices
over 360 rotations. Passes.

Classic attachment frame contributes an X quarter-turn. Corrected command is
YAW=-64 PITCH=0 ROLL=-64 (degrees: yaw -90, roll -90). UCC stores the inverse
command rotation. The compiled WeaponAdjust now satisfies A^T * B = Source^T
(max matrix error 2.22e-16), where B is classic X quarter-turn and Source is
the authored socket rotation. An initial alternate Euler encoding was rejected
by this native-data check before staging. Native corrected-roll test pending.

Added explicit AttachmentProof test mutator plus harness --attachment-weapon
Package.Class. It equips exactly that class, removes competing weapon inventory,
and blocks competing pickups. Only active when explicitly selected. Its config
is isolated to the stage System/AttachmentProof.ini and removed on other runs.
Baseline exact-enforcer proof passed and logged EliteBot weapon=Botpack.enforcer.
Current run: elite_roll90_enforcer_states. Need corrected-roll stock rifle and all
three Halo weapons, Chief regressions, normal gameplay recording, and final review.

Corrected-roll exact Enforcer proof passed 46.1 seconds, 58.425 steady FPS;
walking and running pairs viewed individually, barrel forward and grip at hand.
Compiled socket identity also checked through 360 varying 3D poses.

First forced HaloPlasmaRifle run (elite_roll90_plasma_states) is REJECTED: config
class property used StaticFindObject without loading the class and left bot
unarmed. New summary gate checks the actual XSKELWEAPON draw identity and failed
the run. AttachmentProof now uses a config string and DynamicLoadObject during
PreBeginPlay, logs requested/resolved class, and gives ammo/switch priority when
spawning the weapon. Rerun elite_roll90_plasma_loaded_states is pending.

Four source tests pass after current edits: actual FCoords attachment transforms,
timed/uniform non-loop endpoints, 20-120Hz proof clock independence, aim hierarchy.
Weapon comparisons remaining: corrected roll HaloPlasma, HaloAssault, HaloPistol,
standard PulseGun (prior roll180 PulseGun proof is not current acceptance), Chief
regressions and final live recording.

The corrected-roll plasma run resolved and drew the intended weapon but exposed
a separate mesh-origin problem. Original MeshOrigin was toward the muzzle; the
authored Bone_weapon is the grip anchor. All three world-mesh origins now equal
that reference-space grip (Assault uses composed child-bone position). Exact
coordinates are in their .uc directives. Read-only audit_halout_weapon_grips.py
checks the compiled mesh origins against native ref-skeleton composition, worst
distance 0.0000011 mesh units. First-person origins are untouched.

Current plasma grip proof elite_grip_plasma_states passed ten categories and
weapon identity, 45.6 seconds, 57.1125 steady FPS, min 30760 KB. Running pair
viewed individually: gun moved forward into hands with the authored grip at
socket. Other poses and full live play remain to be reviewed.

A sequential live test driver (terminal session 45489 at writing) is running
exact HaloAssault, HaloPistol, standard PulseGun, Chief+Enforcer, Chief+HaloAssault
proofs, each 45 seconds, same canonical package. Do not restart on timeout;
inspect the existing handle/process and resulting individual evidence dirs.

User additionally requires Halo weapons on normal UT skins to isolate character
versus weapon faults. After session 45489 finishes, smoke all three exact Halo
weapons on Botpack.TMale2Bot (standard Soldier mesh) with --attachment-weapon
and --right-side-proof, WITHOUT --skeletal-state-proof (native state override
intentionally selects skeletal meshes only). Camera/identity diagnostics already
support standard bots. Use Botpack.TMale1Bot or female stock class for another
standard shape if useful. These tests remain outstanding; do not count Chief
as the standard UT character test.

## Close-up supersedes coarse roll acceptance

XBE with test-only XboxWeaponCloseup.ini marker built successfully; canonical
SHA256 BA943C8EE5EC9C25F2B03A3087076336DC902ECF0234DCA9EE491B6F55869EB6.
No Xbox*.ini proof markers are present in canonical build.

All three Halo weapons passed identity/stability smoke on standard UT meshes:
stock_assault_closeup and stock_pistol_closeup use Botpack.TMale2Bot/Soldier;
stock_plasma_closeup uses Botpack.TMale1Bot/Commando. Three consecutive camera
images viewed individually for each. These are normal gameplay, no pose override.
Chief Enforcer and HaloAssault ten-state regressions passed; run/fire pairs
viewed individually.

The Elite pistol close-up exposed an inversion missed by coarse full-body views.
ROLL=-64 was WRONG: pistol grip pointed up, whereas standard Soldier was upright.
Final candidate is YAW=-64 PITCH=0 ROLL=64, zero translation. This flips only
Elite's weapon roll, preserving forward and the grip pivot. Source-matrix identity
alone was insufficient because it omitted the full handedness chain.

New audit_elite_frame in audit_halout_weapon_grips.py evaluates native Breath1
through hierarchy, WeaponAdjust, reflected character transform, classic frame,
and each reflected weapon transform. It requires barrel dot pawn-forward >.95
and weapon-top dot world-up >.95. Current values are .996845 and .996634. The
build now includes this gate, along with full source-frame pose and grip audits.

Current elite_upright_pistol_closeup run/fire pairs viewed individually: pistol
is upright, its grip lies inside the hand, barrel points forward. Previous Elite
roll-90 visual acceptance is superseded. Need recheck standard Enforcer/Pulse
and HaloAssault/Plasma on corrected Elite, all-animation coverage and final live
60-second gameplay recording. Normal UT and Chief tests are unaffected by this
Elite-only metadata change.

## Final validation and recording review

Final Enforcer/Pulse/Plasma close-up tests and all 68 animation aliases passed
with exact weapon identity and no missing state categories. Matrix saved at
build_cli/elite_final_validation_matrix.json. Enforcer/Pulse/Plasma attack pairs
viewed individually; upright, forward and gripped. Aim up/down and jump/land
pairs from the all-animation sweep also viewed individually. The 136 sweep
images were not all visually inspected; coverage is native telemetry plus the
all-source-frame numerical pose audit, not an every-frame visual claim.

First final gameplay run elite_final_gameplay passed 90.3 seconds with four
live Elite bots, 55.35 steady FPS, no fatal/flicker alerts, minimum 25508 KB.
Video has 1200 native captures, 59.959s acquisition span, encoded 60.000s/20fps,
640x480/no audio; full decoder check passes. Visual review sampled every five
seconds plus endpoint, shows movement/combat/deaths but revealed subject loss
after target death in the last seconds. Do not present it as final accepted
recording. Added immediate living-bot fallback in marker-gated proof Draw camera;
rebuild session 71472 active at writing. Next: await build, rerun live 60-second
recording with same four bots/mutator and no pose override, inspect handoff/end,
write final evidence report, verify canonical hashes and completion criteria.
Package SHA256 unchanged: 33417417095989029439C035FBFE5343C74E9B10FB9770D61AD987C047CB808D.

User correction: apparent target loss in final video is the bot picking up
cloaking. Do not classify this as camera failure. Speculative living-bot fallback
removed from source. Await in-flight build then rebuild canonical source without
that change, or restore verified matching previous XBE if exact available.
Existing 60s video remains valid live gameplay evidence with expected invisibility.

COMPLETE: user approved Elite after watching gameplay. See
ELITE_FINAL_VALIDATION_2026-09-09.md for final fixes, tests, recording and limits.
