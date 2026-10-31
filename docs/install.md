# Installing SkyrimCoop (playtest)

You need Skyrim Special Edition (or Anniversary Edition) on Steam, game version 1.6.1170 (Steam's current version),
on Windows or on Linux through Proton. The zip has everything else: SKSE, Alternate Start - Live Another Life and the
Unofficial Skyrim Special Edition Patch it needs.

## Install

1. In Steam, right-click Skyrim Special Edition > Manage > Browse local files. That opens the game folder (the one
   with `SkyrimSE.exe`).
2. Extract the whole zip into that folder. Say yes when asked to replace `SkyrimSELauncher.exe`.
3. Press Play in Steam.

That's it. Each start checks your game version, enables the zip's plugins and starts Skyrim through SKSE. A message box
says what's wrong if something is missing.

If you have never started Skyrim on this computer, the launcher makes its settings files from the game's High
preset; change graphics in the game's Settings menu.

## Playing

- Everyone uses the same zip and no other mods.
- To host a new game: New Game skips the Helgen intro (Alternate Start). You wake in a prison cell, make your
  character, then pick how your life begins at the statue of Mara (escaping Helgen is one of the choices).
- The host loads a save and invites friends from the Steam overlay (Shift+Tab). A friend accepts the invite: from
  the main menu, or with the game closed. Someone the host's save has not met makes a character first; naming it
  joins.
- Only the host saves. Use a save for multiplayer only.
- Bugs and crashes: see `playtest.md` in the zip (`/bug`, `/export` in the chat).

## Updating and uninstalling

- A new zip: extract it the same way, replacing everything.
- Verifying the game's files in Steam puts Steam's own launcher back: extract the zip again.
- To uninstall: in Steam, Properties > Installed Files > Verify integrity of game files (restores Steam's launcher),
  then delete `Data/SKSE/Plugins/SkyrimTogetherClient.dll`, `Data/SkyrimCoopStart.esp` and the
  `alternate start - live another life*` and `unofficial skyrim special edition patch*` files in `Data`.
