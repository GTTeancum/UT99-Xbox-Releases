# Four-player dashboard presentation performance

All eight 90-second combat runs passed with four local players and eight bots.
The equal-map average was **30.43 FPS at 4:3** and **32.64 FPS with widescreen
pillarboxing**. The observed difference is +2.21 FPS (+7.3%), but a single pair
per map does not establish a causal speedup. HangEmHigh was slower pillarboxed.

| Map | 4:3 average FPS | Pillarboxed average FPS | 4:3 lowest window | Pillarboxed lowest window | 4:3 minimum free MiB | Pillarboxed minimum free MiB |
|---|---:|---:|---:|---:|---:|---:|
| Deck16][ | 31.01 | 36.27 | 21.87 | 31.64 | 17.90 | 19.77 |
| Oblivion | 27.72 | 29.26 | 24.66 | 25.60 | 20.22 | 20.24 |
| Morpheus | 32.66 | 37.97 | 25.81 | 27.98 | 19.12 | 20.11 |
| HangEmHigh | 30.33 | 27.08 | 23.97 | 20.03 | 13.36 | 13.03 |
| Equal-map mean | **30.43** | **32.64** | | | | |

## Method and limits

- Xemu, 640 x 480 progressive output, the same corrected Release XBE and
  optimized raster path in every run. Four players plus eight bots means
  twelve participants. The presentation order alternated between maps.
- Standard output used 320 x 240 quadrants. Widescreen output used 240 x 240
  quadrants inside the centered 480 x 480 region, with projection compensation
  for physical 4:3 presentation. Logs confirm 480p flags 0x40 and 0x50 respectively.
- FPS comes from native XFRAME wall-FPS records ending 20–75 seconds after
  combat starts. Approximately five-second windows are averaged, then each map
  is weighted equally. Loading, menus, pauses and single-player are excluded.
  The lowest window is not a per-frame minimum or a 1% low.
- Memory is the minimum available Xbox RAM observed by each run's soak
  summary, rather than a value restricted to the FPS sampling interval.
- No gameplay screenshots or transition proof ran during these measurements.
  One loading capture occurred before the measurement interval.
- These are emulator results, not retail-Xbox FPS. Dynamic combat and host load
  varied. Another game was active during the early Deck16 measurement period;
  it was not stopped by this task. Xemu host windows were 1280 x 960 for 4:3
  and 1280 x 720 for widescreen. These factors prevent attributing the measured
  difference solely to the smaller rendered region.
- The results support comparable overall performance for the proposed layout,
  but do not guarantee unchanged FPS on every map or scene. HangEmHigh's
  pillarboxed result was 3.25 FPS lower. Hardware confirmation remains open.

## Reproduction and build identity

The complete identities, timings, hashes and evidence paths are in
[DASHBOARD_4P_MEASUREMENTS_2026-09-10.json](DASHBOARD_4P_MEASUREMENTS_2026-09-10.json).
Local raw evidence and the run plan are under
`UT99-Xbox/build_cli/dashboard_perf_20260910/`.

Regenerate the summary with:

```powershell
python UT99-Xbox/Tools/summarize_dashboard_performance.py UT99-Xbox/build_cli/dashboard_perf_20260910/plan.json --output UT99-Xbox/Docs/DASHBOARD_4P_MEASUREMENTS_2026-09-10.json
```

The summarizer rejects failed combat proofs, mismatched map/binary identities,
wrong player/bot counts, wrong scan/layout flags, and capture/transition runs.

Canonical `build/default.xbe` SHA-256:
`5f1f9457974b3c66d29680f850dfbddce453d757f953f569f5f51956a7cffe71`.

Functional implementation and transition evidence are in
[DASHBOARD_VIDEO_2026-09-10.md](DASHBOARD_VIDEO_2026-09-10.md).
