# Widescreen UI presentation - 2026-09-12

UT99-OPEN-010. The 640x480 widescreen framebuffer has a horizontal pixel aspect
of 4/3. The existing world projection compensated for this, but Canvas tiles,
font glyphs and native menu primitives did not. Their displayed shapes stretched
horizontally. Split-screen video settings also used fixed full-screen columns
and eleven unscrolled rows, which did not fit smaller viewports.

The post-render 2D pass now exposes square-pixel horizontal Canvas coordinates
and converts those coordinates to framebuffer pixels when drawing. Tile and
font clipping use the same coordinate space. Native menu rectangles, textures
and weapon-wheel rings use that conversion too, and native 3D menu preview slots
convert their bounds back to framebuffer coordinates. World frames, first-person
DrawActor projection, framebuffer resolution and split-screen regions remain
at their existing dimensions. The coordinate adjustment ends after the UI pass.

Video settings switch to a compact scrollable list in short or narrow viewports.
The selected row remains visible, values have their own column, and the footer
stays below the list. The full-size preview remains on large viewports.

The opt-in native presentation proof captures gameplay and video-settings rows
0, 6 and 10. Together those menu views cover all eleven settings. A cyan 20x20
test square in gameplay exercises the same native drawing path used by menus.
RAM assertions check Canvas-to-framebuffer width and pixel-aspect compensation
per rendered viewport. All input and capture remain emulator-local.

Release build passed. Final matrix: 1P, 2P and 4P at both explicit 4:3 and 16:9
emulator aspect settings. All 56 viewport/phase assertions passed. All 24 native
captures were individually inspected. The fourteen reference squares measured
20x20 or 21x20 displayed pixels (one-pixel resampling tolerance), confirming
the horizontal stretch was removed. Tests used the existing 92% picture margin.
The 4:3 HUD keeps its existing coordinates and filtering.

The first exploratory 4P wide run exposed narrow-font stroke loss and menu
background interference. Widescreen font filtering and a dark compact-menu panel
were added before the final matrix. That run also inherited a combat-duration
assertion despite intentionally pausing in menus; presentation mode now uses its
own checks. build_cli/presentation_4p_wide is exploratory, not final evidence.

Final matrix evidence: build_cli/presentation_verified_{1,2,4}p_{wide,standard}/
01_DM-Oblivion. Retained numeric results:
[measurements](WIDESCREEN_UI_MEASUREMENTS_2026-09-12.json).
Build SHA256:
`cf1edac277fd532750c4c7cebad378d4c9d5583b145037f26c73ade707f55b74`.

Production-helper tests also passed for video-mode selection (480p preference,
480i fallback, HD/50Hz rejection and enumeration failure) and all fifteen
player-slot layout masks, including projection reset on split exit. Mode
selection code is unchanged by this UI work. Earlier native 480i/480p evidence
is recorded in [dashboard video qualification](DASHBOARD_VIDEO_2026-09-10.md).

The first three-cycle transition run passed its ownership/input/layout checks,
but visual inspection caught legacy Console.DrawLevelAction centering its
LOADING label with the physical Console.FrameX. Console PostRender now exposes
the same square-pixel width as Canvas and restores the physical value afterward.
The native loading spinner also remains inside the corrected 2D pass.
build_cli/presentation_transition_2p_wide records this intermediate run; it does
not qualify final loading-label alignment. The matrix above predates this
console-only follow-up; its build hash is retained in the measurements.

Final Release build and repeated transition regression passed. Three cycles of
2P widescreen -> pause -> resume -> main menu -> single-player -> main menu
passed all 18 phase checks and nine ownership/input checks. Each single-player
session registered input movement. Native PlayerPawn layout checks passed.
The pause notice's corrected alignment and the main menus were visually checked;
the first two single-player captures show full-width gameplay. Capture 17 arrived
after the third session had returned to the frontend, so that session's gameplay
qualification rests on its runtime checks. No final LOADING-label image was
captured; pause uses the same Console.PrintActionMessage centering function.

Final evidence: build_cli/presentation_transition_final_2p_wide/01_DM-Oblivion.
[Transition measurements](WIDESCREEN_TRANSITION_MEASUREMENTS_2026-09-12.json).
Final canonical default.xbe SHA256:
`57a1fc04d448f116cfa54abc8b16e94803834041ff3886367ae380cddd1c4ec2`.
All test emulators were closed. Whole-picture safe-zone tests passed 2,880 cases
and production teardown tests passed too. Real-Xbox/TV presentation has not been
tested in this run. The software changes are ready for review.

## Acceptance

Steve accepted and closed widescreen presentation (UT99-OPEN-010) on 2026-09-12.
The item was removed from OPEN_ITEMS.md. This acceptance does not change the
verification scope above: real-Xbox/TV presentation was not tested in this run.
