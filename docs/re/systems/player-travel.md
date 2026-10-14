# Player travel

How the player gets from one place to another: doors, fast travel, scripted moves, `coc`. They all end in
one engine call, which sends `PositionPlayerEvent`. That event is what our arrivals hang on
(`PlayerCharacter::ConsumeTeleported`, `TravelService`).

## The path (1.6.1170, verified in IDA 2026-10-13)

| Step | ID | What |
|---|---|---|
| Queue the move | 40442 | Copies a `PLAYER_TARGET_LOC` (CommonLibSSE-NG `PlayerCharacter.h`) into the player's `queuedTargetLoc` (`+0x648` on 1.6.1170: world `+0x648`, interior `+0x650`, location `+0x658`, `resetWeather` `+0x688`, `allowAutoSave` `+0x689`). Every travel source calls it. |
| Carry it out | 40438 | `ProcessEvents_Enter` (36564) calls it at the top of every frame; it does nothing unless a move is queued. Sends `PositionPlayerEvent` through the player's event source at `+0x388`: phases 0, 3, 4 (a `coc` or a door), 0 to 4 (fast travel, which also updates packages). Autosaves on the way when the target allows it (`Actor` 40445 → `BGSSaveLoadManager::Save_Impl`). |
| `coc` / `coe` / `cow` | 40437 `CenterOnCell_Impl` | Queues through 40442, then calls 40438 at once. |

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
- Fast travel passes game time on the traveler's game (measured: 1.2 h for ~16,000 units, the river to
  Riverwood). Under `HostClock` the client keeps the host's time even after its own trip.
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
