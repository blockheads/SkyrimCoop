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
- **Owners step every frame, without `force`:** the 63587 hook clears the update data's interpolation flag (`+0x2B`)
  for a recorded owner, so its graph steps each frame at the engine's own LOD and foot IK. A copy's replayed updates
  run the same way. The step recorded is the behaviour graph's real one (58329).
- **Why not `force`** (`+0x2A`, or the actor's `kForceAnimGraphUpdate`): forced, 63587 skips 63575, so the LOD is full
  and the foot IK flag (`+0x249`) stays on for an actor the engine would not solve it for (off camera). Measured on
  `host-kill-move`'s restrained wolf, dropped 72 units: forced by the hook alone it hangs 45-72 units up and drifts;
  forced with `+0x249` cleared after each update it lands; unforced with interpolation off it lands. Only 63581 reads
  the flag (58423 solves, 58427 resets). Not read: how the solved pose holds the actor up.
- **Generate passes are ops:** a graph's modifiers and blenders move in its generate pass (63568), run once after each
  frame it updated in (63589). The owner's are recorded in order with its events and updates; a copy runs them in
  place and never its own (the hook clears `+0x246` after its replay). On the wire a pass right after its update is
  one bit of that update.

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
    update: the real 63587 with the op's step, interpolation off (not forced: engine LOD and foot IK)
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
copy: GraphSyncRequest(session id)  ->  owner, as its next update begins (after its graph's generate pass):
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

- **Pairs: built (2026-11-09).** A pair's start (32803) puts both actors in its instance, then plays the target's half
  through its graph (63591). A copy on the stream that is in an instance on this game (`Actor::InPair`, 32808) is held
  off its stream (`GraphStreams::HoldWhile`): it takes that event, runs today's paths, and asks for a new sync point
  once out of the instance. Before, the copy refused the event and a guest's kill move on a host NPC never started
  (`client-kill-move`).
  A pair on its owner's game alone (the host's kill move on its own NPC) makes no instance on a guest: a replayed
  pair names no target event for a copy partner, and each copy plays its half alone. There the copy leaves its stream
  at its owner's half of the pair, a paired action (`AnimationSystem`, `GraphStreams::HoldUntilPoint`), plays it as
  before the stream, and is back from a sync point taken after that action. The owner is not recorded while it is in
  an instance (`GraphStreams::PauseWhile`, 32808): it gives no point, and its recording starts over after, so its
  copies see the gap and ask. A synchronized clip waits for the mark on its instance, which no stream carries.
- **Knock-downs: built (2026-11-09).** A copy stands up only by its owner's `ActionGetUp` replayed on it
  (`AnimationSystem`, `KnockReplay`), and a copy on the stream takes actions as bookkeeping: it never got up
  (`knockdown-lies-as-host`). While its knock state is not 0 the copy is held off its stream the same way, and asks
  for a sync point once it stands. A death ends the stream (`AnimationSystem`).

## Players (phase 4, 2026-11-10)

Each game records its own player as it does its NPCs, and the host passes a guest's ops and sync points on by the
actor's owner (`HostAuthority::OnGraphOps`, the `FromOwner` and `ToOwner` relays): nothing in the messages is the
host's alone.

- **Only the third-person graph.** A player's manager holds two graphs under one holder, third person at index 0 and
  first person at 1; 63587 and 63591 run for both (63362 sends an event to every graph). A copy is a third-person
  actor with one graph, so the hooks take the player's index 0 alone (`CopiesHave`).
- **A streamed copy takes its owner's actor state with each action** (`ReplayState::Flags1` / `Flags2`): on its stream
  an action is its record, and sneaking is the actor's state, not its graph's (`sneak-host-player` failed without it).
- **A kill move keeps its owner off the stream to its end.** The pair's instance ends at the victim's `PairEnd`, the
  attacker's kill move 1.3 s later (`host-kill-move`: 1820 against 3112 ms). A sync point taken between them is inside
  the attacker's synchronized clip, which a copy starts over: the whole move played twice. An owner is on its own
  body while in an instance, in a kill move, or still in the pair's clip (`OwnBody`, `GraphStreamHooks.cpp`).
- **The live check** (`graph_stream_check`) compares both ways now: the host's actors against the guest's copies and
  the guest's player against the host's copy, a player by its third-person graph.
- **A copy held for its owner's pair** asks for its sync point only once it has played that action
  (`GraphStreams::PairPlayed`) and its own body is out of it (`ReplayState::CopyOwnBody`): a point loaded before then
  cuts the copy's own clip short.
  - Its kill move over.
  - Getting on: `GETTING_ON_OFF_MOUNT` cleared by `MountDismountEndHandler` (42881); a rider's copy stayed "getting
    on" for good.
  - Getting off: its sit state out of wants-to-stand. A copy gets off by its owner's `HorseExit` action alone: no
    `Actor::Dismount` (37906) sets the flag on it, and only its clip's own exit event (`ChairFurnitureExitHandler`
    42835) unlinks it from the horse. Its owner's sync point came 2.4 s after the action, the copy's clip not over:
    back on its stream it rode on for good (`host-rides-horse`, `guest-rides-host-horse`: the step timed out in 5
    runs of 6 before, in none of about 70 after).
