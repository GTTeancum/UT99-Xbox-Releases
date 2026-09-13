# Performance review — 2026-09-09

On 2026-09-10, the user accepted the four-map 4P performance result as
sufficient and requested closure of related open items. OPEN-001 and the
performance-specific follow-ups under OPEN-008 are closed by that acceptance.
The broader, unfinished code review remains open. This acceptance does not
claim new retail hardware measurements or close the other defects in GitHub #9.
The canonical output is `build/default.xbe`.

Accepted Xemu result: 28.39 FPS, the equal-map average of matched 20-75-second
combat windows with four local players and eight bots. Deck16: 26.30 FPS;
Oblivion: 26.21; Morpheus: 32.99; HangEmHigh: 28.08. Sources are the
`perf_raster_alternating4` and `perf_raster_alternating_remaining4` cohorts.
These runs exclude gameplay captures and correctness audits, and retain the
previously documented limits from variable combat and host load. Earlier
pending performance-acceptance statements below describe work before this
acceptance; they do not reopen the accepted performance item.

Completed combat samples and fixed-window timings are preserved in
[COMBAT_MEASUREMENTS_2026-09-09.json](COMBAT_MEASUREMENTS_2026-09-09.json).
This evidence separates audit runs from performance runs; pending trials are
not included as completed results.

## Implemented changes

- `Render/Src/UnMeshRnLOD.h`: face lists and collapsed wedges now use the
  existing GMem scratch stack instead of allocating/growing/freeing TArrays
  for every actor/view. Full-detail meshes read their original wedges directly.
  Face capacity is bounded by Mesh->Faces.Num(); each face is appended at most
  once. Scratch lifetime is the existing DrawLodMesh mark, including recursive
  rendering. The no-environment-texture return now unlocks textures and pops
  that mark. Geometry, lighting and LOD selection remain the same.
- `XboxRender.cpp`: common triangle submissions use eight inline vertices
  rather than a 512-vertex stack array. Larger clipped fans allocate their
  actual count. Texture binding now precedes UV conversion, fixing stale scales
  on the first polygon after material changes. Saturated nonnegative RGB
  conversion uses truncation instead of three out-of-line floor calls per vertex.
- `XboxRender.cpp`: BSP base/lightmap/macro/fog passes reuse one surface-local
  scratch buffer. Up to 16 vertices stay inline; larger polygons reserve their
  actual maximum once outside all polygon loops. The 512-vertex limit, pass
  order and vertex values remain unchanged. Native compiler output confirms
  the fixed stack reservation drops from 16,416 bytes plus a stack probe to
  540 bytes. DrawPrimitiveUP consumes the data during the call; the VB fallback
  copies it before returning, so the next polygon can safely reuse the buffer.
- `XboxAudio.cpp`: persistent ambient voices cache their last successfully
  submitted volume and playback rate. Unchanged volume avoids the logarithm
  and DirectSound call; failed submissions are retried. Slot reset clears the
  cached settings, and changes in global volume still propagate.
- `FFileManagerXbox.h`: in-buffer seeks retain already-read package bytes.
  Precache retains that buffer and Serialize refills on exhaustion. Direct
  reads invalidate the consumed buffer so later cached seeks cannot read stale
  data. This addresses nested export loading without increasing buffer size.
- Diagnostic marker presence and absence are cached at startup in the game,
  renderer and controller paths. Previously the shared helper cached positive
  results but retried two filesystem paths for every missing marker on every
  call. Test markers must be staged before launch; the test tools already do so.

Earlier work in HALOUT_AND_PERFORMANCE.md covers final-frame display correction,
presented-frame FPS accounting, skeletal pose reuse and secondary console setup.

## Idle rendering microbenchmark

The table below has zero bots and neutral player input. It is not combat FPS.

`run_xemu_stress_matrix.py --frontend-loading-proof split-smoke
--split-benchmark --split-players 4` creates real local players with neutral
process-local input. It avoids the controls proof's weapon switching, menus and
pauses. `XSPLIT PERF` now averages draw time over a window, and `XFRAME` averages
engine work separately from the frame limiter. No host input or capture is used.

`summarize_frame_benchmark.py` reports matching guest-time windows. Final
comparisons use 40,000–75,000 ms inside 60-second live soaks, excluding boot.
The baseline XBE and matching linker map are retained together under
`build_cli/perf_20260909_baseline`. Never pair a different XBE with that map.

