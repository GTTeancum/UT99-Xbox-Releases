# OldSkool Amp'd Weapons

The Xbox package stages only the standalone weapon mutator from OldSkool
Amp'd 2.39:

- `System/OLweapons.u`
- `System/OLweapons.int`
- `System/OldSkool.ini`
- mutator class `olweapons.oldskool`

The campaign framework, menus, backgrounds, model packs, and map-pack
registrations are intentionally not included. In particular, do not stage
`oldskool.u`, `olroot.u`, `osxbackgroundchanger.u`, or the original
`oldskool.int` in the UT package.

The package came from ModDB's `oldskool230noumod.zip`. The downloaded archive
must match MD5 `fc68243f56a8601ff454ea83b800baf5`. Although the ModDB filename
says 2.30, its included documentation identifies the contents as version 2.39.

The custom Xbox frontend exposes the mutator as **OLDSKOOL WEAPONS** whenever
`OLweapons.u` exists. At match launch it appends:

```text
?Mutator=olweapons.oldskool
```

The same two System files are self-contained staging inputs for a later
Unreal-focused package. That package may add the broader OldSkool framework
separately; it must not make the UT package depend on those files.

OldSkool Weapons is an arena-style inventory replacement. Do not combine it
with another arena mutator such as Instagib.

`OldSkool.ini` explicitly enables `bmini` for `olweapons.OldSkool`. The
standalone package declares that field as a config property but leaves it
disabled by default, which otherwise keeps the UT minigun instead of replacing
it with `OLminigun` on Xbox where the original PC configuration window is not
included.