- **A request met by a graph with no state** (being rebuilt after a teleport) is kept for the owner's next update
  (`GraphStreams::AddPoint`); it was spent.
- **Only actors' graphs.** Furniture and weapons hold graphs too (a forge's ran through the hooks): `CopiesHave`
  takes a holder that is an `Actor` alone, before anything reads actor fields off it.

**Not the stream's (2026-11-10):** `teleport-keeps-copies` failed on a "crouched" copy of the guest's player. Its
graph was its owner's all along (the live check: 524 updates equal through a failing run); the copy was culled on the
host, and a culled skeleton keeps its last drawn pose, here the bottom of a landing
(`docs/re/systems/culled-npc-pose-readings.md`). Nothing re-activates a graph in place on a teleport: 58328's callers
are a new graph (32893) and a save load (32883 from `TES` 13381), read in IDA.

**A guest crash, explained (2026-11-10):** 59960+0x95 under 59327 under 60079, in a copy's first replayed update
after its sync point (seen twice in about 160 runs, both as a guest arrived in Riverwood; the second on Faendal's
copy). Read in IDA:

```
BSCyclicBlendTransitionGenerator (a walk's direction blend), mode at +0xA8
  0 following, 1 frozen: plain members, carried by a point
  cross-blend event (63133): its effect (+0xA0) gets its two blenders (+0x50, +0x90), mode 3, then 2
  mode 2: the active-node walk asks its sync through the effect (63140 -> 59960), which reads both blenders unchecked
copy: activate (63130) makes a new effect with no blenders; the point's plain members then wrote mode 2 onto it
  -> next walk: 59960 reads a null blender
```

A point cannot carry a cross-blend: its second blender is a clone with the first's node id, and its effect is no
active node. So an owner takes no point while one of its cyclic generators is in mode 2 or 3
(`GraphState::CarriesCyclicMode`, `GraphInternalState::Take`); the point is wanted again at its next update, a
fraction of a second later. GameTests "A copy asking while its owner's walk blend cross-blends gets its point after
the blend" (the fake's `Blend::CrossBlendOn`), ClientTests `[graph-state]`. The wait never showed in about 70 duo runs
after the fix. Forced on the duo (2026-11-10): in Whiterun every copy's stream restarted over and over for 150 s (the
harness's `graph_stream restart`), 8,013 restarts of 36 copies and 7,754 sync points; the wait held a point back 71
times and neither game crashed.

