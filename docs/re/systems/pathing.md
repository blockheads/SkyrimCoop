# Navmesh pathing: PathToReference and why it refuses

Read on 1.7.104, 2026-11-06, for the playthrough flow's refused walks inside Embershard Mine
(`docs/design/playthrough-flows.md`, Open). Names are in the IDA database (journal
`docs/re/journal/1.7.104/agent-playthrough-flows.tsv`).

## The call

```
papyrus Actor.PathToReference (54731)
  Actor::RequestPathToReference (37893)
    Actor::FillPathToLocation (37820): a PathingRequest
      start = the actor's place (Pathing_DataCtor1 30635: its cached triangle, else its position and pathing cell)
      goal  = the target's position + pathing cell (Pathing::sub_30640 -> GetPathingCell 30682)
      Actor::FillPathingRequest (41642): speed, radius, height, flags (+0xD0: can walk / swim / fly, open doors)
    Actor::SetMovementPath (37801) -> ActorMover::SetGoal (41607)
      -> movement controller's IMovementSetGoal: MovementPathManagerArbiter::SetGoal (90853)
           PathingRequest::IsValid (90654): goal has a pathing cell, the actor moves somehow     else refused
           PathingRequestQueue::Submit (91394): bDisableUnloadedPaths (start or goal loaded);
             bBackgroundPathing: queued for the search thread, else Pathing::BuildPathNow (90396) now
    accepted: PathRequestRegistry::Register (91842) gives the id the Papyrus latent call waits on
    refused (-1): Papyrus log "Could not build path", returns false at once
```

The answer comes back as a movement message to the actor:

```
MovementPathManagerArbiter::UpdatePath (90873), each update
  state 1 (searching): the search's result
    0 -> follow it (state 2)
    else -> EndPath(failed) (90880)   unless partial paths are allowed (+0xBA & 1) and it has >= 2 points
  state 2 (following): arrived -> PathComplete; the follower gave up -> EndPath(failed)
EndPath(failed) -> MovementMessagePathFailed -> Actor::OnMovementPathFailed (37884)
                -> PathRequestRegistry::Finish(request, false) (91843) -> PathingCallbackMgr::Handle (53640)
                -> the latent PathToReference returns false
PathComplete    -> Actor::OnMovementPathComplete (37883) -> Finish(request, true)
```

