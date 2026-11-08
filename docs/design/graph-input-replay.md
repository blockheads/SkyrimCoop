# Graph input replay: the proof (design, 2026-11-08)

Status: the one-game proof passed; over the network, most runs pass (2026-11-08, below). Open: the sync point misses
modifiers' memory (a humanoid's turn speed). This is step 0 of a possible
new pattern for animation sync, decided with the user.

## Why

A copy's behaviour graph is a second simulation of its owner's, fed some of its owner's inputs (actions, snapshot
variables, seeds) and some of its own engine's. Every desync found on PR #38 was one input that differed:

- the variables a refused action still writes (39004);
- a far owner stepping in chunks (63587, animation interpolation) while its copy steps every frame;
- the copy's playback steering while its clips run on frame time;
- the order of writes and events inside a frame.

Each fix found the next one, and a state machine left in a wrong state stays there. The idea: stop syncing what the
graph does, and give the copy's graph exactly the inputs its owner's got, in the same order. If a graph update is
deterministic given its inputs, the copy's state machines then match by construction.

## What a graph reads (read in IDA 1.7.104; names and comments journaled on agent/copy-idle-graph)

```
each frame, animation job (41453) -> 20123 (UpdateAnimationGraphs)
  Actor::ModifyAnimationUpdateData (38054): deltaTime = frame seconds x VATS multiplier, force, visible
  coop_BSAnimationGraphManager_Update (63358):
    1. every bound channel polls this game's actor (PollChannelUpdateImpl):
         Speed, TargetSpeed, Direction, TurnDelta, Pitch, PitchDelta, Roll, WeaponSpeed, LeftWeaponSpeed,
         TimeDelta, WardHealth, iWantBlock, look-at, copied variables (BSTAnimationGraphDataChannel<Actor,...>)
    2. coop_BShkbAnimationGraph_ApplyChannels (63590): each channel's value into the graph variable of its name
    3. coop_BShkbAnimationGraph_UpdateInterpolated (63587): the behaviour graph's update with a step
         (dt every frame when forced or near; whole chunks when far, see animation-replay-recovery.md)

between updates, on any thread:
  events:    BShkbAnimationGraph::SendEvent (63591) -> root behaviour handleEvent at once (vtable +0x30);
             every input event passes here (actions via 38949/38953, engine notifications 63362, 63372).
             Transition conditions read the variables as they stand then.
  variables: IAnimationGraphManagerHolder::SetGraphVariable* (32885-32887) -> 32932 (int) -> each graph's
             BShkbAnimationGraph::SetGraphVariable*: written at once, under the manager's lock
```

