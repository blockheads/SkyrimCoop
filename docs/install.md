# Installing SkyrimCoop (playtest)

Every player installs the same package. Skyrim Special Edition on Steam, game version 1.6.1170 (the current
Steam version), on Windows or on Linux through Proton.

## 1. Requirements

Install these first, from Nexus Mods, into the game folder (`steamapps/common/Skyrim Special Edition`):

- **SKSE64** for 1.6.1170 (skse.silverlock.org): `skse64_loader.exe` and `skse64_1_6_1170.dll` beside
  `SkyrimSE.exe`, its `Data` folder merged into the game's.
- **Address Library for SKSE Plugins**, the "All in one (Anniversary Edition)" file:
  `Data/SKSE/Plugins/versionlib-1-6-1170-0.bin`.

## 2. SkyrimCoop

Extract the zip into the game folder, merging its `Data` folder with the game's. You get:

- `Data/SKSE/Plugins/SkyrimTogetherClient.dll` (and its `.pdb`, which makes crash reports readable)
- `Data/SKSE/Plugins/SkyrimCoop/ui/` (the in-game co-op screens)
- `Data/SkyrimCoopStart.esp` (where a new character is made when joining for the first time)
- `SkyrimCoopLauncher.exe`

Enable `SkyrimCoopStart.esp` in your load order: in a mod manager tick it; by hand add the line
`*SkyrimCoopStart.esp` to `Plugins.txt` (Windows: `%LOCALAPPDATA%\Skyrim Special Edition\Plugins.txt`; Proton:
`steamapps/compatdata/489830/pfx/drive_c/users/steamuser/AppData/Local/Skyrim Special Edition/Plugins.txt`).
Starting a normal New Game is unchanged by it.

## 3. Steam starts Skyrim through SkyrimCoop

So that accepting a Steam invite with Skyrim closed starts the game with SKSE and joins: rename the game folder's
`SkyrimSELauncher.exe` to `SkyrimSELauncher.exe.vanilla` (keep it, it restores the normal launcher) and copy
`SkyrimCoopLauncher.exe` to `SkyrimSELauncher.exe`. Pressing Play in Steam then starts SKSE.

To uninstall, delete the files above and rename `SkyrimSELauncher.exe.vanilla` back.

## 4. Playing

- Every player's mods must match the host's.
- The host loads a save and invites friends from the Steam overlay. A friend accepts the invite: from the main
  menu, or with the game closed. Someone the host's save has not met makes a character first; naming it joins.
- Only the host saves. Use a save for multiplayer only.
- Bugs and crashes: see [playtest.md](playtest.md) (`/bug`, `/export`).
