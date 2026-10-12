# Ideas for later

One place for ideas and decided-but-not-built features. Each entry: what, why, and where its notes are.
Move an entry out when work on it starts (into a design doc in `docs/design/`).

## Gameplay

- **Shared dialogue.** When a player talks to an NPC, every player sees the dialogue menu at the same
  time and anyone can pick the option. Chaotic on purpose: a co-op game, and a bad pick is the party's.
  Builds on the dialogue sync (voice lines and subtitles already reach copies).
- **Shared party bounty.** One player's crime counts for the whole party, judged on the host (its NPCs are
  the witnesses). Decided 2026-10-12; today a remote player cannot commit a crime on the host at all.
  Notes: `docs/design/copy-contract.md` (crime row).
- **Peer teleport requests.** Teleport is host-only today. Later a peer can ask for one as a
  Borderlands-style request that anyone can cancel, like leaving a cell under the party tether.
  Notes: `docs/design/host-authority-migration.md` (Decisions, 2).

## Sync

- **World state through the save system.** The host's world (every mod's quests, globals, containers,
  objects) synced from the engine's own save/load code: a full sync at join, and the changed forms over
  time. Design: `docs/design/world-state-sync.md`.
- **Mod patches.** Scripts that check `== Game.GetPlayer()` patched to `SkyrimTogetherUtils.IsPlayer`;
  maybe one Papyrus function that forwards a mod event to the other games, so a patch can sync its own
  state. Notes: `docs/design/world-state-sync.md`, `docs/design/copy-contract.md` (deliberate exceptions).
- **Mod list check on by default in host mode** (`ModPolicy:bEnableModCheck`), so both players run the
  same mods.
- **Havok sync.** Physics objects and ragdolls from the host. Should fix failures where the games'
  physics disagree.
- **Graph replication (prediction + reconciliation).** Actions and variables keep driving copies
  (prediction); the owner's behaviour graph state, delta-synced (a byte-level delta of each
  `SaveState` snapshot against the last one sent, so the format stays opaque), corrects what drifted
  (reconciliation). Payoff: animation mods sync with no descriptors, and refused replays get corrected.
  The work is the correction layer, since `LoadState` is a hard set: fire the annotations a forward jump
  skips (weapon attach, footsteps, hit frames), suppress repeats on a backward one, keep the actor's
  state flags in the same correction. Log every correction's size; scenarios assert they stay rare.
  First an experiment: time `SaveState`/`LoadState`, delta size per tick (players and host NPCs), and
  whether `LoadState` fires state-enter events. Builds on the spawn graph state
  (`docs/re/systems/actor-ai-and-copies.md`).
- **Graph state over the world-sync buffers.** `AnimationGraphState` has its own stream adapter for the
  engine's graph serializer; once world sync has save/load form buffers, the graph state can use them.
- **Rest of the copy audit.** After the AI update (`docs/re/systems/actor-ai-and-copies.md`): combat,
  detection, magic/magicka. Then gather the scattered `IsRemote()` checks into one copy update.

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

## UI

- **The other screens in RmlUi.** The party panel is done (`Code/client/Ui/`, `skyrim.rcss`). Still to
  make, in the same SkyUI look: chat, player list, connect, settings, notifications.

## Code cleanup

- **`SKYRIM_STRUCT_ASSERT` → `static_assert`.** clang-cl defines `_MSC_VER`, so the macro is already a
  `static_assert` in every build we make; its MinGW branch is dead. Replace the ~209 uses (79 files),
  drop the macro from `TiltedOnlinePCH.h`, and drop the `is_plat("mingw")` branches in `xmake.lua` and
  `Code/client/xmake.lua`.
- **`TiltedPhoques` containers → std.** `Vector`, `String`, `List`, `Deque`, `UniquePtr` are std types
  with TiltedCore's allocator (`Code/TiltedCore/Stl.hpp`); `Map`/`Set` are `tsl::hopscotch`. Plain std
  types (with mimalloc as the global allocator) would end the copies between `String` and `std::string`.
  Large: the types are in the message structs shared with the server; check the `ScopedAllocator` uses
  (`TransportService.cpp`) first. `Buffer`/`Serialization`, `ThisCall`/hooks and networking have no std
  equivalent and stay (maybe in our own namespace).
