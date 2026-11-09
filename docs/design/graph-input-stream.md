# The graph input stream: production design (2026-11-08)

Status: proposed. Built on the proofs in [graph-input-replay.md](graph-input-replay.md): a behaviour graph fed its
owner's inputs (variables, events, steps, seed, clip files), from its owner's sync point, matches its owner on every
update, on one game and over the network. This designs the production version: every copy's graph driven by its owner's
input stream, instead of replayed actions plus snapshot variables.

## What changes for a copy

```
today                                          the stream
owner's actions -> replayed on the copy        owner's graph inputs -> replayed into the copy's graph
  (ForceAction, a refused one recovered          in order, on the copy's playback clock
  from the owner's graph state)
movement snapshots' graph variables -> loaded  every graph variable, written before each input
graph points (random picks), every-N counters  the seed with each input; node memory at the sync point
the copy's own channels, engine events,        kept out of the copy's graph
  LoadState, seeds set by replays
```

Unchanged: the copy's position and movement (interpolation), its actor state flags (sent beside the stream), and the
cases where a copy uses its own body: ragdoll, get-up, paired animations (below).

## Owner: recording (every game, for the actors it owns)

```
63591 SendEvent, an owned actor's graph            63587 its graph update (forced, step = deltaTime)
  op = { Event, event id, variables changed }        op = { Update, step, variables changed, seed if changed }
                 \                                     /
                  per actor, in order, update indexes counting up
                                  |
CharacterService's send round (10/s; 30/s in a fight) -> GraphOpsRequest { session id, first update index,
                                                         owner tick of the first update, ops }
```

