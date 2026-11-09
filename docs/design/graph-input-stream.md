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

Measured on the duo (2026-11-08, `graph_replay_net` with `external`, Riverwood). Every encoding is lossless, and the copy
matched its owner on every update while it ran:

| encoding | Faendal, chores | wolf, moving now and then | wolf, idle |
|---|---|---|---|
| the proofs' (names, 6 bytes a variable, every variable changed) | 1495 B/s | 507 B/s | 186 B/s |
| only what changed since the owner's last op ended | 1041 | 491 | 352 (more per-update overhead) |
| the stream's encoding (below) | **559** | **171** | about 10 |
| Snappy on that | 565 | 173 | |

Plus each batch's step table: one 4-byte step per frame for every NPC in it, 230 B/s split among them (about 11 B/s each
for 21 NPCs). Today's movement messages from the host weigh 750-1870 B/s per synced NPC and carry each NPC's graph
variables; those go.

**The stream's encoding** (`Structs/GraphOps.h`; `GraphOpsRun`, TPTests `[encoding.graph_ops]`):
- **Only variables set from outside the graph:** an op carries the variables that changed since its owner's last op ended.
  Whatever the graph's own update writes, the copy's graph writes too. Correct only if the copy's variables are its
  stream's alone: its own channels (63590) and our snapshot loads are kept out. The proof restores the copy's
  variables after each op to do that.
- **`TimeDelta` is the step:** it held the update's step, bit for bit, in 3502 of 3502 updates (Faendal). It isn't sent;
  the copy writes it.
- **An update in the next frame with nothing changed is one bit;** otherwise a few bits of kind, frame and seed.
- **A variable is its index step and the non-zero bytes of its XOR** with its last sent value (a 4-bit mask).
- **What's left is float channels,** `Direction`, `TurnDelta` and `Speed` (300-400 B/s for a walking humanoid), and the
  foot IK gain. They're noise to every compressor: Snappy gains nothing, and predicting a float from its last two
  values saves 3%. They have to be exact, and a copy can't compute them itself: its motion is interpolated.

## Testing (the bug-fix flow's tiers)

- **TPTests:** `GraphOpsRequest` / notify and sync point round trips.
- **ClientTests:** the pure rules: ops due at a playback tick, catch-up cap, gap detection, pause and resume.
- **GameTests:** the fake's graph made op-driven (its owner records `GraphVariables`, events and steps; its copy
  replays them): a copy matches through loss, late spawn, a gap, a ragdoll pause, a catch-up. The wolf flows'
  expectations stay.
- **Duo:** `graph_replay_net` becomes the live check: every copy's states hashed against its owner's per update, in
  every scenario, failing on the first difference (the proofs' readout, generalized). The wolf scenario and a humanoid
  chores scenario.

## Phase 1's seam: one recorder and replayer, two engines

The stream's logic is pure and shared. Skyrim's hooks and the fake engine both call it, so GameTests run the code that
ships.

```
Games/Animation/GraphStream.h (pure, ClientTests)
  Recorder (owner, per actor):
    OnEvent(eventId, words, seed)      <- 63591 hook   | FakeGraph::SendEvent
    OnUpdate(step, words, seed)        <- 63587 hook   | FakeGraph::Update
    OnUpdated(words)                   <- after 63587  | after FakeGraph::Update
    Take() -> ops since the last take: the variables changed since the last op ended, TimeDelta left out
  Replayer (copy, per actor):
    Feed(first update, owner tick, ops) / Gap() / Point(sync point)
    Run(playbackTick, IGraphTarget&): due ops in order, capped per frame

IGraphTarget: the one engine surface the replayer drives
  Words() / WriteWords(words)   the graph's variable array (hkbVariableValueSet)
  Seed() / SetSeed(seed)        hkbBehaviorGraph+0x78
  SendEvent(eventId)            63591, the copy's own events kept out
  Update(step)                  63587, forced
  LoadPoint(point) / SavePoint  SaveState/LoadState + counters
Skyrim: over BShkbAnimationGraph (Games/Animation). The fake: over FakeGraph.
```

**FakeGraph** replaces the fake's special cases (`Bound`, `Loop`, `GraphPicks`, `ActionWrites`) with one small
deterministic graph. Each special case becomes a rule on it, and each game feeds it its own inputs, as Skyrim does:

```
FakeGraph { Words, State, StateMs, Seed, Rules }
  Update(step):  channels already written (Words[Speed] = this actor's speed: a copy's interpolated one)
                 StateMs += step; a clip end -> next state (bound start: Words[i]; random: Seed)
  SendEvent(e):  a transition if its condition holds on Words now
each game's inputs: its actor's channels, its AI's and engine's events, its own step (LOD chunks far away)
```

- **Without the stream,** a copy's FakeGraph gets its own channels and events. That reproduces today's desyncs in the
  wolf flows, for the true reason.
- **With the stream,** the copy's inputs come only from the replayer, and its state equals its owner's after every
  update. The flows check that each update: the live check in the fake.

## Phases

1. **Host NPCs' copies,** with the stream and sync points, old paths kept as the fallback for an actor with no stream yet.
   Order:
   1. `GraphStream` and its encoding, with ClientTests and TPTests.
   2. FakeGraph, with the wolf flows rewritten on it, failing without the stream.
   3. The stream in GameTests.
   4. The Skyrim hooks.
   5. The live check on the duo.
2. **Node memory serialization** in sync points.
3. **Remove the old paths** for copies on the stream; movement drops graph variables.
4. **Players' copies** (a player's graph driven by its input; first-person graphs excluded).

## Open questions

- Which engine code sends events into a copy's graph that the copy still needs (death, ragdoll, our own replays)
  outside the exceptions: everything else is dropped.
- Graph rebuilds (3D reload, race change): a new graph on either side means a sync point.
- Open Animation Replacer's random conditions use their own generator (graph-input-replay.md), outside the stream.
- **Clip files on `IdleWoodPutDownEnter`:** in 2 of 6 external runs, Faendal's copy entered `FurnitureState` with
  `AOWoodPutDown` lacking its file and left it at once. The clip wait didn't hold for it. Read why before the stream
  relies on `ActionClips::Request`.
- **A wolf went apart 15 updates after its sync point** (1 run of 4), all variables equal: `CannedTurnState` against
  `LoopingTurnLeft`. That fits node memory missing from the network sync point (phase 2). Not shown.
- Players: whether a player's graph has inputs the stream misses (camera-driven variables, first-person).
