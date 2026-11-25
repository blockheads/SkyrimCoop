# Mod sync and launch: SkyrimModSync (2026-11-25, v0.5 rewrite the same day)

A separate Rust CLI, `sms` ([blockheads/SkyrimModSync](https://github.com/blockheads/SkyrimModSync)), replaces our
packaged launcher and `-full` zip. The host's MO2 profile is the pack; friends sync it, load it into their game, play,
and unload it. This repo builds only the mod's archive (`scripts/package.sh`).

Decided with the user (2026-11-25):

- **Files, not recipes.** The pack is the host's installed files plus load order. No Wabbajack recipes.
- **Bytes from the source when known.** A file found in the Nexus archive its mod was installed from comes from Nexus
  (or a URL / the newest GitHub release); everything else, and any download that fails, from the host over P2P.
- **MO2 is the host's editor** (Wabbajack lists are MO2 instances). The tool reads it, never writes it.
- **Friends need no mod manager.** Load / Unload deploy the pack into the game folder, Vortex-style.
- **Nexus only through the user's own API key** (Premium downloads; optional, nothing automates the website).
- **No invite with the game closed.** Our launcher in Steam's slot is gone; friends accept at the main menu.
- **CLI only for now**, `--json` on every command. Linux and Windows releases; macOS later.
- **The tool is general**; codes travel between friends, no catalogue, no mod content shipped.
- **v0.5 (simplest that works):** one hash (BLAKE3), no signing layer, the host serves files from where they are.

## The whole flow

```
pack  = one JSON: files {path, size, blake3, from} + plugins + loadorder + ini + host id
        from = base (friend's own copy must match) | archive{hash, member} | host
code  = the host's iroh endpoint id (iroh authenticates the host's key)

host:    sms host init → sms host publish (prints the code) → sms host serve
         sms play: its own pack, hardlinked from its MO2 files, no sync
friend:  sms setup → sms play <code> (sync, load, steam://run/489830) → sms unload
         later `sms play` alone re-syncs from the same host
both games: the DLL's join gate checks the result (plugins today, file hashes later)
```

## 1. Reading the profile (`mo2.rs`)

```
game folder: only licensed files and skse64_* root files; then enabled mods, lowest
priority first (Root/ to the game root), then overwrite/. Logs, saves, executables never.
each mod's origin: meta.ini installationFile → downloads/<archive> + .meta (Nexus ids)
```

## 2. Sources (`host.rs`)

```
per mod: override url/github → download once (cache by URL); host → none;
         else its archive + Nexus ids; RAR → none
hash every file and archive (BLAKE3, parallel, cached by path+size+mtime)
archive members listed once per archive hash (parallel)
per file: licensed → base; its mod's archive holds the hash → archive; else host
published.json = the pack + local: hash → the host's file (what serve sends, host play loads)
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

Measured (this machine, 22 GB clean game, page cache smaller than the data): a cold `host status` 10-17 s
(v0.4.1: 21 s); memory-mapped hashing was slower (25-32 s) and was dropped. Binary 9.5 MB (v0.4.1: 26 MB), same
speed (size-optimized build, hashing/unpacking/crypto at full optimization).

## Releases

- SkyrimModSync: public, [blockheads/SkyrimModSync](https://github.com/blockheads/SkyrimModSync); a `v*` tag builds
  `sms` for Linux and Windows into a GitHub release with `SHA256SUMS`.
- SkyrimCoop: `scripts/release.sh TAG` publishes `scripts/package.sh`'s archive as a prerelease of this repo from a
  clean, pushed commit; a tag naming another commit, or a DLL holding a home path, is refused. Symbols stay local
  (`<zip stem>-symbols/`, with BUILD and RUNTIME, for `scripts/re/symbolize_crash.py --build`). Hosts point the mod at it: `sms host source SkyrimCoop github blockheads/SkyrimCoop`.

## What changed in this repo

- Deleted: `Code/launcher/` (SkyrimCoopLauncher, LaunchChoice.h), `Code/tests/client/launcher_tests.cpp`, the
  `SKYRIMCOOP_LAUNCH_ARGS` carrier in `SteamBridge::GetLaunchLobby`, `scripts/package-vendor*.sha256`, the `-full`
  zip, check.sh's launcher build.
- `scripts/package.sh`: one MO2-installable archive (Data/ root: `SKSE/Plugins/SkyrimTogetherClient.dll`, the UI,
  `SkyrimCoopStart.esp`, docs and licenses under `SKSE/Plugins/SkyrimCoop/`), plus the symbols folder.
- `scripts/package_check.py`: the zip and `build/runtime-vendor` as two MO2 mods, published with `sms`, synced over a
  local P2P connection, loaded into a clean hardlinked game folder, booted through SKSE in Steam's launcher slot,
  unloaded back to the same files.
- `scripts/release.sh`: the archive as a GitHub prerelease.
- `docs/install.md`: host and friend steps with `sms`, both tools' release pages.

## Tests (in SkyrimModSync, `cargo test`)

- Units: pack rules (licensed files only as base, no executables, journal, saves or repeated paths), MO2
  priorities / Root / overwrite / Wine paths, zip and 7z members, INI merge and personal sections, Steam library
  discovery, the hash cache.
- Flows (`tests.rs`, each on its own fake games): sources by hash with private files never shared; a GitHub source;
  without a key everything from the host; with a key, sync, load (own mod set aside, own copy replaced, SKSE in the
  launcher slot, plugins and INIs written) and unload to identical bytes; a load cut short undone; a cut host
  transfer resuming; host play (publish refused while loaded); `clean`.

## Later

Vortex hosts (`vortex.deployment.json`), macOS (CrossOver), the lobby carrying the pack revision so a friend's game
can say "update first", file-hash layer 2 in the gate, a GUI on `--json`, RAR archives.

## Open

- Wabbajack instances: confirm meta.ini/.meta ids on a real installed list (the SME copy on disk is SkyrimModSync's
  own reconstruction, which drops meta.ini).
- Windows: the tests pass in CI; not yet played on. Untested: `steam://run` itself, a joined session set up by `sms`,
  P2P over the internet, a Nexus Premium download.
- Files the game creates while a pack is loaded (logs) stay after `unload`.
