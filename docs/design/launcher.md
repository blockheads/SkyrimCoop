# Mod sync and launch: SkyrimModSync (2026-11-25)

A separate Rust CLI, `sms` (the `SkyrimModSync` project, branch `file-sync`), replaces our packaged launcher and
`-full` zip. The host's MO2 profile is the pack; friends sync it, load it into their game, play, and unload it.
This repo builds only the mod's archive (`scripts/package.sh`).

Decided with the user (2026-11-25):

- **Files, not recipes.** The pack is the host's installed files plus load order. No Wabbajack recipes.
- **Bytes from the source when known.** A file found in a Nexus archive the host installed from comes from Nexus;
  everything else (generated, patched, edited, hand-installed) from the host, P2P.
- **MO2 is the host's editor** (stated requirement; Wabbajack lists are MO2 instances). The tool reads it, never
  writes it; only what MO2 doesn't know is the tool's (per-mod source overrides, excludes).
- **Friends need no mod manager.** Load / Unload deploy the pack into the game folder, Vortex-style.
- **The user picks the store folder.** Hardlinks on the same drive as Skyrim, a copy otherwise.
- **No invite with the game closed.** Our launcher in Steam's slot is gone; friends accept at the main menu.
- **Nexus through an API key only** (Premium): the documented API. No `nxm://` handler, nothing automating the
  website for free accounts (their terms). No Premium key: those mods come from the host.
