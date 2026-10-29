# Ideas for later

One place for ideas and decided-but-not-built features. Each entry: what, why, and where its notes are.
Move an entry out when work on it starts (into a design doc in `docs/design/`).

## Gameplay

- **Experience mods (after v1.0.0, 2026-10-27).** Former Experience wiring items 7 and 8: with Experience
  (17751) installed, verify that a guest killing a host NPC earns kill XP only on the killer's game; with
  Skill Uncapper AE and Experience installed, verify that a routed Speech gain applies the receiver's
  multiplier once. Installations, mod-specific scenarios and compatibility claims are outside v1.0.0
  acceptance. Plugin TESQuestStartStopEvent parity is deferred with this compatibility work (`bugs.md`);
  the audit removed its additional event relay. Native personal XP, barter Speech and vanilla quest reward
  regressions remain in scope. Notes:
  `docs/re/systems/experience.md`, `docs/design/party-model.md` ("Experience").
- **The party is the Dragonborn** (shared quest rewards and identity; each player's own items and gold; cutscenes for
  everyone; shared dialogue): decided, `docs/design/party-model.md`; shared dialogue in `docs/design/shared-dialogue.md`.
- **More natives on the player** (2026-10-22). Rule 2 sends the natives that act on this player (the table in
  `docs/re/systems/papyrus-guest-scripts.md`, from `scripts/re/papyrus_natives.py`) to the guest's game, values
  included. Left: member natives that
  open this player's menus on another actor (`ShowGiftMenu`, `ShowBarterMenu`, `OpenInventory`: a method call on the
  guest's copy, with barter on the picker's game), `GetPlayerGrabbedRef` (returns an object a `ScriptValue` cannot
  carry), words of power (the party's: mirror to every game), and a save taken while a script waits for a guest's
  answer (the wait has nothing to answer it after a load). Also open, a decision: what a guest's script gives the
  player to own (`party-model.md`, "Items and gold").
- **Giving items between players** (2026-10-22). Each player has their own inventory: a guest hands a quest item
  to the host to turn it in (dialogue conditions ask the host's inventory). `party-model.md`, "Items and gold".
  A trade menu (user, 2026-10-24): the barter menu showing each player's inventory. After 1.0; in 1.0 players
  drop items or leave them in chests.
- **Shared dialogue: others' cursors, picks and names on each menu.** Each menu shows only its own player's
  highlight; another player's pick shows in chat, and the menu waits for the next list. Built and dropped
  2026-10-19: a shared cursor, other players' options marked `[Name]`, and showing a pick on every menu with
  that topic. All three live only in the vanilla SWF (`TopicList`, `SetSelectedTopic`, `onSelectionClick`), so
  a replaced dialogue menu breaks them, and a pick of a topic a menu lacks never showed anyway. Bring back
  together, as a menu feature with a fallback for replaced SWFs. Notes: `docs/re/systems/dialogue.md` (the
  SWF, "Showing a pick from elsewhere").
- **Shared party bounty.** One player's crime counts for the whole party, judged on the host (its NPCs are
  the witnesses). Decided 2026-10-12; implementation and measured native coverage:
  `docs/design/shared-party-bounty.md` (merged into dev 2026-10-26).
- **Peer teleport requests.** Teleport is host-only today. Later a peer can ask for one as a
  Borderlands-style request that anyone can cancel, like leaving a cell under the party tether.
  Notes: `docs/design/host-authority-migration.md` (Decisions, 2).
- **Tether countdown.** Today anyone's travel takes the party along at once (Decisions, 4). A short
  countdown that anyone can cancel, as Borderlands does, before the party follows.
- **Death: spectate, then respawn together** (user, 2026-10-23). A player who goes down stays down and watches
  the others (a spectator camera on a party member) until the party has been out of combat for 10 s, then gets
  up. Replaces upstream's respawn (`PlayerService`'s 5 s timer, `PlayerCharacter::RespawnPlayer`'s move to the
  cell's COC marker), which today only stops pulling the party (`EnterCellRequest::Travel::kRespawn`).

## Sync

- **Per-player system quests: the rest (mod support).** Built 2026-10-28 for quests a player's own script calls
  into (`docs/design/system-quests.md`; vanilla's `PlayerVampireQuest` first). Left: a survival mod's quest that
  only runs on its own timers (Frostfall's exposure, needs mods) is never entered, so it stays the host's; test with
  a real survival mod (Frostfall, Sunhelm) for how its quests are entered, and keep an instance's alias scripts and
  stages in the record when one needs them. Notes: `docs/research/guest-scripts-and-mods.md`.
- **A reward after a scene goes to the player whose pick started it** (2026-10-28). A pick that starts a scene loses
  its player at the scene: its end's stage runs with no cause, so `Game.GetPlayer()` there is the host's player
  (VC01: the pick, the dawn ritual's scene, stage 195, stage 200's `VampireCure`). Carrying the cause through
  `Scene.Start` as the story would give such a reward to the picker; not needed for the cure, which is the party's.
- **Switch a guest's own weather choice off** (2026-10-20). Today a guest's game keeps picking its own weather
  (the region's climate, cell changes, scripts), and `GuestWeather` checks every frame and forces the
  host's back (`GuestEngine::HoldWeather`). Cleaner: while a guest, stop the game choosing at all, so the
  host's weather is set once per change and nothing needs checking each frame.
  - First: find where `Sky::Update` picks the next weather and the lever to stop it, checking existing SKSE
    mods before IDA.
  - Then: implement the `IWorldBridge::SetGameWeatherSystemEnabled` stub (or a `GuestEngine` call) and
    turn it back on when the guest leaves.
  - Keep: the world map's own weather, and `weather-follows-host`.

- **Quest sync: compare only the quests marked dirty** (2026-10-20). `HostWorldState` compares a snapshot
  of every quest (about 1500) twice a second, and every global every 250 ms. The comparison catches every
  writer, but costs a full scan each time and up to 500 ms of delay. Not measured yet. If a measurement
  shows the scan costs too much, let the engine mark a quest dirty and compare only those:
  - the events it has: `TESQuestStageEvent`, `TESQuestStartStopEvent`;
  - hooks where it has no event: `ForceRefIntoAlias` 25052, `ClearRefAlias` 25051,
    `BGSQuestObjective::SetState` 23933, `SetCompleted` 24991 and `SetFailed` 24988.

  Keep a slow full scan as the backstop for writers no hook sees, such as a plugin writing the fields
  directly. Globals have no event at all (`docs/re/systems/quest-apply.md`, `world-state-sync.md`).

- **Everyone's load starts with the leader's:** departure announcement built 2026-10-27
  ([party-travel.md](design/party-travel.md)). The engine's queued destination is sent before its native load;
  actual loading-screen onsets retain network/frame skew. A cached load can finish within that skew.
- **Mods' world state through the save system.** A mod's forms changed on the host sent as their save data
  (`FormState`), for what no message covers. Vanilla state goes as messages and the join as save transfer;
  design: `docs/design/world-state-sync.md`.
  - Where it goes (2026-10-21): the change funnel already reports every change on the host (`Forms/FormChanges`,
    `HostEngine`'s watcher). Today the watcher keeps only the flags of `Games/ReferenceStates.cpp`'s rows and
    the host reads each with its row. The mods' branch: a changed form or flag with no row is sent as
    `FormState::Save(form, flags)`, a guest applies it with `FormState::Load`.
  - Needs: a queue that retries when `Save`/`Load` refuse (a save or load running); the reference location
    header and created (`0xFF`) forms inside the data (`docs/re/systems/form-state.md`, "Not covered yet");
    a filter for the funnel's volume (ACHR moves alone are ~100/s idle, `docs/research/world-state-drift.md`);
    each form type tried live.
  - Why vanilla stays on engine calls: "Engine calls for vanilla, save data for the rest" in the design's
    "As built".
- **Mod patches.** Maybe one Papyrus function that forwards a mod event to the other games, so a patch can
  sync its own state. Notes: `docs/design/world-state-sync.md`. (Player checks need no patch: the party
  model's rules, `docs/design/party-model.md`.)
- **Joining (identical installs, mod sync, updating, host-stored characters, save transfer, the join
  panel):** the agreed flow is `docs/design/join-flow.md`; mod sync (and with it whether a launcher is
  needed) is a research spike, `docs/research/mod-sync-spike.md`.
- **Saves on a friend's side (experimental).** Only the host saves (`join-flow.md`). A friend saving
  the co-op world locally (to play it alone, or as a backup) raises questions with no answers yet:
  whose world is it then, and what happens on the next join.
- **Havok sync, later parts.** Built 2026-10-24 (`docs/design/physics-sync.md`): objects and dead NPCs' ragdolls from
  the host. Later, once a scenario shows the need: damping in the shared guess, each guest's budget weighted by what
  is near it, Havok's own island listeners if the change funnel's havok move misses bodies, a guest far off in an
  exterior the host has not loaded.
- **A corpse's pose at a cell load or join (small bug).** A body killed or spawned while a guest was elsewhere is
  killed again on the guest's game as its cell loads, so it drops in place on the guest's own physics: lootable, about
  where the host's lies, but a different pose (a corpse from the save lies as the save has it). Fix: the host sends
  the ragdoll's rest pose with the copy's spawn (`NotifyCharacterSpawn`), placed as a rest once the copy's 3D is in
  (`docs/design/physics-sync.md`, the dead actor at a cell load).
- **Animation mods on copies (parked).** Graph variables synced by name from the engine's graph state,
  so behaviour mods need no descriptors; then the mods' animation events and Open Animation Replacer's
  clip choice, which are probably what a modded copy misses first. Start with one modded setup through the
  scenarios. Design, measurements and why it is parked: `docs/design/graph-replication.md`.
- **Graph state over the world-sync buffers.** `AnimationGraphState` has its own stream adapter for the
  engine's graph serializer; once world sync has save/load form buffers, the graph state can use them.
- **Rest of the copy audit.** After the AI update (`docs/re/systems/actor-ai-and-copies.md`): combat,
  detection, magic/magicka. Then gather the scattered `IsCopy()` checks into one copy update.

## Crash prevention and debugging tools

Deferred until a bug needs them (the lock checker, hang dump and `symbolize_crash.py` come first).

- **EngineGuard.** Generic engine crash prevention, which mods will need: class-based lock dependency
  checking, safe self-upgrade of a held read lock, Dimmunix-style avoidance of known deadlock signatures,
  rescue points.
- **Probes.** A hot-reloadable in-game C++ test DLL: the bridge interfaces plus `Resolve(addressId)`
  through a C ABI host table, run on a fiber from the main loop, requested through `probe.txt` in the
  control directory; with `coop_test.py lab --repeat`.
- **Warm spare game,** so a crash or hang does not cost a game restart.

## Testing

- **Stress tests with more players:** 3-4 games (or bots) in one session, and headless games if Skyrim
  can run without rendering.
- **A three-game duo** (`duo --players 3`, a second client `client2` on display :101). The archive's version
  (tag `archive/shared-dialogue-client`: `third_game()`, `games()`, the desync check skipping the second client's
  own actors, `dialogue-three-players`) did not apply on today's duo and its half was removed 2026-10-22; port it
  whole, `command_duo` and `reload_games` included, when a scenario needs a third player.

- **Animation trace, next steps.** The trace (`AnimTrace`, `docs/progress.md`) runs inside scenarios.
  Next: a `coop_test.py trace --seconds N` for the running games without a scenario (both players and
  everything near them); a trace kept automatically when a scenario fails; marking the first frame where
  an owner and its copy diverge (an action or graph event one has and the other lacks); the copy's
  replay outcomes (taken, refused, how late) as entries rather than log lines.

## UI

- **The other screens in RmlUi.** The party panel is done (`Code/client/Ui/`, `skyrim.rcss`). Still to
  make, in the same SkyUI look: chat, player list, connect, settings, notifications.

## Code cleanup

- ~~One template for relayed messages~~ (done 2026-10-23, progress.md "Test suite"). A new message's header is still
  included in its factory by hand: the preprocessor cannot make includes from the list, and a missing one fails
  the build naming the type.
- ~~One line per hook~~ (done 2026-10-23). `HOOK_ID(ID, &Real, HookFn)` replaces 122 initializer triples;
  a thin macro supplies the breadcrumb name to `Hooks::Hook`, and `HOOK_ID_IMMEDIATE` selects immediate
  installation. All verified function types and globals stay, including originals called outside their hooks.
  Left unchanged: 24 installs with setup in between, reused relocation names or another target form (listed in
  progress.md, "Test suite"). IAT, vtable and raw patches stay as they were.
- ~~Event connections without a member each~~ (done 2026-10-23, progress.md "Test suite"): `Events/Listen.h`, 123
  connections in 26 vectors.
- **Events over polling** (user, 2026-10-22: "we always want to use events over polling if possible"). Discrete
  state should go out when an engine event or hook says it changed, not from a timer that re-reads it.
  Continuous state (movement, animation variables) on the tick is fine. What each engine event covers and misses:
  `docs/re/systems/change-events.md`. Left, with why:
  - `ActorValueService`: done (2026-10-23, progress.md "Test suite"). Actor::SendActorValueUpdate (38470)
    marks written values; TESDeathEvent and Resurrect (37321) mark death. Small hits flush on the frame.
    Pools keep their 100 ms comparison for regeneration; copies converge from cached owner values on writes
    and 3D loads, then stay quiet. Copy death waits for the owner's replayed action (optional tick on the wire).
    GameTests cover immediate resurrection, owner/copy refusal and stale replay, and fail with resurrection
    event handling omitted. Evidence and signatures: change-events.md.
  - `InventoryService` (unchanged, stalled 2026-10-22). Equipment snapshots (3 s): the owner's `TESEquipEvent` covers
    its own changes, but the case the snapshot exists for is a copy drifting in its own game (`repair-equipment-drift`
    strips the copy only), where the owner sees nothing. Looked at 2026-10-23, stays: the equip hooks refuse every
    change the engine makes to a copy, so a copy drifts only by losing an item (an unequip by removal) or by an
    owner's equip that failed (the item not there yet). Neither raises anything today, and the snapshot also covers
    drift whose cause is not known. An event version: the copy keeps its owner's worn set from every change message
    (`CurrentInventory`) and reconciles on a removal and on the missing item's arrival. Weapon state (each frame): the state bits
    (`ActorState+0xC` bits 5-7) have no single writer (two found, for drawn and sheathed only). The naked NPC check
    (1 s): its cause is not recorded (upstream `ef2ce4b9`); 3D loads and discovery would cover an NPC loading naked,
    not one stripped later, so it waits for the cause.
  - `CharacterService` actor discovery (each frame over the high process list): stays (looked at 2026-10-23). The
    set is "in the high process list with 3D", which changes on `TESObjectLoadedEvent` (3D) and on the four process
    moves (`Actor::MoveToHigh` 37621, `MoveToLow` 37622, `MoveToMiddleLow` 37623, `MoveToMiddleHigh` 37624, shared by
    every actor class). An event version marks it dirty on those and rescans; `ReferenceEvents` runs only in a session,
    so a session's start rescans too. Proving no change slips by needs an audit run in Skyrim (the scan beside the
    events, logging what they missed) for a scan that costs a few dozen actors a frame and is never late.
  - `CharacterService` presence (2 s): a spacing between moves of a copy the engine may drop, not a poll; nothing
    says a move was dropped. Stays.
  - `HostWorldState` quests and globals: done (2026-10-23, progress.md "Test suite"). Initial snapshots,
    then quest events, alias-fill/clear and Reset hooks, quest change flags and alias-reference invalidation;
    globals from Papyrus SetValue/Mod, console/compiled-script assignment and beast perk writes. No timers;
    the calendar stays with HostClock. GameTests isolate both alias clears and both beast globals decreasing
    to zero; they fail with delta handling omitted. Evidence and signatures: change-events.md.
  - `DialogueService`'s menu and speaker check: left for the shared dialogue rework (main checkout, 2026-10-22).
  - Done: `AiStateService` (`TESPackageEvent`, `TESCombatEvent`, `TESSceneEvent`, `TESDeathEvent`, combat target
    switches, the player's combat flag), `EffectVisualsService`, `PlayerService` race and level, `CharacterService`
    factions and experience, difficulty (progress.md, "Events over polling").

- ~~Rename `BridgeTests` to `HostAuthorityTests`~~ (done 2026-10-23, progress.md "Test suite").
- ~~One call for a form's session id~~ (done 2026-10-23 as no translation at all, progress.md "Test suite"). Left:
  `AnimationSystem::Serialize` maps an action's form and target only to drop an action on a created form, and
  sends neither.
- **Upstream's MMO leftovers** (survey 2026-10-23; the order is the user's call). The host is the only authority,
  and every game has the same build and plugins:
  - ~~The character assignment round trip~~ (done 2026-10-23, progress.md "Test suite"): the host hands out every
    id, a player's in its join answer.
  - ~~Taking ownership of an actor~~ (done 2026-10-23): mounts work end to end (progress.md "Test suite"). Open: a
    copy made while its owner rides (progress.md, loose ends "Mounts").
  - ~~Server settings and unused password~~ (done 2026-10-23, progress.md "Test suite"). Difficulty alone in
    auth and `NotifySettingsChange`; fixed greetings/PvP/death rules stay in code, homes/calendar flags and the
    unchecked token/password are gone.
  - ~~Extra chat channels~~ (done 2026-10-23). One player-chat category, including dialogue picks, plus system
    lines. Player requests carry text only; the host chooses the category and name.
  - "Server" names go as their code is touched: settings now use host difficulty, and the debug address/connect
    panel is Connection. The existing Session roster panel keeps its name.
  - Stays: clock sync (interpolation's time base), compression, the connect's name lookup, kick. Actor session
    ids: a placed NPC could go by its form id now, but created NPCs and players' copies still need one; a redesign,
    not a deletion.
- **Client services by role.** The client keeps upstream's layout, one service per message family, so each
  service mixes our own player, copies and the host's actors, and copy checks (`IsCopy()`) are scattered
  through all of them. `CharacterService` is ~1,600 lines with 26 subscriptions (ownership, spawns,
  movement, presence, factions, experience, dialogue, mounts, packages, beast form, respawn);
  `PlayerService` groups respawn, difficulty, levels and beast form. Target: services by role (our own
  player: travel, death and respawn, levels and XP, the host's settings; copies: spawn, presence, replay,
  one copy update in place of the `IsCopy()` checks; the host's actors), as `Services/Host/` already is.
  `TravelService` (2026-10-14) was the first piece. The world went host/guest on 2026-10-20: `GuestService`
  makes `Services/Guest/` (clock, weather, globals, quests) on joining, `HostEngine`/`GuestEngine` hold each
  side's engine calls, `HostBridge` became `HostAuthority`. After phase 5 (the listener moving into the bridge
  changes how messages reach these services): a short survey and plan in `docs/design/`, then one piece at
  a time with its scenarios, deleted from the old place in the same change.
- ~~Drop the engine bridge interfaces~~ (struck 2026-10-23): tier 2's fake engine (`GameTests`) now runs every
  service through those interfaces, so they are the seam the fast tests stand on.
- **One id scheme for references.** Two today: actors travel by a session id the host hands out (`SessionId`,
  `HostCharacters`), created objects by a `GameId` under `ObjectChange::kCreatedModId` (`HostEngine`'s
  `m_created`, `GuestEngine`'s twins). Only references whose form id differs between games need a session id:
  each player's character (`0x14` in its own game, a created copy elsewhere) and anything a game creates (summons,
  spawned NPCs, dropped items). A placed NPC (Lydia, `A2C94`) is the same form in every game and could travel by
  `GameId` like a placed object. Unified: a reference's id is its `GameId`, and the session-id space covers only
  the per-game ones, actors and objects alike. About 40 messages carry actor ids, and `CharacterService` keys its
  maps by them; its own refactor, after the `SessionId` → `SessionId` rename (2026-10-21).
- **Modern C++ over `TiltedPhoques`.** Done 2026-10-21 (`docs/progress.md`): no `TiltedPhoques` code is left. A track of its own now the server is gone (phase 5, 2026-10-21). What is
  left of `TiltedPhoques`: `TiltedCore`, `libraries/networking` (the enet transport under `HostNetwork` and a
  guest's `TransportService`), `libraries/hooks` and `libraries/reverse`.
  - Containers: `Vector`, `String`, `List`, `Deque`, `UniquePtr` are std types with TiltedCore's
    allocator (`Code/TiltedCore/Stl.hpp`); `Map`/`Set` are `tsl::hopscotch` (iterators need
    `it.value()` to assign). Plain std types would end the copies between `String` and `std::string`
    at every boundary (spdlog, nlohmann, RmlUi, engine strings).
  - First the allocator question: the DLL replaces global `new`/`delete`
    (`AllocatorSymbolOverride.hpp`), tied to SKSE load order, foreign threads and hot reload
    (`test_allocator_startup.py`, `Code/tests/hot_reload/allocator_startup.cpp`). Changing a container
    changes which allocator frees what; check the `ScopedAllocator` uses (`TransportService.cpp`) too.
  - Interfaces: `std::span`/`std::string_view` at boundaries, `std::expected` in place of `Outcome`
    (both builds are C++23 since 2026-10-21).
  - `Buffer`/`Serialization` stay as the wire format (pinned by `wire_tests.cpp`); only their
    interface could change. `ThisCall`/hooks have no std equivalent.
  - Start with a clang-tidy `modernize-*`/`readability-*` baseline count, then a short plan
    (`docs/design/`), then one area per change with the bridge tests and the affected scenarios.

## Join flow, later (2026-10-24)

- **Per-player mod data.** Mods keep player state the record cannot see: Papyrus variables on player aliases (survival
  needs, vampire overhauls, Wintersun), per-player globals, StorageUtil/JContainers keyed on the player, other plugins'
  co-save records. A declared adapter per mod (what is the player's, how to read and apply it) into
  `PlayerRecord::Extras`; RaceMenu first, through its `IPresetInterface` (also for copies' looks).
- **Bringing a character again.** Replace the host's record with the one of the friend's own game on purpose (today the
  host's save wins); export a co-op character to a single player save.
- **More of the character:** misc stats (global data 0), favourites and hotkeys (109), player-made potions and
  enchanted items (created `0xFF` forms), lasting effects (diseases, blessings).
- **Copies' looks follow a record:** a record whose base changed (barber, RaceMenu, race change) refreshes the friend's
  copies on the other games.
- **Delete the guest's `SkyrimCoopJoin` save** once loaded, so it does not sit in its load list.

- **First-time join through the vanilla intro** (out of v1, 2026-10-29). v1's first-time join skips Helgen with Quick
  Start - SE. Joining from the vanilla intro would bring a character made in the middle of Helgen's scene (bound
  hands, prisoner clothes, MQ101 items): check what it carries, then let players choose the cutscene
  (`docs/design/join-flow.md`, "First-time join").

- **The bundled character start with mods (2026-10-29).** `CreationStart` gives a new character the prisoner tunic
  and shoes (hard-coded) and its race's spells through vanilla's `MQ101QuestScript.AddRaceSpells` on MQ101 (3372B).
  A mod that starts players differently (its own spells or kit outside MQ101's script) is not applied. When mods are
  in scope: a mod-settings override for the kit and starting spells, or running the start mod's own setup.
