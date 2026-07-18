# Console Map Conversion Notes

## Current Scope

PS2-exclusive map conversion is complete enough for current CXBX-R testing.
Dreamcast conversion is deferred until the Dreamcast assets are extracted again.

## Dreamcast Follow-Up

Dreamcast conversion is deferred until the PS2 path is stable. Candidate maps to
revisit later:

- `DM-Core`
- `DM-Sorayama`

## PS2 Findings

PS2 map entries in `!PS2/PSX2LINS.UMD` are seek-free UE1 packages, not renamed
PC maps. The real import/export tables sit immediately after the name table.
The summary import/export offsets point into cooked object data and must not be
used directly.

The object bodies are also not uniform PC tagged-property streams:

- `ULevel` is stored in a console-specific/cooked form and must be rebuilt.
- `AActor` exports with `RF_HasStack` contain a saved state frame followed by
  binary `SerializeBin` property data.
- PC/Xbox package loading expects `SerializeTaggedProperties`, so a correct
  conversion has to translate those binary property values into tagged property
  records.
- Dropping actor bodies to `None` makes structural testing advance, but is not
  an acceptable map conversion because it loses placement and gameplay data.

`UT99-Xbox/Tools/convert_ps2_seekfree_maps.py` is the current workbench. It
recovers the physical object locations, fixes known PS2 import package mistakes
such as `BotPack.Barrel` -> `UnrealShare.Barrel`, and synthesizes a PC-shaped
`ULevel` record. It still needs the `SerializeBin` -> tagged-property translator
before any converted PS2 map should be installed as final.
