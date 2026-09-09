# Elite animation and weapon attachment review

Work in progress. The first gameplay recording was rejected for weapon placement;
it is not acceptance evidence. The requested final recording must show the weapon
hand from the opposite side, with a live Elite bot and normal bot combat damage.

**Playback-speed failure:** the old state harness swept every clip over 120 ticks,
regardless of its frame count or rate. Those captures do not validate playback
speed. It now uses elapsed simulation seconds multiplied by Rate/NumFrames, like
UE1 PlayAnim. Inspection-slot duration no longer controls animation speed. Earlier
soak passes below establish only their stated pose/name coverage, not acceptance.

## Corrections

- The shared skeletal weapon builder used bone-matrix rows as transformed local
  axes. Skinning uses row dot products, so transformed local unit axes are the
  columns. The builder now uses the same vector transform as skinning. This keeps
  the weapon's grip offset rotating with the hand instead of inversely to it.
- Elite uses its authored `Bone_weapon` child of the right hand, with identity
  `MESH WEAPONPOSITION`. The earlier YAW=128 adjustment reversed the rifle in
  live gameplay and was removed. The shared builder supplies the classic-axis
  conversion; the asset must not add another half-turn.
- Mesh origin yaw is -16384, matching the original HaloCreatures Elite metadata
  dumped with UE Viewer. The shared PS2 skeletal evaluator remains in use.
- Firing uses Rifle_Burst, switching uses Weapon_Switch, chat uses idle_chat,
  looking uses Idle_Character01, and Wave/WaveL use gesture_beckon. These were
  previously substituted with idle or level-aim animation.
- Up/down aim are single-frame poses derived from Rifle_Aimed. A 45-degree
  upper-body subtree rotation preserves the authored relative arm/hand/socket
  transforms. The conversion checks that the resulting subtree basis equals
  the requested world-space rotation of the original basis.
- Mapped multi-frame cycles retain Halo keys but use UT's original cycle duration.
  Death mapping is explicit instead of a hash of the animation name. DeathEnd
  variants match their respective death sequences; Dead9B holds Dead9's final
  pose instead of restarting a different death.
- Original TMale2 notifications are applied to EliteAnims, the UAnimation object
  from which the runtime copies its sequences.
- One-shot timed tracks hold their final key, matching the existing uniform-key
  path. Previously the timed path blended its final key back to key zero even
  when Loop was false. The proof now loops sustained movement/idle families and
  plays jump, landing, hit, gesture and death motions once.

## Evidence and limits

- `test_skeletal_attachment.py` compiles the actual attachment builder and checks
  it against independently specified skinned local axes over 360 rotations.
- `build_cli/elite_grip_states`: ten representative states, two screenshots each;
  all 20 inspected individually. Rifle direction follows the hand through the
  captured poses. This predates the complete timing/aim/notification corrections.
- `build_cli/chief_attachment_basis_states`: Master Chief regression, ten states,
  two screenshots each, all 20 inspected individually. The 60-second soak passed.
- `build_cli/elite_all_animation_states` was stopped because the capture parser
  accepted lowercase category labels but not actual mixed-case sequence names.
  It is not counted as a pass. The parser now accepts sequence names and digits.
- `build_cli/elite_all_animation_verified` used the rejected fixed-duration
  sweep. `elite_native_rate_gameplay` verified running speed but still showed a
  backward rifle. Neither is final visual acceptance evidence.
- `build_cli/elite_authored_socket_states` failed a jump pose alert while the
  proof incorrectly looped that one-shot motion; the failure is retained.
- `test_skeletal_proof_timing.py` checks the actual proof clock at 20-120 Hz.
  `test_skeletal_track_end.py` checks actual uniform and timed sampler endpoints,
  including continued interpolation across a genuine loop boundary. Both pass.
- `build_cli/elite_final_animation_states`: 309-second completed run, all 68
  sequence names captured twice, all 136 PNGs inspected individually. No pose
  flicker alerts. RunLg cycle 0.5882 seconds, WalkLg 0.8333 seconds; measured
  advancement ratios are 1.00. Jump and death advancement also measure 1.00.
  Some death/swim poses extend below the close camera's bottom edge; these
  screenshots do not establish complete body visibility or every transition.

Canonical build for the final state run:

- default.xbe SHA256: `1811C54A7C2206FD156592F2A4EBCEF7C8D00433D665C42C55722B7AF55A34C0`
- UnrealTournament.map SHA256: `CC25A84F49EF38B516FE7CB290D72F5CA4AAF23860EF272A6D3A26FD65E3CAAB`
- HaloUTXbox.u SHA256: `74F682E3ACFED62334C34BAE5A3663565172C946EB9FB746D570B340629FF412`

A 60-second gameplay clip cannot establish every animation or transition. Full
name coverage, pose inspection, and live gameplay are separate checks. No claim
of 100% visual correctness or hardware qualification is made by a soak pass.
