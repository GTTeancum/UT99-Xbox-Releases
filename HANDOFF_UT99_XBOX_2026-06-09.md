# UT99 Xbox Handoff - 2026-06-09

## Current Goal

Use XEMU LLE smoke testing to get the UT99 Xbox port running well: crashes fixed, sound working, console-efficiency improvements implemented, menus and in-match bot play smoke-tested, and enough RAM recovered to make 3D player previews realistic again.

Steve's constraints are important:

- Do not visually downgrade the game for "optimization."
- Keep the CityIntro flythrough/menu experience.
- Keep music working.
- Real OG Xbox hardware is the final target; XEMU is the LLE smoke-test loop.
- Close XEMU when each session is done.
- Prefer console efficiency and cleanup over disabling features.

## Repo And Build State

Repo: `C:\Programming\GitHub\UnrealTournament_1.40`

Latest built XBE:

- `UT99-Xbox\build_cli\release\default.xbe`
- Built successfully on 2026-06-09 around 1:04 AM.

Current XEMU ISOs:

- `UT99-Xbox\build_cli\ut99_xemu_frontend_trim.iso`
- `UT99-Xbox\build_cli\ut99_xemu_match_smoke.iso`

The working tree is intentionally dirty. Do not reset it. There are many existing project changes from earlier work; preserve them unless Steve explicitly asks otherwise.

## What Changed Most Recently

### CityIntro frontend memory trim

The current theory is that CityIntro was loading too much Botpack player-family content during map load because `LevelInfo.DefaultGameType` pointed at `Botpack.UTIntro`. Hardware logs showed broad mesh loads during `LoadLevel`, before the normal game init/login path:

- `Botpack.Bot`
- `Botpack.Commando`
- `Botpack.Soldier`
- `Botpack.FCommando`
- `Botpack.SGirl`

That means `Game=Engine.GameInfo` in the startup URL is not sufficient by itself, because the expensive class defaults can be pulled while deserializing the map package.

Added tool:

- `UT99-Xbox\Tools\patch_cityintro_frontend.py`

It validates and patches only the staged `CityIntro.unr` copy:

- Offset: `33273`
- Old compact ref bytes: `dc02` (`Botpack.UTIntro`)
- New compact ref bytes: `f401` (`Engine.LevelInfo`)

This is a staging/package patch, not a source-data edit. Original game data should remain untouched.

### Lightweight CityIntro player spawn

Changed:

- `Engine\Src\UnLevAct.cpp`

`ULevel::SpawnPlayActor` now detects Xbox CityIntro/frontend first and skips normal player-class resolution for that path. This matters because resolving `URL.Class` / `DefaultPlayer.Class` was still capable of loading a normal player mesh/skin before the lightweight spectator path.

The frontend path now:

- Detects `UTIntro` or URL map `CityIntro` / `CityIntro.unr`.
- Loads `Botpack.CHSpectator`, falling back to `Engine.Spectator`.
- Finds `Botpack.SpectatorCam` actors.
- Spawns a hidden spectator at the last camera, sets `ViewTarget`, and logs camera count/target.

This should preserve the flythrough camera behavior while avoiding the heavy `UTIntro` game-default path.

### Earlier important related work still present

These were already in the tree before the latest handoff:

- `Engine\Src\UnGame.cpp`
  - Skips hidden `Entry` warm-up level on Xbox.
  - Adds extensive `XboxMemMark` logging.
  - Uses lightweight frontend startup options when no `XboxStartURL.ini` override is present.
- `UT99-Xbox\XboxAudio\src\XboxAudio.cpp`
  - Music path has been actively worked on; `Uttitle.wav` in `MusicXbox` loaded in hardware testing at one point.
- `UT99-Xbox\XboxDrv\src\XboxViewport.cpp`
  - Player setup currently uses placeholder head/skin textures for preview on Xbox, not live 3D actors.
  - This was a stability/memory compromise; Steve ultimately wants full-body images or restored 3D preview if RAM allows.

## XEMU Setup That Worked

Do not use the old `xemu_ut99.exe` helper for automated testing. It injects stale launch parameters and caused duplicate HDD drive-index errors.

Working path:

- Isolated instance: `C:\Games\Emulators\Xemu\UT99Codex\xemu.exe`
- Config: `C:\Games\Emulators\Xemu\UT99Codex\xemu.toml`

Working launch pattern:

```powershell
$inst = 'C:\Games\Emulators\Xemu\UT99Codex'
$config = Join-Path $inst 'xemu.toml'
$iso = 'C:/Programming/GitHub/UnrealTournament_1.40/UT99-Xbox/build_cli/ut99_xemu_frontend_trim.iso'
$port = 4477

$p = Start-Process `
  -FilePath (Join-Path $inst 'xemu.exe') `
  -ArgumentList @('-config_path', $config, '-dvd_path', $iso, '-monitor', "tcp:127.0.0.1:$port,server,nowait") `
  -WorkingDirectory $inst `
  -PassThru `
  -WindowStyle Hidden
