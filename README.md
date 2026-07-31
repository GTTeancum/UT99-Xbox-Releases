# Unreal Tournament Xbox

Release-only page for the original Xbox port of *Unreal Tournament: Game of the Year Edition*.

<p>
  <a href="https://github.com/GTTeancum/UT99-Xbox-Releases/releases/latest"><strong>Download the latest release</strong></a>
</p>

## Feature Highlights

- Full Unreal Tournament GOTY gameplay on original Xbox
- Tournament, Instant Action, Jailbreak III Gold, split-screen, and System Link
- 39 converted PlayStation 2 and Dreamcast arena maps
- PlayStation 2 character pack 3.0 and Master Chief
- Per-player profiles for names, characters, team preference, controls, and Tournament progress
- Xbox-focused menus, controller presets, safe-zone controls, video adjustment, and dashboard artwork

## Screenshots

<p>
  <img src="screenshots/jailbreak-menu.png" alt="Jailbreak game type visible in the Xbox Instant Action menu" width="760">
</p>

<p>
  <img src="screenshots/dm-halberd.png" alt="DM-Halberd console map preview" width="280">
  <img src="screenshots/ctf-phalanx.png" alt="CTF-Phalanx console map preview" width="280">
</p>

## Version 1.1 Includes

- Original Xbox `default.xbe`, dashboard icon, save image, and tested System files
- Xbox menu assets, controller icons, character portraits, and converted Xbox music
- PlayStation 2 character pack 3.0, Master Chief, and matching skins
- Jailbreak III Gold runtime files and original documentation
- UT99 Console Map Pack PS2/DC 2026-05-29 with 39 converted console arena maps
- CTF-Titania, DM-HangEmHigh, and DM-Halo-Derelict
- OldSkool Amp'd Weapons 2.39 mutator

## Installation

This package is for owners of *Unreal Tournament: Game of the Year Edition* on PC. It does not include the base PC game assets.

1. Create a folder on your Xbox hard drive, for example `E:\Games\UnrealTournament\`.
2. Copy everything from the 1.1 release into that folder.
3. From your own Unreal Tournament GOTY PC installation, copy only these asset folders and merge them with the release:

```text
UnrealTournament\
  Maps\
    *.unr
  Textures\
    *.utx
  Sounds\
    *.uax
  Music\
    *.umx
```

4. Do **not** copy the PC `System` folder. Version 1.1 includes the tested Xbox System set and configuration.
5. Launch `default.xbe`.

The first press of Start after the intro flyby opens profile selection. Create or load a profile to enter the main menu.

For an upgrade from 1.0 RC1, install 1.1 into a clean folder and then copy the four owned PC asset folders above. Do not merge the old RC1 System folder into 1.1.

## Notes

- Jailbreak is available from Instant Action as `JAILBREAK`.
- Console and community maps appear under their normal DM, CTF, DOM, and JB prefixes.
- Keep the release's `Default.ini` and `UnrealTournament.ini`; both are required for Xbox menus, controls, audio, profiles, and bundled content.
- Hardware System Link and extended long-play qualification remain ongoing.

## Credits

*Unreal Tournament* was created by Epic Games and Digital Extremes. Original PC publishing was by GT Interactive, with later console publishing by Infogrames. Original music credits include Straylight Productions and Michiel van den Bos.

The converted PlayStation 2 and Dreamcast map set preserves work by Cliff Bleszinski, Dave Ewing, Eric "Ebolt" Boltjes, Cedric "Inoxx" Fiorentino, Juan Pancho "XceptOne" Eekels, Rich "Akuma" Eastwood, Alan "Talisman" Willard, and Warren Marshall. Per-map details are retained in `Docs/Console_Map_Pack_README.txt`.

[Jailbreak III Gold](https://unrealarchive.org/unreal-tournament/gametypes/J/jailbreak-iii/index.html) is credited to Daikiki, ElBundee, Mychaeel, its original and Gold map-pack teams, Sioux "NYGrrrl" Blue, its mutator/interface authors, testers, and all additional contributors retained in the original documentation.

Community content:

- CTF-Titania: Squacky; UTDMT by Patrick Cyr (GorGor); Quake III-derived art by id Software
- [DM-HangEmHigh](https://unrealarchive.org/unreal-tournament/maps/deathmatch/H/dm-hangemhigh_3b0fe14d.html): jkcrmptn
- [DM-Halo-Derelict](https://unrealarchive.org/unreal-tournament/maps/deathmatch/H/dm-halo-derelict_0ea4e9f6.html): [^..^]APOCALYPSE (Cirion UT 2K4 / Perfect Chaos), with additional credits retained from its readme
- PlayStation 2 Character Pack 3.0: AlCapowned; original PS2 models by James Green and Epic Games
- Advanced Model Support: Psychic_313
- Master Chief conversion: author not identified in the supplied package metadata; Unreal Archive also lists it as Unknown
- [OldSkool Amp'd Weapons 2.39](https://unrealarchive.org/unreal-tournament/mutators/O/oldskool-ampd-v239_f1f653ad.html): UsAaR33

Halo, Master Chief, and related content are credited to Bungie and Microsoft; the Derelict map lineage also credits Gearbox. Full attribution, source references, map authors, and original Jailbreak documentation are included in every release.

Xbox port, integration, validation, and release package: GTRemyLebeau and OpenAI Codex.

## Legal

This package is for owners of *Unreal Tournament: Game of the Year Edition*. Unreal Tournament, Unreal, the Unreal logo, and all original game assets remain property of their respective owners. Third-party mod and map content remains property of its respective authors.
