# Joining a session

What happens from the moment a friend clicks "join" until they stand next to the host, and what is
built now versus later. Agreed 2026-10-13. The goal is the project's core value: the friend does no
setup at all. The guarantee behind it: **both games are identical installs**, and **the host's game is
the only authority** (`docs/design/host-authority-migration.md`).

Today's code reaches only part of this. The server migration (section 3, the session) builds the gate
in the host's bridge as the stages below, so each later feature fills in its stage instead of
reworking the flow.

## The flow

```
friend clicks join (Steam invite: how is not decided)
  1. our mod's build        same commit?            no -> refuse; later: update and restart
  2. game version           same Skyrim runtime?    no -> refuse (hard error)
  3. installs identical     same mods, same order?  no -> refuse with the differences;
                                                          later: sync the mods and restart
  4. session settings       the host's, sent to the player (and again when they change)
  5. player record          new player -> character creation (later)
                            known player -> their stored character (later)
  6. arrival                the player loads in where the host is (later: the tether)
```

A refusal always says which stage failed and what differs, in words a player can act on.

### 1. Our mod's build

- **Check:** the joining game's build commit equals the host's (`AuthenticationRequest::Version`
  against `BuildCommit()`). Exact match: every game runs the same build, which is also why the wire
  format can change freely between commits.
- **Today:** checked; refusal `kWrongVersion` with both versions.
- **Later: auto-update.** Offer to update to the host's build and restart the game. Needs a release
  channel (for example GitHub releases, by commit) and a launcher step: a loaded DLL cannot be
  replaced in a player's game (hot reload is a test-build tool only).

### 2. Game version

- **Check:** the Skyrim runtime version is the same; a mismatch is always an error, never synced.
- **Today:** not sent. SKSE gives our plugin `runtimeVersion` at load (`Code/client/skse_entry.cpp`).
  The plugin declares 1.6.1170 as its only compatible runtime, so SKSE does not load it on any other
  version and two players cannot differ yet; the check is for when more versions are supported.
- **Now:** send it in the join request and refuse on a mismatch.

### 3. Identical installs

The two things that must match: **the same mods**, and **the same load order**.

- **Layer 1, plugins (now):** the plugin list in load order, with the light (ESL) flag, read from the
  host's game, compared entry by entry against the joining game's (it already sends its list:
  `AuthenticationRequest::UserMods`, from `IFormBridge::GetLoadedMods`). Any difference refuses, and
  the refusal lists it: missing on the player, extra on the player, and the first position where the
  order differs. The comparison is one plain function (two lists in, the differences out) in shared
  code, so the host's gate and later the launcher use the same rule and cannot disagree.
- **Layer 2, the manifest (later):** plugins alone do not make an install identical. Loose files,
  archive (BSA) contents, SKSE plugin DLLs and some INI settings change the game too. Each game builds
  a manifest: game and SKSE versions, the plugin list in order, and a hash of every file under
  `Data/` (cached by size and modification time, so only changed files are read again). The host
  compares manifests; any difference refuses.
- **SKSE:** its version (`SKSEInterface::skseVersion`) belongs in the manifest, and in layer 1 as a
  plain version check. Mod Organizer 2 does not matter: it is how files get deployed, and the manifest
  sees the result.
- **Later: the mod syncer.** On a mismatch, offer to download and configure the host's mods and
  restart the game. The host must not send mod files itself: Nexus permissions of most mods and
  Bethesda's terms forbid redistributing them. The working pattern is Wabbajack's: the host sends
  where each file comes from (Nexus mod and file ids, hashes), and the friend's launcher downloads them
  with the friend's own Nexus account (automated downloads need Nexus Premium; otherwise a click per
  file). Hook into Wabbajack or an equivalent rather than writing a downloader. The layer 2 manifest is
  the first half of this.
- **Dropped:** the old ModPolicy check (`cModCheck`, off) compared against a load order file the
  server read from disk (`ESLoader`), not the host's game; it goes in section 3a. The SKSE/MO2
  "allowed" flags go with it.

### Mod ids (not a check, but part of joining)

Every `GameId` on the wire is (session mod id, form id within the mod). The host gives each mod a
session-wide id and returns the table in the join response (`AuthenticationResponse::UserMods`);
the client maps its own load order onto it (`ModSystem`). Once installs must be identical (stage 3),
every game has the same table; until then it must still agree across different load orders.
Section 3d moves this table to the bridge, with tests first.

### 4. Session settings

- **Rule:** the host sets them, players read them. Sent in the join response
  (`AuthenticationResponse::Settings`) and again on any change (`NotifySettingsChange`).
- **Today:** hard-coded values (difficulty forced to Master, the death system on, greetings, PvP,
  player homes and calendar sync off).
- **Now:** the host's real values; difficulty from the host's game.
- **Later:** a host settings screen for the co-op options.

### 5. Player records: characters live on the host

- **Decision (2026-10-13): the host stores every player's character.** A friend needs no save of
  their own; the co-op game is one save on the host.
  - A **new** player goes through character creation, then joins.
  - A **known** player loads in as their stored character.