**A copy made on its stream kept "about to sit" (fixed 2026-11-10).** Combined run, `npc-clips-match`: Lucan's copy
read sit state 1 while its owner leaned on his counter (3), in 6 of 8 arrivals, 0 of 8 after the fix. The sit action
leaves 1 and the furniture procedure sets 3 with no action (`AIProcess::SetSitSleepState` 39912); the spawn's graph
state carries both actor state words (`LoadAnimationGraphState`). A copy already on its stream dropped that state
unloaded (its sync point is the newer graph) and then took its spawn action's state after, the stale 1. Now it takes
the spawn state's actor state words alone (`TESObjectREFR::LoadGraphStateFlags`, its own ragdoll and weapon
transition kept) and its spawn action as taken, as off the stream. GameTests "A copy made after its owner sat down is
seated, not about to sit as its owner's last action left it" (the fake's saved graph state now carries `State1`).

**`npc-clips-match` failed on repeats: the scenario's (fixed 2026-11-10).** On the branch most repeats timed out on
"the menu opens on Lucan" or "its conversation ends". Not the stream's: the outcome follows Lucan's sit state at the
moment the host activates him, the same on dev's plugin (8 runs each with the state logged: standing passes, sitting
down (2) opens no menu, leaning on his counter (3) he gives no gesture and the topic walk ends in his shop's barter
menu, which the scenario cannot close). A pass leaves him standing for the next run and a failure leaves him seated, so
runs chain: dev's clean first block was such a chain, not a difference. His walk back to the counter starts at the
same times on both (10 runs each from the logs); an earlier note here of "a second sooner on the branch" rested on one
sample and is withdrawn. The scenario now waits until he stands or sits, moves a seated Lucan to where he is so he
stands (`moveto` himself), reads its trace up to the moment it checks, takes only a gesture he keeps (one replaced a
frame later never shows in a copy's trace, which samples a frame's last action), never asks for his shop, and says his
state when the menu stays shut. The clip comparison's loop test allows 5% of the time between samples (both games had
the same idle 0.26 s behind over 9.4 s; 0.25 s was the limit). After: the branch 14 runs in 14, two of them started
with him seated; dev's plugin 7 in 10 on the first fix (clips differing or started without their files).

**Cost measured (2026-11-10).** `coop_test.py profile --load wolves=10 --seconds 30 --no-tracy`, against dev's plugin
(the fight differs run to run):

| | dev | the branch before | the branch now (3 runs) |
|---|---|---|---|
| messages | 73.6 KB/s | 84.8 KB/s | 15.0, 21.5, 17.6 KB/s |
| host's update, ms a frame (p50) | 1.80 | 2.14 | 1.45-1.55 |
| guest's update | 0.64 | 0.83 | 0.56-0.60 |
| fps (host / guest) | 59.4 / 59.9 | 58.9 / 58.4 | 59.1-60.3 / 59.8-60.3 |

"Before", every accepted action still carried its owner's whole graph state for a copy's recovery
(`HookPerformAction`, `SaveAnimationGraphState`, 7-8 KB each), which was most of a fight's traffic on dev too
(`NotifyReferencesMove` about 70 KB/s). A streamed owner's copies take no actions, so it no longer saves or sends it
(`GraphStreams::Streaming`); a paused owner (in a pair) still does. A copy off its stream while its owner streams
(knocked down) plays actions without that recovery. The pair-clip check (`Actor::InPairClip`) is inside the update
times, not measured apart.

**A rider's copy in "deferred kill" after the ride is by design (read 2026-11-10).** Every living copy is in a
scripted deferred kill, started each update (`docs/re/systems/actor-ai-and-copies.md`, A copy is in deferred kill).
Getting on or off a mount (37462) starts and ends an unscripted one, and the end (`coop_AIProcess_EndDeferredKill`)
only applies when its argument equals the flag the start set: the owner's clears, the copy's scripted one stays. The
comparison already skips the field.

## What went away with the stream (phase 3, 2026-11-09)

- **Movement's graph variables:** a streamed owner (recorded, not in a pair: `GraphStreams::Streaming`) sends none
  with its snapshots, and a copy on its stream loads none (`ApplyRemoteMovement` loaded them every frame; the replayer
  restored them away before each op).
