# Installing SkyrimCoop (playtest)

You need Skyrim Special Edition (or Anniversary Edition) on Steam, game version **1.7.104**,
on Windows or on Linux through Proton. The zip has everything else: SKSE, Alternate Start - Live Another Life and the
Unofficial Skyrim Special Edition Patch it needs.

The package filename names its Skyrim version. A separate **1.6.1170** build remains available for older
installations; install the package matching your executable. The default build and primary support target are 1.7.104.

## Install

1. In Steam, right-click Skyrim Special Edition > Manage > Browse local files. That opens the game folder (the one
   with `SkyrimSE.exe`).
2. Before the first installation, rename Bethesda's `SkyrimSELauncher.exe` to `SkyrimSELauncher.bethesda.exe`.
   Keep that file: it provides the graphics options. If SkyrimCoop already replaced the launcher, use Steam's
   Properties > Installed Files > Verify integrity once to restore it, then rename it.
3. Extract the whole zip into that folder.
4. Press Play in Steam, then **Play co-op**.

That's it. Each start checks your game version, enables the zip's plugins and starts Skyrim through SKSE. A message box
says what's wrong if something is missing.

Choose **Graphics settings** to open Bethesda's original launcher in the same Steam/Proton environment. Choose
**Options** there to change resolution, window mode and graphics quality; then **Exit** and return to **Play co-op**.
Bethesda's own **Play** button starts vanilla Skyrim without SKSE, so use our Play co-op button to play together.
Invitations start co-op directly, skipping the menu. Steam launch options `--play` and `--graphics` also select those
actions directly; `+connect_lobby` invitations always take priority.
Launch/exit errors are recorded in `SkyrimCoop/launcher.log` under your Documents/My Games settings folder.

If you have never started Skyrim, Play co-op creates missing settings files using the game's High preset. Existing
settings are preserved.

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

- A new zip: extract it the same way. Keep `SkyrimSELauncher.bethesda.exe`; updates do not include or replace it.
- Verifying the game's files in Steam puts Steam's own launcher back: extract the zip again.
- To uninstall: in Steam, Properties > Installed Files > Verify integrity of game files (restores Steam's launcher),
  then delete `Data/SKSE/Plugins/SkyrimTogetherClient.dll`, `Data/SkyrimCoopStart.esp` and the
  `alternate start - live another life*` and `unofficial skyrim special edition patch*` files in `Data`.
