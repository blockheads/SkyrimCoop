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
  if no start yet: wait for a sync point (GraphSyncPointNotify), load it (Havok's setInternalState 58373 + seed)
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
  GraphSyncPointNotify { update index, the whole graph (Havok's getInternalState 58372, below), its words, seed,
                         the event whose clip files its process holds }
```

- **The whole graph over the wire:** each active node's internal state is a small Havok object described by its
  `hkClassMember` list; one reflection-driven writer and reader serializes any class (A graph's whole state, below).
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
- **A variable is its index step and the non-zero bytes of its XOR** with its last value in the batch (a 4-bit mask).
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

The stream's logic is pure and shared (`Games/Animation/GraphStream.h`, built, ClientTests `[graph-stream]`). Skyrim's
hooks (step 4) and the fake engine call the same entry points on `World::GetGraphStreams()`, so GameTests run the code
that ships.

```
owner game, an owned NPC's graph (CharacterService records it each send round; players: phase 4)
  63591 hook | FakeEngine::ReplayAnimation   OnEvent(formId, tick, eventId, words, seed)   before it's handled
  63587 hook | FakeEngine::UpdateGraph       OnUpdate(formId, tick, step, words, seed)      channels applied
                                             OnUpdated(formId, words) -> true: a copy wants a sync point
                                             AddPoint(formId, {tick, SaveState, words, seed})
  CharacterService's send round              TakeOps() -> GraphOpsRequest; TakePoints() -> GraphSyncPointRequest
host rules: GraphOps to the others, each run if its sender owns the actor; GraphSync to the owner; points from it
copy's game (CharacterService)
  NotifyGraphOps         Feed(copy, run, frames); a copy with no start: GraphSyncRequest (once; again after a gap)
  NotifyGraphSync        WantPoint(owned)
  NotifyGraphSyncPoint   Load(copy, point)
  each frame             SetPlayback(copy, playback tick); on the stream, its actions are bookkeeping (deaths replay)
  its graph update       63587 hook | FakeEngine::ReplayStream: Replay(copy, IGraphTarget) instead of its own

GraphReplayer::Run(playback, target), each due op in order (at most 4 updates a frame):
  words = its words after its last op + the op's changes (TimeDelta = the step); WriteWords; the seed if sent
  event: SendEvent (due with the update after it, when its owner had it); update: Update(step)
  words = Words()   (so its own writes between ops, channels and snapshot loads, never reach a decision)

IGraphTarget: Words / WriteWords, SetSeed, LoadState, SendEvent(eventId), Update(step)
  Skyrim: over BShkbAnimationGraph (step 4). The fake: FakeGraphTarget over its graph.
```

- **Event ids:** an event's index in the graph's event table, the same on every game (the host's behaviour files). The
  fake's table: every event its rules name, sorted.
- **Each batch decodes alone:** the host relays it to every guest, so a variable goes as its XOR with its last value in
  the same run, not the last one sent (about 60 B/s more for a busy humanoid).

**The fake's graph** (`Code/tests/games/FakeGraph.h`, built): `FakeEngine::Graph` holds the rules every game's graphs
share. Each actor's graph is its `GraphState`, `GraphVariables`, `GraphRandom` and `Graph` memory, updated once a
frame its graph steps (`FakeEngine::UpdateGraph`, 63358's order):

```
UpdateGraph(actor), when its graph steps this frame (AnimationStepMs: a frame, or a far owner's chunk)
  Timeline += step                       its owner's graph time (a copy's starts at the sync point it loaded)
  channels: Floats[TurnDelta] = this actor's heading change / step (63590; a copy's heading is interpolated)
  ranges:   in State, Floats[Input] >= Bound -> Above, else Below (hkbEventsFromRangeModifier)
  clips:    a clip's end -> Starts[Integers[StartFrom]], unless its exit's variable holds none of ExitValues (stuck)
  History += (Timeline, state, "update")
events (GraphTransitions, PerformAnimation / ReplayAnimation): History += (Timeline, state, "event X")
owner AI (OwnerClipEnds): its clip-end actions LeadMs before the end; refused, their ActionWrites (39004) land
```

- **The live check** (`FirstGraphDifference`): from a copy's first sync point (its stream's start; before it, it runs on
  the old paths), at every point of its history, its state is its owner's at the same owner graph time. The fake's
  saved graph state carries its owner's graph time and its clip's progress, standing in for the stream's sync point's
  update index (Skyrim's SaveState has no clock).
- **Without the stream** (step 2), every wolf flow failed it for the true reason: the owner took `cannedTurnRight180` at
  132 ms of its graph time, and its copy was still `Standing` there, its events on its own playback clock.
- **A desync the interim fixes can't reach:** a quick turn (60 ms at 3 rad/s). The owner's `TurnDelta` picks
  `TurnRightFast`, while its copy's, from its interpolated heading, picks `TurnRightSlow`. A hypothesis: no channel
  desync has been seen on the duo yet.
- **With the stream** (step 3), the copy's inputs come only from the replayer: every wolf flow passes the check, over a
  slow line too, and the quick turn picks its owner's speed. The setup waits for the copy's stream to start.
- Still separate rules, moved onto the graph when a flow needs them: `Loop`, `GraphPicks`, `SkippedClips`.

## Skyrim on the stream (duo, Riverwood, 2026-11-09)

```
owner, 63587 hook (forced)   OnUpdate(words, seed) -> real update -> OnUpdated(words); a point: SaveGraphPoint
owner, 63591 hook            event id: the project's event table (hkxDB +0xAC, hkHashTable keyed by the pooled
                             name, bucket = hash 68221 of it): what 63591 reads; then OnEvent
