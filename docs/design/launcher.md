# Mod sync and launch: SkyrimModSync (2026-11-25; v0.6: our own mod list, no MO2)

A separate Rust CLI, `sms` ([blockheads/SkyrimModSync](https://github.com/blockheads/SkyrimModSync)), replaces our
packaged launcher and `-full` zip. A host's mod list (`playtest/modlist.json` here) resolves into a pack; players sync
it, load it into their game, play, and unload it. This repo builds the mod's archive (`scripts/package.sh`) and
`scripts/release.sh` attaches the pack to each release.

Decided with the user (2026-11-25):

- **Files, not recipes.** The pack is the host's installed files plus load order. No Wabbajack recipes.
- **Bytes from the source when known.** A file found in the Nexus archive its mod was installed from comes from Nexus
  (or a URL / the newest GitHub release); everything else, and any download that fails, from the host over P2P.
- **Our own mod list, no mod organizer** (v0.6): a JSON list with one source per mod (nexus, url, github, local) and a
  simple install rule (`folder`, `root`). MO2, Vortex or Wabbajack setups become converters into it, later, for
  modpacks; the per-file pack stays the stable interface.
- **Packs by link** (v0.6): a pack with no host-only files needs no host online (`sms play github:OWNER/REPO`).
- **Friends need no mod manager.** Load / Unload deploy the pack into the game folder, Vortex-style.
- **Nexus only through the user's own API key** (Premium downloads; `sms setup` asks for it; without Premium a host
  downloads the archives by hand into `downloads/`, and players get the files from the host).
- **No invite with the game closed.** Our launcher in Steam's slot is gone; friends accept at the main menu.
- **CLI only for now**, `--json` on every command. Linux and Windows releases; macOS later.
- **The tool is general**; codes travel between friends, no catalogue, no mod content shipped.
- **v0.5 (simplest that works):** one hash (BLAKE3), no signing layer, the host serves files from where they are.

## The whole flow

```
modlist.json (host writes) → sms host publish → pack.json (resolved, pinned)
pack  = files {path, size, blake3, from} + plugins + loadorder + ini + host id
        from = base (player's own copy must match) | archive{hash, member} | host

release: scripts/release.sh TAG → GitHub prerelease with the zip and pack.json
players: sms setup (folders, Nexus key) → sms play github:blockheads/SkyrimCoop
         (fetch pack, sync, load, steam://run/489830) → sms unload; later `sms play`
host:    sms host serve only when someone has no Premium key (or for local files)
both games: the DLL's join gate checks the result (plugins today, file hashes later)
```

## 1. The mod list (`list.rs`)

```
mod = name + one source: nexus{mod, file} | url | github{repo, asset?} | local (folder)
      + folder (only that folder inside the archive) + root (files beside SkyrimSE.exe)
install rule: root files → game root; Data/… → Data/; a mod with a Data/ folder installs
only that and its root files (SKSE: skips src/); otherwise everything → Data/; fomod/ never
later mods win conflicts; plugins = load order; ini = shared settings
sms host add LIST <nexus file link> appends a Nexus mod
```

## 2. Publish (`host.rs`)

```
every archive first: cached by source; Nexus link (Premium) or downloads/<name> by hand
  (missing ones listed together with their Nexus links); github → newest release asset
base files: the host's own licensed game files (version/edition check for players)
members listed once per archive hash; each mod's files by its install rule
published.json = pack + local files + archive members (the host serves either,
extracting a member on first request); --output writes pack.json
```

## 3. Transfer (`peer.rs`)

```
one iroh connection, one stream per request, 8 at a time:
  "pack"                  → the pack JSON
  "file <hash> <offset>"  → '1' + bytes from the host's own file, or '0'
friend writes <hash>.part, checks size and hash, renames into the store; a cut file resumes
```

## 4. Sync (`sync.rs`)

```
plan → what the game and store lack; base files that differ → refuse (version/edition)
archives 3 at a time: Nexus link (key) or URL → hash checked → wanted members into the store
any failure / no key → those files from the host
```

## 5. Load / unload (`deploy.rs`)

```
one walk of the game folder (folded path → real casing)
plan: Dir, Aside (friend's Data/ files not in the pack), Place (hardlink; config copied;
      SKSE's loader in SkyrimSELauncher.exe), User (Plugins.txt, loadorder.txt, INIs merged)
journal.json written once → carried out → `loaded` marker
unload: journal backwards, each step tolerant of one that never ran; interrupted = undone first
```

Measured on v0.5 (this machine, 22 GB clean game, page cache smaller than the data): a cold publish read 10-17 s
(v0.4.1: 21 s); memory-mapped hashing was slower (25-32 s) and was dropped. Binary 9.5 MB (v0.4.1: 26 MB), same
speed (size-optimized build, hashing/unpacking/crypto at full optimization).

## Releases

- SkyrimModSync: public, [blockheads/SkyrimModSync](https://github.com/blockheads/SkyrimModSync); a `v*` tag builds
  `sms` for Linux and Windows into a GitHub release with `SHA256SUMS`.
- SkyrimCoop: `scripts/release.sh TAG` publishes `scripts/package.sh`'s archive as a prerelease of this repo from a
  clean, pushed commit; a tag naming another commit, or a DLL holding a home path, is refused. Symbols stay local
  (`<zip stem>-symbols/`, with BUILD and RUNTIME, for `scripts/re/symbolize_crash.py --build`). It then publishes
  `playtest/modlist.json` (whose SkyrimCoop entry is this repo's newest release) and attaches `pack.json`.

## What changed in this repo

- Deleted: `Code/launcher/` (SkyrimCoopLauncher, LaunchChoice.h), `Code/tests/client/launcher_tests.cpp`, the
  `SKYRIMCOOP_LAUNCH_ARGS` carrier in `SteamBridge::GetLaunchLobby`, `scripts/package-vendor*.sha256`, the `-full`
  zip, check.sh's launcher build.
- `scripts/package.sh`: one mod archive (Data/ root: `SKSE/Plugins/SkyrimTogetherClient.dll`, the UI,
  `SkyrimCoopStart.esp`, docs and licenses under `SKSE/Plugins/SkyrimCoop/`), plus the symbols folder.
- `scripts/package_check.py`: the zip and `build/runtime-vendor` as two local mods of a list, published with `sms`, synced over a
  local P2P connection, loaded into a clean hardlinked game folder, booted through SKSE in Steam's launcher slot,
  unloaded back to the same files.
- `scripts/release.sh`: the archive and its `pack.json` as a GitHub prerelease; `playtest/modlist.json`: the playtest's
  mods (SKSE, Address Library, USSEP, Alternate Start from Nexus; SkyrimCoop from our releases).
- `docs/install.md`: `sms setup` and `sms play github:blockheads/SkyrimCoop`.

## Tests (in SkyrimModSync, `cargo test`)

- Units: pack rules (licensed files only as base, no executables, journal, saves or repeated paths), the install
  rules (SKSE's layout: wrapping folder, root files, Data/, skipped src/), Nexus links, zip and 7z members, INI merge
  and personal sections, Steam library discovery, the hash cache.
- Flows (`tests.rs`, each on its own fake games, a fake Nexus and GitHub): a list resolved with private game files
  never shared; a player with a key plays from `github:` and unloads to identical bytes; a pack without host files
  needs no host; without a key Nexus mods come from the host (extracted from its archive); without Premium the
  host's hand downloads; a load cut short undone; a cut host transfer resuming; host play; `host add`; `clean`.

## Later

Converters into the mod list or straight into a pack (scanning an installed game folder against the downloaded
archives, MO2, Vortex, Wabbajack), macOS (CrossOver), the lobby carrying the pack revision so a friend's game
can say "update first", file-hash layer 2 in the gate, a GUI on `--json`, RAR archives.

## Open

- The playtest list's install rules are checked against the real archives once they are downloaded (the SKSE
  archive's wrapping folder name is assumed `skse64_2_03_01`).
- Windows: the tests pass in CI; not yet played on. Untested: `steam://run` itself, a joined session set up by `sms`,
  P2P over the internet, a Nexus Premium download.
- Files the game creates while a pack is loaded (logs) stay after `unload`.
