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
friend: accepts the invite; their game connects, from their own game or from the main menu
  1. our mod's build        same commit?               no -> refused (later: update and restart)
  2. game version           same Skyrim runtime?       no -> refused (hard error)
  3. installs identical     same mods, same order?     no -> refused, with the differences
  4. who are you            account id (Steam id)      known -> their character in the host's save
                                                       new   -> the character of their own game
                                                                (at the main menu: make one first)
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

- **Decision: the host stores every player's character, in its own save** (revised 2026-10-24). The co-op game is
  one save on the host; loading an older save takes everyone's characters back with it: one world, one timeline.
- **Identity:** an account id, not a character name. The Steam id (the Steam API runs in process) is the key; a
  generated key is the fallback.
- **A new player brings a character from their own game.** They start a game of their own (Skyrim's character
  creation and every mod's: RaceMenu, Alternate Start) or load one of their saves, then join from it. Their game takes
  their character from its running player before it loads the host's save. That own save is used once: from then on
  the host's save keeps the character. A friend at the main menu whom the host's save does not know goes straight
  into making one, then joins with it (first-time join, below).
- **While playing, the friend's game keeps the record current.** It owns its player (direct authority), so the
  engine counts its experience, levels, perks and items as in a single player game; its game sends the character
  on joining, on its changes (a level, a perk, a menu closing, a race or look change) and on leaving. The host keeps
  the newest and writes every friend's into its SKSE co-save when it saves. A look changed mid-session refreshes
  the friend's copies from the same record.
- **The record is the engine's own data, split where the engine splits character from world** (research
  2026-10-24, below): the player base's save data by change flag, and the character parts of the player itself.
  Mods that add forms (perks, spells, items, races, hair) travel with it; RaceMenu's look through its own preset
  interface; per-player script state of mods (survival needs, vampire overhauls) stays the host save's until mods
  are supported (`docs/ideas.md`).
- Another host's world keeps its own record of the friend; changes made in the friend's own save after their first
  join do not reach the host's (bringing a character again is a later idea).

#### First-time join (planned 2026-10-29, v1)

A friend the host's save does not know accepts the invite at the main menu (or Steam starts the game from it). No
choice to make and no screen of ours:

```
invite accepted, host answers no_character (no record, no character brought)
  -> the lobby is kept as the pending join; a new game starts by itself
       Quick Start - SE (in the playtest package): its "Skip the introduction?" box, yes goes straight to
         character creation
  -> the race menu, the name confirmed: the character is made
  -> the pending join runs from this game: its character brought, the host's world loaded, placed beside the host
next time: the invite loads the kept character (as built)
```

- The join after naming is the end of the intro on purpose: the rest of Helgen is the host's world's business.
- Test: a GameTests flow (no record -> new game -> character made -> joined, the record kept), and the Skyrim
  scenario `guest-makes-character-then-joins` from the invite at the main menu with Quick Start (the test games
  have it).
- v1 supports Quick Start's skip only. Joining from the vanilla intro (answering no, or without the mod: a character
  made in the middle of Helgen's scene, bound hands, prisoner clothes, MQ101 items) is out of v1 scope and untested
  (`docs/ideas.md`).

#### Where the engine keeps the player (IDA 1.6.1170, 2026-10-24)

- **The player base (`0x7`, `TESNPC::SaveGame` 24777)** writes a section per change flag: `BaseData` (level, sex
  flag, offsets), `Attributes`, `SpellList` (spells and shouts), `FullName`, `Factions`, `NPCSkills`, `Class`,
  `Face` (head parts, colours, morphs, presets), `Gender`, `Race`. A late vanilla save's is about 220 bytes. The record
  takes every section but `Factions`: guild and faction membership is the party's (`party-model.md`).
- **The player (`0x14`, `PlayerCharacter::SaveGame`, vtable 0x0E)** is one unlabelled blob, about 38 KB late in a
  game: `Actor::SaveGame` (values, spells, effects, items), then the character (perks `0x4B8`/`0x4D0`/`0x4E8`, skills
  `0x9B8`, perk points `0xB09`, tints `0xB18`) mixed with the world (quest log, objectives, crime per faction, map
  markers, teammates, camera). Loading another game's blob would bring that game's journal, so the record reads
  and writes the character parts through the engine's calls instead.
