# Terrain: the ground as a walking player meets it

Read on 1.7.104, 2026-11-06, for the harness's `terrain` readout (`TestHarness/Terrain.cpp`,
`scripts/coop_harness/terrain.py`), after the playthrough flow kept guessing at walls and ramps in Embershard Mine
(`docs/re/systems/pathing.md`). Names are in the IDA database (journal `agent-playthrough-flows.tsv`).

## The player's capsule

Each claim says how it is known: read (IDA), measured (in the game), CommonLib (its layout, checked by
`static_assert`), or estimated.

- The player's character controller (`middleProcess->charController`) is a `bhkCharProxyController`; the pointer is to
  its `bhkCharacterController` part, at +0x10 (vtable ID 240560). CommonLib; measured (the vtable at runtime).
- `radius` +0x290 and `height` +0x294, in Havok units (x 69.99 for game units). CommonLib; measured 18 and 128 for the
  player.
- `actorHeight` +0x260 (measured 128), `scale` +0x258. CommonLib.
- The proxy: `bhkCharacterProxy` at full +0x340, its `hkpCharacterProxy*` at the wrapper's +0x10, so +0x340 of the
  controller part: read (`bhkCharProxyController::GetLinearVelocityImpl` 79145 reads it).
  `hkpCharacterProxy::maxSlopeCosine` +0xD8 (Havok's layout) measured 0 on the player: not where Skyrim keeps the
  walkable slope. Open: where it does.
- Step height: not found. Havok's proxy has none (it climbs by slope); whether Skyrim adds one is not read.
- `terrain.py`'s limits are estimates until then: a 50-degree slope, 10 units a capsule's round bottom rides over, a
  surface standable from normal z 0.5.

## Rays

`bhkWorld::PickObject` (77860) refuses a pick (`pickFailed`) once the frame's ray time passes a setting (two floats,
one chosen by a flag; a third flag lifts the limit). So a scan of thousands of rays runs over frames: `terrain` starts
a job and gives a ticket, `TerrainScan::Poll` casts columns each frame until a pick fails, `terrain_result` reads it.
A 41x41 scan (1,000 units at 50) took 5 frames in the mine.

## The readout

Per cell, a column of downward rays on layer 30 (what stops a walking player), each going on 4 units under the last
hit: up to 4 surfaces with height, normal z, reference and layer; an upward ray over each standable one (normal z
> 0.5) gives its headroom. The loaded navmesh under the spot is rasterized into the same grid (no rays), labelled in
pieces (`WalkFrom` from the player's triangle, then the target's, then the rest; links can be one way, a ledge down,
so a later piece keeps an earlier piece's triangles).

`terrain.py` floods from the player's cell: a neighbour's floor is walkable when its rise is within
cell x tan(max slope) + 10 and it has the player's height of room. The rest is drawn by why it stopped (wall, drop,
low ceiling), with the navmesh's pieces, obstacles, the player and the target. MCP `terrain`, verb `terrain`; the
crossing's stalls and `fight`'s no_path carry its summary and file.

## Embershard Mine (2026-11-06)

The obstacle cutting the ramp (CollisionMarker 105C99, `pathing.md`) is an invisible wall for the player too: the
input crossing stops against it, and a jump rises 19 units and stops in the same place. The cut is the level's
design; the first bandit (B6DBB) and three others are behind it. `clear` now fights who the engine's search reaches
from the player, nearest along the way, and skips the rest: in the mine two bandits (B6FE4, B6FDB), four skipped.

## Riverwood and the lookout (2026-11-06)

- Rays pass through actors (Terrain.cpp `IsActor`): a copy standing beside the player read as a 13-unit ceiling, and
  the walk started on a tree's branches (layer 13) above it (measured: the scan's columns). The walk starts on the
  floor nearest the player's feet.
- `player.moveto` the mine's outer door (the fixture B6CCE) left the party in a nook off the navmesh, walled from the
  lookout's slope (measured: the scan and the probe, both games' searches failing from it; why they fail there is
  not read). The flow now goes in (`coc EmbershardMine01`) and out by the door, so the game sets the party at the door's
  marker. `step_onto_navmesh` (the terrain's walkable way to the nearest navmesh floor, by input) exists for such
  nooks; in this one it stalled against the other player, so it is not relied on yet.
- Outdoors the readout draws only the navmesh under the player's cell; neighbouring cells' read as walkable off it.