- **Event ids, not names:** both games load the same behaviour files (the host's mod list is authoritative), so an
  event is its index in the graph's event table (63591 hashes the name to it). Unknown on the copy: resync.
- **Exact values:** a graph decides on the bits it reads (`lowerBound <= input`). Variables go as they are: changed
  ones only (u16 index, u32 word), never quantized. Compression is lossless only (below).
- **Owner time:** each update op's tick is the first update's tick plus the steps before it, so the copy can place every
  op on its playback clock.
- **Already true:** owners update every frame (`SetGraphUpdatedEveryFrame`), so their steps are the frame's. Cost: about
  4 ms/s of job-thread time per NPC, with no measurable frame-time change in Whiterun (animation-replay-recovery.md).

## Transport

- **Reliable and in order,** per actor (enet's default `kReliable`). A lost op would leave the copy one input short for
  good.
- **Host relays** as for movement: a host NPC's ops to every guest with its copy; a guest player's ops to the host and
  the other guests (later phase).
- **Gaps:** an op batch whose first update index is not the next expected one (a dropped connection, a late spawn)
  stops the copy's replay until it gets a sync point.

## Copy: replaying

```
main thread (CharacterService): GraphOpsNotify -> the copy's queue (RemoteAnimationComponent)
copy's graph update (63587 hook, its animation job):
  if no start yet: wait for a sync point (GraphSyncPointNotify), load it (LoadState + seed + counters + node memory)
  due = queued ops whose owner tick <= playback tick (events: with the update after them)
  for each due op:
    write every variable that differs from the op's, and the seed
    event: its clip files first (ActionClips::Request; hold the queue, at most 120 updates), then SendEvent (63591)
    update: the real 63587 with the op's step, forced
  behind: several updates in this frame; nothing due: no update (the graph holds its pose)
  kept out: the copy's own SendEvent calls, LoadState calls but its own, its channels (overwritten by the op's words)
```

- **Playback, not the graph's own time:** ops are placed by their owner's tick, so a copy runs as far behind its owner
  as its movement does, and its graph's time follows the owner's steps (what action spacing approximated).
- **Catch-up cost:** several updates in a frame cost what several frames do; capped (say 4 a frame), with a sync point
  when a copy falls further behind than 0.5 s.

## Sync points

A copy needs one to start, and after any gap or exception.

```
copy: GraphSyncRequest(session id)  ->  owner, after its next update:
  GraphSyncPointNotify { update index, graph state (SaveState 63601), seed, every-N counters,
                         node memory of active modifiers (getInternalState, slot 0E) }
```

- **Node memory over the wire:** each internal state is a small Havok object described by its `hkClassMember` list
  (static in `.rdata`, read by the clone audit); one reflection-driven writer and reader serializes any class.
  Phase 2: the network proof passed without it (a modifier active at the sync point is rare to decide differently).
- **Spawn:** the copy's spawn message carries the owner's graph state today; it becomes a sync point request at spawn.

## Exceptions: the copy's own body

| case | why the stream pauses | after |
|---|---|---|
| ragdoll (knocked down, dead) | the graph is driven by the copy's own ragdoll (`BSRagdollContactListenerModifier`) | death: the stream ends |
| get-up | the clip is picked from the copy's ragdoll pose (`hkbPoseMatchingGenerator`) | sync point once the owner stands |
| paired animations (kill moves) | a synchronized clip plays against its partner's graph (`BSSynchronizedClipGenerator`) | sync point after the pair |

The owner marks the pause in its ops (an op kind) when it enters one, and sends a sync point when it leaves; the copy
runs today's paths in between (`KnockReplay`, death actions, `ReplayState` pairs).

## What goes away for copies on the stream

- action replay into the graph: `ReplayStep` staging, `PlayReplayedAction`, `ReplayGraph::Recover`;
- refused actions' variables-only sends (`kVariablesOnly`) and action spacing (`ActionDue`);
- graph variables in movement snapshots (`ReferenceUpdate` variables), graph points and every-N counters (`GraphRandom`);
- the PR #38 fixes for copies, which the stream makes unnecessary; owners updating every frame stays.

Actions still go to the host for its rules (hits, kills, AI state) where something reads them; only their replay into a
copy's graph goes.

## Weight

Measured (proofs, before compression): 0.2-0.3 KB/s for an idle wolf, about 1.5 KB/s for Faendal doing chores. Today's
movement messages: 0.3-2.3 KB/s per synced NPC, which carry each NPC's whole set of graph variables; those go.
Lossless savings to apply: event ids instead of names; one step for a frame's updates; a byte count of changed variables
per op; zstd or LZ4 per batch. Estimate: 0.3-0.8 KB/s for a busy humanoid.

## Testing (the bug-fix flow's tiers)

- **TPTests:** `GraphOpsRequest` / notify and sync point round trips.
- **ClientTests:** the pure rules: ops due at a playback tick, catch-up cap, gap detection, pause and resume.
- **GameTests:** the fake's graph made op-driven (its owner records `GraphVariables`, events and steps; its copy
  replays them): a copy matches through loss, late spawn, a gap, a ragdoll pause, a catch-up. The wolf flows'
  expectations stay.
- **Duo:** `graph_replay_net` becomes the live check: every copy's states hashed against its owner's per update, in
  every scenario, failing on the first difference (the proofs' readout, generalized). The wolf scenario and a humanoid
  chores scenario.

## Phases

1. **Host NPCs' copies,** with the stream and sync points, old paths kept as the fallback for an actor with no stream yet.
2. **Node memory serialization** in sync points.
3. **Remove the old paths** for copies on the stream; movement drops graph variables.
4. **Players' copies** (a player's graph driven by its input; first-person graphs excluded).

## Open questions

- Which engine code sends events into a copy's graph that the copy still needs (death, ragdoll, our own replays)
  outside the exceptions: everything else is dropped.
- Graph rebuilds (3D reload, race change): a new graph on either side means a sync point.
- Open Animation Replacer's random conditions use their own generator (graph-input-replay.md), outside the stream.
- Players: whether a player's graph has inputs the stream misses (camera-driven variables, first-person).
