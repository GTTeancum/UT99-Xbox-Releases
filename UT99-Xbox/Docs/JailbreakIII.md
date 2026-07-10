# Jailbreak III Gold Integration

Jailbreak III Gold is staged as third-party game content, not committed to this
source tree. The build tooling downloads the raw archive from Unreal Archive,
checks the published SHA1, extracts the gameplay folders, and patches the
runtime ini files in the selected build output.

## Build

```
build_xbox.bat jailbreak
```

Equivalent direct command:

```
python UT99-Xbox\Tools\build_xbox_cli.py --config Release --with-jailbreak
```

The archive is cached under `UT99-Xbox\build_cli\downloads`, which is ignored by
Git. To use a manually downloaded copy:

```
python UT99-Xbox\Tools\build_xbox_cli.py --with-jailbreak --jailbreak-archive C:\path\JailbreakIII-Gold-zip.7z
```

By default the Xbox build stages only `System`, `Maps`, `Textures`, and
`Sounds`. Add `--jailbreak-include-docs` if a full desktop-style payload is
needed.

## Standalone Staging

To add Jailbreak to an existing CXBX-R or hardware mirror without rebuilding:

```
python UT99-Xbox\Tools\stage_jailbreak.py --target C:\Games\Emulators\CXBX\UT99x
```

The staging step adds the mod's server package entries to existing
`System\Default.ini` and `System\UnrealTournament.ini` files. Delete a stale
runtime `UnrealTournament.ini` before testing config changes if you want the
engine to regenerate it from `Default.ini`.

## Smoke Test

`UT99-Xbox\XboxStartURL.jailbreak.sample.ini` can be copied to the runtime root
as `XboxStartURL.ini` to boot straight into a low-memory Jailbreak map:

```
StartURL=JBTutorialMap.unr?game=JailBreak.JailBreak?MinPlayers=0?Difficulty=1
```

Use this only as a quick load smoke. It is not representative soak coverage:
the tutorial map is tiny and the sample URL intentionally loads no bots.

## Current Validation Notes

The tutorial-map smoke previously proved only that Jailbreak could load. It is
not accepted as soak evidence. Real soak coverage must use larger Jailbreak maps
with bots loaded and must run until the match ends or a failure is recorded.

Large-map bot settings used for validation:

```
?game=JailBreak.JailBreak?MinPlayers=8?InitialBots=7?GoalTeamScore=5.0?TimeLimit=15?MaxPlayers=16?Difficulty=2
```

Reliable bot evidence comes from the `SMOKE tick` fields `priBots`, `numBots`,
and `teamBots`; the older pawn-chain field named `bots` is not reliable for
Jailbreak.

Build command:

```
python UT99-Xbox\Tools\build_xbox_cli.py --config Release --with-jailbreak
```

CXBX-R evidence gathered on July 10, 2026:

- `JB-Talaeron-Gold` passed with 7 bots, match-ended at tick 28200, min free
  memory 2988 KB.
- `JB-Grudge` passed with 7 bots, match-ended at tick 24000, min free memory
  3068 KB.
- `JB-Complex` passed with 7 bots, match-ended at tick 49200, min free memory
  2732 KB.
- `JB-Alcatraz` loaded 7 bots and stayed stable, but recorded no scoring and no
  match-end marker before timeout. Treat it as a no-contact gameplay failure,
  not as a pass.

CXBX-R evidence folders:

- `UT99-Xbox\build_cli\jailbreak_cxbx_large_matrix_20260710_091832`
- `UT99-Xbox\build_cli\jailbreak_cxbx_large_matrix_resume_20260710_101358`

Xemu evidence gathered on July 10, 2026:

- `JB-Talaeron-Gold` passed with 7 bots in Xemu, match-ended at tick 51900,
  min free memory 2820 KB.
- A first `JB-Grudge` Xemu attempt reached tick 27000 with 7 active bots and
  live scoring, then the harness stopped before writing a final summary. This is
  not a pass.
- `JB-Grudge` and `JB-Complex` have been staged for the next Xemu parity pass
  without launching the emulator. The setup manifest includes per-map XISOs,
  copied no-extension configs, and launch command arrays using `-display none`.

Xemu evidence folders:

- `UT99-Xbox\build_cli\jailbreak_xemu_large_matrix_20260710_110654`
- `UT99-Xbox\build_cli\jailbreak_xemu_setup_remaining_maps`

Current pass criteria:

- `Game class is 'JailBreak'`
- `Running Jailbreak JB3 SP2`
- `XboxRender::Init: CreateDevice OK`
- `priBots=7`, `numBots=7`, and nonzero `teamBots` on the active teams.
- Bots must leave waiting/spectator state: `priSpec=0` and `priWait=0`.
- For gameplay soak, scores or deaths must move and a `SMOKE match-ended` marker
  must be recorded.
- No matched fatal/error/OOM lines.
- Xemu parity is required before marking the integration complete.

Automation:

- `UT99-Xbox\Tools\run_jailbreak_soak.py` runs CXBX-R large-map bot soaks.
- `UT99-Xbox\Tools\run_jailbreak_xemu_soak.py` stages Xemu configs and XISOs.
  It is setup-only by default and will not launch Xemu unless `--launch` is
  explicitly passed.
- Setup-only mode now stages every selected map, not just the first one, and
  writes `setup.json` with the exact command array that would be used for each
  map.
- `UT99-Xbox\Tools\update_xemu_soak_config.ps1` directly edits the Xemu config
  files with PowerShell `Set-Content`. Use it when only the TOML/runtime config
  needs to be updated; it does not launch Xemu or invoke file associations.
- The Xemu runner writes its generated config as `xemu_soak_runtime_config`
  without a `.toml` extension to avoid shelling through the user's `.toml`
  editor association.

The required runtime fixes are:

- `GLazyLoad=1` on Xbox, so third-party package payloads stay demand-loaded
  instead of exhausting the 64 MB target before renderer creation.
- An Xbox-only `DynamicLoadObject(..., true)` fast path for optional objects in
  already-loaded packages. Jailbreak asks for optional map-local skin overrides;
  when the loaded package linker proves an export is absent, the runtime now
  returns `None` immediately and lets the mod fall back to its default texture.

`JB-Raid-Gold.unr` is still treated as a heavy content stress case for the
current 64 MB runtime. The safe automated smoke target is `JBTutorialMap`; the
accepted soak targets are larger bot-loaded maps that produce match-completion
evidence.
