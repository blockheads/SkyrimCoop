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

## Sync

- **Everyone's load starts with the leader's** (2026-10-19): today a follower starts travelling once the
  leader has arrived, so the trip costs the leader's load and then its own (`docs/progress.md`, "Tether
  gaps"). Start every game's load when the leader's starts (`coc`, a load door, fast travel), from the
  queued move (40442) rather than the arrival (40438), so the loads overlap.
- **World state through the save system.** The host's world (every mod's quests, globals, containers,
  objects) synced from the engine's own save/load code: a full sync at join, and the changed forms over
  time. Design: `docs/design/world-state-sync.md`.
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

- **Client services by role.** The client keeps upstream's layout, one service per message family, so each
  service mixes our own player, copies and the host's actors, and copy checks (`IsCopy()`) are scattered
  through all of them. `CharacterService` is ~1,600 lines with 26 subscriptions (ownership, spawns,
  movement, presence, factions, experience, dialogue, mounts, packages, beast form, respawn);
  `PlayerService` groups respawn, difficulty, levels and beast form. Target: services by role (our own
  player: travel, death and respawn, levels and XP, the host's settings; copies: spawn, presence, replay,
  one copy update in place of the `IsCopy()` checks; the host's actors), as `Services/Host/` already is.
  `TravelService` (2026-10-14) was the first piece. After phase 5 (the listener moving into the bridge
  changes how messages reach these services): a short survey and plan in `docs/design/`, then one piece at
  a time with its scenarios, deleted from the old place in the same change.
- **Engine bridge interfaces: finish them or drop them.** Services reach the engine through `I*Bridge`
  interfaces (`IActorBridge`, `IFormBridge`, `IPlayerBridge`, `IEventBridge`, `IWorldBridge`, and
  `IDialogueBridge` on the parked dialogue branch) so they could run headless against `ReplayBridge`, which answers with
  a recording's canned values. That plan stopped half way: `ClientReplayTests` (5 cases) tests `ReplayBridge`
  itself and runs no service's logic, `SkyrimTogetherClientServices` (the target proving services build
  without engine headers) does not compile and nothing links it (`Code/client/xmake.lua`), and
  `IDialogueBridge` has no second implementation at all. Today each interface is a second copy of every
  declaration for a substitution nobody makes. Decide for all of them at once:
  - Finish: a scripted fake per bridge where a service has logic worth testing headless, then tests in the
    style of `BridgeTests`. `DialogueService` first (who is in, a start that never opens, picks racing a new
    list; covered only by two-game scenarios today), then the boundary target building.
  - Or drop: services hold the concrete bridges (their headers are already free of engine types), and
    `ReplayBridge`, `ClientReplayTests` and the boundary target go.
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
