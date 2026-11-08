# Graph input replay: the proof (design, 2026-11-08)

Status: proposed. Nothing built yet. This is step 0 of a possible new pattern for animation sync, decided with the user.

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
host game, Riverwood
  owner:  the wolf (9B0DD), forced, recorded at 63591 and 63587
  shadow: a second actor of the same base, near it, AI off, invisible, forced
          start: the owner's graph state, variables and seed copied in at an update boundary
          each frame: the owner's ops up to the last frame, replayed into the shadow's graph
          its own channels (63590), engine events (63591) and variable writes kept out
  after every shadow update: digest(owner after update i) == digest(shadow after update i)?
```

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

## Open questions for the proof's RE

- 63587's signature and forced path when called several times in one frame with a chosen step (the shadow's update).
- Capturing and loading graph state, variables and seed at one update boundary (`SaveAnimationGraphState` /
  `LoadAnimationGraphState`, `GetGraphRandom`).
- Which Bethesda modifiers in the canine and character behaviours read the world (behavior_dump over the classes in use).
- Paired animations (kill moves): a synchronized clip reads its partner's graph; out of the proof's scope.