- **Elsewhere:** words of power known (`WOOP` records: the party's), misc stats and favourites (global data 0, 109;
  later). Player-made potions and poisons (`0xFF` forms) are in the record as recipe ids with `Recipes`
  (`crafted-potions.md`).
- Comparable projects: skymp writes the player base's record into a save and lets the engine load it; Skyrim Together
  has each player load their own save (each world its own, quests drift); character transfer and New Game Plus
  mods carry skills, gold and perks and drop items because they break quests. RaceMenu exposes
  `IPresetInterface::SavePreset/LoadPreset(actor)`, the whole look (morphs, overlays, sculpt, tints) of any actor.

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
- Session rules are fixed: greetings off, the death system on, friendly fire always on (built 2026-10-26,
  `friendly-fire.md`; no setting). Homes and calendar sync have no
  settings flags. The host's clock is shared by the guest clock, independently of difficulty.

### Saving: only the host

- Only the host saves and manages saves. While connected, the friend's game cannot save (no manual,
  quick or auto saves; Skyrim autosaves at load doors and on fast travel, so those are blocked too).
- Leaving takes the friend back to the main menu; their character stays with the host.
- Saves on the friend's side are an experimental idea (`docs/ideas.md`).

## As built: stages 4 to 6 (2026-10-24)