So a graph's inputs are: the variable values (written directly, or by channels at each update's start), the events
sent between updates, each update's step, and the graph's random seed (already synced by `GraphRandom`). Events the
graph makes itself (a clip's end, `cannedTurnStop`) follow from those.

Not yet known, to be measured by the proof: nodes that read the world instead of variables. Foot IK raycasts,
look-at and the ragdoll driver shape the pose. A Bethesda modifier that sends events from physics contacts or
distances would change states.

Elsewhere: SkyMP and Skyrim Together replay named animation events on a remote actor (SkyMP leaves the engine's AI on).
AnimSync Together, a Skyrim Together add-on, syncs selected graph variables and events for animation mods. None replays
a graph's inputs. No public material on replaying Havok Behavior graphs was found.

## The pattern, if the proof holds

```
owner's game, per graph:                       copy's game, per graph, behind:
  at each SendEvent (63591):                     ops arrive in order
    op = { variables changed since last op,        each op: write those variables (over the copy's own
           event }                                   channels and writes), then send the event, or run one
  at each update (63587 entry, after 63590):         update with the op's step (63587, forced)
    op = { variables changed since last op,        several updates in one frame if behind; none if no
           step }                                    update is due
  start: graph state + variables + seed at an     start: load them, replay from the next op
         update boundary
```

The copy's own channels (63590) and engine events are kept out of its graph. Everything a graph reads comes from its
owner, in its owner's order, whatever wrote it. This one stream would replace refused-action writes, action spacing,
snapshot variables for graphs, graph points and recovery loads.

## The proof

One game, no network: does a graph replayed from its owner's recorded ops stay in its owner's states?

```
host game, Riverwood: two actors of the wolf's base, placed together (fresh graphs)
  owner:  AI on, forced, recorded from its graph's first update:
            63591 (an event) -> op { variables changed, event }
            63587 entry, after 63590 applied its channels -> op { variables changed, step }
  shadow: AI off, forced; its seed set to the owner's at the start
          in its own 63587 call (its animation job), the owner's ops not yet replayed, in order:
            write every variable that differs from the owner's at that op (over its own channels)
            then send the op's event (real 63591), or call the real 63587 with the op's step
          its engine's own events to its graph (63591 outside a replay) dropped
  after every replayed update: digest(owner after update i) == digest(shadow after update i)?
```

Variant 2, the start a copy would need: the shadow starts from the owner's saved state (63601/63602) mid-life instead of
a fresh graph, to measure what a saved state misses.

- **Digest:** each active state machine's active state (as the harness's `graph_machines` reads them), plus clip local
  times to 1 ms.
- **Readout, a harness command `graph_replay`:** updates compared, updates equal, and for the first mismatch its update
  index, the machines that differ, and the last ops before it.
- **Pass:** the wolf and a humanoid NPC (Faendal or a guard), 3 minutes each, with every update equal. Any mismatch
  names the input we miss, which is the finding either way.
- **All harness-side:** the hooks on 63587, 63590 and 63591 live in `TestHarness/` and act only on the probed pair, so
  no production code changes for the proof. The shadow shares its owner's game, so the same clip files are loaded,
  the same graph data is used, and there's no network.

Then, if it passes, a second proof over the network: the same ops sent to the guest's copy, compared by update index,
and the bandwidth measured. Only after that, the production design.

## The proof's RE (read 2026-11-08)

1. **Calling 63587 with a chosen step, several times a frame.** `coop_BShkbAnimationGraph_UpdateInterpolated(graph,
   BSAnimationUpdateData*, scratch)`; 63358 passes its scratch only for its active graph (+0xB0), else null. With force
   (+0x2A) the countdowns reset and the step is exactly `deltaTime`. Per call it also swaps its two pose buffers, writes
   the pose (63583), runs foot IK, and calls the data's functor, `coop_Character_ApplyGraphMovementType` (38019): the
   graph's movement type to the actor's movement controller when it changed, an output. The engine does this itself:
   `LoadState` (63602) calls 63587 with its own data, step 0 and force set, so the loaded nodes activate.
2. **The start.** A saved state (63601) holds active states, variables and clips, but not the every-N counters
   (`GraphCounters`) or the seed (`GraphRandom`), and loading adds a zero-step update its owner never had. So the
   proof starts both graphs fresh and sets the seed (variant 1); variant 2 measures the saved-state start.
3. **Nodes that read the world** (classes in canine and character behaviours, `behavior_dump`):
   - state-relevant, fed by physics: `BSRagdollContactListenerModifier` (an event on ragdoll contact) and
     `hkbPoseMatchingGenerator` (a get-up clip picked by the ragdoll's pose), only in get-up, reanimation and ragdoll
     states (`dogbehavior.hkx`, `0_master.hkx`, a kill move's victim in `1hm_behavior.hkx`);
   - pose only: look-at, direct-at, foot IK, ragdoll controls, `hkbGetUpModifier`, `hkbRotateCharacterModifier`;
   - everything else reads only variables, events and time: blenders, clips, selectors, expressions, every-N counters,
     timers, cyclic blends, state tagging.

   So movement, idles, turns and attacks should replay exactly. A ragdoll and its get-up are each game's own body, as
   copies are today.
4. **Paired animations:** `BSSynchronizedClipGenerator` plays against its partner's graph through
   `BGSSynchronizedAnimationManager`; out of the proof's scope, as are ragdoll states.

## Result (duo, Riverwood, 2026-11-08; harness `graph_replay`, `TestHarness/GraphReplay.cpp`)

- **Every replayed update matched.** A wolf's shadow: 6772 of 6772 updates with the same active states, over 107 s of
  its owner's graph time. Faendal's (a humanoid, `0_master`): 6936 of 6936, over 95 s.
- **Two variables differed now and then, never a state:** the wolf's `TurnDeltaDamped` (a damping modifier's output) in
  3 updates, and Faendal's `LookAtOutOfRange` (the look-at modifier's, which reads where its target is) in 7.
- **The start is the one a copy needs:** the shadow loads its owner's sync point (graph state, seed, every-N counters,
  `SaveGraphPoint`) right after one of the owner's updates, and is fed the ops from there. Two fresh graphs do not start
  alike: in the first run Faendal's twins differed at update 0 (`MT_IdleLookingA` against `B`), a pick made as each
  graph activated with its own actor's seed.