So a `PathToReference` that returns false within a second (the harness's `path_leg` "refused") is either the
SetGoal refusal or the search failing. The harness can't tell these apart from Papyrus: both return false.

## Ends on the navmesh

`BSPathingLocation::ResolveToClosestNavmeshAndTriangle` (90369) picks, among the pathing cell's navmeshes, the
triangle closest to the point (height weighted: 10x below the triangle, 5x above). A point off the navmesh still
resolves, to the nearest triangle, so being off the mesh alone is not a refusal; a point that lands on a triangle
of another level or an unconnected piece gives a search with no answer. The search (`Pathing::BuildPathNow` 90396)
clears both ends' triangles (`BSPathingLocation::ClearTriangle`) and resolves them again in its builder.

## The readout

`{"cmd": "path_probe", "target": id}` (`EngineCalls::ProbePath`, test builds; `"from": id` for another actor, the player by default) does these steps for the player
without moving anyone. It resolves both ends (NavMesh form id, triangle, asked point and the point on the triangle), then fills a
request as `PathToReference` does and runs `BuildPathNow` at once (`built`, `incomplete`, `nodes`). With both ends
on one navmesh it walks that navmesh's triangles: edges, and linked edges that stay in it (portals, ledges). That
gives whether they join (`linked`, `hops`, closed doors and ledges on the way), the goal's piece (`goal_piece`
triangles, `goal_piece_exits` into other navmeshes), the door portals on each piece and the closed doors.
`walk_to` and `fight` put it in their `no_path` failure (`probe_line`), and `fight` logs it when it starts.
Threads (read in IDA): `BuildPathNow` ignores its first argument (singleton 401950) and calls the request type's
builder from a static table. Game code calls it on the game thread whatever `bBackgroundPathing` says
(`Character::sub_37434`, `ActiveEffect::sub_34360`, `Submit` with background off) while the search thread runs it for
queued requests (`PathingTaskData` 91414/91416), so the probe calling it from the game thread is what the game does.
Cleanup as `Submit` does it: `BSPathingSolution::dtor` (90962) then free the 176 bytes; each location's
`PathingData_dtor` (90350) releases its navmesh info.
Structs: `Games/Skyrim/AI/Movement/Pathing.h` (CommonLibSSE-NG's BSNavmesh, BSPathingLocation, PathingDoor).

## Obstacles

`NavMeshObstacleManager` (singleton 400442, ctor 30441) keeps a `BSTHashMap` at +0x10 (capacity +0x1C, entries
+0x38) from an obstacle reference's form id to its cut: an array at +0x18 of cuts, each listing at +0xA0 the
BSNavmeshInfos it touches (`NavMeshObstacleManager::PrintObstacles` 30514, the "Current Obstacles" text).
`path_probe` lists them as `obstacle_refs` (form id, base, actor, distance, whether it cuts the start's navmesh), and
`probe_line` names those cutting it, with the harness's role (player, copy).

## Embershard Mine (2026-11-06)

The flow's `fight` step, from the `enter` checkpoint (the host at the mine's door, the first bandit B6DBB about
1,900 units in), six runs on 2026-11-06:

- Refused in five; the probe ran in four of them. Each time both ends resolved onto one navmesh, B6FC4: the player 3 to 33 units off a
  triangle by the door (98, 99, 105, 108 or 117), the bandit 38 units off triangle 326. The engine's search found no
  path.
- Triangle 326 is in a piece of B6FC4 of 649 triangles that no edge, same-navmesh portal or ledge leaves, and no
  link leads out of it into another navmesh. The player's triangles are outside that piece. No door portal sits on
  either piece. The search holds five closed-door entries: the mine's door B6CCD, and B6D81 and D1F0A twice each.
  The probe read 6 at CommonLib's obstacles array (+0xD0). The engine's debug text
  (`NavMeshObstacleManager::PrintDebugText` 30452) reads +0xF8 on the NavMesh form, whose BSNavmesh part starts at
  +0x28 (TESForm 0x20, TESChildCell 8): the same array.
- Passed in one, from the same checkpoint. `leave` ran then for the first time and worked.

Two more refused runs with the obstacle readout (2026-11-06): the references cutting B6FC4 are the same four each
time, none an actor, so the guest's copy is not the cut. An NPC's request is refused the same way (`path_probe`
`from` the bandit to the player: the entrance's piece, 397 triangles, has no way out either), so it is not the
player's request.

Disabling them in the running host game (console `disable`, then the probe again):

- all four off: the engine finds the path both ways, 145 triangles, no ledges (the ramp the player stops at);
- each alone back on: only 105C99 cuts it. It is a `CollisionMarker` (STAT 0x21) of Skyrim.esm at its editor
  position (807, 3859, 8392); the two HandCart01Wheel movable statics (B6D19, B6C20) and 613179F don't.

Without a session (`coop_test.py launch` of the same save, our plugin loaded but idle) it is the same in three
loads of three: the same six obstacles, refused both ways. So no copy, message or host rule of ours makes it; the
game cuts that ramp for every navmesh path from this save, the player's included. A player walks it by input, which
no navmesh path limits. Not checked: a game without our plugin (the `--vanilla` stand-in has no `path_probe`).

Crossing by input (`walk_to` -> `cross`, `path_probe`'s `crossing`: the start piece's way to the nearest ramp-steep
step onto the goal's piece, shared edges' midpoints). In the mine the player runs the whole way by input, swimming
through the water on it, and stops at the step: (387, 3509, 8548) toward (812, 3341, 8409), 480 units, not moving at
all, near the CollisionMarker (807, 3859, 8392). With a 45-degree limit it picked a wall instead. The marker is an
invisible wall for the player too (a jump rises 19 units and stops there; `terrain.md`): the cut is the level's
design. `clear` now fights only who the engine's search reaches (`reachable_hostiles`), and the crossing stays for
cuts a player can pass.


## Path length (2026-11-06)

`path_probe`'s `path_length`: the built path through `BSPathingSolution`'s loaded nodes (+0x90, `BSPathingNode` 0x48:
location first, CommonLib layout; the dtor 90962 steps 0x48), then on to the target. Riverwood to the Trader's door:
7,541 units against 2,900 straight (round the river), so `walk_to` takes its time limit from it. `clear` counts a
hostile reachable when the search builds a path, or when the triangles join on a navmesh no obstacle cuts (the walk
ignores cuts). Open: at the lookout both games' searches fail (none, 0 nodes) where the triangles join and no obstacle
cuts; and in the mine a search toward B6FDB that `clear` saw built came back none for the walk seconds later.
