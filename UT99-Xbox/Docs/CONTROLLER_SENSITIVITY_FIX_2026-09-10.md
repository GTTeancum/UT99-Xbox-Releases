# Frame-independent Xbox controller input

Accepted and closed at the user's request on 2026-09-10 (UT99-OPEN-002).
This acceptance does not represent an additional hardware test.

Gameplay stick input now runs once per simulation input read, with analog
displacement scaled by the same DeltaSeconds that Unreal's input system uses.
The client render tick performs a fallback menu poll only when simulation has
not already polled that viewport. Fallback polls cannot queue gameplay axes or
buttons for the next simulation tick.

The reference scale is 60 samples per simulation second, preserving the old
single-player baseline of two samples at 30 ticks per second at normal game
speed. This avoids halving the baseline when the duplicate poll is removed.
Sensitivity, dead zone, stick layout and inversion still use the existing
profile settings. Mouse and keyboard input code is unchanged. Game speed and
the existing zoom/FOV multiplier still apply.

## Measured pawn rotation

Same final Release binary, DM-Oblivion, eight bots, FOV 90, Xemu 480p. The
process-local proof supplies a fixed stick and reads actual ViewRotation changes
after PlayerTick. These are degrees per simulation second, not estimates from
screen pixels. No host controller, keyboard or mouse input is generated.

| Local players | Full stick, sensitivity 100 | Full stick, sensitivity 50 | Half stick, sensitivity 100 | Pitch, sensitivity 100 | Observed simulation FPS |
|---|---:|---:|---:|---:|---:|
| 1 | 298.47 | 149.23 | 149.23 | 151.60 | 47.2–52.9 |
| 2 | 298.50 | 149.22 | 149.22 | 151.64 | 37.9–53.3 |
| 4 | 298.57 | 149.24 | 149.24 | 151.77 | 23.7–36.0 |

All active players and all four phases passed a 2% absolute tolerance. The
observed yaw difference between player counts is below 0.04%; pitch differs
by less than 0.12%. Integer rotator quantization accounts for small differences
with different tick rates. Pitch is reset to zero before each proof tick to
avoid measuring against the pitch limit; this is not a test of natural pitch
clamping. The half-stick case is approximately half travel after dead-zone
remapping, not half raw controller travel.

The compiled production scheduling/scaling test also covers 1/2/4 players at
15, 20, 30, 40, 60 and 120 FPS, plus paused fallback polling and resume. It
verifies a single gameplay poll per tick and constant integrated rotation.

## Evidence and qualification details

- `Tools/test_controller_timing.py`: compiled production methods and scaling
  expression, with the engine axis-scaling/movement interface modeled.
- `Tools/summarize_sensitivity_proof.py`: validates every active port and phase,
  measured rates and cross-axis movement; rejects missing evidence.
- [Raw measurements](CONTROLLER_SENSITIVITY_MEASUREMENTS_2026-09-10.json):
  15 single-player windows, 31 two-player windows and 61 four-player windows.
- Local evidence: `build_cli/sensitivity_verified_1p`,
  `sensitivity_verified_2p`, `sensitivity_verified_4p`.
- An earlier 1P attempt used a small, infrequent RAM-log snapshot and was stopped
  without qualification. The final runs use 64 KiB snapshots at one-second
  intervals. The original 2P harness result rejected the allocated viewport
  count of four; two objects were inactive dummies. The corrected analyzer
  validates the native activePlayers=2 record and both active ports. The original
  rejection remains in its raw summary; no failed measurement was discarded.
- An earlier 4P run also passed, but the table uses the final binary's repeat.
- `build_cli/sensitivity_transitions`: sensitivity marker disabled; widescreen
  4P, pause, resume, frontend and subsequent single-player all passed on the
  final build. All five native screenshots were inspected. The single-player
  transition capture is the pre-match screen. The test invokes in-game menu
  functions; physical controller menu navigation is not covered by that proof.

Canonical `build/default.xbe` SHA-256:
`17cc369bdd870940e1ed980b5922499b717286f4020a9aa1fec56bbeaf0489bd`.
Full Release build with Jailbreak succeeded. The proof marker is opt-in and
absent from the canonical build. Hardware controller feel and latency still
require user review; this qualification establishes the input rate correction.