The capture trigger is resolved before launching Xemu. Baseline and updated
builds alternate for four, three and two players, then repeat the four-player
pair in the same order. Each reported FPS/work value averages seven five-second
measurement windows. Draw time averages 17–18 shorter windows.

| Players/run | Baseline FPS | Updated FPS | Baseline work ms | Updated work ms | Baseline KB/tick | Updated KB/tick |
|---|---:|---:|---:|---:|---:|---:|
| 2 | 57.726 | 58.771 | 15.183 | 12.872 | 22.814 | 0.971 |
| 3 | 38.347 | 43.020 | 24.028 | 21.190 | 35.050 | 1.454 |
| 4, first pair | 27.407 | 33.901 | 34.825 | 27.470 | 43.108 | 1.937 |
| 4, repeated pair | 27.834 | 32.659 | 33.996 | 28.652 | 43.118 | 1.938 |

Evidence directories are `build_cli/perf_controlled_{baseline,optimized}{2,3,4}`
and the two `4_repeat` directories. The combined machine-readable summary is
`build_cli/perf_controlled_comparison.json`. All eight soaks passed; there were
no fatal errors, missing-base-texture warnings or steady texture uploads.

Two-player FPS is limited by the existing 60 Hz cap. Allocation traffic is
bytes requested from the heap per engine tick, not resident memory. These
tests have neutral local players and no bots; they isolate rendering cost
and do not represent a combat workload. Baseline and updated logs had no
observed ScriptWarnings in this timing window. All three updated midpoint
captures were inspected for world, weapon and HUD rendering.

This is emulator evidence, not a prediction of Xbox hardware FPS. Host load
still varies, so the observed range is more useful than an exact gain percentage.
Earlier `perf_20260909_*` timing runs are retained as exploratory evidence;
they predate the final BSP/capture fixes and are not the headline comparison.

## Checks and review coverage

The later `build_cli/combat_pilot4` run used four moving local players and eight
active bots. Its 121.5-second live soak passed movement, firing, death and bot-count
checks, averaging 29.625 steady FPS in Xemu. The final counters recorded 58 deaths
and movement from all four local players. This is a workload-validation pilot;
there is not yet a matching baseline with the same combat harness, so it does not
establish a combat performance gain. Native recording runs are excluded from
performance comparisons because capture adds host work.

- Release/XBE builds passed, including the final BSP scratch-buffer change.
- `test_xbox_archive_reader.py` compiles the actual reader class against counted
  in-memory I/O. Cached nested seeks perform no extra I/O; buffered/direct read
  transitions, 10,000 randomized seek/read operations and EOF detection pass.
- The Jailbreak travel harness now removes inherited character/state and
  split-control proof markers. The source runtime carried a character marker
  that would otherwise make its bots invulnerable during match completion.
- Rapid native capture was verified with three successive screenshots: two
  used the same Xemu filename within one second. The helper now detects file
  rewrites and requires the PNG signature and final IEND chunk before copying.
  The initial character-state run was stopped after this capture-tool failure;
  it is not counted as a gameplay or visual pass.
- Traced renderer allocation ownership, texture-batch transitions, stage scale
  ordering, retained scratch chunks and all DrawLodMesh return paths.
- Traced ambient voice creation/reset, volume changes, music packet servicing
  and primary-view-only audio update. Audio already updates once per composite
  frame; no additional skip was introduced.
- Inspected package lookup, export seek/precache/restore, archive buffering,
  allocator accounting, main-loop limiter and production diagnostic gates.
- Inspected nonblocking gameplay UDP receive and menu-only discovery loops.
  No networking optimization is claimed or shipped in this pass.
- Inspected actor ticking, collision link allocation/recycling, point/radius/
  line queries and physics substeps. Collision links already use a bulk free
  list and query results use scratch memory. Actor tick ownership and physics
  cadence are preserved. Long diagonal line queries still traverse the entire
  bounding box; profiling and a collision-equivalence test are needed before
  replacing that traversal.
- Reviewed existing texture hash lookup, resident eviction, render-state
  caches and native music packet refill. Those existing optimizations remain;
  no extra state cache or audio quality reduction was added.
- Release modules already compile with optimization enabled; changing the
  toolchain or globally weakening floating-point semantics is unnecessary.

## Still open

### Combat follow-up: current conclusions

