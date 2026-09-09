# Halo menu integration — September 9, 2026

Xbox uses fixed lightweight discovery lists rather than the desktop .int registry. Added package-gated HALO ELITE player metadata and HALOUT WEAPONS mutator registration in XboxViewport.cpp. Added RuntimeAssets/System/HaloUTXbox.int for registry consumers. Elite uses the existing missing-portrait placeholder pending a dedicated portrait.

Release build succeeded. Canonical default.xbe SHA256: 35300791FAD7E3C3BD796581BEDD9D54A94997BA6AC406EEA62219578983CE77.

Manual disc: C:/Games/Emulators/Xemu/UT99Test/ut99_elite_menus.iso. Complete runtime data, canonical executable, approved Halo package, regenerated UnrealTournament.ini included before ISO creation, no gameplay proof markers. Config: C:/Games/Emulators/Xemu/UT99Test/xemu_elite_approved.toml.

Verified through read-only live RAM logs during user operation: Player Setup selected/saved HaloUTXbox.Elite; menu enumerated 18 mutators; user launched DM-Halo-Derelict with HaloUTXbox.HaloWeapons; engine added that mutator and entered gameplay with localClass=Elite, three bots and ready=1. Evidence: UT99-Xbox/build_cli/halo_menu_verification/port4477_ut99_ram_log.txt. No automated controller input was used.
