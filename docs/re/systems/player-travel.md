# Player travel

How the player gets from one place to another: doors, fast travel, scripted moves, `coc`. They all end in
one engine call, which sends `PositionPlayerEvent`. That event is what our arrivals hang on
(`PlayerCharacter::ConsumeTeleported`, `TravelService`).

## The path (1.6.1170, verified in IDA 2026-10-13)

| Step | ID | What |
|---|---|---|
| Queue the move | 40442 | Copies a `PLAYER_TARGET_LOC` (CommonLibSSE-NG `PlayerCharacter.h`) into the player's `queuedTargetLoc` (`+0x648` on 1.6.1170: world `+0x648`, interior `+0x650`, location `+0x658`, `resetWeather` `+0x688`, `allowAutoSave` `+0x689`). Doors, fast travel and MoveTo call it; coc writes these fields inline. |
| Carry it out | 40438 | `ProcessEvents_Enter` (36564) calls it at the top of every frame; it does nothing unless a move is queued. Sends `PositionPlayerEvent` through the player's event source at `+0x388`: phases 0, 3, 4 (a `coc` or a door), 0 to 4 (fast travel, which also updates packages). Autosaves on the way when the target allows it (`Actor` 40445 → `BGSSaveLoadManager::Save_Impl`). |
| `coc` / `coe` / `cow` | 40437 `CenterOnCell_Impl` | Writes queuedTargetLoc inline, then calls 40438 at once (rechecked 2026-10-27). |

### Who queues a move (every caller of 40442)

| Caller | ID | In the game |
|---|---|---|
| `TESObjectDOOR` | 17924 | Load doors |
| `MapMenu` | 53102 | Fast travel from the map, once confirmed; sets the marker handle (`fastTravelMarker`) |
| `papyrus::Game::FastTravel` | 55457 | Carriages and scripted fast travel; sets the marker handle, `resetWeather` false |
| `GameFunc::handler::FastTravel` | 22612 | The `FastTravel` console function; sets the marker handle |
| `TESObjectREFR::MoveTo_Impl` | 56626 | Any `MoveTo` / Papyrus `SetPosition` of the player, **even within its cell**: quest teleports, `player.moveto`, jail (by its marker) |
| `PlayerCharacter` (with `GameFunc::handler::MoveToMarker` 22278) | 22279 | Script / console moves to a marker |
| `LatentPlayerMover` | 55427 | Papyrus `Debug.CenterOnCellAndWait` / `PlayerMoveToAndWait` (55368, 55369) |
| `TESObjectCELL` / `WinMain` path | 36642, 36549 | Startup and new game: the default cell, not mid-session |

Not through it: walking or riding across exterior cell borders (no teleport; the grid moves) and
loading a save.

So the arrival needs no hook per kind of travel. What differs between kinds is what happens around the move:

- A door's arrival autosaves; `coc` does not (new vanilla lock patterns on that path, `scripts/lock_order_known.txt`).
- Fast travel measures a calendar step on the traveler's game (1.2 h for ~16,000 units, the river to Riverwood).
  A guest sends that skipped native step to HostClock; the host advances once and shares its calendar (2026-10-27).
- A `MoveTo` within the cell sends the event too; the arrival keeps the copies when the cell is the same
  (`CharacterService::OnPlayerArrived`, `teleport-keeps-copies`), and nobody follows (already there).

The party tether (`TravelService`) moves the others with `MoveTo` on their player (56626, so through 40442 like
any travel): their arrival is the same event, sent as following, not leading
(`docs/design/host-authority-migration.md`, "Party tether"). The players' copies are persistent and moved with
their owners, so no travel drops or respawns them (`docs/design/copy-contract.md`).

Scenarios: `party-uses-door` (the host leads out, the client leads back in),
`party-fast-travels` (each leads once, the other following within the same worldspace), `party-changes-cells`
(`coc`), `teleport-keeps-copies` (a move within the cell). The map's own UI (open the map, pick a marker,
confirm) is not driven: the console's `FastTravel` queues the same move with the marker.

## A move inside the cell (1.7.104, read in IDA 2026-11-09)

What `/tpa` relies on: moving another player with `MoveTo` works from anywhere, its own cell included.

- `TESObjectREFR::MoveTo_Impl` (56626, `0x140a5d370`) checks only whether the reference is the player. For the player
  there is no same-cell test: it always builds a `PLAYER_TARGET_LOC` and queues it (40442); actors go through 37313
  instead, other references through `SetPosition`.
- `PlayerCharacter::QueueTravel` (40442) copies it into `queuedTarget`, at `+0x650` on 1.7.104 (`+0x648` on 1.6.1170,
  `PlayerCharacter.h` asserts it with `PlayerShift`).
- `PlayerCharacter::ProcessQueuedTravel` (40438) loads a new location only for another worldspace or more than 4096
  units outdoors, or another interior cell. A target nearer than that still moves the player and still ends in
  `PositionPlayerEvent`, so the arrival is reported (`ConsumeTeleported`) as for any travel.

So the tether's "already there" (`TravelService::FollowBetweenFrames`) is our rule, not the engine's: a forced follow
(`aForce`, used by respawn and `/tpa`) moves a player that is in the cell already.

### /tpa (2026-11-09)

The chat's `/tpa` (or `/teleportall`) brings every other player to whoever typed it:

1. The summoner's game picks a checked spot beside its player (`GameRaycast::SpotBesidePlayer`:
   `CheckedPlacement::Beside` on `CastRay`, the same as a join's placement) and sends `PartySummonRequest`.
2. The host's rule (`HostCharacters::OnPartySummon`) sends `NotifyPartySummon` to every other game and makes the
   spot the tether's last place, so a follower arriving there is not sent on to an older trip.
3. Each game makes a forced tether move there (`FollowTo(..., true)`), so the arrival is following, not leading.
   The arrival stacks the party even inside the same cell. Down players go too, as the tether takes them.

Measured in the duo (`party-summon-in-cell`): the host moved 740 units inside QASmoke, the guest typed `/tpa`, and
the host arrived 120 units from the guest (`Beside`'s radius).

## Copies left behind by travel

Found with `party-fast-travels` (2026-10-13): the host arrived in Riverwood while the client was still at
the river. Outdoors `HostCharacters` counts a player "with the host" by worldspace, so the host got the
client's spawn at the river. The engine deleted that copy (a temporary reference in a cell it does not keep
loaded), and the client's real arrival spawn was then refused as "already spawned": the host never showed the
client again.

Fix: presence reconciliation (`docs/design/host-authority-migration.md`, "Copies converge on their owner").
A spawn for a copy we have refreshes it (`CharacterService::RefreshCopy`), and each copy whose owner is
around us but which is elsewhere, without 3D or gone is moved there or spawned again
(`CharacterService::RunPresence`, the host answering a non-owner's `RequestRespawn` with a fresh spawn).

Since 2026-10-14 players' copies are persistent and travel with the party, so this only applies to the host's
actors (NPC copies); a copy is the actor at its form id only while flagged a copy
(`docs/re/crashes/2026-10-14-reused-copy-form-id.md`).

## Departure before arrival (2026-10-27)

`HookProcessQueuedTravel` (40438) observes the typed `PlayerCharacter::queuedTarget` at +648 before the native
load, queues `PlayerTravelQueuedEvent` for the rules and releases loading only after the destination has been
sent. It catches coc's inline queue as well as 40442's callers. No networking runs in the native cell-lock path.
The actual arrival still reconciles the tether. Timings and cached-load/network limits:
`docs/design/party-travel.md`.