Four-player, eight-bot combat passed on Deck16, Oblivion, Morpheus, and
HangEmHigh. The three new maps completed at least 90 seconds of live combat.
Their minimum available memory was 21,060 / 21,008 / 13,884 KB respectively.
These are Xemu measurements, not retail Xbox acceptance.

Two larger prototypes were removed after testing; neither established a
reliable speed gain. Demand-gating legacy render statistics appeared faster in
the harness's whole-run summaries, but aligning measurements to 20–75 seconds
**after combat starts** eliminated that advantage:

| Map | Demand-gated stats FPS | Legacy stats FPS |
|---|---:|---:|
| Oblivion | 26.36 | 27.14 |
| HangEmHigh | 27.31 | 27.73 |

Stock vertex-pose reuse passed its correctness audit: 160,607 reused vertices
were independently regenerated with zero differences. However, reuse covered
only 3,833 of 58,986 calls. Its whole-run FPS favored disabling reuse (29.29 vs
23.89), while combat-aligned windows favored enabling it (23.20 vs 26.18).
Variable combat makes this pair inconclusive. Both prototypes and their CLI
switches were removed; the local experiment patch remains in
`build_cli/pose_stats_experiment.patch`. Normal rendering and animation retain
legacy behavior. The cleanup Release rebuild passed; `perf_clean4` is the
post-cleanup four-player Oblivion combat check and passed 95 seconds.

The retained actor-filter cleanup gates diagnostic pawn casts before executing
them and checks owner-visibility flags before walking ownership chains.
`IsOwnedBy` is a pure search; all eight boolean combinations of the visibility
predicate matched. It compiled and ran through the map batch, but no isolated
FPS gain is attributed to this small change.

Retained measurement improvements:
- Marker-gated engine/world/per-view timings distinguish simulation, visibility,
  drawing, HUD, audio, and presentation without instrumenting normal play.
- Split-combat readiness accepts the active player count; ordinary controller
  proof still requires four players. The old stationary `stats_ondemand3` run
  is invalid combat evidence.
- The combat marker supplies the requested map instead of hard-coding Deck16.
  The first `perf_combat_maps4` attempt is invalid multi-map evidence; use
  `perf_combat_maps4_fixed`.
- Combat polling retains the complete 64 KB log ring. The summarizer ignores
  repeated records and can align windows to combat start. A duplicate-record
  regression verified unique sample counts and weighted averages.
- New runs save XBE, symbol-map, and map hashes alongside workload parameters.
  Audit runs are labeled and excluded from performance claims.

Rendering/client work remains the main measured cost. Corrected Deck16 guest
windows averaged 27.88 ms engine work, including 24.03 ms client work. Oblivion
combat-aligned samples averaged about 5.82 ms world rendering and 1.58 ms
HUD/post-render per view. Independent timer windows do not sum exactly.

Additional review ruled out blindly sharing dynamic lightmaps across cameras:
`SetupDynamics` filters light volumes against each view's planes before
`OccludeFrame` captures surface-light lists. Cache reuse needs light-set and
mover/zone equivalence, not merely matching viewport timestamps. Canvas text
already locks font pages once per string. All 113 renderer STAT call bodies
update GStat, and no external Engine/Core/Xbox GStat consumer was found.
Skeletal LOD remains deferred. Broad efficiency review and hardware acceptance
are still open; no new combat FPS improvement is claimed by this follow-up.

The finer `perf_draw_profile4` run passed 92.9 seconds. Combat-aligned per-view
timers measured 0.56 ms surfaces, 1.51 ms actors, 2.26 ms visibility, and 1.68 ms
HUD/post-render. Actor rendering and visibility are stronger next targets than
surface submission alone. The user's accompanying Video Debug snapshot showed
28 FPS, 371 BEGIN_ENDS/INLINE_ARRAYS, 1,384 ATTR_BIND, 41 SHADER_BIND, and zero
texture uploads/shader generation in that sample. Xemu MSPF=5 is not full game
frame time and does not isolate a GPU bottleneck.

A host-load observation found a second Xemu instance and Vigilante82PC consuming
substantial CPU alongside this test. Their historical load is unknown, so the
earlier FPS comparisons have an additional uncontrolled variable. Future combat
runs now log host CPU usage and identify the tested emulator without changing
other processes. Functional/vertex correctness evidence remains valid; reliable
speed attribution needs quieter or demonstrably comparable host conditions.

