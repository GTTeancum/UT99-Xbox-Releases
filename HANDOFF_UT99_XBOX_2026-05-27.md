# UT99 Xbox Handoff - May 27, 2026

This is the fast-start handoff for continuing the UT99 original Xbox source
port in a new chat.

## Project And Working Rules

- Repo: `C:\Programming\GitHub\UnrealTournament_1.40`
- Xbox project: `UT99-Xbox\`
- Build command: `python UT99-Xbox\Tools\build_xbox_cli.py`
- Current CXBX test install: `C:\Games\Emulators\CXBX\UT99x`
- Current game log: `C:\Games\Emulators\CXBX\UT99x\ut99.log`
- Built XBE output: `UT99-Xbox\build_cli\release\default.xbe`
- Deploy XBE after successful build:
  - `Copy-Item UT99-Xbox\build_cli\release\default.xbe C:\Games\Emulators\CXBX\UT99x\default.xbe -Force`
- Always check logs first when Steve says logs are updated.
- Steve strongly dislikes repeated empirical guessing. Audit code and known-good references before asking for another test.
- Build by command line before handing Steve a test candidate.
- Real Xbox hardware is the ultimate target; CXBX-R is the debug path.

## Git State At Handoff

Latest pushed/committed work on `main`:

- `9f6b457 Fix masked texture alpha on Xbox`
- `53e5318 Update Xbox menu prompts and voice wheel polish`
- `6be40bd Add Xbox weapon wheel visuals`
- `2e8680e Stabilize split-screen pause and frontend return`
- `b484c04 Wire Xbox menus and split-screen controls`
- `405fd16 Sanitize Xbox default player URL`
- `121dced Fix player setup preview framing`
- `cd7285b Add player setup preview and voice test`
- `d749c20 Add Xbox menu player setup`
- `e99c2cf Optimize Xbox rendering to 60 FPS`
- `d72851c Optimize Xbox gameplay rendering`
- `79accbc Fix Xbox render projection and texture stability`

`main` was pushed to GitHub after `9f6b457`.

Known untracked local items at handoff:

- `!ORIGINAL_SRC/`
- `CXBXR/`
- `UT99-Xbox/CTF-Face_Screenshot.png`
- `UT99-Xbox/Docs/`
- `UT99-Xbox/snapshots/`
- `Unreal Tournament Xbox UI mockup.zip`
- `data/`
- `xbox_hardware_resource_audit.md`

Do not accidentally `git add --all` these. Steve clarified that "commit dirty
files" means tracked files only unless explicitly stated otherwise.

## What Currently Works

- UT99 boots on Xbox/CXBX-R through the Xbox-specific launch, render, audio,
  input, menu, and split-screen paths.
- Hardware-accelerated D3D8 rendering is active.
- Gameplay can hold roughly 60 FPS in tested maps after the render optimization
  phase.
- Textures render with corrected projection/UVs.
- Masked/alpha textures such as vines are fixed as of `9f6b457`.
- Audio works, including music and voiceover behavior in CityIntro/main menu.
- Start opens the Xbox-style main menu from CityIntro and gameplay.
- Gameplay pause menu exists:
  - `RESUME`
  - `MAIN MENU`
  - `SETTINGS`
- In single-player matches, gameplay and music pause while the pause menu is
  open. CityIntro is not frozen like a match.
- Instant Action menu is functional with map thumbnails and mutator overlay.
- Mutator overlay scrolls and supports multi-select.
- Settings menu has sliders/previews for sensitivity, deadzone, volumes,
  crosshair, HUD color/opacity, etc.
- Player Setup menu has a 3D player preview and voice sample playback.
- Weapon wheel is functional using rendered side-profile weapon sprites.
- Split-screen works enough for P1 movement/combat and dummy players; later
  work wired controller port == viewport index.
- System Link has only a discovery/probe framework, not full gameplay netcode.

## Important Recent Fixes And Why They Matter

### Masked Texture / Vines Fix

Commit: `9f6b457 Fix masked texture alpha on Xbox`

Problem:

- Some textures that should have been masked alpha rendered as black rectangles.
- The renderer could do masked alpha, but the Xbox-compatible `UTexture` layout
  no longer exposed package-authored flags such as `bMasked` through
  `UTexture::PolyFlags()`.

Fix:

- `Engine\Src\UnTex.cpp` now synthesizes `PF_*` flags from the loaded texture
  bool properties when no side-table override exists.
- `PolyFlagsRef()` initializes side-table values from those synthesized flags.
- Xbox texture cache tracks a `MaskedAlpha` variant so P8 textures uploaded
  opaque first can be re-uploaded with palette index 0 as alpha 0.
- World and skin texture LOD are currently forced to full detail (`0`) in
  `UT99-Xbox\XboxDrv\src\XboxClient.cpp`.

Follow-up from audit:

- `UTexture::Destroy()` still does not remove its side-table entry. Fix that
  next for long-play safety.

### Render Optimization Phase

Major wins:

- Dynamic world/tile batching.
- `DrawPrimitiveUP` path accepted and removed heavy dynamic VB lock churn.
- D3D state cache.
- Texture upload/cache cleanup.
- FPS is now effectively engine/frame capped near 60 in tested maps.

Current expectation:

- Keep FPS counter always visible.
- If performance regresses, check `PERF` lines in `ut99.log`.

### Menu/UI Phase

Current UI direction:

- Main menu style is the gold standard for fonts and footer prompts.
- Green A and red B button images should accompany contextual footer actions.
- Button images were replaced with Duke-style buttons from
  `xbox_duke_buttons.png`.
- Menu background uses live CityIntro/PC-ish background behavior for now.
- Main menu should keep CityIntro music, but suppress VO while menu is open.
- If CityIntro loops while menu is open, VO should remain suppressed.

Known CXBX artifact:

- The first-character glitch in `A / SELECT` was confirmed as CXBX-R-only and
  does not occur on hardware.

## Current Open Work

### 1. Long-Play / Stale Resource Audit

New document:

- `xbox_hardware_resource_audit.md`

Purpose:

- Steve asked to rewrite this for UT99 Xbox and specifically look for stale data
  that could hurt prolonged play.

Top findings:

- `UTexture` polyflag side table is keyed by `UTexture*`; `UTexture::Destroy()`
  should remove its entry.
- Static menu XUI texture cache (`GXboxMenuTextures[32]`) has no explicit
  release/clear path.
- Audio buffers can duplicate raw `USound::Data`; consider unloading raw data
  after DirectSound buffer creation and storing sample rate.
- Music `Music->Data.Add(1024)` needs checking: make sure repeated starts do not
  mutate persistent bulk data.
- Split-screen runtime must reset on every non-split travel/frontend return.
- System Link probe should close socket and clear peers whenever leaving the
  screen or traveling.
- Config writes should be batched when leaving menus rather than flushed on
  every slider/input change.

Suggested next implementation order:

1. Remove stale texture side-table entries in `UTexture::Destroy()`.
2. Add transition resource snapshot logging.
3. Add explicit menu texture cache count/release policy.
4. Audit audio raw data duplication.
5. Verify music failure cleanup and padding behavior.
6. Keep split-screen/System Link cleanup explicit on transitions.

### 2. System Link

Current state:

- Probe exists for UDP broadcast discovery.
- It is not full UT gameplay network integration.
- CXBX-R testing can run two instances on the same machine if they use separate
  game folders, e.g.:
  - `C:\Games\Emulators\CXBX\UT99x_A`
  - `C:\Games\Emulators\CXBX\UT99x_B`

Expected probe behavior:

- Enter `SYSTEM LINK` on both instances.
- One binds port `9777`, the other should bind `9778` or another probe port.
- Logs should show `XSL peer discovered ...`.

Caveat:

- Full System Link gameplay still needs proper Xbox secure-session and Unreal
  net-driver integration.

### 3. Split-Screen

Current state:

- Viewport index must equal controller port:
  - controller 1 -> viewport 1
  - controller 2 -> viewport 2
  - controller 3 -> viewport 3
  - controller 4 -> viewport 4
- If a controller is absent, that viewport defaults to dummy behavior.
- Steve confirmed P1 can move around and kill dummy players.
- Split-screen pause variant exists but may need polishing.

Important:

- Keep split-screen totally siloed from single-player.
- Do not leave extra viewports during normal map travel/frontend.

### 4. Controls / Weapon Wheel

Current direction:

- Weapon wheel on Black/RB hold.
- Tap Black/RB cycles weapons.
- Wheel arrests right-stick look only; player should still walk/jump/etc.
- Scoreboard and pause controls should be blocked while wheel is open.
- Y should become dodge + direction eventually.
- Taunts can go on D-pad.

Weapon wheel visuals:

- 16 equal slices, top three omitted, matching Steve's provided template.
- No numbers on slices.
- Use weapon silhouettes/sprites, not boxes.
- Current committed state was accepted and then locally committed.

### 5. Player/Menu Polish

Current state:

- Player Setup moved near Settings on main menu.
- Player preview should show the full character facing screen, not spinning.
- Voice sample plays on voice changes.

Potential future polish:

- More visual Photoshop work on menu backgrounds.
- Main menu and Instant Action can be refined, but functionality is acceptable.

## Critical Files To Know

Render:

- `UT99-Xbox\XboxRender\src\XboxRender.cpp`
- `UT99-Xbox\XboxRender\inc\XboxRender.h`
- `Engine\Src\UnTex.cpp`
- `Engine\Src\UnGame.cpp`

Input/menu/split/system link:

- `UT99-Xbox\XboxDrv\src\XboxViewport.cpp`
- `UT99-Xbox\XboxDrv\src\XboxClient.cpp`
- `UT99-Xbox\XboxDrv\inc\XboxDrv.h`

Audio:

- `UT99-Xbox\XboxAudio\src\XboxAudio.cpp`

Launch/platform:

- `UT99-Xbox\XboxLaunch\src\XboxLaunch.cpp`
- `UT99-Xbox\XboxLaunch\src\XboxEngine.cpp`
- `UT99-Xbox\XboxLaunch\inc\FFileManagerXbox.h`
- `UT99-Xbox\XboxLaunch\inc\FXboxLogger.h`

Build/tools/assets:

- `UT99-Xbox\Tools\build_xbox_cli.py`
- `UT99-Xbox\Tools\patchxbe.py`
- `UT99-Xbox\Tools\build_menu_assets.py`
- `UT99-Xbox\Tools\render_weapon_wheel_assets.py`

Ongoing notes:

- `UT99-Xbox\RENDER_ATTEMPTS_2026-05-16.md`
- `xbox_hardware_resource_audit.md`

## Logging And Testing Habits

Steve often says "log updated"; always inspect:

- `C:\Games\Emulators\CXBX\UT99x\ut99.log`

Useful log prefixes:

- `PERF` - frame/render performance summary.
- `XMENU` - menu state and UI operations.
- `XSPLIT` - split-screen setup/reset/input.
- `XSL` - System Link probe.
- `XboxAudio` - audio init/music/sound events.
- `RTEX` / `RTEXUP` / `RDRAW` - render texture/draw diagnostics.

Keep logs quiet unless debugging. Steve asked several times to clean log noise.

## Build/Deploy Snippets

Build:

```powershell
python UT99-Xbox\Tools\build_xbox_cli.py
```

Deploy to current CXBX install:

```powershell
Copy-Item -LiteralPath 'C:\Programming\GitHub\UnrealTournament_1.40\UT99-Xbox\build_cli\release\default.xbe' -Destination 'C:\Games\Emulators\CXBX\UT99x\default.xbe' -Force
```

Check log:

```powershell
Get-Content 'C:\Games\Emulators\CXBX\UT99x\ut99.log' -Tail 200
```

Check tracked dirt only:

```powershell
git status --short --untracked-files=no
```

## Next Best Task

Start the new chat by opening `xbox_hardware_resource_audit.md`, then implement
the first concrete cleanup:

- remove `UTexture*` entries from the texture polyflag side table in
  `UTexture::Destroy()`;
- add a cheap side-table count or transition snapshot if feasible;
- build via command line;
- deploy XBE for Steve to test.

This is low-risk, directly tied to the masked-texture fix, and exactly aligned
with the current long-play concern.
