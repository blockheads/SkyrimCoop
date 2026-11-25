# Packaging and launching (2026-11-25)

What SkyrimCoop ships and how it starts, after our own launcher and the bundled `-full` zip were removed.

Decided with the user (2026-11-25):

- **One mod archive.** `scripts/package.sh` builds an ordinary mod archive (its root is `Data/`): the plugin without
  the test harness, the UI, `SkyrimCoopStart.esp`, docs and licenses under `SKSE/Plugins/SkyrimCoop/`. No vendor
  files (SKSE, Address Library, USSEP, Alternate Start): players install those from their sources
  ([install.md](../install.md)).
- **Installing and syncing mods is out of scope.** Players use any mod manager or tool; nothing in this repo depends
  on one. The join gate stays the check that both games loaded the same plugins in the same order.
- **No launcher of ours, no invite with the game closed.** The game starts through SKSE (or a mod manager); friends
  accept the invite at the main menu.

## Releases

`scripts/release.sh TAG` publishes the archive as a GitHub prerelease of a clean, pushed commit; a tag naming another
commit, untracked files, or a DLL holding a home path are refused. Symbols stay local (`<zip stem>-symbols/`, with
BUILD and RUNTIME, for `scripts/re/symbolize_crash.py --build`).

## What changed in this repo

- Deleted: `Code/launcher/` (SkyrimCoopLauncher, LaunchChoice.h), `Code/tests/client/launcher_tests.cpp`, the
  `SKYRIMCOOP_LAUNCH_ARGS` carrier in `SteamBridge::GetLaunchLobby`, `scripts/package-vendor*.sha256`, the `-full`
  zip, check.sh's launcher build.
- `scripts/package.sh`: the one archive, plus the symbols folder; the PDB path in the DLL is a bare name.
- `scripts/package_check.py`: installs the archive and `build/runtime-vendor` into a clean hardlinked game folder with
  a new Proton prefix and boots it through SKSE.
- `docs/install.md`: what to install and how, tool-neutral.