Further visibility cleanup removes four redundant `appRound(appCeil(...))`
calls per sprite: Xbox `appCeil` returns INT, and `appRound` computes
`floor(Value+0.5)` using the double literal. Equivalence passed 114,343 finite
float cases, including fractional screen boundaries and large coordinates.
The final weapon diagnostic pawn cast is also gated before execution when
skeletal auditing is off. Release compilation/linking passed; the four-player
Morpheus functional run `perf_sprite_cleanup4` passed 93.3 seconds with eight
bots and 19,560 KB minimum available memory, and recorded competing host work.
These are small eliminated-work changes, not a claimed combat FPS increase.

The light-radius early-exit experiment was rejected and removed. Four-player,
eight-bot audit runs passed on Oblivion (95.7 seconds) and HangEmHigh (94.9
seconds), with zero positive contributions incorrectly rejected. However,
only 7,568 of 9,947,210 contributions qualified on Oblivion (0.076%), and
1,533 of 7,299,943 on HangEmHigh (0.021%). The extra test on every contribution
is not justified by such rare savings. Audit timings are not performance evidence.
The experimental source is archived in `build_cli/light_radius_experiment.patch`;
UnLight.cpp is restored, the experimental harness options are removed, and
the Release Render rebuild and canonical XBE relink passed in
`build_cli/perf_light_radius_revert_build.log`.

The finer visibility run `perf_visibility4` passed 90.8 seconds of four-player,
eight-bot Oblivion combat (21,076 KB minimum available). Across 11 combat-aligned
windows and 5,862 views, actor/dynamic setup averaged 0.574 ms per view and BSP
traversal 1.665 ms; total visibility was 2.256 ms in the adjacent world timer.
Actor drawing averaged 1.636 ms and surfaces 0.562 ms. These measurements narrow
the next target to BSP traversal and actor drawing; they do not establish a gain.
The new marker-gated `XPROFILEVIS` timers include child work once and report per
root view. Release build passed. A parser fixture verified weighted means and
duplicate exclusion. No live benchmark remains running from this batch.

Source review identified an unimplemented candidate in `OccludeBsp`: ordinary
hardware surfaces allocate clipped output spans in `CopyFromRasterUpdate` or
`CopyFromRaster`, then release them after saving hardware polygons. The screen
occlusion mutations and visible result must remain identical; sky, mirror and
zone/warp portal paths consume the output spans and must retain them. Xbox has
`SpanBased=0`. Before changing this path, qualify an output-free implementation
against the original span results and screen mutations, including fragmented
scanlines, full/partial rejection, and portal fallback. A query-only path is now under qualification for the non-occluding subset.
`FSpanBuffer::RasterVisible` scans until the first nonempty overlap and allocates
no output nodes. Existing output-index allocation/release remains intact. The
caller excludes portals, mirrors, fake backdrops and invisible surfaces, and
only uses the query on non-span hardware rendering. Occluding surfaces still
use the original updating algorithm.

The actual legacy and candidate C++ functions matched in 389,376 exhaustive
cases (all 8-pixel occupancy masks, empty/reversed/touching raster intervals,
and overlapping/disjoint vertical ranges). The reproducible host test is
`Tools/test_raster_visibility.py --compiler C:/msys64/mingw64/bin/g++.exe`.
The Release build passed in `build_cli/perf_raster_query_build.log`. Four-player
Oblivion/Morpheus query qualification passed in `perf_raster_query_proof4`. During qualification the combat marker verifies each
optimized query against the original function and aborts on mismatch. This
intentional audit overhead disqualifies its FPS as optimization evidence.
The screen-only occluding routine `UpdateRasterScreen` retains the legacy
screen-link, endpoint and ValidLines updates in their original order while
omitting destination-node construction. Both routines matched their actual
legacy C++ counterparts in 389,376 exhaustive plus 100,000 seeded multi-line
cases, comparing visibility and every remaining screen span/ValidLines.
The test source is extracted from the current renderer before compilation.

Both query-only runtime audits passed: Oblivion 90.8 seconds and Morpheus 91.3
seconds. Checked-query lower bounds are stored with the audit records in the
measurement JSON. No mismatch occurred. These runs intentionally executed the
legacy query too, so their FPS cannot measure the optimization benefit.

