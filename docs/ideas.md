# Ideas for later

One place for ideas and decided-but-not-built features. Each entry: what, why, and where its notes are.
Move an entry out when work on it starts (into a design doc in `docs/design/`).

## Gameplay

- **The party is the Dragonborn** (shared gold, quest rewards, quest items, identity; cutscenes for everyone; shared
  dialogue): decided, `docs/design/party-model.md`; shared dialogue in `docs/design/shared-dialogue.md`.
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
- **Havok sync.** Physics objects and ragdolls from the host. Should fix failures where the games'
  physics disagree.
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

- **Animation trace, next steps.** The trace (`AnimTrace`, `docs/progress.md`) runs inside scenarios.
  Next: a `coop_test.py trace --seconds N` for the running games without a scenario (both players and
  everything near them); a trace kept automatically when a scenario fails; marking the first frame where
  an owner and its copy diverge (an action or graph event one has and the other lacks); the copy's
  replay outcomes (taken, refused, how late) as entries rather than log lines.

## UI

- **The other screens in RmlUi.** The party panel is done (`Code/client/Ui/`, `skyrim.rcss`). Still to
  make, in the same SkyUI look: chat, player list, connect, settings, notifications.

## Code cleanup

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
- **`SKYRIM_STRUCT_ASSERT` → `static_assert`.** clang-cl defines `_MSC_VER`, so the macro is already a
  `static_assert` in every build we make; its MinGW branch is dead. Replace the ~209 uses (79 files),
  drop the macro from `TiltedOnlinePCH.h`, and drop the `is_plat("mingw")` branches in `xmake.lua` and
  `Code/client/xmake.lua`.
- **Modern C++ over `TiltedPhoques`.** A track of its own after the server is gone (phase 5 deletes
  `Code/server` and replaces `libraries/networking`, a large share of the `TiltedPhoques` code).
  - Containers: `Vector`, `String`, `List`, `Deque`, `UniquePtr` are std types with TiltedCore's
    allocator (`Code/TiltedCore/Stl.hpp`); `Map`/`Set` are `tsl::hopscotch` (iterators need
    `it.value()` to assign). Plain std types would end the copies between `String` and `std::string`
    at every boundary (spdlog, nlohmann, RmlUi, engine strings).
  - First the allocator question: the DLL replaces global `new`/`delete`
    (`AllocatorSymbolOverride.hpp`), tied to SKSE load order, foreign threads and hot reload
    (`test_allocator_startup.py`, `Code/tests/hot_reload/allocator_startup.cpp`). Changing a container
    changes which allocator frees what; check the `ScopedAllocator` uses (`TransportService.cpp`) too.
  - Interfaces: `std::span`/`std::string_view` at boundaries, `std::expected` in place of `Outcome`
    (check what C++ version clang-cl with the MSVC STL allows; C++23 likely).
  - `Buffer`/`Serialization` stay as the wire format (pinned by `wire_tests.cpp`); only their
    interface could change. `ThisCall`/hooks have no std equivalent.
  - Start with a clang-tidy `modernize-*`/`readability-*` baseline count, then a short plan
    (`docs/design/`), then one area per change with the bridge tests and the affected scenarios.
