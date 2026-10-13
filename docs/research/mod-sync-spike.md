# Research spike: making a joining player's mods match the host's

A brief for an agent working alone, without the conversation that produced it. Read it whole before
starting. Write findings to `docs/research/mod-sync.md` (create it); nothing else in the repository
changes except throwaway prototypes under `tools/spikes/mod-sync/` (see "Rules").

## The project in five lines

SkyrimCoop is a co-op mod for Skyrim Special Edition (runtime 1.6.1170), forked from Skyrim Together.
It is an SKSE plugin (a Windows DLL, `SkyrimTogetherClient.dll`, cross-built on Linux) that lets one
player host from inside their game and friends join; the host's game is the only authority. Players
are on Windows, and on Linux through Steam's Proton. The target experience: the host clicks Host and
invites a friend through Steam; the friend accepts and, without loading a save of their own, is in the
host's world as their character. Read `docs/design/join-flow.md` first: it is the agreed flow, and the
section "Mod sync: open, under investigation" is why this spike exists.

## Why mods matter

Joining loads the host's save in the friend's game, and every object id on the wire names a mod by
its place in the load order. Both games must be **identical installs**: the same plugins in the same
order, and the same files (loose files, archives, SKSE plugin DLLs, generated output, relevant INI
settings). The plugin will write a text manifest of its install and the host refuses a join whose
manifest differs, listing the differences. That part is settled. This spike is about the next step:
**making the friend's install match, ideally automatically, like Factorio** (which downloads and syncs
a server's mods in game, from its official mod portal, and restarts).

## The question

Can a friend's install be made identical to the host's automatically (or with as little effort from
the friend as possible), and how? With that: **is a separate launcher program needed, or can the
in-game plugin do everything** (a Steam invite starts or reaches the game, an in-game join panel shows
what differs and fixes it, the game restarts and rejoins)? No decision about a launcher is made yet;
your findings decide it. Nothing built so far depends on the answer.

Known constraints (verify, do not assume):

- Most Skyrim mods are on Nexus Mods. Automated downloads through its API are said to require Nexus
  Premium; free users click a download button per file (the `nxm://` link flow).
- Mods are archives, often with FOMOD installers whose choices change what is installed. Loose files
  override each other by priority; plugins have a load order.
- Players manage mods with Mod Organizer 2 (a virtual file system, USVFS: the game's `Data/` is not what
  the game sees) or Vortex (deploys links into `Data/`). Some install by hand.
- Some files are generated on each machine and cannot be downloaded: Nemesis/Pandora behaviour files,
  BodySlide meshes, LOD (DynDOLOD, xLODGen), Synthesis and xEdit patches.
- Most Nexus mods forbid redistribution, so the host must not send its mod files to the friend.
- Plugins, archives and SKSE DLLs load at game start: new mods need a restart.
- Many mods are native code (SKSE DLLs). Nothing may be installed silently because someone sent an
  invite: the friend sees what would be installed and from where, and confirms.

## Questions to answer

Answer each with evidence (links to documentation, source code, terms of service, or your own test),
and mark each claim **verified** (you saw it in docs/source or tested it) or **unverified**.

### A. Identifying the host's mods

1. Given the host's installed plugins and files, how can their source be found (Nexus mod id, file id,
   version)? Evaluate: the Nexus API's lookup by file hash (does an MD5 search endpoint exist, what
   does it return, for archives or for installed files?), MO2's `meta.ini` per mod, Vortex's state,
   Wabbajack's method (it identifies every file of an install by hash against downloaded archives).
2. What about mods from elsewhere: Bethesda's Creations (including Creation Club content shipped with
   Anniversary Edition), LoversLab, GitHub releases, files installed by hand?
