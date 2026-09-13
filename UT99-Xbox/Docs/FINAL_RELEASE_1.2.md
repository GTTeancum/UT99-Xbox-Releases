# Final release: v1.2

Published September 12, 2026. Version 1.2 is the final release; no further releases are planned. The open-work tracker is closed. Older handoffs, roadmaps, and validation notes are historical records.

- [Download v1.2](https://github.com/GTTeancum/UT99-Xbox-Releases/releases/tag/1.2)
- Final gameplay fix: reversed left/right Y-button dodging, source commit `92f90b2`. Canonical Release build passed; 196 mapping cases and all 92 four-player controls checks passed, including 16 directional dodge checks.
- [Issue #9](https://github.com/GTTeancum/UT99-Xbox-Releases/issues/9) closed with a v1.2 link.
- Release archive: `UT99-Xbox-1.2.zip`, 433,304,383 bytes; SHA-256 `72d7dbc6547414b79db84acb0c3457d733a22c6fa0254ca6d721bcb3c93d9b1b`. GitHub's uploaded asset digest matches.
- 441 files, 54 maps including all 39 PS2/DC maps. All 436 previous-release files accounted for; eight corrected maps match the qualified fix archive. HaloUT, Halo Elite, Epic custom models, and required dependencies are included.
- Box art appears before the other README images in the release repository.

Remaining limitations: System Link is experimental; Co-op Tournament and skeletal LOD improvements are not included. Output is limited to 480i/480p. These are limitations of the final release, not promised follow-up work.

The published ZIP, bundled release notes, and file inventories remain unchanged. GitHub's release description and current project documentation identify v1.2 as final. The source and release repositories were subsequently consolidated as described below.

## Repository consolidation

On September 12, 2026, all 6,858 tracked source files and the complete source main-branch history were merged into the public [UT99-Xbox-Releases repository](https://github.com/GTTeancum/UT99-Xbox-Releases), commit `9e5472c`. The release README remains the front page; the source README is preserved as `SOURCE_README.md`. All source file blobs were verified unchanged, with only the README relocated.

Both local checkouts (`C:/Programming/GitHub/UnrealTournament_1.40` and `C:/Programming/GitHub/UT99-Xbox-Releases`) use the merged public repository. The former private `GTTeancum/UnrealTournament_X` remote repository was deleted after the public merge and local synchronization were verified. Release assets and tags remain available. A verified local Git bundle backup was retained at `release_work/UnrealTournament_X-final-backup.bundle` in the original source checkout.