- **CLI only for now**, `--json` on every command; a GUI can come later on top of it.
- **The tool is general** (any friends' pack); packs travel only as codes between friends, no catalogue, the tool
  ships no mod content. Users respect the permissions of what they share.

## The whole flow

```
host
  MO2: mods, priorities, plugins, load order   (edited there, as usual)
  sms host init --mo2 <instance> --profile <name>
  sms host status      every mod: source (nexus / url / host), host bytes
  sms host source M …  per-mod override: host | url URL | github OWNER/REPO [ASSET]
                       (the newest release's archive, at each publish) | nexus
  sms host publish     → signed pack → code SMS4.… (--output: a pack file)
  sms host serve       host-only files over P2P (iroh)
  sms play             no code: its own pack, synced from its own files (hardlinks,
                       nothing downloaded), loaded, started; or play through MO2
  publish refuses while its game folder has a pack loaded (copies would read
  as game files)

friend
  sms setup            finds Steam/Skyrim and the Proton prefix, asks for the store
  sms nexus-login      Nexus API key (Premium)
  sms play <code>      = sync → load → steam://run/489830
  sms unload           puts the game back (Bethesda's launcher too)

both games: the DLL's join gate checks the result (plugins today,
            file hashes later): the backstop, unchanged
```

## 1. Reading the host's MO2 profile (`mo2.rs`)

```
instance = folder with ModOrganizer.ini
game     = gamePath (@ByteArray(...), Z: = / under Wine) or Stock Game; --game overrides
profile  = profiles/<name>/: modlist.txt (+enabled, top = highest), plugins.txt,
           loadorder.txt, Skyrim*.ini (local INIs)
effective files:
  game folder: only licensed files (matched, never sent) and skse64_* root files;
  then each enabled mod lowest priority first, then overwrite/
  a mod's Root/ lands in the game root; meta.ini, *.mohidden, logs, saves skipped
each mod's origin: meta.ini installationFile → downloads/<archive> + its .meta
                   (modID, fileID, gameName → Nexus slug)
```

## 2. Finding each file's source (`host.rs`, publish)

```
per mod: override url → download it once (host cache by URL); github → the newest
         release's archive (prereleases too) → its asset URL; override host → none
         else archive in downloads + Nexus ids → Nexus (checked still available
         when the host has a key); RAR → none (unsupported)
         archive members hashed once, cached by the archive's SHA-256
per file: protected game file (Skyrim.esm, BSAs, cc*)   → base
          its mod's archive has a member with this hash   → archive{sha, member}
          otherwise                                       → host{blake3}
          SkyrimSE*.exe, logs, dumps, saves are never packed, and a pack naming them
          (or our journal) is refused by the friend
```

A wrong or stale MO2 record costs a host fallback, never a broken install: every hash is checked.

## 3. The pack (`pack.rs`, `channel.rs`)

```
Pack { schema 4, name, runtime (SkyrimSE.exe version), plugins, loadorder,
       ini: file → section → key → value (screen/controls stay personal),
       archives: [{sha256, size, name, origin: nexus{game,mod,file} | url}],
       files: [{path, size, sha256, mod, source: base | archive | host}] }
id = SHA-256 of the canonical JSON; a Head (ed25519) signs id + sequence
code SMS4.… = channel identity (publisher key, iroh endpoint, pack id)
```

A friend refuses an older head than one it accepted (no silent rollback). Licensed files can only be `base`.

## 4. Sync (`sync.rs`)

```
for each file: the friend's own copy (or its backup while loaded) matches? skip
               base and differs → refuse: "same Skyrim version and edition?"
               else needed unless the store has it
group needed archive members by archive:
  store/downloads/<sha> or download (Nexus download_link with the key, or the URL),
  checked against size + SHA-256, extract only the listed members → store/objects
host files → iroh from the host → checked SHA-256 → store/objects
the pack becomes the store's current pack
host playing its own pack: packs/<id>.local.json (SHA-256 → its MO2 file) fills the
  store first; a file its game folder already has (SKSE's loader) is taken from there
```

## 5. Load / Unload (`deploy.rs`)

```
journal <game>/.skyrim-mod-sync/journal.jsonl, each op durable before it is done
load:  Begin
       Data/ files not in the pack → Aside (moved to backup/)
       each pack file whose current copy differs → Place (original to backup/,
         hardlink from the store; copy across drives and for config files)
       SkyrimSELauncher.exe → Place with skse64_loader.exe's bytes
       Plugins.txt, loadorder.txt, INIs (merged in place) → User (copy kept)
       Loaded
unload: ops replayed backwards; journal and backups removed
any command first undoes an interrupted journal (no Loaded line)
```

## 6. Play and setup (`launch.rs`, `steam.rs`)

```
setup: Steam roots (registry / ~/.steam / Flatpak) → libraryfolders.vdf →
       appmanifest_489830.acf; user folders: Windows AppData/Documents,
       Linux <library>/steamapps/compatdata/489830/pfx/.../steamuser
play:  version check (pack runtime) → sync → load → steam://run/489830
       load/unload refuse while SkyrimSE.exe runs
```

## Releases

- SkyrimModSync: public, [blockheads/SkyrimModSync](https://github.com/blockheads/SkyrimModSync); a `v*` tag builds
  `sms` for Linux, Windows and macOS (Apple Silicon, Intel) into a GitHub release with `SHA256SUMS`.
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

- Units: pack validation (licensed files never host/url, HTTPS only, case-folded duplicates), signed heads and pack
  files, MO2 priorities / Root / overwrite / excluded mods / Wine paths, zip and 7z members, INI merge and personal
  sections, Steam library discovery, journal recovery (crash before and after moving an original, folders made).
- Flow (`flow_tests.rs`): an MO2 profile published, a friend synced through a fake Nexus (refused without a key,
  then downloaded) and a real local iroh host, loaded (own mod set aside, own copy replaced, SKSE in the launcher
  slot, plugins and INIs written), loaded twice (no-op), unloaded to byte-identical folders, an interrupted load
  recovered; then the host plays its own pack (synced from its own files with the P2P host stopped, publish refused
  while loaded, unloaded back to its own folder).

## Later

Vortex hosts (`vortex.deployment.json`), macOS (CrossOver), the lobby carrying the pack revision so a friend's game
can say "update first", file-hash layer 2 in the gate, a GUI on `--json`, RAR archives.

## Open

- Wabbajack instances: confirm meta.ini/.meta ids on a real installed list (the SME copy on disk is SkyrimModSync's
  own reconstruction, which drops meta.ini).
- Windows: built and clippy-clean for `x86_64-pc-windows-gnu`, not yet run on Windows.
