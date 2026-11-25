# Installing SkyrimCoop (playtest)

You need Skyrim Special Edition on Steam, game version **1.7.104**, on Windows or on Linux through Proton,
and the same edition as everyone else (all with the Anniversary content, or all without). On Linux, start
Skyrim once from Steam first.

## Everyone

1. Download **SkyrimModSync** (`sms`) from its
   [latest release](https://github.com/blockheads/SkyrimModSync/releases/latest): `sms-windows-x86_64.exe` on
   Windows, `sms-linux-x86_64` on Linux (`chmod +x` it).
2. Once: `sms setup`. It finds Skyrim, asks where to keep the mods, and asks for your Nexus API key
   (nexusmods.com > Site preferences > API keys). With a Premium key, the mods download from Nexus.
3. To play:

   ```
   sms play github:blockheads/SkyrimCoop
   ```

   It installs the newest playtest build with SKSE, Address Library, USSEP and Alternate Start
   (`playtest/modlist.json`), and starts Skyrim. Next time `sms play` is enough; it picks up new builds.
4. After playing: `sms unload` puts your game back as it was.

Without a Premium Nexus key, the mods come from the person who made the release, who must be running
`sms host serve` while you sync.

## In game

- The host loads a save (one used only for multiplayer) and invites friends from the Steam overlay
  (Shift+Tab). Friends accept from the main menu. Someone the host's save has not met makes a character first;
  naming it joins. Only the host saves.
- New Game skips the Helgen intro (Alternate Start): you wake in a prison cell, make your character, then pick
  how your life begins at the statue of Mara.
- Bugs and crashes: see `playtest.md` (in `Data/SKSE/Plugins/SkyrimCoop`): `/bug`, `/export` in the chat.

## Making a playtest release (developers)

`scripts/release.sh <tag>` builds the mod, publishes the GitHub prerelease and attaches `pack.json`. It needs
`sms` on `PATH` (or `SKYRIM_MOD_SYNC`) set up with `sms setup`. Without Nexus Premium it first lists the four
archives to download by hand into `playtest/downloads/` (once).
