# Terrain: the ground as a walking player meets it

Read on 1.7.104, 2026-11-06, for the harness's `terrain` readout (`TestHarness/Terrain.cpp`,
`scripts/coop_harness/terrain.py`), after the playthrough flow kept guessing at walls and ramps in Embershard Mine
(`docs/re/systems/pathing.md`). Names are in the IDA database (journal `agent-playthrough-flows.tsv`).

## The player's capsule

The player's character controller (`middleProcess->charController`) is a `bhkCharProxyController`; the pointer is to
its `bhkCharacterController` part, at +0x10 (vtable ID 240560).

- `radius` +0x290 and `height` +0x294, in Havok units (x 69.99 for game units): 18 and 128 for the player.
- `actorHeight` +0x260 (128), `scale` +0x258.
- The proxy: `bhkCharacterProxy` at full +0x340, its `hkpCharacterProxy*` at the wrapper's +0x10, so +0x340 of the
  controller part (`bhkCharProxyController::GetLinearVelocityImpl` 79145 reads it). `hkpCharacterProxy::maxSlopeCosine`
  +0xD8 reads 0 on the player: not where Skyrim keeps the walkable slope. Open: where it does; the readout uses 50
  degrees meanwhile.
- No step height: the Havok proxy climbs by slope, and the capsule's round bottom rides small edges.

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
