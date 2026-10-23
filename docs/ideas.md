# Ideas for later

One place for ideas and decided-but-not-built features. Each entry: what, why, and where its notes are.
Move an entry out when work on it starts (into a design doc in `docs/design/`).

## Gameplay

- **The party is the Dragonborn** (shared quest rewards and identity; each player's own items and gold; cutscenes for
  everyone; shared dialogue): decided, `docs/design/party-model.md`; shared dialogue in `docs/design/shared-dialogue.md`.
- **More natives on the player** (2026-10-22). Rule 2 sends the natives that act on this player (the table in
  `docs/re/systems/papyrus-guest-scripts.md`, from `scripts/re/papyrus_natives.py`) to the guest's game, values
  included. Left: the screen's natives (controls, fades, camera, shakes: the cutscene design), member natives that
  open this player's menus on another actor (`ShowGiftMenu`, `ShowBarterMenu`, `OpenInventory`: a method call on the
  guest's copy, with barter on the picker's game), `GetPlayerGrabbedRef` (returns an object a `ScriptValue` cannot
  carry), words of power (the party's: mirror to every game), and a save taken while a script waits for a guest's
  answer (the wait has nothing to answer it after a load). Also open, a decision: what a guest's script gives the
  player to own (`party-model.md`, "Items and gold").
- **Giving items between players** (2026-10-22). Each player has their own inventory: a guest hands a quest item
  to the host to turn it in (dialogue conditions ask the host's inventory). `party-model.md`, "Items and gold".
- **Shared dialogue: others' cursors, picks and names on each menu.** Each menu shows only its own player's
  highlight; another player's pick shows in chat, and the menu waits for the next list. Built and dropped
  2026-10-19: a shared cursor, other players' options marked `[Name]`, and showing a pick on every menu with
  that topic. All three live only in the vanilla SWF (`TopicList`, `SetSelectedTopic`, `onSelectionClick`), so
  a replaced dialogue menu breaks them, and a pick of a topic a menu lacks never showed anyway. Bring back
  together, as a menu feature with a fallback for replaced SWFs. Notes: `docs/re/systems/dialogue.md` (the
  SWF, "Showing a pick from elsewhere").
- **Shared party bounty.** One player's crime counts for the whole party, judged on the host (its NPCs are
  the witnesses). Decided 2026-10-12; today a remote player cannot commit a crime on the host at all.
  Notes: `docs/design/copy-contract.md` (crime row).
- **Peer teleport requests.** Teleport is host-only today. Later a peer can ask for one as a
  Borderlands-style request that anyone can cancel, like leaving a cell under the party tether.
  Notes: `docs/design/host-authority-migration.md` (Decisions, 2).
- **Tether countdown.** Today anyone's travel takes the party along at once (Decisions, 4). A short
  countdown that anyone can cancel, as Borderlands does, before the party follows.
- **Shared wait and sleep** (user, 2026-10-20). Whoever starts a wait asks the host; every game shows the
  Wait menu and pauses, and the host's countdown (`PlayerCharacter::WaitStep`, 40485) drives them all, so
  nobody plays through the jump. Today the host's wait jumps everyone's clock while the others play on, and
  a guest's wait passes no time (`docs/re/systems/calendar.md`).

## Sync

- **Per-player system quests (mod support, 2026-10-21).** Under "only the host decides", every quest's
  scripts run on the host only, so a quest that tracks one player's body (Frostfall's exposure system,
  needs mods, vanilla `PlayerVampireQuest`) tracks only the host's player: a guest never gets cold or hungry
  on its own. Later, an opt-in list of such quests that also run on each guest's game for its own player,
  with the globals their scripts name kept per game (left out of the host's global sync). The list first,
  shipped for known mods; then found by a heuristic (no stages, objectives or aliases but the player's) or
  from the compiled scripts (`.pex`: which natives, on what). Their world writes (a campfire, harvesting) go
  to the host as a guest's actions. Notes: `docs/research/guest-scripts-and-mods.md`.
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

- **Everyone's load starts with the leader's** (2026-10-19): today a follower starts travelling once the
  leader has arrived, so the trip costs the leader's load and then its own (`docs/progress.md`, "Tether
  gaps"). Start every game's load when the leader's starts (`coc`, a load door, fast travel), from the
  queued move (40442) rather than the arrival (40438), so the loads overlap.
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
- **Havok sync.** Physics objects and ragdolls from the host, live. Should fix failures where the games'
  physics disagree. Planned after phase 5 (`docs/design/host-authority-migration.md`, "After: physics sync");
  rest positions come first with world sync (`docs/progress.md`, "World sync: finishing it", step 5).
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

- **One template for relayed messages** (2026-10-22, the user's review: "too much boilerplate?"). 36 of the 84
  message headers are a request or notification wrapping a shared payload (`Messages/Payloads.h`), each the same
  20 lines (756 in all; `PlayerNativeRequest.h` and `NotifyPlayerNative.h` are a pair). A template
  (`Relayed<Payload, Opcode>`) makes each a `using` line. A new message is also listed by hand in three places
  (`Opcodes.h`, `RequestMessageFactory.h`, `NotifyMessageFactory.h`): one list could make all three. The bytes stay
  as `wire_tests.cpp` pins them. Its own commit, repo-wide.
- **One line per hook** (2026-10-22). 127 hooks repeat `POINTER_SKYRIMSE`, `Real = .Get()` and `HOOK` in their
  initializer, plus a function-pointer global each. A helper (`Hook<ID>(&Real, &HookFn)`) makes it one line; the
  signature check against the community name and IDA stays. When a file is touched anyway.
- **Event connections without a member each** (2026-10-22). 109 `entt::scoped_connection` members, each with its
  connect line (`DialogueService` has 7). A `std::vector<entt::scoped_connection>` filled by one
  `Listen<&T::OnX>()` helper drops the members. When a file is touched anyway.
- **Events over polling** (user, 2026-10-22: "we always want to use events over polling if possible"). Discrete
  state should go out when an engine event or hook says it changed, not from a timer that re-reads it.
  Continuous state (movement, animation variables) on the tick is fine. The timed reads left, mostly upstream's,
  with the event or hook that could replace each (not checked in IDA unless said):
  - `ActorValueService`: small health changes (250 ms), pools (100 ms), death states (250 ms, plus a 500 ms
    retry), all actor values (1 s): `TESDeathEvent`, hooks on the actor value modifiers (`DamageActorValue`,
    `RestoreActorValue`, `SetBase`).
  - `CharacterService`: factions (2 s): hooks on `SetFactionRank`/`RemoveFromFaction`; experience (1 s): the skill
    use hook it already has; actor discovery (each frame over every loaded actor): `TESCellAttachDetachEvent`,
    `TESObjectLoadedEvent`; presence (2 s retry).
  - `InventoryService`: weapon state (each frame), equipment snapshots (3 s), the naked NPC check (1 s):
    `TESEquipEvent`, the draw/sheathe actions it already hooks.
  - `PlayerService`: level (1 s, "the LevelUp hook is kinda weird"), beast form (250 ms, the race):
    `TESSwitchRaceCompleteEvent`; difficulty (each frame): the settings write.
  - `EffectVisualsService`: 250 ms checks and a full send every 2 s: `TESActiveEffectApplyRemove`.
  - `DialogueService`: menu open and speaker each frame: `MenuOpenCloseEvent` (the archive's client had it).
  - `HostWorldState`: globals and quests on intervals: `TESQuestStageEvent`, `TESQuestStartStopEvent`; globals
    have no event (a hook on the global's value write).
  - Done: `AiStateService` (2026-10-22, `TESPackageEvent`, `TESCombatEvent`, `TESSceneEvent`).
  Per service, with its scenarios; drop any periodic full resend once the spawn or join carries the state.

- **Rename `BridgeTests` to `HostAuthorityTests`** (2026-10-20). `HostBridge` became `HostAuthority`, but
  its tests are still the `BridgeTests` target in `Code/tests/bridge/` (`[Bridge]` tags, `BridgeTest`
  namespace). Rename the target, folder and tags together with every doc and script that names them
  (`CLAUDE.md`, `progress.md`, the design docs).
- **One call for a form's session id** (2026-10-21). ~60 call sites repeat
  `if (m_mods.GetServerModId(formId, id))` to turn a game form id into the wire's `GameId`, and on the host it
  almost never fails: every standard plugin's form passes, one from a plugin missing from the session list
  silently gets session id 0 (the first plugin, normally `Skyrim.esm`, so the guest finds the wrong form),
  and a created `0xFF` reference gets `0xFFFFFFFF`, which no guest finds. Only a light plugin missing from the
  list fails. Replace it with one call returning an optional `GameId` (`ModSystem::ToSession`), which fails for
  `0xFF` ids and unknown plugins of both kinds and logs once; then drop the repeated checks. Code:
  `Systems/ModSystem.cpp`, session ids from `HostSession.cpp`.
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
- **Drop the engine bridge interfaces (later refactor, leaning drop, 2026-10-20).** Services reach the engine
  through `I*Bridge` interfaces (`IActorBridge`, `IFormBridge`, `IPlayerBridge`, `IEventBridge`, `IWorldBridge`,
  and `IDialogueBridge` on the parked dialogue branch), meant for running services headless against a fake.
  The fake never got there: `ReplayBridge` (canned answers from a recording), its `ClientReplayTests`, the
  services-only target that never compiled and `SkyrimBridge`'s recording were deleted 2026-10-20. Today
  each interface is a second copy of every declaration with one implementation.
  - The plan: services hold the concrete bridge; a service with logic worth testing headless gets a narrow
    seam of its own where it needs one (callbacks, as `HostGame` gives the bridge's rules to `BridgeTests`),
    not a fake of the whole engine. If `SkyrimBridge` (~1700 lines, every area) is too big to hold whole, split
    it into concrete per-area classes (`ActorBridge`, `WorldBridge`...) without the virtual layer.
  - What it costs: a fake engine for services is no longer an option (nobody uses one); an edit to the bridge's
    header rebuilds every service, not only the bridge; the interfaces' grouping by area goes unless the
    bridge is split. Services stay free of engine types as long as the bridge's header is.
  - What it gains: one declaration per method instead of two, no virtual calls, no guessing which interface a
    method belongs on (`GetModCount` is on both `IFormBridge` and `IWorldBridge`).
  - Not a reason to keep them: a second game or platform (Skyrim only; the Linux tools use no client service).
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