The screen-only routine is connected for ordinary hardware surfaces with the
same special-surface exclusions. Both `perf_raster_update_proof4` audits passed:
Oblivion 91.4 seconds and Morpheus 94.4 seconds. Verified update/query counts
are preserved in the measurement JSON. Each audit cloned the original screen
into GMem scratch and compared visibility, ValidLines, endpoints and links
against the legacy result on every optimized update. No mismatch occurred.
The scratch-pool alias check also passed. These FPS values include audit costs.

Subsequent consumer review confirmed Xbox DrawComplexSurface and
DrawGouraudPolygon (including decals) do not dereference the span pointer;
Release only pops its allocation mark. Special surface/portal paths still
use complete output spans. Portal visual qualification remains outstanding.

The current source/build separates `--raster-mode optimized`, `legacy` and
`audit`. Only XboxRasterAudit.ini enables expensive comparison;
XboxLegacyRaster.ini selects original behavior in the same binary. The harness
removes stale markers, records mode/audit overhead and requires verified
occluding updates for audit success. Release rebuilding passed in
`build_cli/perf_raster_modes_build.log`. The first four-player Oblivion timing pair passed in
`perf_raster_timing4_legacy` then `perf_raster_timing4_optimized`, using the same
binary. Combat-aligned 20–75 second windows measured 21.85 versus 19.55 FPS,
45.10 versus 50.65 ms engine work, and 2.058 versus 2.321 ms BSP traversal per
view. Unchanged actor setup also increased from 0.603 to 0.701 ms. This pair
contradicts a speedup claim; host logs show other games and tools active, and
combat is not deterministic. Whole-run steady FPS moves in the opposite
direction, reinforcing the need to retain consistent comparison windows.
Both reversed-order runs passed: optimized 94.3 seconds, legacy 95.9 seconds.
Aligned FPS was 27.93 optimized versus 24.29 legacy; BSP cost was 1.588 versus
1.756 ms/view. Unchanged setup cost also dropped (0.525 versus 0.596 ms/view).
This reverses the first pair's direction and still does not establish a gain.

`compare_raster_benchmarks.py` verifies build/map hashes, mode, workload,
completion and audit absence before comparing aligned windows. A fixture
accepted identical timings and rejected nine incompatible identity variants.
This validates comparison eligibility, not deterministic workload equivalence.

To reduce slow host-load drift, `--raster-mode alternating` selects the raster
algorithm from each viewport FrameCount parity and reports separate
`XRASTERPAIR` setup/BSP aggregates. Child work is included once under its root
view's mode; both modes retain independent sample counts. This measures a
local cost difference, not separate whole-game FPS. Gameplay/camera content
can still differ between successive views, so repeat across maps. The parser's
weighted grouping and duplicate exclusion fixture passed, and Release built in
`perf_raster_alternating_build.log`. Both alternating runs passed and both timing groups were explicitly verified.
Across the same 20–75 second combat windows:

| Map | Legacy BSP ms/view | Optimized BSP ms/view | Legacy/optimized setup ms/view |
|---|---:|---:|---:|
| Oblivion | 1.718 | 1.647 | 0.582 / 0.581 |
| Morpheus | 1.653 | 1.584 | 0.587 / 0.593 |

Each map has 11 reporting windows and roughly equal mode counts (Oblivion
2,887/2,885; Morpheus 3,630/3,631 legacy/optimized). This supports a modest
roughly 4% reduction in BSP traversal, about 0.28 ms across four views. It does
not prove an overall FPS increase. Retain the candidate for visual qualification;
do not turn the conflicting separate-run FPS pairs into a headline gain.

Native four-player visual checks completed separately in `perf_raster_visual4`
on Morpheus and HangEmHigh, with general captures enabled. Morpheus passed
99.3 seconds; its initial, middle and final native frames were all inspected.
World textures, weapons and HUDs are present. Close-wall and falling camera
views limit occlusion coverage; this does not establish mirror/portal-specific
qualification. HangEmHigh passed 96.7 seconds with 13,992 KB minimum available,
zero fatal errors and zero skeletal-flicker alerts. All three gameplay frames
were inspected; textured geometry, sky, bots, weapons and all four HUDs are
present. No obvious missing textures or corruption was identified in these six
frames, but they are not a temporal flicker proof or mirror/warp-portal proof.
No benchmark or visual-run process remains active from these batches.
These runs have capture overhead
and must not be treated as timing evidence. The comparison tool now rejects
runs containing gameplay captures; the prior timing pair still passes its checks.

