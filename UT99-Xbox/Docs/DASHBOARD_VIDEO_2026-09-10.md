# Dashboard video policy and evidence

The dashboard controls aspect ratio. Output stays at 640 x 480, preferring
480p when a 60 Hz progressive mode is enumerated for the current AV pack and
dashboard settings, and falling back to 480i otherwise. Neither 720p nor 1080i
is selected. Device creation also retries 480i if progressive creation fails.

| Dashboard | Menus / single player | Two or more local players |
|---|---|---|
| 4:3 | Existing 4:3 presentation | Existing full-frame split layout |
| 16:9 | Anamorphic output with Hor+ world projection | Centered physical 4:3 region with sidebars |

The widescreen split region is 480 x 480 stored pixels at x=80. With the
4:3 pixel aspect ratio of 16:9 output, this becomes a physical 4:3 region.
Projection compensation preserves the original split-screen field of view.
Three-player mode clears the unused lower-right quadrant as well as the bars.
The video signal is not recreated when switching layouts.

The later [widescreen UI correction](WIDESCREEN_UI_2026-09-12.md) compensates
Canvas and native-menu drawing for pixel aspect without changing this output
policy or the world/split-screen render regions.

## Verification

Evidence directories are under `UT99-Xbox/build_cli/` (local, ignored).

| Run | Result |
|---|---|
| `dashboard_video_wide4_fixed` | 4P, widescreen, 480p disabled: 480i flags 0x30, correct sidebars; 91.6 s combat soak passed; all three native captures inspected. |
| `dashboard_video_wide2_480p` | 2P layout visually correct, but this earlier build incorrectly selected 480i; excluded as evidence of progressive support. |
| `dashboard_video_wide3_480p_fixed` | 3P, widescreen with all HD flags enabled: 480p flags 0x50, sidebars and unused quadrant clear; 92.7 s combat soak passed; all three captures inspected. |
| `dashboard_video_standard4_480p` | 4P, 4:3 plus 480p: flags 0x40, original full-frame quadrants; 95.7 s combat soak passed; all three captures inspected. |
| `dashboard_video_transition_input` | 2P widescreen 480p: split, pause, resume, frontend, and subsequent single-player map all passed; all five native captures inspected. Single-player capture is the pre-match screen. |
| `dashboard_video_transition_deferred4` | Intermediate deferred teardown, 4P widescreen 480p: five phases passed, but this is superseded by the native-layout fix and does not qualify the final build. |
| `dashboard_video_transition_standard4` | Intermediate deferred teardown failed during player-object garbage collection, exposing the native-layout mismatch. |
| `dashboard_video_abi_fixed_standard4` | Full rebuild with corrected native layout: 4P 4:3 480p, all five transition phases pass, native layout audit passes for every local player; all five captures inspected. |
| `dashboard_video_abi_fixed_wide4` | Same final binary, 4P widescreen 480p: all five transition phases and all player-layout checks pass; all five captures inspected. |

Two defects were caught before qualification: initial layout selection read the
split-active flag before activation completed, and mode enumeration incorrectly
required scanout format to equal the swizzled backbuffer format. Both were fixed.
The initial `dashboard_video_wide4` run is excluded as a presentation pass.

Transition testing also exposed a test-hook defect: combat jump input selected
Resume while paused. Combat input is now neutral in menus. Transition actions
were also moved from PostRender to PollController so travel occurs before
rendering; the later native-layout audit identified the production teardown
crash described below. The earlier `transitions`,
`transitions_fixed`, and `transition_diagnostic` runs are failures, not passes.

The production exit path previously removed only dummy viewports, leaving real
joined secondary viewports active. Teardown now removes every secondary viewport,
detaches its shared render device and follows UPlayer's pawn ownership teardown.
The first four-player transition attempt (`dashboard_video_transition_final4`)
caught a protection fault in `CleanupDestroyed`. Deferring pawn destruction
passed one widescreen run but failed a 4:3 repeat during garbage collection;
that workaround was removed after finding the actual native-layout defect.

The loaded PlayerPawn has **WeaponPriority[50]**, four bytes per entry, at byte
1340. The C++ header declared only 20 entries. SmoothMouseX was therefore at
1420 natively versus 1540 in reflection, and ProgressMessage at 1540 versus
1660. All later native strings were displaced by 120 bytes, so native destruction
could free gameplay data as pointers. Both the native declaration and source
UnrealScript declaration now have 50 entries. Runtime offset checks cover
SmoothMouseX, ProgressMessage and ngWorldSecret. A full build recompiles every
module using the corrected header. The proof audits all native pawn/player
string offsets for each local player and rejects any mismatch or wrong array size.
`dashboard_video_field_audit` and `dashboard_video_priority_audit` captured the
mismatch and were stopped after diagnosis; neither is an acceptance pass.
`Tools/test_split_runtime_reset.py` checks real and dummy removal, borrowed pawn
preservation, renderer ownership, and repeated reset without double destruction.

`Tools/test_dashboard_split_layout.py` compiles the production layout/projection
helpers and checks both aspects and every nonempty player-slot mask, including
layout initialization before activation and projection reset after exit.
`Tools/test_dashboard_video_modes.py` compiles the production mode selector and
checks progressive preference, interlaced fallback, enumeration failure, and
rejection of HD or 50 Hz modes for both aspects.

Tests use private EEPROM copies; the user's EEPROM is unchanged. Captures come
from Xemu's native facility. No desktop input or capture is used. The optional
`--dashboard-video-proof` invokes game-local menu functions and records separate
split, pause, resume, frontend, and single-player checks and captures.

## Limits

Canonical build: `build/default.xbe`, SHA-256
`5f1f9457974b3c66d29680f850dfbddce453d757f953f569f5f51956a7cffe71`.
The build directory contains no smoke/proof/benchmark marker INIs. The user
accepted the implementation and emulator evidence on 2026-09-10 and requested
commit and push. Release packaging and hardware deployment remain separate.

Xemu verifies selected flags and rendered layout, not a retail encoder's actual
signal or CRT overscan. Composite/S-video fallback and component 480p still need
real-Xbox confirmation. Device-creation failure fallback is a code path, not an
emulator fault-injection result. Existing HUD stretching and safe-area behavior
remain tracked separately in OPEN_ITEMS.md. The completed eight-run, four-map
4P comparison is recorded in
[DASHBOARD_4P_PERFORMANCE_2026-09-10.md](DASHBOARD_4P_PERFORMANCE_2026-09-10.md),
including sampling limits and the slower HangEmHigh pillarboxed result.
