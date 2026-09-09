# Halo presentation checkpoint — paused for usage limits

Historical checkpoint. Resumed work is complete; see [final validation](HALO_PRESENTATION_VALIDATION_2026-09-09.md).

User requested stopping work and committing/pushing the dirty workspace. This
checkpoint includes other existing dirty work and is not a release approval.

## Verified

- Elite animation and third-person attachment were approved in gameplay.
- Actual-model, face-on Elite portrait is blue, matching forced menu team 1.
  Native evidence: `build_cli/halo_presentation_harness/portrait/portrait.png`.
- Loading proof observed `musicPlaying=0 stream=0 activeEffects=0`, then loading
  silence off and native menu music restarting. Rifle proof runs captured idle,
  primary and alternate fire; these behavioral passes are not visual approval.
- Scope is the original three playable HaloUT guns, one non-reload set.

## Unfinished

- Rifle first-person perspective, angle and display need final native validation.
  User wants the rifle pulled toward the camera, accounting for absent FPV hands.
  Original Xbox reference screenshots:
  https://superadventuresingaming.blogspot.com/2011/09/halo-combat-evolved-xbox.html
  (`Halo_Xbox_15.jpg` and `Halo_Xbox_17.jpg`).
- Last edits set AR view yaw -68, scale .45, offset (8,-3.8,-7.7).
  View scale/offsets were enlarged together to preserve projection beyond the
  renderer's 1-unit near plane. World attachment origins are unchanged.
- Original display materials blend with no depth writes and self-illumination.
  Last importer correction uses ActorX MTT flags 18 (translucent plus unlit),
  not runtime EPolyFlags. Previous wrong flags compiled as modulated materials
  and produced a black display. Latest correction is not yet verified in Xemu.
- Rebuild `build_halout.py`, verify compiled material flags and native display,
  finish all three Halo guns on Elite and standard UT characters, and standard
  gun regression on Elite. `verify_halout_presentation.py` drives only isolated
  process-local proofs and captures through Xemu's native screenshot facility.
- Finish clean normal runtime/ISO packaging from canonical `build/`, with newest
  package, XBE and portrait; remove proof markers/configuration. Keep complete
  game data and generated `System/UnrealTournament.ini` on the read-only disc.

Goal remains unfinished. No additional test or gameplay launch is requested
until the user resumes work.
