# Whole-picture TV safe margin - 2026-09-10

TV SAFE ZONE scales the entire composed game picture into a safe rectangle.
World geometry, first-person weapons, HUD, and menus share that rectangle.
PICTURE OFFSET X/Y moves that picture within its available border.
The earlier HUD-only implementation was rejected and removed.

## Implementation

- One transform maps the 640x480 logical screen to the safe rectangle, including
  existing widescreen split-screen pillarboxes. Split tiles share transformed
  boundaries; no extra margins are inserted between players.
- Identical bounds apply before render lock and master scene setup. Canvas
  inherits that scene. No separate HUD viewport or crosshair correction remains.
- Clear the full backbuffer before the first local view; later views clear their
  own regions. This removes stale world pixels from the border when settings
  change. Transform the unused three-player tile too.
- Native menu model previews include the scene origin. The video menu miniature
  scales the full-screen offset instead of treating it as preview pixels.
- Keep existing SafeAreaSize/X/Y configuration keys and persistence.

## Verification

- Canonical Release build passed: build/default.xbe.
- SHA-256: 45e47894b0fa75dbc956ff75510d476cd017765585267c83e9170a92dc1613da.
- Production geometry helper: 2,880 cases covering 85-100%, extreme offsets,
  one through four players, both aspects, and gap-free tile joins.
- Controller timing regression passed (15-120 FPS and menu/resume).
- Six native Xemu cases passed revalidation: standard 1P/2P/4P and widescreen
  1P/3P/4P. Each changes 100% to 85%, then 92% with offset (+18,-12), back to
  100%, then 85% with a menu. Proof input is contained inside the game process.
- All 30 native screenshots were inspected individually. All 75 logged viewport
  bounds and 30 screenshot border checks passed, including the unused 3P tile.
- The first widescreen 4P validator assumed 640x480 captures and rejected Xemu's
  native 853x480 output. The corrected aspect-aware validator passed the same
  saved captures. The original failure remains recorded in the raw evidence.
- Measurements: [SAFE_PICTURE_MEASUREMENTS_2026-09-10.json](SAFE_PICTURE_MEASUREMENTS_2026-09-10.json).
- Evidence directories: UT99-Xbox/build_cli/safe_picture_* (local, ignored).
  These short display proofs are not a new performance benchmark.
- Automated proof emulators were closed after each run.

Accepted and closed by Steve on 2026-09-10. CRT-user confirmation is not a
closure requirement. Any subsequently reported CRT clipping should be filed
as a new ticket. Removed UT99-OPEN-004 and the duplicate safe-margin task from
the open tracker; unrelated widescreen presentation work remains separate.
