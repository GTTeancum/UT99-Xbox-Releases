# UT99 Xbox v1.2 release manifest

Prepared: 2026-09-12. Scope: final assembled and validated v1.2 release archive.

## Changes since v1.1.9b

- Halo Elite character with team-colored, face-on menu thumbnail and corrected animation conversion and weapon placement.
- HaloUT three-weapon set in one non-reload variant per weapon, with adjusted first-person scale, position and recoil, and opaque assault-rifle display backing.
- Epic custom models included with the character packages.
- Split-screen performance improvements; skeletal LOD work remains deferred.
- Dashboard-controlled display: preserve 4:3; widescreen uses 480p when supported, otherwise 480i. Single-player uses a wider view; multiplayer uses physical 4:3 pillarboxing. No 720p/1080i output.
- Whole-picture TV safe margin, including world, weapons, HUD and menus.
- Corrected widescreen HUD proportions, console notice alignment and compact, scrollable split-screen video settings.
- Consistent controller sensitivity across player counts.
- Independent per-profile crosshair, crosshair color, HUD color and HUD opacity, including migration of older profiles.
- Persistent match type, map, bots, skill, score/time limits and mutators across restarts.
- Qualified split-screen exit and subsequent single-player operation.
- Main-menu version label: v1.2.

## Required release payload

- Root: default.xbe, TitleImage.xbx, SaveImage.xbx, TitleMeta.xbx, CONTENT_CREDITS.txt.
- Runtime directories: Docs, Maps, MenuAssets, Music, MusicXbox, Sounds, System and Textures, selected by Tools/package_release.py.
- Explicit character/weapon assets: System/EpicCustomModels.u; System/HaloUTXbox.u and .int; MenuAssets/char_haloelite.xui; System/HaloMasterChief.u and .int; HaloMasterChief skin packages/registrations; System/HaloTex.utx; System/AdvancedModelSupport.u; System/UTPS2Characters.u.
- Existing required content remains included: AgentX, Akimbo Arena, ChaosUT, Jailbreak, OldSkool Weapons and Rocket Arena, plus their installed dependencies and credits.
- Preserve the release packager's console-map collection and Jailbreak documentation additions.
- Exclude retired packages, build/debug artifacts, logs and smoke-test markers under the existing packaging validation rules.

## Final archive verification

- Archive: UT99-Xbox-1.2.zip, 433,303,887 bytes.
- SHA-256: `1a111a3cb3a837814a44ae180378302bfd43ca3710290840c0dd1b8d4897cafa`.
- 441 files verified in the archive and again after fresh extraction.
- 309 canonical build files mirrored exactly; 54 maps total, including all 39 PS2/DC maps.
- All 49 System class/mesh package dependencies passed; 16 configured packages resolved.
- No proof/smoke markers, retired packages or forbidden build artifacts.
- Release default.xbe matches the version-smoked canonical build: `2f4af76f3b5811f121e6e455d2135793b439d328495165141f5b16c1ec0d1c7e`.

The initial packaging gate caught missing Jailbreak ServerPackages entries after a plain build. The existing stage_jailbreak.patch_ini helper restored the five required entries in both canonical Default.ini and UnrealTournament.ini. The resulting configs exactly match the prior release. To reproduce the complete build configuration, use the build tool's --with-jailbreak option.

Downloaded v1.1.9b comparison: all 436 previous files accounted for, with 429 unchanged, four updated and three release-specific documents replaced/regenerated. The four updates are default.xbe, CONTENT_CREDITS.txt, CTF-Phalanx.unr and CTF-Sundial.unr. Five files are new: ISSUE_FIXES_2026-09-08.md, char_haloelite.xui, EpicCustomModels.u, HaloUTXbox.int and HaloUTXbox.u.

The Elite player/bot and three Halo weapons, including the HaloWeapons mutator, are bundled in System/HaloUTXbox.u. Its .int registers the player/bot/mutator; MenuAssets/char_haloelite.xui supplies the portrait.

[File inventory](RELEASE_FILES_1.2.json), [directory tree](RELEASE_FILE_TREE_1.2.txt), and [previous-release comparison](RELEASE_COMPARISON_1.1.9b_TO_1.2.json) describe the final archive. Packaging log: release_work/release_1.2_package.log. The internal Docs/FILE_MANIFEST_SHA256.txt covers all 440 other files.

Final configuration menu smoke: native screenshot confirms v1.2; emulator closed. The generic combat runner reports false for missing gameplay/bot coverage because this run intentionally stops at the main menu. The capture itself succeeded. Evidence: build_cli/release_12_menu/01_DM-Oblivion/screenshots/release_12_main_menu.png.
