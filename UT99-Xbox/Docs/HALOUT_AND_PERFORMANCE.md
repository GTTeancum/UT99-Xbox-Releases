# HaloUT port and performance review

Work in progress, 2026-09-08. This report records implementation and evidence;
OPEN_ITEMS.md remains the list of unfinished acceptance work.

## Shared skeletal architecture

Master Chief's working package contains Engine.SkeletalMesh objects and imports
AdvancedModelSupport.EpicUTPS2MaleAnimation. The Elite uses the same
USkeletalMesh::Serialize/GetFrame implementation, UAnimation representation,
per-actor GCache allocation, LOD rendering and weapon attachment path. Its own
HaloUT motion is mapped to the animation names expected by UT99 players/bots.
There is no second skeletal renderer or Elite-specific runtime loader.

HaloUT's source is Project Torlan's UT2004 release on
[ModDB](https://www.moddb.com/mods/halout/downloads/halout-allfiles-in-one1).
Archive: HaloUT_Allfiles_In_One.1.zip; MD5
`d1c998d07f1b7cbfcd3b0d88ead016a7`, SHA1
`8d6a090a3372ae3d48a1f5da3add947c2b52a079`.
The archive's original Help/HUTP175_readme.txt contains the author credits.
Models/skins are credited to EvilEngine and Project Torlan; weapon work also
credits Dandel1984, porkmanii, Oxygene2k and Exodus.

The command-line UE Viewer exporter supplies PSK/PSA, textures and sounds.
An isolated OldUnreal 469e UCC compiles the UE1 package. Its SDK's FMeshExtWedge
serializes a reserved WORD between the vertex index and UV floats. The existing
console-compatible format used by Master Chief omits that WORD. The converter
removes only those reserved words, keeps export positions and lazy-array
offsets intact, and writes updated export sizes. UE Viewer reproduces the
original failure before conversion and reads the converted mesh successfully.
The runtime serializer remains unchanged.

Character textures are capped at 256 square and use P8 with mipmaps. The Elite
has 1,120 points; vertex animation baking is unnecessary. Player, bot and carcass
classes are provided. Current team skins cover red and blue; four-team color
coverage and visual acceptance remain unfinished.

The weapons port selects HaloUT's UT-style behavior. Its assault rifle, pistol
and plasma rifle use their source first/third-person meshes and firing sounds,
with UT99's inventory, firing states and replication. The source compiled
defaults specify assault damage 6–10 and 0.08-second firing, pistol damage 20
and 0.375-second firing, and plasma firing at 0.15 seconds. The plasma source
projectile specifies damage 13.5 and speed 6500. Complete behavior, presentation,
AI and network qualification remain open; this is not yet a release package.

## Traced performance changes

1. XboxClient::Tick draws each local viewport and passes Blit only for the last.
   XboxRender::Unlock previously ran full-backbuffer brightness/contrast/gamma
   correction after every viewport, repeatedly copying the entire screen and
   correcting earlier players multiple times. Correction now runs only on the
   final presented image. Neutral calibration already skips this pass, so this
   change alone does not promise a speedup at default calibration.
2. Renderer FPS previously counted every viewport as a frame. It now measures
   intervals between final viewport submissions. The existing XSPLIT PERF
   drawMS measures the complete group of views. Other renderer work counters
   still describe a viewport, not the whole composite frame.
3. USkeletalMesh::GetFrame resampled animation, rebuilt bone transforms and
   skinned vertices for every view. Local vertices and bone bases now reuse
   the existing per-owner GCache allocation when animation owner, animation,
   sequence, frame, loop flag and requested LOD coverage agree. Each view still
   transforms vertices and weapon attachments into its own coordinates.
   Changed animation or increased LOD demand recomputes the pose. Added cache
   storage is 48 bytes per bone, plus three header fields. Runtime verification
   across movement, tweening, death, LOD changes and travel is still required.
4. Secondary local viewports were created without a Console, although
   ChallengeHUD.PostRender reads PlayerOwner.Player.Console.bTyping each frame.
   Active secondary viewports now receive the existing lightweight Engine.Console
   through XboxEnsureConsoleClass. This removes repeated script errors without
   suppressing diagnostics. UViewport already retains and destroys its Console.

## Validation status

- Canonical Release/XBE builds passed after the display correction and pose
  cache edits, with Jailbreak staging enabled.
- HaloUT's player/bot and three weapon scripts compile with zero errors or
  warnings. Package build alone does not establish in-game correctness.
- Initial four-view smoke evidence is **not an accepted benchmark**: the old
  smoke setup produced dummy-player HUD errors and waited for a nonexistent
  bot roster. The harness now uses the existing real-player controls proof and
  expects zero bots for that scenario.
- A four-player controls run with the Console repair completed 75.8 seconds,
  with zero observed ScriptWarning entries. Native screenshots show all four
  views. Its 23.975 mean sampled FPS is not an isolated performance benchmark:
  the controls proof switches weapons and opens the pause menu. Split-screen
  performance remains open, including real-hardware verification.
- The first Elite load failed on the reserved wedge WORD. The conversion was
  traced against the compiler SDK and the working Master Chief layout before
  writing a repair. A corrected package then completed a 75.4-second run with
  one Elite bot, no reported fatal/mesh-flicker failures, at least 28,360 KB
  available memory, and zero steady-state texture uploads. Mesh centering and
  a missing third-person plasma material were corrected during that work.
  This run predates the latest assault display and starting-inventory changes;
  deterministic animation and first-person weapon acceptance are still open.
- The next Elite run completed all ten deterministic state checks (idle, walk,
  run, jump, attack, hit, dodge, swim, crouch and death), with each state PNG
  inspected. It completed 76.3 seconds, retained at least 31,512 KB available,
  and reported no skeletal failures or ScriptWarning entries. This proves
  selected state coverage, not every animation frame or LOD/travel behavior.
  BackRun, stationary DeathEnd aliases and weapon lowering received additional
  source corrections afterward and require their own runtime check.
- Master Chief then completed the same ten-state check in 76.1 seconds with
  the shared pose-cache edit. Each state screenshot was inspected; no observed
  ScriptWarning or skeletal-failure entries occurred. Minimum available memory
  was 31,192 KB and steady texture uploads were zero. Different models, weapons
  and camera coverage prevent treating the two character runs as an A/B timing
  comparison.
- Full code review is ongoing. These findings do not constitute review of all
  engine, platform, audio, networking, script and build-tool code.

## Remaining qualification

The Elite and three non-reload weapons are integrated and user-approved.
Menu discovery, the blue face-on portrait, weapon framing and recoil, opaque
AR display and loading audio have Xemu evidence in
[the presentation validation](HALO_PRESENTATION_VALIDATION_2026-09-09.md).
The approved HaloUTXbox.u is stored in RuntimeAssets; release validation
requires it, its .int and Elite portrait, alongside EpicCustomModels.u.

- Qualify the integrated characters and weapons on original Xbox hardware,
  including pickups, repeated travel, carcasses and network behavior.
- Continue the full code review and isolate split-screen timing with two,
  three and four players, including non-neutral display calibration, shared
  skeletal actors, LOD changes and repeated travel. Hardware acceptance remains
  separate from emulator proof.
