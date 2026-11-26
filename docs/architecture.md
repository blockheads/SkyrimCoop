# Architecture

How the code is organised, and why. One page; the details live in [docs/design/](design/).

## The idea

The host's game is the only authority. It owns every NPC and the world. Each guest owns only its own player.
There is no server. The words (host, guest, owner, copy) are in [the glossary](design/glossary.md).

## The layers

```
plugin    starts everything, wires the parts together
   |
engine    Skyrim itself: its structs and our hooks
   |
sync      what to send, what to accept, what to show
   |
encoding  the messages and how they go on the wire
```

**A layer may only use the layers below it.** That one rule is the architecture.

| Layer      | Holds                                              | Knows about Skyrim? |
|------------|----------------------------------------------------|---------------------|
| `plugin`   | SKSE entry, start-up, crash handler, UI            | yes                 |
| `engine`   | typed engine structs, hooks, calls into the game   | yes                 |
| `sync`     | every decision the mod makes                       | no                  |
| `encoding` | messages, shared structs, the wire buffer          | no                  |
| `network`  | the enet transport                                 | no                  |

`sync` never sees Skyrim, so it builds and runs on Linux in seconds, with no game. Most work happens there.

## How sync talks to Skyrim: ports

`sync` cannot call the engine, so each feature says what it needs in a small interface, a **port**.
`engine` implements it. The tests implement it too, with a fake.

```
sync/crime/CrimeService       "did anyone see that theft?"
        |  calls
sync/crime/ICrimeBridge       the port: a few methods, no Skyrim types
        ^  implemented by
engine/Bridges/CrimeBridge    the real one: asks Skyrim's witnesses
tests/games/FakeCrime         the fake one: witnesses the test sets up
```

Hooks go the other way. A hook in `engine` sees something happen and reports it as an event. It makes no decision.

## One folder per feature

Everything for a feature sits together:

```
sync/crime/
  CrimeService.h/.cpp     runs in every game
  HostCrime.h/.cpp        only the host runs this
  HostEye.h/.cpp          only the host runs this
  GuestEye.h/.cpp         only a guest runs this
  ICrimeBridge.h          the port
  CrimeRules.h            pure logic
  CrimeRules_test.cpp     its unit tests
```

Host-only code is in `Host*` files and guest-only code in `Guest*` files. Neither asks "am I the host?".

The folders in `sync`:

| Folder        | Holds                                                     |
|---------------|-----------------------------------------------------------|
| `core`        | what every feature uses: transport, session, ids, records |
| `game`        | wires the features into a host's or a guest's game        |
| `join`        | joining, save transfer, saves, Steam                      |
| `travel`      | the party travelling together, the map, markers           |
| `characters`  | spawning, who owns an actor, claims, looks, mounts        |
| `movement`    | how a copy moves                                          |
| `animation`   | how a copy animates                                       |
| `actorstate`  | actor values, actor state, AI state, perks                |
| `followers`   | followers                                                 |
| `combat`      | hits, aggro, kill moves, traps                            |
| `magic`       | spells and their effects                                  |
| `death`       | dying, spectating, respawning                             |
| `items`       | inventory, containers, party items, recipes               |
| `world`       | world state, doors, levers and other objects              |
| `physics`     | loose bodies and ragdolls                                 |
| `time`        | the clock, weather, time scale, waiting and sleeping      |
| `dialogue`    | conversations                                             |
| `scripts`     | Papyrus, quests, cutscenes                                |
| `crime`       | crime, bounty, stealth                                    |
| `tools`       | dev commands, bug reports                                 |

Two rules keep this simple:

- `core` uses no feature. A feature may use `core` and, sparingly, another feature. Only `game` uses them all.
- Something goes in `core` only when three or more features need it.

## How a message flows

```
a guest's service sends a request
  -> the network -> the host's rule for it (a Host* file)
  -> the host notifies the other games
  -> each game's service applies it
```

The host's own game takes the same path, without the network.

## The tests

| Tier | What                                          | Needs Skyrim? | Time    |
|------|-----------------------------------------------|---------------|---------|
| 1    | unit tests of pure logic                      | no            | seconds |
| 2    | whole games in one process, on a fake engine  | no            | seconds |
| 3    | two real Skyrims side by side ("the duo")     | yes           | minutes |

Tiers 1 and 2 run on every change (`scripts/check.sh`). More in [design/testing.md](design/testing.md).

## Where things are

| Layer    | Folder          | Target                                  |
|----------|-----------------|-----------------------------------------|
| `plugin` | `Code/plugin/`  | `SkyrimCoopPlugin` (with `engine`)      |
| `engine` | `Code/engine/`  | built into the plugin                   |
| `sync`   | `Code/sync/`    | `SkyrimCoopSync`, also in `GameTests`   |
| harness  | `Code/harness/` | test builds of the plugin only          |

- **A file's folder is its layer and its feature.** Includes say it: `<crime/CrimeService.h>`, `<core/World.h>`.
- **`sync` cannot see Skyrim.** It builds with no engine folder on its include path, and `scripts/check.sh` fails on
  an include that breaks a layer rule (`scripts/check_layout.py`).
- **One wiring.** `World` takes an `IEngine` (`Code/sync/core/IEngine.h`): every port in one place. The plugin passes
  Skyrim (`Code/plugin/SkyrimEngine.cpp`); the tests pass the fake. The same `World`, `HostService` and `GuestService`
  run in both.
- **`engine` keeps Skyrim's own groups** (`Forms/`, `AI/`, `Papyrus/`, ...), not our features. Our side of each port
  is in `engine/Bridges/`.

Not done yet:

- `IActorBridge` is eight parts, one per feature (`combat/IActorCombat.h`, `magic/IActorMagic.h`, ...), joined in
  `sync/ports/IActorBridge.h`. Most services still take the whole of it, not their part.
- Seven ports are one header with two `.cpp` files (the engine's and the tests', picked by the linker), not an
  interface: `scripts/PartyNatives.h`, `PlayerNatives.h` and `SystemQuests.h`, `core/RunningGame.h`,
  `MainLoopTasks.h` and `ItemIds.h` (`CreatedItems`), `combat/ActorUpkeep.h`. They work and are kept: a target
  that links all of `sync` (`GameTests`) must define each, in `Code/tests/games/Seams.cpp` or `HostSeams.cpp`.
- `CharacterService` is one class in four files by topic (`CharacterServiceSpawn.cpp`, `...Movement.cpp`,
  `...Mounts.cpp`); speech is its own service (`dialogue/SpeechService`), its mounts, looks, graph stream and
  movement not yet.
- Unit tests are in `Code/tests/`, not beside their feature.