Special-surface map selection now uses installed package data. A read-only scan
following UPrimitive/UModel, FBspNode and FBspSurf serialization parsed all 137
maps' largest serialized Model exports after excluding zero-size export stubs.
The initial scan incorrectly attempted to read zero-size stubs; its failures
were corrected before selecting test maps. The final inventory is
`build_cli/special_surface_inventory.json`. Oblivion has one mirrored surface;
Fractal has six WarpZoneInfo actors, independently verified from its export
classes (`build_cli/warp_zone_inventory.json`).

The Release audit build in `perf_raster_special_build.log` adds limited
XRASTERSPECIAL branch evidence (0=sky, 1=mirror path, 2=warp path), gated by the
raster audit marker. Mirror-path entry alone does not prove a reflected child
was rendered because the existing non-shiny fallback can bypass it. The
four-player/eight-bot runs in `perf_raster_special4` include native captures
and full raster correctness audit. Fractal passed 95.4 seconds with 24,924 KB
minimum available. Warp branches executed at recursion depths 0 and 1. All
three gameplay captures were inspected: textured world, bots, weapons and HUDs
are present without obvious corruption. This is runtime/visual coverage, not
a pixel-exact legacy image comparison. Oblivion passed 94.4 seconds with
19,892 KB minimum available; all three captures were inspected without obvious
corruption. The mirror branch executed. Its surface 472 has flags 0x08000004
(mirrored/translucent), and Xbox StaticConstructor defaults ShinySurfaces to 0
with no Xbox-specific override in the staged INI. The existing non-shiny fallback
therefore applies. This qualifies the current mirror-surface behavior, not
rendering with reflections enabled. All special-surface runs have completed.
Do not use their FPS as performance evidence.

Retain the raster change as a modest local BSP optimization with host
correctness tests and four-player runtime/visual coverage, including warp and
current mirror-fallback behavior. Do not claim a whole-game FPS gain from it.
Hardware qualification and the broader efficiency review remain open.

Additional actor-texture review traced DrawLodMesh texture resolution, its
environment fallback and UTexture::Lock/Unlock. Lock is not a pure descriptor
lookup: it can load mip data, advance animation through Update(CurrentTime),
and consume bRealtimeChanged. EnvironmentInfo is used when a material requests
environment mapping or its regular texture is absent. A cached descriptor or
skipped unused environment lock therefore needs explicit animation/realtime
semantics; no such shortcut was introduced. Skeletal texture diagnostics already
return before their loops when the audit marker is absent.

Packaging review confirmed public runtime roots are allowlisted by
package_release.py, excluding the diagnostic marker files. The separate
Jailbreak travel-soak cleanup was missing XboxRasterAudit.ini,
XboxLegacyRaster.ini and XboxAlternateRaster.ini. They are now cleared alongside
other inherited proof markers. A temporary-directory check executed the actual
cleanup function, verified all three were removed, and confirmed System's
UnrealTournament.ini was preserved. Python compilation passed. This prevents
measurement-mode contamination; it is not a gameplay FPS optimization.

HUD text review found two temporary FString constructions per drawn wrapped
line: a copy of the entire remaining text and a copy of the selected prefix.
WrappedPrint now passes its existing buffer and iCleanWordEnd to DrawString.
The wrapping/measurement loop is unchanged. DrawString has an optional length
limit, tests it before reading a character, and respects it during ampersand
lookahead. Existing unbounded callers keep the default MAXINT limit.

`test_canvas_text_slice.py` compiles the original DrawString from commit
8fcbc4f and the current function against captured glyph/texture calls. All
100,000 cases matched output width, glyph coordinates and lock/unlock order,
including clipping, invalid glyphs, multiple pages and ampersands. The only
host adaptation declares the final loop variable locally for standard C++
(the Xbox compiler retains VC6 scope). Release Engine rebuild/XBE linking passed
in `perf_text_slice_build.log`. The four-player/eight-bot Oblivion visual/runtime
check in `perf_text_slice4` passed 92.8 seconds of live combat with zero fatal
records and at least 21,092 KB available in smoke samples. All three native
captures were inspected sequentially: wrapped combat messages, centered
announcements, names and numeric HUD remain readable in all four views, with
no obvious text corruption. Captures make this correctness evidence, not an
FPS comparison. No text-path FPS gain has been measured.