copy on the stream, 63587    Replay(target): its owner's due ops instead of its own update
copy on the stream, 63591    its own events dropped
the stream's own calls       a thread-local flag lets them through (LoadState's zero-step update 63602 calls 63587)
harness                      GraphStreamCheck chained under 63587: states hashed per owner update index, both games
```

The live check (`graph_stream_check`, 120 s, every streamed copy in Riverwood), three runs:

| run | first difference | cause | now |
|---|---|---|---|
| 1 | Faendal, update 182 of 177 equal: chair entry with its clips "(no file)" | the replayer sent an event before its clip files were in (the proof waited for them) | an event waits for its files, up to 120 runs (`IGraphTarget::RequestClips`, ClientTests) |
| 2 | 13484, update 77 of 35 equal: the copy in its chair, its owner standing | its spawn's graph state loaded after its stream had started, over the stream's (`AnimationSystem` loaded it first) | a copy on the stream drops its spawn's state (GameTests flow) |
| 3 | wolf 9B0DD, update 36 of 31 equal: `CannedTurnState` against standing | soon after its sync point, the same events on both | phase 2, below |
| 4-6 | a hare, a chicken, Faendal at updates 20-90: a turn's state against standing | both in a turn clip from the sync point on, its end trigger (`TurnInPlaceEnd`, `cannedTurnStop`) came on the owner's first: the point held no clip's progress (`hkbClipGeneratorInternalState.localTime`), nor a modifier's memory | phase 2 |
| 7 | **none: 23 copies matched 6608 of 6608 updates each, a hare 2829 of 2829** (wolves, Faendal, villagers, a dog, chickens; 120 s) | | |

- A copy's death ends its stream (the design's exceptions): the copy forgets it and replays its death as before; its owner
  stops recording a dead actor.
- Not done yet: the live check in the wolf and a humanoid scenario (combined runs), then PR #38 out of draft.

## A graph's whole state in sync points (2026-11-09)

Havok saves and restores a whole behaviour graph itself; the game never calls it (read in IDA,
docs/re/systems/animation-replay-recovery.md, A graph's whole state):

```
owner, the update a copy asked at: hkbBehaviorGraph::getInternalState (58372)
  per active node but transition effects: createInternalState (0D), getInternalState (0E), getInternalStateUser (0F)
  into an hkbNodeInternalStateInfo { sync info, node id, activated, state }
  -> GraphState::Node { id, check (class and name), sync, activated, state bytes (NodeMemory), runtime bytes }
copy: its graph deactivated (58331), its variable set kept, the owner's words in it
  hkbBehaviorGraph::setInternalState (58373): a map node id -> info, then updateActiveNodes with it (60079)
    per node as it activates (59326): Activate, setInternalState (10), setInternalStateUser (11), then its children,
    read from its restored state: a machine's restored state, a blender's restored active children
  then each BS node's runtime members onto its new clone