- **What the proof had to get right:** the owner forced from its first update (its first updates come before
  `CharacterService` forces it, and a chunked step is not the recorded one); the shadow's AI left on (an actor without
  AI is not animated, and its own events are dropped anyway: 190 for the wolf, 6483 for Faendal).

## Over the network: the second proof (design, 2026-11-08)

The same replay into a real copy on the guest's game, whose engine and our production code keep running around it.

```
host game                                 harness (python, the proof's wire)      guest game
  owner: the Riverwood wolf (9B0DD)
  63591 / 63587 -> ops, each against the
    one before: kind, step or event name,
    changed variables (u16 index, u32
    value), the graph's random state
  after each update: its active states
  a sync point after one update   --- take every 100 ms ---> feed --->    the copy of that wolf
                                                                            next 63587: load the sync point,
                                                                            then the ops, in order:
                                                                              write variables + random state
                                                                              event (63591) / update (63587, forced)
                                                                            after each update: compare states
```

### What else touches a copy's graph, and how the proof keeps it out

- **Events** (replayed actions via ForceAction 38953, engine notifications): dropped by `DropEvent`, as the shadow's.
- **Graph state loads** (a refused action's recovery, `ReplayGraph::Recover`; sync points, `RunGraphPoint`): a harness
  hook on `BShkbAnimationGraph::LoadState` (63602) refuses them for the probed copy. A refused load also keeps
  `RunGraphPoint` from setting its counters.
- **Seeds** (an action's `GraphRandom`, set before its event): each op carries the graph's random state and writes it,
  so whatever set it in between is overwritten.
- **Variables** (snapshot variables, `LoadAnimationVariables`; its own channels): each op writes every variable that
  differs.
- **Its graph update**: replaced by the replay (the 63587 hook), as the shadow's.

The copy's position and movement are left as they are (interpolation): the proof is about its graph.

### Measured weight (one-game proof, 40 s each)

| | ops/s | changed variables/op | bytes/s |
|---|---|---|---|
| wolf (93 variables) | 57 updates + 3.4 events | 1.3 | 690 |
| Faendal (301 variables) | 56 updates + 1.3 events | 1.5 | 700 |

About 0.7 KB/s per NPC before compression: 20 NPCs near a guest come to 14 KB/s from the host to that guest. The
header (kind and step, 3 bytes) is a quarter of it; a frame's updates share a step.

### Pass

The copy's active states equal its owner's after every replayed update for 60 s, on the wolf and on Faendal; the bytes
per second as sent. The proof replays as soon as ops arrive and compares by update index: timing is the production
design's.

### After the proof: what production needs (not designed yet)

- **When:** a copy runs ops at its playback time (its owner's tick, about 0.1-0.25 s behind), several updates in one
  frame when behind, none when ahead.
- **Loss:** every op matters, so ops go reliable and in order; a broken stream restarts from a sync point.
- **Where it stops:** ragdolls, get-ups and pairs are each game's own body; the stream pauses there and restarts from a
  sync point after.
- **What it replaces:** refused-action writes, action spacing, snapshot variables for graphs, graph points and recovery
  loads (the PR #38 fixes become unnecessary for copies on the stream).
- **Players' copies:** a player's graph is driven by its owner's input the same way; to be checked separately.

## Network result (duo, Riverwood, 2026-11-08; verb `graph_replay_net`)

The host recorded a synced NPC; the harness carried its ops to the NPC's copy on the guest every 100 ms; the copy
replayed them from its owner's sync point while our production code and its engine ran on.

| run | NPC | updates equal | over | waits for clip files |
|---|---|---|---|---|
| 1 | wolf 9B0DD | 3428 / 3428 | 60 s | (not yet waited) |
| 1 | Faendal | 590, then apart | | (not yet waited) |
| 2 | Faendal | 3517 / 3517 | 60 s | 7 |
| 3 | wolf 9B0DD | 3528 / 3528 | 60 s | 20 (bounded at 120 updates) |
| 3 | Faendal | 607, then apart | | |
| 4 | Faendal | 6922 / 6922 | 120 s | 20 |

- **Clip files, the one input the first design missed.** Run 1's Faendal entered `FurnitureState` on
  `IdleWoodPutDownEnter` and left it in the same update on the copy: its clip file was not loaded, and a clip without
  its file lasts 0.0133 s (59263). The owner's game loads an action's files before sending its event (39625). The copy
  now requests them (`ActionClips::Request`) and holds the stream until they are in, at most 120 updates as
  `ClipWaitOver` (a load the process cancels never reports in: the wolf's waited 6662 frames without the bound).
- **Open: the sync point misses node memory.** Faendal's turn speed diverged three times (updates 608, 3603, 8567 of
  runs from 60 to 300 s; 0 in 360 s between them): after `TurnRight`, one game's `TurnRightBehavior` went
  `MT_TurnRightFast` (with its 360 clips), the other's `MT_TurnRightSlow`. Neither game sends these events: the graph
  raises them, from `TurnRight_EventsFromRangeMod` (`hkbEventsFromRangeModifier`, its input bound to `TurnDelta`). At
  the last one the run ruled the inputs out: variables identical after the update; events, steps and seeds carried;
  every state machine equal until then, inactive clones included; the same clips playing, all with their files; no
  owner event sent from another thread mid-update. Left: a modifier's own memory (a range modifier knows which range it
  was in last frame), which the copy brings from before its sync point. The wolf's rare `TurnDeltaDamped` difference
  (a damping modifier's memory) fits. Inferred, not shown. Havok nodes expose that memory generically
  (`getInternalState` / `setInternalState`; 60010 is one in this executable): the next RE step is a sync point with
  every active node's internal state.
- **Weight:** an idle wolf's ops 206-303 B/s; Faendal doing chores, 1506-1515 B/s. Today's movement messages from the
  host come to 924-2338 B/s per synced NPC (`perf`, `NotifyReferencesMove`, over 22-23 NPCs), and they carry each
  NPC's whole set of graph variables, which the stream would replace.

## Node memory: what a sync point misses (read in IDA, 2026-11-08)

```
every Havok node (hkbNode vtable, the same slots in every class):
  12 cloneNode   13 createInternalState   14 getInternalState   15 getInternalStateUser
  16 setInternalState   17 setInternalStateUser
  e.g. hkbEventsFromRangeModifier (59728-59730): its state is wasActiveInPreviousFrame (hkArray<bool>, node +0x60),
  which range it was in last frame: what decides its next range event (TurnRight's fast / slow)

whole graph:
  coop_hkbBehaviorGraph_getInternalState (58372): each active node's state (13, 14, 15) in an
    hkbNodeInternalStateInfo with its node id and node info, plus the variables. Never called by code.
  restoring: coop_hkbBehaviorGraph_ActivateNode (59326) calls a node's 16 and 17 when given a map of
    node id -> state info; its only entry (coop_hkbBehaviorGraph_ActivateNodes, 12 callers) passes none.
Skyrim's own save (SaveState 63601 -> hkbCharacter::SnapshotGraphState 65148): state machines (only those away from
  their start state or mid-transition), variables, clips. No node memory.
```

So the copy starts with its own nodes' memory, from before its sync point, and a node whose next output depends on
it (range events, damping, timers, edge detectors, every-N counters: already carried by `GraphCounters`) can decide
differently. That is the turn-speed divergence, and the wolf's `TurnDeltaDamped`.

**The plan:** a complete sync point. On the owner, `getInternalState` (58372) after the update the point is taken at;
on the copy, after loading the point, each active node's `setInternalState` (slot 16) with its owner's state for the
same node id. One game first: the objects pass in memory, and the wolf's `TurnDeltaDamped` and Faendal's turn should
stop differing. Over the network the states need serializing: they are small Havok objects (bools, numbers, vectors,
arrays of them), each with its `hkClass`, so one reflection-driven writer covers every class.
