# GitHub issues 8, 10, 11 and 12

Issue 9 is excluded from this change.

## Crosshair preview (#8)

The native Video menu previously drew nine invented rectangle patterns, while
ChallengeHUD.DrawCrossHair loads CrossHairs[Crosshair] and caches the texture in
CrossHairTextures. The preview now uses that same live HUD texture/configuration,
falling back to ChallengeHUD's configured defaults when no live HUD is available.
There are no persistent raw texture pointers across map changes.

## Phalanx and Sundial world textures (#10, #11)

The PS2 converter created dummy grayscale textures and marked their exports as
unusable. FBspSurf conversion then replaced the texture references with zero,
causing Engine.DefaultTexture to appear on 46 Phalanx and 114 Sundial surfaces.

The original local PSX2LINS.UMD contains the retained P8 mip chains and palettes.
The cook omits the highest mip's data array but keeps its lazy offset and size;
subsequent mip arrays retain their data. Each recovered chain is matched to its
export by its final original lazy-array offset plus the ten-byte mip dimensions.
The extractor checks the complete source-package SHA-256, every mip's dimensions,
data length and logical lazy-offset sequence, the palette reference, all 256
palette colors and their agreement with the source texture's MaxColor.

The extractor's fixed physical offsets and package hashes are recorded in
`Tools/recover_ps2_map_textures.py`. Phalanx's WALL222, wall3phal and blah11
retained mip bytes independently match the published OriginalPS2maps conversion;
their palette bytes also occur identically on the original disc. The two other
Phalanx palettes occur between those anchors in export order and match the
individual texture color metadata. Sundial's distinct red and blue palettes
match their respective texture MaxColor metadata. No downloaded texture data is
used to build the maps.

The highest retained mip is half the original width and height. UTexture::Lock
uses Texture.Scale in its BSP UV scale, so Scale=2 preserves the original world
tiling while retaining the original cooked pixels. All retained mipmaps,
MaxColor and MipZero are preserved. Texture-group UPackage exports are rebuilt
with their proper None property terminator rather than copied from logical
offsets that point into unrelated seek-free data.

## Sundial navigation crash (#11)

The supplied hardware log ends in APawn::breadthPathToInventory. Its active
navigation loop indexes ReachSpecs using NavigationPoint.Paths, then follows the
reach endpoints. The shipped map contains 2,235 reaches while nodes retain
original indices up to 2,258: 38 references are out of range and another 1,401
point to the wrong endpoint after compaction.

The converter now records old-to-new indices whenever it excludes a reach and
remaps Paths, upstreamPaths and PrunedPaths together. Removed entries are
compacted within each 16-entry node array, preserving order and -1 termination.
The rebuilt Sundial retains 213 navigation nodes and all 2,259 original reaches;
Phalanx retains 61 nodes and 390 reaches. Both pass the index/endpoint audit.
A regression test exercises removal of a middle reach so correctness does not
depend on all reaches surviving this particular conversion.

## Missing EpicCustomModels (#12)

The full dependency chain is UTPS2Characters -> MultiMesh -> EpicCustomModels.
RocketArenaMultiMesh also imports its meshes. The package was present in RC1 but
was incorrectly prohibited by the release packager as retired. It is restored
under RuntimeAssets/System and required by release validation. A new dependency
audit follows import outers for every shipped class/mesh import across all .u
packages. The pre-fix build fails for both MultiMesh and RocketArenaMultiMesh;
the repaired 48-package System directory passes.

## Verification

- Canonical Release compiler/linker/XBE build succeeded.
- Navigation compaction regression test passed.
- Both rebuilt maps pass navigation and embedded-world-texture audits.
- All 48 shipped System packages pass class/mesh dependency validation.
- Runtime and visual qualification results are recorded below as completed.

The harness uses Xemu's existing native PNG writer through the process-local
screenshot helper reused from OpenJKDF2ogx. It does not capture host windows or
send keyboard input. No additional framebuffer code remains in the game.
The native-writer address is resolved before timed camera proofs begin.

Runtime qualification on Xemu 0.8.136, 64 MB:

- Phalanx: 182.8 seconds live with six custom-character bots (both Skaarj
  hybrids, Xan, Rampage, Dominator and Damien); no fatal error. All eight native
  camera captures inspected individually. Restored walls render in both bases.
  Evidence: `build_cli/issue10_phalanx_native_20260908`.
- Sundial: final-build 181.0-second live run with six bots passed, with no fatal
  error and 6,300 logged ticks. All eight native camera captures individually
  inspected; the world textures render in the captured views.
  Evidence: `build_cli/issue11_sundial_native_final_20260908`.
- Crosshair: all nine selections captured and individually inspected. Each
  selection resolves the corresponding live HUD texture (CHair1 through CHair9).
  Evidence: `build_cli/issue8_crosshair_native2_20260908`.

Real Xbox verification remains for Steve. These local checks do not constitute
hardware sign-off or a published release.

Source reports: https://github.com/GTTeancum/UT99-Xbox-Releases/issues/8,
https://github.com/GTTeancum/UT99-Xbox-Releases/issues/10,
https://github.com/GTTeancum/UT99-Xbox-Releases/issues/11,
https://github.com/GTTeancum/UT99-Xbox-Releases/issues/12.

Local delivery: `release_work/issue_fixes_20260908/UT99-Xbox-1.1.9b.zip`.
This is an unpublished issue-fix candidate built from the canonical `build/`.

Release staging retains the existing Jailbreak ServerPackages entries via
`stage_jailbreak.patch_ini` in both canonical INI files. For a full rebuild, use
`python UT99-Xbox/Tools/build_xbox_cli.py --config Release --with-jailbreak`.
