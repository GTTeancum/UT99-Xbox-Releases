# Per-profile HUD preferences - 2026-09-11

Crosshair selection, crosshair color, HUD color and HUD opacity now use the
owning XboxProfile section. Existing control/weapon preferences retain their
per-player implementation. Shared display/audio and mature-language settings
are outside this change.

Older profiles receive a one-time snapshot of legacy HUD settings, clamped to
the supported menu ranges and palette. Subsequent edits write only that
profile, then apply to its live HUD. No HUD SaveConfig or HUD class-default
writes remain in the native settings handlers.

The renderer checks HUD/player/level/profile identity before pre-render and
before post-render. This restores preferences after travel and lazy HUD
creation without doing config/reflection work on ordinary gameplay frames.
The video menu refreshes its values from the owner's profile before drawing
or editing, preventing the previous owner's cached menu values from carrying
over.

Validation: canonical Release build succeeded and git diff --check passed.
No game, emulator, runtime tests, or desktop interaction was started.
Build log: UT99-Xbox/build_cli/profile_hud_build.log.
XBE SHA-256: c55f90b071a09e0314c3a7ead76c01a4e4c189565bea104b79db1808c71038ba

## Smoke follow-up - 2026-09-12

2P standard and 4P widescreen passed five phases: distinct initial preferences,
P2-only edits through the video settings handler, HUD destruction/recreation,
configuration eviction/read-back plus HUD recreation, and returning to P1's
video menu. Actual HUD values and saved profile values matched for every player
in all 30 phase/player combinations. Crosshair, both colors, and opacity were
checked. Captures also show a separate cramped/clipped split-screen video-menu
layout, recorded under the existing presentation item.

The first optical-disc run passed live isolation and HUD recreation but failed
persistence because D:\System\User.ini is on read-only DVD media. A marker-gated
file-manager redirect uses only U:\ProfileHUDSmoke.ini for this test; the same
production User.ini calls then passed storage reload. This does not add general
DVD save support. Raw failed evidence remains in build_cli/profile_hud_2p_standard.
Passing evidence: build_cli/profile_hud_writable_2p_standard and
build_cli/profile_hud_writable_4p_wide. Assertions are recorded in
PROFILE_HUD_SMOKE_2026-09-12.json.

The old nine-crosshair smoke was updated to drive the profile setter instead of
overriding only the live HUD. It seeds a selected test profile in the isolated
staging configuration. All nine preview selections passed; all 19 native captures across the three
passing runs were inspected individually. All UT smoke emulators were closed
after completion; the unrelated running emulator was left untouched.

Smoke-tested canonical XBE SHA-256:
79b894f25f9ce4dc62309a49977a6a0c491226cfcc722c155dd17fab7217ed75.

Accepted and closed by Steve on 2026-09-12 after smoke results. Removed
UT99-OPEN-003 from the open tracker.
