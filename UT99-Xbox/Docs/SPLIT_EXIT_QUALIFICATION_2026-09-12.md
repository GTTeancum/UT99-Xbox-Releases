# Split-screen exit qualification - 2026-09-12

Scope: repeated split-screen entry/exit and subsequent single-player use in the
same emulator process. This is UT99-OPEN-005 (formerly tracker item 2, now 1).
Per-profile HUD preferences were accepted and closed separately.

The existing production exit path resets split runtime state, removes child
viewports, restores the frontend console, and queues CityIntro. No additional
production exit fix has been identified during this work.

The opt-in repeat smoke uses three cycles per process:
split gameplay, pause, resume, frontend, single-player gameplay, frontend again.
It checks the single remaining viewport and full logical region, primary-player
binding, console/input availability, menu ownership, menu navigation, and
single-player yaw movement from an in-process test pad. It then re-enters split
screen using the ready-slot and launch handlers. No host input is generated.

Two smoke assumptions were corrected before qualification:
- The initial automatic split setup bypasses the frontend profile gate. On the
  first return the test must select the existing profile through its handler
  before testing main-menu navigation.
- Single-player load time exceeded the initial observation window. The input
  observation timer now starts when the gameplay level becomes active.
Raw exploratory failures are retained in build_cli/split_exit_repeat_4p_wide
and build_cli/split_exit_final_4p_wide; they are not qualified passing runs.

Qualification passed: three cycles with 4P widescreen and three with 2P 4:3.
All 36 phase checks and 18 exit/input checks passed. Each of the six subsequent
single-player sessions registered yaw movement; every return restored one full
logical viewport, primary menu ownership, input and console. Native PlayerPawn
layout checks passed in both runs. All 36 native phase captures were saved.

The initial split map was DM-Oblivion with eight bots. Menu-launched sessions
used DM-Halo-Derelict and the normal menu match defaults. Two-player mode retains
four viewport objects while active but marks two inactive; exits removed the
children and left exactly one viewport as expected.

Visually reviewed frontend and single-player captures from every cycle in both
runs. The 2P cycle-two single-player capture caught a LOADING overlay during the
next transition, so that image alone is not a clean settled-gameplay proof;
the input/layout checks passed before travel and the following frontend capture
was normal. This is not an every-frame visual review or a long-duration soak.

Evidence: `build_cli/split_exit_verified_4p_wide/01_DM-Oblivion/` and
`build_cli/split_exit_verified_2p_standard/01_DM-Oblivion/` (logs, summaries and
screenshots). Compact retained results: [measurements](SPLIT_EXIT_MEASUREMENTS_2026-09-12.json).
Canonical Release build succeeded; default.xbe SHA256:
`0ec7e531536e0439042cf3211d6c2a402c5250294afbc5debbc4ec62c13838b8`
(matches the retained smoke-stage executable).

No additional production exit fix was needed. The repeat proof is opt-in and
uses only process-local input. Test emulators were closed after completion.
These results qualify the exercised Xemu paths; real-hardware behavior was not
tested in this run.

Accepted and closed by Steve on 2026-09-12. UT99-OPEN-005 was removed from
OPEN_ITEMS.md and the remaining entries renumbered.
