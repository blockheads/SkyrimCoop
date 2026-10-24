# Joining a session

What happens from the moment a friend accepts an invite until they stand next to the host, and what
is built now versus later. Agreed 2026-10-13 (revised the same day: joining from the main menu, the
host's world by save transfer, the error model, the join panel). The goal is the project's core value:
the friend does no setup at all. The guarantees behind it: **both games are identical installs**, and
**the host's game is the only authority** (`docs/design/host-authority-migration.md`).

Today's code reaches only part of this: upstream's handshake (`AuthenticationRequest`, answered by the
bridge's `HostSession`), joined from a loaded save, and only the test harness can start a session
(`docs/progress.md`, "Product pieces"). The server migration (section 3, the session) builds the gate
as the stages below, so each later feature fills in its stage instead of reworking the flow.

## The flow

```
host: loads their save, clicks Host (in game), invites a friend through Steam
friend: accepts the invite; their game connects from the MAIN MENU (no save of their own)
  1. our mod's build        same commit?               no -> refused (later: update and restart)
  2. game version           same Skyrim runtime?       no -> refused (hard error)
  3. installs identical     same mods, same order?     no -> refused, with the differences
  4. who are you            account id (Steam id)      known -> their stored character
                                                       new   -> character creation
  5. the world              the host sends its save; the friend's game loads it
  6. arrival                the friend's character, in the host's world, next to the host
while playing: only the host saves; every player's character is saved in the host's save
leaving (or the session ending): the friend's game goes back to the main menu, and says why
```

### 1. Our mod's build

- **Check:** the joining game's build commit equals the host's (`AuthenticationRequest::Version`
  against `BuildCommit()`). Exact match: every game runs the same build, which is also why the wire
  format can change freely between commits.
- **Today:** checked by the bridge (`HostSession`); refusal `kWrongVersion` with the host's version.
  The id is `git describe --tags --always --dirty`, written on every build.
- **Later: updating.** Download the host's build, restart the game, rejoin. A loaded DLL cannot replace
  itself, but a small resident loader can load the newest staged build at start (the test-only hot
  reload loader, `Code/hotreload/Loader.cpp`, already loads the client from a copy). The `request`
  file (below) carries "rejoin this session" across the restart. Whether this lives in the plugin or
  in a launcher is open until the mod sync spike (below).

### 2. Game version

- **Check:** the Skyrim runtime version is the same; a mismatch is always an error, never synced.
- **Today (3a.2):** the join request carries the Skyrim and SKSE versions SKSE gives our plugin at
  load (`GameVersions`, `RunningGame`); the host refuses any difference (`wrong_game`, with its own
  versions). The plugin declares 1.6.1170 as its only compatible runtime, so SKSE does not load it on
  any other version and two players' Skyrim cannot differ yet; their SKSE can.

### 3. Identical installs

The two things that must match: **the same mods**, and **the same load order**. Loading the host's
save (stage 5) is only safe when they do.

- **The manifest:** a text file each game writes at start, one fact per line in load order: game and
  SKSE versions, our build, each plugin (with its light flag); later a hash per file under `Data/`
  (cached by size and modification time). The running game writes it because only the game knows
  what really loaded (under MO2's virtual file system an outside tool cannot see the effective
  install). The gate's check is a diff of two manifests; the refusal lists the differences: missing on
  the player, extra on the player, the first position where the order differs. The comparison is one
  plain function in shared code, so every place that checks (the gate, any later tool) agrees.
- **Layer 1, plugins (today, 3a.3):** the plugin list in load order, with each plugin's light flag,
  against the host's game (`CompareLoadOrders`). A refusal carries the host's list; the refused game
  lists missing, extra, light-flag and the first order difference as its join status's details.
- **Layer 2, files (later):** plugins alone do not make an install identical. Loose files, archive
  (BSA) contents, SKSE plugin DLLs and some INI settings change the game too.
- **SKSE:** its version is checked in stage 2, and is a manifest line. Mod Organizer 2 does not
  matter: it is how files get deployed, and the manifest sees the result.
- **Making installs match (open, see "Mod sync" below):** 1.0 at least detects and lists the
  differences, with links where the source is known. Downloading and installing for the player is the
  subject of a research spike.
- **Dropped:** the old ModPolicy check compared against a load order file the server read from disk
  (`ESLoader`), not the host's game (removed in 3a.1); the SKSE/MO2 "allowed" flags went in 3a.2.

### Mod ids (not a check, but part of joining)

Every game loads the host's plugins in the host's order (stage 3), so a form has the same id in every
game: a `GameId` on the wire is the form id split in two (load index, the rest; `FormIds.h`). Only a form
a game created (0xFF) is its own; it goes without an id, or as a created id where a message rebuilds it.
No table is kept (3d, 2026-10-23).

### 4. Who are you: characters live in the host's save

- **Decision: the host stores every player's character, in its own save.** A friend needs no save of
  their own; the co-op game is one save on the host.
- **Identity:** an account id, not a character name (today the name is read from the loaded
  character, which a friend at the main menu does not have). The Steam id (the Steam API runs in
  process) is the key; a generated key is the fallback.
- **How it is stored, the way Skyrim stores everything else:** the host's game already holds each
  friend's character, as its copy of them, synced live. That copy plus what copies do not carry yet
  (skills, perks, spells and shouts, level) is the record. When the host saves, our plugin writes
  every player's record into the save's SKSE co-save (the `.skse` file SKSE writes and reads with each
  save for its plugins). When a friend joins, the host sends what it has: their live state if they
  were here earlier this session, else the loaded save's record, else nothing (character creation).
  Loading an older save takes everyone's characters back with it: one world, one timeline.
- **New player:** Skyrim's character creation in the friend's game, then stored on the host.

### 5. The world: the host's save

- **Decision: save transfer, from the first playable join.** The host's game saves (a save of its
  current state, with its SKSE co-save) and sends it; the friend's game loads it. The world is the
  host's by construction: quests, dead NPCs, emptied chests, opened doors. No intermediate that starts
  the friend in a fresh world (`coc` from the main menu) is shipped: the worlds would visibly differ.
- The loaded save's player is the host's character: the friend's plugin replaces it with the friend's
  character (appearance, race and name, inventory and equipment, skills, level, perks, spells and
  shouts, lasting effects). Copies already carry appearance, face tints, inventory and values.
- Design details are `docs/design/world-state-sync.md` (change forms through save and load); its rule
  "each player's character stays their own" is replaced by stage 4.

### 6. Arrival

- The friend appears in the host's world next to the host, as their character; the live sync takes
  over from there (copies, time, weather).

### Session settings

- The host's difficulty is sent on joining (`AuthenticationResponse::Difficulty`) and on changes
  (`NotifySettingsChange::Difficulty`). A guest holds it while joined and gets its own back on leaving;
  the host's is never changed.
- Session rules are fixed: greetings and PvP off, the death system on. Homes and calendar sync have no
  settings flags. The host's clock is shared by the guest clock, independently of difficulty.

### Saving: only the host

- Only the host saves and manages saves. While connected, the friend's game cannot save (no manual,
  quick or auto saves; Skyrim autosaves at load doors and on fast travel, so those are blocked too).