- **Graph points and every-N counters** (`GraphRandom.h`, `RunGraphPoint`, `ActionEvent::GraphCounters`): removed. They
  were sent only for living, unpaired NPCs, which are all streamed.
- **The branch's interim fixes:** refused actions' variables-only sends (`kVariablesOnly`) and action spacing
  (`ActionDue`): removed.

What stays, for copies off a stream (a copy before its first sync point, knocked down,
in a pair, dying): action replay into the graph (`ReplayStep`, `PlayReplayedAction`, `ReplayGraph::Recover`) with the
action's variables, seed and graph state. A paused owner (in a pair) sends its graph variables with its movement
again. Actions still go to the host for its rules (hits, kills, AI state) and to a streamed copy as bookkeeping
(its last action, a death, its owner's pair).

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
owner game, an owned actor's graph (CharacterService records it each send round, its player's too)
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
UpdateGraph(actor), when its graph steps this frame (AnimationStepMs: a frame; a far owner nobody records, a chunk)
  Timeline += step                       its owner's graph time (a copy's starts at the sync point it loaded)
  channels: Floats[TurnDelta] = this actor's heading change / step (63590; a copy's heading is interpolated)
  ranges:   in State, Floats[Input] >= Bound -> Above, else Below (hkbEventsFromRangeModifier)
  clips:    a clip's end -> Starts[Integers[StartFrom]], unless its exit's variable holds none of ExitValues (stuck)
  History += (Timeline, state, "update")
