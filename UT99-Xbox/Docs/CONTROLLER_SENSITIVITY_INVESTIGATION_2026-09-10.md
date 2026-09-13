# Controller sensitivity investigation

Open item 1 / UT99-OPEN-002, investigated against commit `6dcc737`.
Investigation only: no runtime changes, rebuild, or emulator input test performed.

This describes the initial investigation. The subsequently authorized correction
and measurements are in [CONTROLLER_SENSITIVITY_FIX_2026-09-10.md](CONTROLLER_SENSITIVITY_FIX_2026-09-10.md).

## Findings

Two source-level defects explain why split-screen can feel much slower at the
same sensitivity. Neither depends on the recently corrected PlayerPawn layout.

### Single-player accumulates two stick samples per simulation frame

1. `Engine/Src/UnLevTic.cpp:406` calls the local player's `ReadInput(DeltaSeconds)`.
2. `Engine/Src/UnCamera.cpp:754` calls `UpdateInput(0)`.
3. `UT99-Xbox/XboxDrv/src/XboxViewport.cpp:17993` calls `PollController()`, which
   feeds stick axes through `ProcessControllerInput`.
4. Player input and movement execute; `ReadInput(-1.0)` clears the input axes.
5. After simulation, `Engine/Src/UnGame.cpp:3362` calls `Client->Tick()`.
6. `UT99-Xbox/XboxDrv/src/XboxClient.cpp:200` polls again only when split-screen
   is inactive. That sample survives until the next simulation input read,
   which adds another sample. There is no per-frame polling guard in PollController.

During steady gameplay with a held stick and connected controller, single-player
therefore consumes two samples per frame, while each real split player consumes
one. At the same FPS, profile values and field of view, this predicts approximately
twice the single-player rotation rate. Initial frames and menu transitions differ.

### Stick input is treated as per-frame displacement rather than a rate

- `XboxViewport.cpp:17832` computes stick value times sensitivity, without time.
- `XboxViewport.cpp:17894` feeds it as an IST_Axis event.
- `Engine/Src/UnIn.cpp:329` accumulates `0.01 * input * axisSpeed`.
- `Engine/Src/UnIn.cpp:446` scales axes by `20 / DeltaSeconds`.
- `PlayerPawn.uc:3444` applies the walking turn multiplier, 0.24.
- `PlayerPawn.uc:3185` adds `32 * DeltaTime * aTurn` to yaw.

The two time factors cancel. Each input sample produces a fixed angular step;
more frames per second produce more rotation per second. This is appropriate for
mouse displacement, but not for a held analog stick representing a turn rate.
Pitch uses the same pipeline, subject to pitch clamping. Movement axes also lack
time normalization, although movement clamping makes their symptoms different.

With equal stick, FOV and settings, the steady-state source prediction is:

`split rotation rate / SP rotation rate = split FPS / (2 * SP FPS)`

| Illustrative SP / split FPS | Split setting 100 corresponds to SP setting |
|---|---:|
| 30 / 30 | 50 |
| 40 / 30 | 37.5 |
| 60 / 30 | 25 |

These are arithmetic examples, not measured controller results. The 40/30 case
closely fits the report that split-screen 100 feels like single-player 40.

## Settings path and proposed correction

Single-player reads Client->ScaleRUV; split-screen uses the current port's cached
profile LookSensitivity. Controls changes save the context profile and reload
that port's cache (`XboxProfileSaveControlsForContext`, line 7807). The trace does
not establish an additional stale-cache defect; independent settings remain a
separate open item. PlayerInput also scales turning with DesiredFOV for zoom.

The correction should feed gameplay stick axes once per simulation input read,
using that read's DeltaSeconds, while preserving menu polling when simulation
is paused or unavailable. Normalize the Xbox analog input before Unreal's
inverse-time scaling; leave mouse and keyboard processing intact. Preserve an
explicit reference turn rate when choosing the scale, so removing duplicate
polling does not accidentally halve the intended sensitivity baseline.

Qualification should hold a fixed stick inside the targeted game process and
measure yaw degrees per second for 1P, 2P and 4P at several frame rates, identical
FOV, profiles and settings. Include pitch, partial-stick response, zoom, movement,
paused menus and split exit. This live measurement and a fix remain outstanding.