- Leaving takes the friend back to the main menu; their character stays with the host.
- Saves on the friend's side are an experimental idea (`docs/ideas.md`).

## Leaving and kick

- Party and session are the same: everyone connected is in it, the host is its authority
  (`host-authority-migration.md`, Decisions).
- **Leaving:** `NotifyPlayerLeft` to the others; their copies of the player go. The leaving friend's
  game goes back to the main menu.
- **Kick:** the host alone, never itself (`HostAuthority::Kick`). The kicked game goes back to the main
  menu and says so. Reachable today from the debug overlay and the test harness only; later a kick
  button in the in-game player list. A kick (`Server::Kick`) is delivered after what is queued for the
  peer, so a refusal or a reason sent first arrives.

## Errors: data first, shown by whoever shows them

An error is data, and showing it is a separate job: a stable code for programs and tests (an exit
code), one sentence for people (stderr), and a log of everything.

**State: where the game is, and if something failed, why.** Current and persistent (replaced by the
next attempt, never timed out):

```
state refused
reason mods_differ
message Your mods differ from the host's.
detail missing Unofficial Skyrim Special Edition Patch.esp
detail extra SkyUI_SE.esp
detail order 12: the host loads Immersive Armors.esp, you load Lux.esp
```

- `reason` is stable: `wrong_build`, `wrong_game`, `mods_differ`, `full`, `unreachable`,
  `lost_connection`, `host_left`, `kicked`, `transfer_failed`, `host_failed` (the host's own: port in
  use, for example). Tests and tools check it, never the wording.
- `message` is one sentence a player can act on, worded in one place (`JoinRefusal`).
- `detail` is one line per difference: diffable, copyable, pasteable.
- States on the way: `idle`, `connecting`, `checking`, `receiving_world`, `creating_character`,
  `loading`, `joined`, `hosting`, `refused`, `ended`.