events (GraphTransitions, PerformAnimation / ReplayAnimation): History += (Timeline, state, "event X")
owner AI (OwnerClipEnds): its clip-end actions LeadMs before the end; refused, their ActionWrites (39004) land
```

A pair (`ReplayAnimation` with a target event) puts both actors in an instance (`Actor::InPair`, 32808) and plays the
victim's half as its own action, sent when the victim is an owner's. A replayed pair with a copy partner makes no
instance (`PlayReplayedAction` names no target event). An owner in an instance is not recorded.

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
owner, as the update after a copy asked begins: hkbBehaviorGraph::getInternalState (58372)
  per active node but transition effects: createInternalState (0D), getInternalState (0E), getInternalStateUser (0F)
  into an hkbNodeInternalStateInfo { sync info, node id, activated, state }
  -> GraphState::Node { id, check (class and name), sync, activated, state bytes (NodeMemory), runtime bytes }
copy: the clip sets its owner's process held, loaded first (a clip binds its file as it activates)
  its graph deactivated (58331), its variable set kept, the owner's words in it
  hkbBehaviorGraph::setInternalState (58373): a map node id -> info, then updateActiveNodes with it (60079)
    its walk (59319, which drops the map in Skyrim's build) handed to Havok's own 59318 while the restore runs
    per node as it activates (59326): Activate, setInternalState (10), setInternalStateUser (11), then its children,
    read from its restored state: a machine's restored state, a blender's restored active children
  its event queue emptied (the activations' enter and exit events; its owner's was empty)
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
- **Clip files:** the owner's clip loads are ops in its stream (`GraphOp::ClipSet`: set 3 an action's, 4 an instant
  interaction's, 47 a movement idle's, from `coop_LoadEventClips` 43621, its queued twin 43622 and a save's reload
  43624), replayed in their place and holding what follows until the files are in; the point names each set held, and
  the copy loads them before it restores (a clip binds its file as it activates). The owner's are keyed by its graph
  manager: a save's reload names no actor. A copy asks for no event's clips itself: asking for every event's loaded
  sets its owner never loaded, in place of the one it held. A movement idle's set has no process slot: `ActionClips`
  holds a copy's.
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

## The live check's report

When a copy parts from its owner, `graph_stream_check` says why: the states each holds alone, the events each took, the
variables they held differently as the update began and ended (by name), the events each graph handled with the node
that raised them (`HandleEventOnActiveNodes` 58377), and every clip's local time from the first update they differ.
What it found on the duo (2026-11-09), in order:

| first difference | cause | now |
|---|---|---|
| machines in their start states right after a restore | Skyrim's `updateActiveNodes` walk drops the restore map | 59319's walk handed to 59318 during the restore |
| an idle machine back in its initial state, no input event | the restore's activations raised enter events | the copy's queue emptied after its restore |
| `AnimObjectCounterRightToArm (no file)`, `RailLeanEnter00 (no file)` | sets 4 and 47 not mirrored; a copy's own per-event loads replaced its action set | the owner's loads in the stream, every set |
| `TurnToWalkEnd` a few updates late, its clip's time a step behind | the clip restored before its movement set was in | a point's sets loaded before it restores |
| a copy's clips a little ahead from its first update, its `TurnDeltaDamped` apart | the point was taken right after an update; the owner's generate pass then moved blenders' sync and modifiers' outputs | the point is taken as the next update begins; the events just before it are in it |
| `Wall_IdleBackEnter (no file)`, `Chair_FrontEnterVar1 (no file)` | sets loaded by the queued loader (43622) and by a save's reload (`FinishLoadGame`, 43624, which names a graph manager, not an actor) | every loader recorded, by graph manager |
| `TurnDeltaDamped` written by the copy's update only; its walk blend picks another child | owners back on the engine's stepping generated only after their chunks, copies every frame | generate passes are ops; owners step every frame without `force` |
| a blend's child kept or dropped at a copy's first update | the owner's generate pass before the point set `stateOrTransitionChanged`, which Havok's restore clears | the point carries the flag |
| `MT_WalkForwardLeft` where the owner has `Right`, at a copy's first update | `BSCyclicBlendTransitionGenerator` had frozen its blender's `blendParameter` (63132), a plain member in no internal state | the point carries every active node's plain members |
| a hare's graph given a wolf's point | the guest's leveled reference did not have its owner's base yet | a point names its behaviour project; another project's is dropped until the copy's graph is replaced |
| a copy with another project's graph never on its stream again, its graph replaced with the right one | the hook replays, and so saw the new graph, only for a copy on its stream; a dropped point leaves it off | `GraphStreams::SeeGraph` each update, on or off the stream (GameTests "A copy with another project's graph...") |
| `host-kill-move`: the wolf's copy stands 330-400 ms after its blow, 2 runs in 4 | off its stream after the pair, its death was spaced (`ActionDue`) from the last action it replayed before the stream, 5 s earlier; its graph's clock had run 110-150 ms short of its owner's ticks | a copy on its stream has no action anchor: its next action off it comes at playback (180-237 ms in 6 runs) |
| `host-kill-move`: the guest's blow 20-70 ms before the host's, its victim's clip 60 ms shorter | the wolf's copy took the kill move's event from its stream (63587 on the stack, not the pair's 43567) outside any instance: its synchronized clip started at once, its owner's after 150 ms at the mark | an owner in an instance is not recorded; its copy leaves the stream at the half's action until a point from after the pair (GameTests "A host's kill move takes its victim's copy off its stream...") |
| none: every streamed copy matched its owner at every update, the wolf scenario's 180 s and the town's 120 s (again 2026-11-09 with the rows above) | | |

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
3. **Remove the old paths** for copies on the stream; movement drops graph variables: done (above).
4. **Players' copies** (a player's graph driven by its input; first-person graphs excluded): in progress, not
   shippable yet (Players, above: Open).

## Open questions

- Which engine code sends events into a copy's graph that the copy still needs (death, ragdoll, our own replays)
  outside the exceptions: everything else is dropped.
- Graph rebuilds (3D reload, race change): a new graph on either side means a sync point.
- Open Animation Replacer's random conditions use their own generator (graph-input-replay.md), outside the stream.
- **Clip files on `IdleWoodPutDownEnter`:** in 2 of 6 external runs of the proof, Faendal's copy entered
  `FurnitureState` with `AOWoodPutDown` lacking its file and left it at once. The production replayer holds an event
  for its files (below); whether that covers this event is for the live check to show.
