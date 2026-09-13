# Match settings persistence - 2026-09-12

UT99-OPEN-006: the native match menu initialized its time-limit index to 2
(10 minutes) on every launch. Instant Action and split/System Link adjustments
changed the shared menu state without writing it to configuration. Starting a
match correctly consumed the selected value, but restarting discarded it.

The native menu now loads match settings once at viewport startup and flushes
changes immediately through the existing User.ini configuration path. It saves
bot count, skill, score limit, time limit (including zero/None), game class,
selected map per game class, and mutator selection. Map and mutator names are
stored instead of discovery indices. Missing content retains the normal menu
fallback; unsupported numeric values retain the existing defaults. Tournament
progress and profile preferences remain separate.

Sections: Xbox.MatchSettings, Xbox.MatchMaps and Xbox.MatchMutators.
Instant Action and split/System Link continue sharing their existing match
configuration. Changes are saved when adjusted, without requiring a match start
or graceful application shutdown.

Release build succeeded. The two-boot native proof uses the real menu adjustment
handlers on the first boot and only reads/asserts on the second boot. Optical
test media redirects User.ini to U:\MatchSettingsSmoke.ini under explicit proof
markers; only the write boot removes this dedicated test file. Production HDD
installations continue using their existing User.ini path. This does not add
general optical-disc configuration saving.

The initial exploratory run missed its one-time result marker because of the
log collector's short capture window and was stopped. It is retained under
build_cli/match_settings_write and is not counted as a passing test. The collector
now uses the larger RAM-log window already used by other native proofs.

Both final boots passed, in separate emulator processes. The first started with
the default 10-minute limit, then saved six bots, Masterful skill, score limit 30,
time limit None, DM-Zeto and Low Gravity. The second restored all six choices;
its log contains no match-settings save operation. Both native menu screenshots
were visually inspected and show the expected selections. Test emulators closed.

Evidence is under build_cli/match_settings_verified_write/01_DM-Oblivion and
build_cli/match_settings_verified_read/01_DM-Oblivion. Retained results:
[measurements](MATCH_SETTINGS_MEASUREMENTS_2026-09-12.json).
Release default.xbe SHA256:
`5606cc5348f4aec1377f58dcef401f926f3d7ce1db50cb000f57c034495d7f76`.

Reproduce using run_xemu_stress_matrix.py with --match-settings-proof write,
then --match-settings-proof read, retaining the same emulator HDD between runs.
Use separate evidence directories. These flags boot the frontend and select the
dedicated proof storage automatically. This verifies a fresh-process disk reload
in Xemu, not a real-hardware restart. Ready for review.

Accepted and closed by Steve on 2026-09-12. Removed UT99-OPEN-006 from the tracker.