**Events: what happened in the session.** Timestamped lines in a stream: `Serana joined`,
`Intruder couldn't join: they run another build`, `Serana left`.

**The wire carries data, the receiver words it.** A refusal carries its code and the data behind it
(the host's build; from 3a.3 the host's manifest); the friend's plugin computes the differences and
words them. The host never describes the friend's install.

**Who shows what:**

| Who, where | State | Events |
|---|---|---|
| Friend at the main menu | The join panel (below) | |
| Friend in game when the session ends | Back to the main menu; the join panel says why. Every friend-side error ends in one place | |
| Host in game | Its co-op menu (host problems) | Chat system lines (the chat is the session's event stream) |
| Tools, scripts, tests | The `status` file / harness status | The `events` file |
| Always | `tp_client.log` | `tp_client.log` |

## The plugin's interface: plain files

The plugin runs sessions; how people find each other and what screen shows the state are front ends.
Everything outside the game reaches it through plain files in its own folder (outside `Data/`, which
MO2 redirects into its overwrite folder; for example `My Games/Skyrim Special Edition/SkyrimCoop/`):

- `request` (in): one line, such as `host`, `join 203.0.113.5:10578`, `join steam:<lobby>`, `leave`.
- `status` (out): the state above, rewritten when it changes.
- `events` (out): the event stream, appended.
- `manifest` (out): the install, written at start.

Any front end drives the plugin the same way: the in-game menus, a Steam invite, a script, the test
harness (whose `session.txt` and status JSON are the test-only version of this; the harness moves onto
the production files when they exist). For players the files are plumbing and a debugging aid; they
click Host and Join.

## The join panel

One component, the only place a friend sees anything about joining: a dark full screen over Skyrim's
main menu with our panel on it (RmlUi). It opens by itself when a Steam invite arrives, or from a
co-op entry at the main menu, and follows the state through its steps; a refusal or an ended session
is one of its states, shown in the same panel, never a separate popup:

```
+-----------------------------------------------+
|               JOIN A SESSION                  |
|  -------------------------------------------  |
|  Address  [ 203.0.113.5:10578        ]  Join  |   idle (until Steam invites; also for testing)
|                                               |
|  Connecting to Nick's game...                 |   connecting
|  Checking your install...                     |   checking
|  Receiving the world  ########....  62%       |   receiving_world
|  Create your character                        |   creating_character
|  Loading...                                   |   loading
+-----------------------------------------------+

+-----------------------------------------------+
|           COULDN'T JOIN THE SESSION           |   refused, or ended
|  -------------------------------------------  |
|  Your mods differ from the host's.            |
|    missing  Unofficial Skyrim SE Patch.esp    |
|    extra    SkyUI_SE.esp                      |
|    order 12 host: Immersive Armors.esp        |
|             you:  Lux.esp                     |
|                               [Copy]  [Back]  |
+-----------------------------------------------+
```

It needs input for RmlUi (today only the chat line takes keys, nothing takes the mouse): clicks and
focus, typing (the chat's keyboard-layout typing), and keeping Skyrim's main menu from reacting to the
same input; every later screen (player list with kick, settings) needs the same. It also needs to
know the game is at the main menu. The panel only shows the state; the scenarios test the state.

The host's side is smaller: a Host button in game (pause menu or a key) and its state (hosting on a
port, or why hosting failed).

**Today:** not built. A refusal reaches the join status (the log, the harness) and is shown nowhere
in game; the join screen is built once, in this form (Build order below). An earlier panel that
showed refusals over the game world (2026-10-13) was removed: a second place for the same message.

## Mod sync: open, under investigation

Factorio downloads and syncs a server's mods in game. Skyrim makes that harder: mods come mostly from
Nexus (automated downloads need Nexus Premium; free users click per file), they are archives with
installers whose choices matter, loose files override each other by priority, players manage them with
MO2 or Vortex, some files are generated on each machine (Nemesis/Pandora, BodySlide, LOD, Synthesis),
most mods forbid redistribution, and new plugins need a restart. So automatic sync works, if at all,
for a mod set our plugin manages, not for any MO2 setup; and installing anything must never happen
silently on accepting an invite (many mods are native code): the player sees what would be installed
and from where, and confirms.

- **Settled:** 1.0 detects and lists the differences (stage 3), with links to the source where it is
  known, and a copy of the list.
- **Open:** whether and how the friend's game gets the host's mods automatically, where such mods
  live and how Skyrim loads them on the next start, and with that **whether a launcher is needed at
  all**. Nothing built so far depends on the answer. Research brief: `docs/research/mod-sync-spike.md`;
  findings go to `docs/research/mod-sync.md`.

## Steam

Researched 2026-10-10, shelved in `git stash` ("steam/launcher work": a `steam_probe` diagnostic, its
`RESULTS.md`, and a PySide launcher prototype; and "steam lobby/session files", 2026-10-11):

- Steam networking works inside Skyrim's process under Skyrim's own App ID (489830): authentication,
  Valve's relay network and a P2P listen socket reached ready, and a same-process echo passed. The
  relays matter beyond invites: they get through home routers, where plain ENet needs the host to
  forward a port.
- Two accounts, tested 2026-10-23 (one machine, the second account's Steam in its own network namespace:
  `docs/design/testing.md`, "Two Steam accounts"): the host's game makes a friends-only lobby and invites the
  friend; the friend accepting hands its running game the lobby (`GameLobbyJoinRequested_t`), which joins it,
  connects to the lobby's owner over `ISteamNetworkingSockets` P2P, says hello, and joins the host's session through
  that connection: no address, no port forwarding (`guest-joins-by-invite`; GameTests `[steam]` on a fake Steam).
  Steam relays when it must and goes direct when it can (both seen between the two test games).
- As built: `ISteamBridge` (Skyrim's `steam_api64` flat API, SDK 1.5x: `SteamBridge`) and `SteamService`. The
  session is enet tunnelled through the Steam connection, unchanged: the friend's transport dials a loopback port
  (`Net::LoopbackPort`) whose datagrams go to the host as unreliable Steam messages (no Nagle delay; enet keeps its
  own reliability, clock sync and timeouts), and the host's end of each tunnel delivers into its own listener, one
  loopback port per friend. A Steam host starts hosting its session first if it does not yet. Control lines (the
  versioned hello/welcome) and datagrams share the connection, a kind byte first. Harness: `steam_host`,
  `steam_invite`, `steam_leave`, status `steam`.
- Hosting is automatic (2026-10-23): a game that loads a save (`TESLoadGameEvent`, `LoadGameEvents`) hosts its session
  and a friends-only lobby, so inviting is Steam's own UI (Shift+Tab, Invite to Game) and friends see "Join Game".
  A game hosting alone that accepts a friend's invite steps down and joins it; one with friends in its game refuses
  ("leave it first"). When a friend's Steam connection goes, the host kicks the session connection behind its tunnel
  at once. The chat tells each player: hosting, X joined, X left, joined X's game, couldn't join (why)
  (`SessionNoticeEvent`). Scenario `loaded-save-hosts-then-joins-friend`. Next: `+connect_lobby` for a game Steam
  starts, then joining from the main menu with the character the host's save keeps (stages 4 to 6).
- Skyrim's App ID (489830) for lobbies and connections, as in-process mods do (decided 2026-10-23). Fallbacks if
  that ever stops working: Discord invites, a join link (`skyrimcoop://join/...`), or typing an address.

**Side note, for fun: make it feel like AOL.** A buddy list of who is online and in which session, a
small global chat for everyone using the mod, and each session's own little chat room (the same room
as the in-game chat). Sounds, away messages and all. Needs a small online service for presence and the
global chat; Steam friends and lobbies could cover presence.

## Build order

Now, during the server migration (section 3, `host-authority-migration.md`, "Server sections"):

1. **3a, the gate:** stages 1, 2, 3 layer 1, the host's real settings; refusals as data (the error
   model: join state and events).
2. **3b, player list (done):** joined and left notifications from the bridge (behaviour unchanged).
3. **3c, player records (done):** the session record moves to the bridge, keyed and shaped for stage 4.
4. **3d, mod id table (done):** a plugin's place in the load order; no table.

Then, roughly in this order. Items 1 to 4 are one piece of work done back to back, so the join
screen is built once, in its final place; nothing player-facing is built for today's join from a
loaded save, which stays as test scaffolding until save transfer replaces it.

1. Joining from the main menu, driven through the harness (connect and gate with no save loaded).
2. The production files (`request`, `status`, `events`, `manifest`); the harness moves onto them.
3. The join screen (the panel on a full-screen backdrop over the main menu, with its progress steps)
   and the Host button, with RmlUi input.
4. Save transfer and the friend's character replacing the save's player (stage 5).
5. Characters in the host's save, character creation (stage 4).
6. Blocking saves on the friend's side; leaving and kicks back to the main menu.
7. Steam invites (after the two-account test).
8. Layer 2 of the manifest; updating our plugin; mod sync as the spike decides.
9. The AOL-style buddy list and chats.
