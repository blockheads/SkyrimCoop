# Installing SkyrimCoop (playtest)

You need Skyrim Special Edition on Steam, game version **1.7.104**, on Windows or on Linux through Proton.
Everyone in a session needs the same mods in the same load order (the host refuses anyone else and says what
differs), and the same edition (all with the Anniversary content, or all without).

## What to install

The SkyrimCoop archive from this repo's [releases](https://github.com/blockheads/SkyrimCoop/releases) is an
ordinary mod archive: its root is the game's `Data/` folder. It needs:

- [SKSE64](https://www.nexusmods.com/skyrimspecialedition/mods/30379) 2.3.1 (Steam 1.7.104)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) v13 (1.7.104)
- [Unofficial Skyrim Special Edition Patch](https://www.nexusmods.com/skyrimspecialedition/mods/266) 4.3.9c
- [Alternate Start - Live Another Life](https://www.nexusmods.com/skyrimspecialedition/mods/272) 4.2.6a

Install them with any mod manager, or by hand: SKSE's `skse64_loader.exe` and `skse64_1_7_104.dll` next to
`SkyrimSE.exe` and its `Data/` contents into `Data/`; the other archives into `Data/`. Enable the plugins in this
order:

```
unofficial skyrim special edition patch.esp
alternate start - live another life.esp
SkyrimCoopStart.esp
```

Start the game through SKSE (`skse64_loader.exe`, or your mod manager's launch). On Linux, start it once from
Steam first so Proton creates the game's settings.

## Playing

- The host loads a save (one used only for multiplayer) and invites friends from the Steam overlay
  (Shift+Tab). Friends accept from the main menu. Someone the host's save has not met makes a character first;
  naming it joins. Only the host saves.
- New Game skips the Helgen intro (Alternate Start): you wake in a prison cell, make your character, then pick
  how your life begins at the statue of Mara.
- Bugs and crashes: see `playtest.md` (in `Data/SKSE/Plugins/SkyrimCoop`): `/bug`, `/export` in the chat.

## Releasing (developers)

`scripts/release.sh <tag>` builds the archive and publishes it as a GitHub prerelease of a clean, pushed commit;
the DLL's symbols stay in `build/package/<zip stem>-symbols/` for `scripts/re/symbolize_crash.py --build`.