The plan, written before building it (revised 2026-10-24: the character comes from the friend's own game, no
character creation inside the host's save). Built 2026-10-24 as planned, with one change found in Skyrim: the host's
save and the guest's load run on the engine's main loop between frames (`MainLoopTasks`), not in our update, which runs
inside the Papyrus VM's update job (`docs/re/systems/save-game.md`); at the main menu the main loop runs our update.
`+connect_lobby` built 2026-10-24 (GameTests only). Not built yet: the join screen, the RaceMenu adapter.
Two more found by making a new character and joining (`guest-makes-character-then-joins`): the base's weight is in no
save section, so the record and the copies' spawn carry it (`Weight`); and a character from another race needs the
tint masks of its race and sex before its colours go on (`ApplyPlayer` rebuilds them, 40696;
`docs/re/systems/save-game.md`, "The player in a save").

### The messages, and what each game does

```
guest (in its own game, or at the main menu)   host (in game, hosting)
 JoinService::Join(endpoint)
   in its own game: the character taken now
     (JoinBridge::ReadPlayer), kept in memory
   connecting -> checking
   AuthenticationRequest{build, game, plugins,
     PlayerKey, WantsWorld, HasCharacter} ─────► HostSession: the gate (build, game, mods, full)
                                                   no record and no character -> no_character
                                                   refused -> response {reason}, kick (as today)
                         ◄───────────────────── AuthenticationResponse{kWorldFollows}
   receiving_world                               HostSaveTransfer: the host's game saves
                                                   "SkyrimCoopTransfer" (Save_Impl, device 2: written
                                                   at once, its co-save with every player's record)
                                                   .ess + .skse, snappy, 32 KB chunks, a few per frame
                         ◄───────────────────── NotifySaveChunk{offset, total, data} x N
   all in: Saves/SkyrimCoopJoin.ess + .skse
   closes the connection, loading
   BGSSaveLoadManager::Load_Impl("SkyrimCoopJoin")
   (co-save load: CharacterRecords gets every
    record the host's save keeps)
   TESLoadGameEvent: the record under PlayerKey,
     else the character it brought, is applied to
     this game's player (the save's is the host's)
   a brought character: checked placement beside
     the host, then connect (a kept one: saved place)
   AuthenticationRequest{PlayerKey} ───────────► HostSession: accepted as a player (as today);
                                                   HostPlayers keeps its key
                         ◄───────────────────── AuthenticationResponse{kAccepted}, copies, world
   joined: GuestService (and GuestRecord) as
   today; saving refused from the first step
   GuestRecord: PlayerRecordRequest{record} ───► HostRecords: kept under its key (CharacterRecords)
     on joining, on its changes (items,               the host's save (co-save callback): each
     equipment, experience, a menu closing, a         joined player's record, its position and
     race change; at most once in 5 s), leaving       cell from the host's copy of it
```

Over Steam the same runs through the Steam connection's tunnel: `SteamService` hands the join to `JoinService` with
the tunnel's loopback endpoint, and both connections go through it. A game Steam starts with `+connect_lobby <id>`
joins that lobby once its main menu is up.

A brought character reconnects only after floor and clearance checks place it 120 units beside the save's player
position. Missing physics waits; blocked bearings try others. Kept characters use their saved position. Details and
regressions: [small-bugs.md](small-bugs.md), Joining beside the host.

### The character record

`PlayerRecord` (`Code/encoding/Structs/PlayerRecord.h`), one per player, keyed by `Key`:

- `Key`: `steam:<SteamID64>` when the game joined over Steam, else `id:<16 hex digits>` its game generated once and
  keeps (`My Games/Skyrim Special Edition/SkyrimCoop/player_id`). Not checked by the host (friends' sessions).
- `Name` (for the host's lists and logs)
- `Base`, `BaseFlags`: the player base's save data (`FormState` of `0x7`) for its change flags but `Factions`: name,
  race, sex, face, level, base spells and shouts
- `Tints`: the face tint layers (as copies carry them)
- `Skills`: the engine's skill block as it is (`PlayerSkills`: level experience and threshold, each skill's level,
  experience and threshold, legendary counts); `PerkPoints`
- `Perks`: the player's perks (`PlayerCharacter::AddPerk`'s list); `Spells`: spells added to the player
- `Values`: base actor values (health, magicka, stamina, carry weight, the skills)
- `PermanentValues`: health, magicka, stamina and carry-weight permanent totals, excluding temporary buffs and
  damage; optional in older records. Earned quest rewards survive loading another player's world (`quest-rewards.md`).
- `Items`: inventory with equipment (`Inventory`, as copies carry it), gold among them; quest items are left out when
  the character comes from another world
- `Extras`: a named blob per adapter (`racemenu`: RaceMenu's preset of the player), for mods
- cell, worldspace, position (the host's copy's, at the host's save)

Stored in the host's SKSE co-save (`SerializationInterface`, unique id `SKCP`, record `CHAR` version 2, the record
list) when the host saves; read back, into `CharacterRecords`, when any game loads a save.

### Failures (the error model's reasons)

| Where | What the guest's join status says |
|---|---|
| can't reach the host, or no answer in 20 s | `unreachable` |
| the gate | `wrong_build`, `wrong_game`, `mods_differ`, `full` (as today) |
| at the main menu, and the host's save has no character for it | `no_character` |
| the host's game cannot save within 60 s, or the transfer breaks | `transfer_failed` |
| the connection drops before joining | `lost_connection` |
| the save does not load | `transfer_failed` |
| no checked spot beside the host within 20 s | `placement_failed`: the host should move to an open spot |

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
  `loading`, `joined`, `hosting`, `refused`, `reconnecting`, `ended`.

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

**Today (2026-10-24):** built as a box rather than a full screen (`Code/client/Ui/README.md`, "In the
game"): it shows a join on its way and a refusal, with Copy and Back, over the main menu or the game.
Idle shows nothing: there is no co-op entry at the main menu, by design for now (below).
Scenario `join-panel-shows-refusal`.

**The flow for now (2026-10-24):** starting a game (New, `NewGame_Enter` 52118, once its loading
screen closes) or loading one hosts by itself (`SteamService::HostWorld`); friends join through Steam's
invite or "Join Game". A game hosting alone steps down to join (`SteamService::StepDown`, also before
`JoinService::Join`). Skyrim's own main menu stays as it is.

**A guest losing its host (2026-10-24):** the panel says "Lost connection to the host. Reconnecting..."
(`reconnecting`) and `JoinService` tries the same host again: 3 tries, each up to 10 s, 2 s apart, as a
plain rejoin (no world transfer; the guest is in the host's world already). A Steam session joins the
same lobby again and dials its new tunnel (`SteamService::Rejoin`, `JoinService::Redial`). Then
`ended`/`lost_connection` ("Join again through Steam, or restart the game."). A kick is
`ended`/`kicked`, with no reconnect. The transport tells them apart (`DisconnectedEvent::Why`: left,
kicked, lost; the host's network closing is code 1, a kick 2). Re-joining by hand after that is
Steam's invite again, or a restart. Tests: GameTests `[session]` (drops and comes back, drops for
good, kick), `[steam]` (the connection drops, the lobby stays); scenario `host-rejoins`.

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