```

- **One reader for every class** (`Games/Animation/NodeMemory.h`, ClientTests `[node-memory]`): plain values, enums,
  inline arrays, structs and `hkArray`s of them; a new state's arrays are sized in Havok's heap (57048). A running
  transition's effect (`hkbStateMachineActiveTransitionInfo`) is remade by its machine (59343); its effect's own state
  travels inside the machine's and is restored onto the new clone.
- **Node ids** are numbered from the same behaviour files alike (setNodeIds 58369); each node also carries a check of
  its class and name, and a copy skips a node whose template differs.
- **BS nodes** have no internal state: their runtime members (`SERIALIZE_IGNORED`, 0x400: a timer's elapsed time, an
  every-N modifier's counts, a tweener's start) are carried by reflection. Skyrim's own restore (65149's functor) cannot
  follow Havok's: it ends every running transition and restarts it approximately (65164).
- **The codec** (`Games/Animation/GraphState.h`, ClientTests `[graph-state]`): sync info as its used points and times.
- **Clip files:** the point names the event whose clips its owner's process loaded last into its action set
  (`coop_LoadEventClips` 43621, set 3), not its graph's last event (`IdleStop` loads nothing).
- **The fake** (GameTests): a blender choosing its child as it activates, which Skyrim's save leaves out, and one action
  clip set per process; its sync point is its whole graph through the same codec.

## Scenario runs with the live check (2026-11-09)

| run | first difference | cause | now |
|---|---|---|---|
| wolf scenario | a mudcrab's copy, 3 updates in: no state at all | its graph replaced after its stream started (its 3D) | a new graph on either side restarts the stream from a new sync point (GameTests flows, ClientTests) |
| wolf scenario | the guest crashed in a blender's generate (60039) | another graph's blender state (which children are active) applied | superseded: Havok's restore activates each generator's children from its own restored state |
| both | humanoid copies in a turn their owners had left, from their start | the save leaves out machines at their start state (65148); the guest's AI ran the NPC before it became a copy, so its graph was not fresh | the copy's behaviour graph is deactivated, then restored whole (A graph's whole state) |
| town | 1A684 at 386, then 441 updates: a counter clip "(no file)" | its owner's process held its last action's clip set, loaded before the copy's start; the point named its graph's last event | the point names the event its owner's process loaded clips for (43621, set 3) (GameTests flow) |
| wolf scenario | Faendal at his first update: one of a turn blender's two clips missing | after the fresh activation the blender chose its own active children | the whole graph through Havok's own restore (58372 / 58373), above (GameTests flow) |

## Phases

1. **Host NPCs' copies,** with the stream and sync points, old paths kept as the fallback for an actor with no stream yet.
   Order:
   1. The encoding (`Structs/GraphOps.h`, TPTests): done.
   2. The fake's graph, with the wolf flows on it, failing the live check without the stream: done.
   3. The stream (`GraphStream.h`, the three messages and their host rules, `CharacterService`), in GameTests: done.
   4. The Skyrim hooks (`Games/Skyrim/Havok/GraphStreamHooks.cpp`): done.
   5. The live check on the duo (`GraphStreamCheck`, verb `graph_stream_check`): built; it finds the copies apart
      soon after their sync point (Skyrim on the stream, below), which is phase 2.
2. **The whole graph** in sync points (Havok's internal state).
3. **Remove the old paths** for copies on the stream; movement drops graph variables.
4. **Players' copies** (a player's graph driven by its input; first-person graphs excluded).

## Open questions

- Which engine code sends events into a copy's graph that the copy still needs (death, ragdoll, our own replays)
  outside the exceptions: everything else is dropped.
- Graph rebuilds (3D reload, race change): a new graph on either side means a sync point.
- Open Animation Replacer's random conditions use their own generator (graph-input-replay.md), outside the stream.
- **Clip files on `IdleWoodPutDownEnter`:** in 2 of 6 external runs of the proof, Faendal's copy entered
  `FurnitureState` with `AOWoodPutDown` lacking its file and left it at once. The production replayer holds an event
  for its files (below); whether that covers this event is for the live check to show.
- Players: whether a player's graph has inputs the stream misses (camera-driven variables, first-person).