```

Clean shutdown through monitor:

```powershell
$m = [Net.Sockets.TcpClient]::new('127.0.0.1', $port)
$s = $m.GetStream()
$b = [Text.Encoding]::ASCII.GetBytes("quit`n")
$s.Write($b, 0, $b.Length)
$m.Close()
```

XEMU monitor does **not** support `screendump` in this build. Non-focused keyboard posts did not trigger XEMU screenshots either. Need another approach for internal screenshots.

## Smoke Test Results

### Frontend boot/menu soak

ISO:

- `UT99-Xbox\build_cli\ut99_xemu_frontend_trim.iso`

Result:

- XEMU LLE stayed alive for 180 seconds.
- Clean shutdown through monitor succeeded.
- No stale XEMU process remained.

Output files:

- `UT99-Xbox\build_cli\xemu_frontend_trim_soak.out.txt`
- `UT99-Xbox\build_cli\xemu_frontend_trim_soak.err.txt`

### Direct match/bot smoke

ISO:

- `UT99-Xbox\build_cli\ut99_xemu_match_smoke.iso`

This ISO temporarily included root `XboxStartURL.ini`:

```ini
StartURL=DM-Deck16][.unr?Game=Botpack.DeathMatchPlus?FragLimit=0?TimeLimit=0?MinPlayers=2?MaxPlayers=4?Difficulty=1?Name=SmokeP1?Class=Botpack.TMale2?team=0?skin=SoldierSkins.blkt?Face=SoldierSkins.Othello?Voice=BotPack.VoiceMaleTwo
```

Result:

- XEMU LLE stayed alive for 240 seconds.
- Clean shutdown through monitor succeeded.
- Staged `XboxStartURL.ini` was removed afterward so default frontend packaging does not inherit it.
- XEMU stderr included `dsound: warning: Voice is not playing`; not enough by itself to diagnose game audio.

Output files:

- `UT99-Xbox\build_cli\xemu_match_smoke.out.txt`
- `UT99-Xbox\build_cli\xemu_match_smoke.err.txt`

## Logging Notes

Hardware log path Steve updates:

- `C:\Programming\GitHub\UnrealTournament_1.40\UT99-Xbox\build_cli\release\ut99.log`

XEMU ISO runs do not currently produce an easy repo-side `ut99.log`, likely because `D:\` is the mounted disc. `FXboxLogger` has fallback paths including `E:\ut99.log`, and the XEMU HDD timestamp changed during runs, so the log may be written into the virtual HDD. No reliable extraction path is set up yet.

There is an old UDP trace file:

- `UT99-Xbox\build_cli\release\xemu_udp_debug.log`

But the current source search did not find the old `UTDBG` sender text in the active modules. Do not assume live UDP logging is still present.

## Immediate Next Steps

1. Get a reliable XEMU-visible log path.
   - Best options: revive a tiny UDP debug mirror, extract `E:\ut99.log` from the XEMU HDD, or add a controlled host-write path only for XEMU/testing.
   - Avoid spending too much time on generic qcow2 tooling unless it looks straightforward.

2. Have Steve hardware-test the latest `default.xbe` plus staged data.
   - Need to confirm whether CityIntro now avoids loading `Commando`, `Soldier`, `FCommando`, `SGirl` during frontend load.
   - Need exact `XMEM` deltas after `LoadLevel`, `InitGameInfo`, `SpawnPlayActor`, and `InitAudio`.

3. Confirm music.
   - User specifically cares that music works.
   - `MusicXbox\Uttitle.wav` exists and has worked/load-started before, but hardware behavior needs current confirmation.

4. Restore/improve player setup preview only after memory headroom is proven.
   - Current Xbox path draws placeholder textures.
   - Steve does not want permanent headshot placeholders; he wants full-body shots or 3D preview if feasible.
   - If 3D preview returns, keep a strict one-current/one-previous asset policy and test Skaarj/Nali/Warcow carefully.

5. Repeat XEMU smoke with more maps.
   - Frontend boot/menu.
   - Direct DM with bots.
   - At least one CTF and one larger map.
   - Close XEMU after each session.

6. Commit only when the current package/code direction is confirmed.
   - There are many dirty files; stage carefully.
   - Do not include generated ISOs or transient staging files.

## Known Risks / Caveats

- The CityIntro patch points `DefaultGameType` at `Engine.LevelInfo` as a harmless placeholder because the URL supplies `Game=Engine.GameInfo`. If a path ever relies on the map's `DefaultGameType` without a URL game override, it may behave differently.
- The lightweight spectator path finds `Botpack.SpectatorCam`; if the camera count is zero in hardware logs, the flythrough will be wrong or static.
- XEMU smoke only proved "process alive for N seconds"; without screenshots/log extraction, it did not prove visual correctness.
- Current player setup 3D preview remains disabled on Xbox via placeholder texture path.

## Useful Commands

Build XBE:

```powershell
python UT99-Xbox\Tools\build_xbox_cli.py
```

Patch staged CityIntro:

```powershell
python UT99-Xbox\Tools\patch_cityintro_frontend.py UT99-Xbox\build_cli\xemu_staging\Maps\CityIntro.unr
```

Rebuild frontend ISO:

```powershell
$stage = Resolve-Path 'UT99-Xbox\build_cli\xemu_staging'
$isoPath = Join-Path (Resolve-Path 'UT99-Xbox\build_cli') 'ut99_xemu_frontend_trim.iso'
if (Test-Path -LiteralPath $isoPath) { Remove-Item -LiteralPath $isoPath -Force }
& 'UT99-Xbox\build_cli\tools\extract-xiso\artifacts\extract-xiso.exe' -m -c $stage $isoPath
```

Check for stale XEMU:

```powershell
Get-Process | Where-Object { $_.ProcessName -like 'xemu*' }
```