The remaining alternating-frame raster comparison completed in
`perf_raster_alternating_remaining4`, using the current text-slice build,
four local players and eight bots throughout. Both cases passed combat checks,
with no fatal records or gameplay captures. Deck16 ran 94.0 seconds (minimum
smoke availability 19,840 KB); HangEmHigh ran 92.0 seconds (13,228 KB).
Aligned 20-75-second combat windows each contain 11 samples per mode:

| Map | Legacy BSP ms/view | Optimized BSP ms/view | Local reduction | Approx. saving across 4 views |
|---|---:|---:|---:|---:|
| Deck16 | 1.4178 | 1.3604 | 4.05% | 0.230 ms |
| HangEmHigh | 1.8534 | 1.7994 | 2.91% | 0.216 ms |

Unchanged setup time was 0.8025/0.8106 ms on Deck16 and 0.6979/0.6982 ms
on HangEmHigh (legacy/optimized). Together with Oblivion and Morpheus, the four
maps consistently support a modest local BSP saving, not a proven whole-frame
FPS increase. Mode selection alternates viewport frames within a single match;
it reduces separate-run variation but is not deterministic replay. The text
change applies equally to both modes. No bot cap, AI frequency, LOD or visual
quality setting changed. Both test runs have completed.

Additional script-dispatch review traced `AActor::ProcessState`,
`UObject::ProcessEvent`/`ProcessInternal`, `FindObjectField`, and native actor
iterators. Function lookup already uses state-aware VF hash buckets. Event
locals use stack allocation, and constructor-linked properties are destroyed
on return; skipping initialization or destruction would change script semantics.
Verbose script tracing caches the marker lookup and short-circuits frame-name
inspection when absent. State execution preserves latent-action polling and
its state-transition limit; reducing its frequency would reduce AI/gameplay
work and is outside this optimization pass.

`AllActors`, `RadiusActors`, and `VisibleActors` resume live actor-array scans
between script-body executions (`PRE_ITERATOR`/`POST_ITERATOR`). A precomputed
list or class cache must preserve actor order and spawning/destruction during
the body. Replacing RadiusActors with the collision hash is not equivalent:
spawn inserts only bCollideActors actors into that hash, whereas RadiusActors
includes noncolliding actors and tests Radius + CollisionRadius. The separate
VisibleCollidingActors API already uses the hash and scratch allocation.
No iterator or script-dispatch shortcut was introduced by this review. This
covers these named paths, not the entire engine or all gameplay scripts.

HUD phase profiling now separates player PostRender, console PostRender,
native overlays and EndFlash under the existing combat benchmark marker.
Ordinary gameplay adds no timer calls. `nativeMS` includes XboxMenuPostRender,
loading activity, audio PostRender and the remaining pre-unlock bookkeeping;
it is not exclusively menu time. The summarizer weights these records by view
count and removes duplicates; a two-window weighted/deduplicated fixture passed.
Release Engine/XBE rebuild passed (`perf_hud_detail_build.log`).

`perf_hud_detail4` completed 95.1 seconds on Deck16 with four players/eight
bots, no fatal records and at least 20,260 KB available in smoke samples.
The aligned 20-75-second window contains 6,400 views in 11 reporting windows:
player HUD 1.4355, console 0.0294, native tail 0.0236, EndFlash 0.0591 ms/view.
Their sum matches the parent HUD measurement (1.5474 ms/view within rounding).
Player HUD accounts for about 93% of this region, or 5.74 ms across four views.
This is cost attribution, not a performance improvement or isolated script-VM
cost: the player HUD includes native canvas/texture/drawing work it invokes.

Source tracing confirmed PlayerPawn.PostRender dispatches to myHUD.PostRender;
ChallengeHUD already retains message dimensions unless resolution changes.
Xbox DrawTile batches matching state/texture tiles; DisableStage1 checks the
existing bound state before touching the device. The pending-rule hook exits
when inactive and the portrait proof caches its absent marker. Optimizing these
small hooks is not the current priority. Next investigate native canvas versus
script execution within player HUD, preserving every visible element and update.

Native canvas attribution now brackets the bodies of all nine UCanvas exec
operations after P_FINISH, armed only around the benchmarked player HUD.
A depth counter avoids counting nested canvas operations twice. It includes
native rendering/texture work invoked by these operations, but excludes their
script argument evaluation and parameter destruction. Inactive execution does
not read the timer. Tests exercised inactive state, nesting, multiple calls,
reset and weighted/deduplicated summaries. Release Engine/XBE rebuild passed
(`perf_canvas_detail_build.log`).