- **Stored per player (later):** appearance, race, name, inventory and equipment, skills and level,
  perks, spells and shouts, active effects worth keeping, and their own quest progress if quests
  ever become per-player.
- **Today:** each player brings their character from their own save. The host keeps only a session
  record: id, name, level, character id (section 3c moves these to the bridge, shaped so the stored
  character can be added).
- **Depends on:** loading a player into the host's world without a save of their own, which is the
  parked world-state work (`docs/design/world-state-sync.md`; its rule "each player's character stays
  their own" is replaced by this decision).

### 6. Arrival

- **Rule:** a player who joins loads in where the host is.
- **Today:** a joining player stays where their own save put them; copies appear only once they meet
  the host (the party tether's pulling half is not built).
- **Later:** move the newly joined player to the host on join (part of the tether).

## Leaving and kick

- Party and session are the same: everyone connected is in it, the host is its authority
  (`host-authority-migration.md`, Decisions).
- **Leaving:** `NotifyPlayerLeft` to the others; their copies of the player go.
- **Kick:** the host alone, never itself (`HostBridge::Kick`). Reachable today from the debug overlay,
  the old CEF overlay and the test harness only; **later: a kick button in the RmlUi player list.**

## The client (launcher)

A standalone desktop client beside the in-game plugin; later work, agreed 2026-10-13.

**Why one is needed:** the game reads its plugins, load order, archives and SKSE DLLs once, at start.
Updating our mod or syncing mods means quitting, changing files and starting again, and our own DLL
cannot replace itself while loaded. So updates and mod syncing run outside the game, in a client that
then starts Skyrim. The co-op itself (sync, the bridge, copies) needs the engine and stays in the plugin.

| The client | The in-game plugin |
|---|---|
| Friends, invites, join links: where "join" lands | The session: sync, the bridge, copies |
| Updates our mod to the host's build (stage 1) | The gate (stages 1-3), always: see below |
| Checks the install and syncs mods (stage 3, Wabbajack-style) | In-game UI: party panel, chat, kick |
| Keeps a separate co-op mod setup, so a friend's own mod list is never touched (as Wabbajack installs a self-contained MO2) | Hosting from inside the game |
| Starts Skyrim with SKSE (Proton on Linux), passing the session to join | |
| Shows why a join was refused and fixes it | |

**The gate stays in the game.** The client is the convenience (it finds and fixes a mismatch before
the game starts); the host's gate is the guarantee (a mismatched game never gets in). The host cannot
know a client ran: a game can be started through SKSE directly, a client can be out of step with the
DLL, files can change after the client checked them. A mismatch otherwise fails silently: form ids
point at different forms, actors spawn wrong, the game crashes far from the cause. This is correctness
between friends, not anti-cheat. Before starting the game the client asks the host what it requires and
runs the same comparison as the gate (stage 3).

**Steam (researched 2026-10-10, shelved in `git stash` "steam/launcher work", with a `steam_probe`
diagnostic, its `RESULTS.md`, and a PySide launcher prototype):**

- Steam networking works inside Skyrim's process under Skyrim's own App ID (489830): authentication,
  Valve's relay network and a P2P listen socket reached ready, and a same-process echo passed. The
  relays matter beyond invites: they get through home routers, where plain ENet needs the host to
  forward a port.
- Not yet tested: two accounts on two machines, which is the real test, and the next experiment.
- Invites: accepting a Steam invite starts Skyrim with `+connect_lobby <lobby id>`. With Skyrim's Steam
  launch options set to run the client (`client %command%`), the client receives the lobby id, checks
  or syncs, then starts the game. The prototype already parses `+connect_lobby`. It needs that one-time
  launch option per player.
- Open question: whether using Skyrim's App ID for our integration is allowed (it works technically; a
  separate App ID would mean publishing on Steam). Fallbacks that do not depend on it: Discord invites,
  or a join link (`skyrimcoop://join/...`) that the client registers and that works from any chat.

**Side note, for fun: make it feel like AOL.** A buddy list of who is online and in which session, a
small global chat in the client for everyone using the mod, and each session's own little chat room
beside it (the same room as the in-game chat). Sounds, away messages and all. Needs a small online
service for presence and the global chat; Steam friends and lobbies could cover presence.

## Build order

Now, during the server migration (section 3, `host-authority-migration.md`, "Server sections"):

1. **3a, the gate:** stages 1, 2 (new), 3 layer 1 (new, against the host's game), 4 with the host's
   real settings. The Lua join hook and the old ModPolicy code go.
2. **3b, player list:** joined and left notifications from the bridge (behaviour unchanged).
3. **3c, player records:** the session record moves to the bridge.
4. **3d, mod id table:** moves to the bridge, tests first.

Later, roughly in this order: the tether's arrival (stage 6), the manifest (stage 3 layer 2), kick
in the UI, the client (starting with the two-account Steam test), host-stored characters with character
creation (stage 5, with world-state sync), the mod syncer and auto-update in the client, the AOL-style
buddy list and chats. The Steam invite itself is tracked in `docs/progress.md` ("Product pieces").