3. What can our plugin see from inside the running game (the effective install, through MO2's VFS),
   and what can only an outside program see (MO2's mod list, download metadata)?

### B. Downloading

1. The Nexus API: authentication (API keys, SSO), the rules for applications (is registration or
   approval required for an app that downloads?), rate limits, what free versus Premium accounts may
   do through it, and the terms of service on automated downloads.
2. The free-user path: how Wabbajack, Vortex and MO2 handle non-Premium users (the `nxm://` handler,
   one click per file). How few clicks can it take?
3. Nexus Collections: what they are, their API, whether a collection can be created from an install
   and installed by a third-party tool, and whether that is a better unit than single mods.
4. Creations / Bethesda.net: can content be downloaded by a program, or only by the game's own menu?

### C. Installing exactly what the host has

1. FOMOD choices: can they be recorded and replayed? How does Wabbajack reproduce an install (it
   matches final files by hash, and ships binary patches for files that differ from the archives;
   confirm and explain)?
2. File conflicts and priority between mods; plugin load order (plugins.txt / loadorder.txt, LOOT).
3. Generated files: how Wabbajack handles them (are they shipped inside a modlist, as patches, under
   what permissions?), and what that means for host-to-friend sync. Is "each player runs the generator"
   workable, and can the manifest tell whether they did?
4. INI settings that matter for identical behaviour.

### D. Where the friend's co-op mods live, and how Skyrim loads them

This is the crux. The friend has their own mod setup, which must not be broken by joining a session.

1. Options to evaluate, each for Windows and for Linux (Proton prefix, case-sensitive file systems):
   - a separate co-op copy of the game (Wabbajack's "Stock Game" folder), started instead of the main
     install;
   - redirecting the game's file access from inside our SKSE plugin, as MO2's USVFS does from outside;
   - installing into `Data/` and `plugins.txt` directly (players without a mod manager);
   - creating an MO2 instance or profile, or a Vortex profile, and starting the game through it;
   - anything else you find.
2. Timing: does an SKSE plugin load early enough to change what the engine loads (plugins.txt is read,
   archives are registered, `Data/` is scanned)? Which SKSE load phases and engine steps are involved?
   Do existing SKSE plugins (for example ones that change load order or redirect files) show it can be
   done?
3. Restarting: can the running game restart itself into the new mod set (through `steam://` URLs,
   SKSE's loader, or MO2), keep a "rejoin this session" note across the restart, and work under Proton?
   What happens for players who start the game through MO2?
4. Steam: when a friend accepts an invite while Skyrim is closed, Steam starts the game with
   `+connect_lobby <id>`. How can the plugin alone read it (the game's command line through SKSE, or
   Steam API calls such as `GetLaunchCommandLine`)? Does it survive SKSE's loader and MO2?

### E. What already exists to build on

Evaluate integration versus reimplementation for: Wabbajack (and its Linux options, such as Jackify),
Nexus Collections with Vortex, MO2 (plugins, command line), LOOT, Linux mod managers (for example
Limo), Synthesis. Licences and whether they can be called as tools.

Look at how other games and multiplayer mods solve this, and what transfers to Skyrim: Factorio,
Minecraft modpacks (CurseForge, Modrinth), RimWorld multiplayer, Valheim and Thunderstore/r2modman,
Steam Workshop, and other Skyrim and Fallout multiplayer projects (Skyrim Together Reborn, SkyMP,
Fallout 76 is not relevant). How do they handle mod mismatches?

### F. Permissions and trust

1. Redistribution: what Nexus permissions and Bethesda's terms allow; whether binary patches
   (Wabbajack's method) or generated outputs may be sent between players.
2. Security: installing native code because of an invite. What consent and verification (hashes, a
   trusted source) would be sound?

## Deliverables in `docs/research/mod-sync.md`

1. A summary (one screen): what is possible, what is not, and your recommendation.
2. The answers to A to F, each claim marked verified or unverified, with sources.
3. Options for making installs match, each with: what the friend experiences (clicks, waits, restarts),
   what we would build, Windows and Linux, MO2/Vortex/manual users, cost, risks, legal standing.
4. **Is a launcher needed?** A direct answer, with the reasons, and what would change the answer.
5. A staged plan: what 1.0 should do (at least: detect and list the differences, with links), and what
   could follow.
6. Open questions that need a decision from the project owner, and experiments still worth running.

## Rules

- Do not change anything under `Code/`, `scripts/`, or the existing docs. Write the report, and if a
  prototype helps answer a question (for example calling the Nexus API, reading MO2 metadata, a tiny
  file-redirect test), put it under `tools/spikes/mod-sync/` with a README saying what it shows.
- Do not use anyone's Nexus or Steam account without being given credentials for it. Unauthenticated
  documentation and public source code are fine.
- Do not commit; the project owner commits.
- Prefer primary sources: official API documentation, terms of service, source code of the tools.
- Keep the report plain and direct: short sentences, no marketing, say what is unknown.

## Useful pointers in this repository

- `docs/design/join-flow.md`: the agreed join flow, the manifest, the plugin's file interface
  (`request`, `status`, `events`, `manifest`), the join panel, and the open mod sync question.
- `docs/design/world-state-sync.md`: loading the host's world through its save.
- `Code/client/skse_entry.cpp`: how the plugin loads under SKSE (load phases, versions it reads).
- `Code/hotreload/Loader.cpp`: a resident loader that loads the client DLL from a copy (test builds);
  relevant to restarting into a new build.
- `IFormBridge::GetLoadedMods` (`Code/client/Bridges/`): what the plugin reads of the load order today.
- Earlier Steam research, kept in git stashes (`git stash list`): "steam/launcher work" holds a
  `steam_probe` diagnostic with `Code/steam_probe/RESULTS.md` and a PySide launcher prototype
  (`Code/launcher/`, which parses `+connect_lobby`). Its files were untracked, so read them with
  `git show 'stash@{N}^3:Code/launcher/README.md'` (N is that stash's index in the list).
- `README-ANIMATION-MODS.md`: animation mods and generated behaviour files (Nemesis/Pandora).