`perf_canvas_detail4` passed 91.7 seconds of four-player/eight-bot Deck16 combat,
with zero fatal records and minimum smoke availability 19,776 KB. The aligned
20-75-second window contains 7,244 views across 11 windows: player HUD
1.3093 ms/view, native canvas bodies 0.5851 ms/view (44.7%), averaging 35.4
outermost native canvas calls/view. The remaining 0.7242 ms/view includes
script execution, argument evaluation/destruction, dispatch and profiling
overhead. Do not label that remainder pure interpreter cost. Timer overhead
has not been calibrated; this diagnostic build is not FPS comparison evidence.

FontInfo already caches selected fonts by width, while ChallengeHUD retains
message dimensions. Neither a redundant font load nor an uncached message-size
loop explains the whole cost. This result narrows the next review: assess
native text/tile bodies and HUD script operations separately, without lowering
HUD update frequency or dropping elements. It does not establish a new gain.

Texture-cache review found a stale last-use timestamp on the already-bound
hit path in XboxRender.SetTextureD3D. The ordinary binding and deferred-update
paths refreshed Entry->FrameCounter, but the matching bound-entry early return
did not. If a texture remained bound across frames and was then replaced on
that stage, eviction could consider it old despite use in the current frame.
The early return now refreshes BoundEntry->FrameCounter. This preserves the
existing Age <= 0 victim exclusion; it does not change upload/animation flags.

A host fixture compiled the actual hit block and victim scan. It verified
same-frame protection after unbinding, later-frame eligibility, pinned-victim
exclusion, masked-variant mismatch and absent-resource behavior. XboxRender
Release rebuild and XBE link passed (`perf_texture_age_build.log`). The
four-player/eight-bot HangEmHigh run `perf_texture_age4` passed 122.0 seconds,
with minimum smoke availability 11,916 KB, no fatal/missing-base-texture alerts,
no RDRAW FAILED and no reuse no-candidate records. All three native gameplay
captures were inspected sequentially; world, weapons, bots and HUD remain
present without obvious texture corruption. Still captures and this short soak
do not prove absence of transient flicker or long-play stability. This is cache
bookkeeping correctness, not a demonstrated FPS gain; captures and profiling
exclude the run from timing attribution.

UTexture.Lock itself still updates animation, resolves LOD/compression, prepares
mip pointers and consumes bRealtimeChanged. UTexture.Update already avoids
repeating Tick at the same CurrentTime. No cross-call descriptor cache or
texture-update skip was introduced.

Repeat and broaden timing; stress moving stock and skeletal actors; validate
reader changes during map loads/travel; verify sound on hardware; review the
remaining engine/script/build-tool paths and long-play resource pressure.
Skeletal LOD is currently deliberately disabled by an existing compatibility
guard. Re-enabling it requires repairing/qualifying collapse data, not removing
the guard for an easy FPS claim. No visual quality reduction is part of this pass.

## Earlier idle-test build identity

Historical SHA-256 values for the earlier idle-test cohort, not the current
combat build (XBE and map must remain paired):

| File | SHA-256 |
|---|---|
| Baseline `default.xbe` | `5BC07DC9F67793538ACECE460B83140144B74E30F726B4E3950B183777A9C916` |
| Baseline `UnrealTournament.map` | `944889B8AF5436F8C1C72742611C2765ECB57CD2170C9F294FB0B530435DB574` |
| Before BSP edit `perf_20260909_pre_bsp/default.xbe` | `365E137AE0F9D1CD7AC2E21AB4CD430A9DFB59BF5038515BB8BC0E08FA726B90` |
| Before BSP edit `perf_20260909_pre_bsp/UnrealTournament.map` | `B222BACF16CB41F91E099B8F7DB07C0AA7B9E8FD07FEEC4D6AF40F173B00A762` |
| Earlier final `build/default.xbe` | `201E4ABFAAFAFF96DC1156335CE4CD3AF9EE0AF2C6F3930CC216CB098810B4B3` |
| Earlier final `build/UnrealTournament.map` | `DA0D53E7EBABE6EC1FED408DAFA0AEEF26A92A797567C050A70DA57704580845` |
