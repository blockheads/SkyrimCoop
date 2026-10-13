# Graph replication: syncing behaviour graphs by name

Status: **parked.** The inputs-versus-outputs question is answered and the walk that finds a graph's own
variables exists, as test tooling (below, "Which variables the graph writes"). Nothing in the client uses
it: a copy-side skip was built, measured and reverted ("Decision"). The generic sync of the inputs is not
built. A narrower state reconciliation was prototyped and shelved (`git stash` "graph reconciliation ...").

## Why

Copies are driven by the owner's replayed actions plus its graph variables. The variables travel through
hand-written descriptors (`Code/encoding/Structs/Skyrim/AnimationGraphDescriptor_*`, 65 files, chosen by
the behaviour's hash; modded behaviours go through `Code/client/ModCompat/` and `README-ANIMATION-MODS.md`).
A behaviour without a fitting descriptor syncs no variables. That is the wall for animation mods (Nemesis,
Pandora, Open Animation Replacer): every behaviour mod changes the hash.

The engine's own savegame serializer writes **every variable by name**, for any behaviour. Syncing from
it would need no descriptors.

## What the graph state is (reversed 2026-10-13)

`BShkbAnimationGraph::SaveState` (63601) builds an `hkbCharacter` snapshot (`hkbCharacter::sub_65148`)
and writes it through the stream's slots (our `AnimationStreamWriter`, `Havok/AnimationGraphState.cpp`).
Thirteen sections, each a count and records keyed by **name** (strings are a 16-bit length and characters):

1. **Active state machines:** `"file.hkb/machine"`, a word (index), the state's name, two words. The root
   machine is listed last (`0_Master.hkb/Master_Behavior -> Root` for people). About 10-16 entries.
2. Two `(name, word)` lists, empty in every sample.
3. **Variables** (`sub_140BB88C0`): only those that differ from the behaviour's defaults (all of them if
   `byte_1420243A8`): name, kind byte (1 = a word, then 4 bytes; 0 = a quad, then 16 bytes). No count:
   the list ends with the name `$-NoMoreVariables-$`. About 300 per person, 95% of the bytes.
4. `(name, byte)`; clips `(3 names, words, shorts, bytes)`; two `(name, word)` lists; modifier data
   (`(name, n, m, n quads, m quads)`, e.g. `0_Master.hkb/BSLookAtModifier`); and five more. Empty in every
   sample except the look-at modifier. **Clip times are not saved.**

`scripts/re/graph_snapshot.py` parses all of it (`parse_actor`, `parse_graph`); 520 sampled snapshots
(players, their copies, an NPC and its copy) consumed to the last byte. Run it against a game:
`scripts/re/graph_snapshot.py --instance client --actor 0x14 --vars`. The bytes come from the harness command
`graph_state` (and `load_graph_state` loads one into an actor), both test-harness only.

The two games write the variables in different orders (a hash map's order), so raw bytes of an owner and its
copy differ in ~83% even when they agree: compare by name, never by bytes.

## Measured (2026-10-13, QASmoke, host player in first person, Lydia)

- **Size:** 7.2-7.8 KB per graph, ~3 KB zlib; a player has two graphs (`DefaultMale`, `FirstPerson`), its
  copy one. **Save** 70-110 us per actor (165 us for a player's two), **load** ~230 us.
- **Change per 100 ms on the owner** (by name):

  | | idle, walking | draw, swing, cast |
  |---|---|---|
  | active states changed | 0 | 0 mostly; bursts of 10-22 at a transition |
  | variables changed | 1 (`TimeDelta`) | median 2, up to 15 |

  Most-changing: `TimeDelta`, `PitchOffset`, `CastBlendDamped`, `bAttached`, `currentDefaultState`,
  `iRightHandEquipped`, `IsAttackReady`. A by-name delta is tens of bytes.
- **Copy vs owner** (owner taken 300 ms earlier, the playback delay): active states equal in 106 of 127
  samples for the player's copy. Differences: the random idle variant (`1HM_IdlePickIdle_Behavior`:
  `PickIdleEarly` vs `PickIdleLate`, each game picks its own) and the weapon idle states during a
  sword-to-spell switch (runs of 0.6 s and 1.1 s). Lydia's copy never entered `JumpFall`/`JumpLand` after
  her spawn drop (owner `JumpLandState`, copy `Default_State`): the drift behind the 200 ms interpolation
  failures in `docs/progress.md`.
- **A mid-play load (correction):** loading the owner's current state into its copy snaps the copy to the
  owner's pose in one frame, its active states then equal the owner's, and it keeps taking replayed actions
  (sheathe, draw). But a load moves nothing attached: loaded across drawn/sheathed, the pose changed and the
  sword stayed in the hand (attach and detach are annotations of the draw/sheathe clips, which a jump skips).
  Sounds not checked (no audio capture).

## Name-keyed variable sync

The owner sends, with each movement snapshot, the variables that changed since the last one, by name (or by
an index both games agree on per behaviour project); the copy applies them. Lost packets: variables are
absolute values, so a periodic full set (or changed-since-acknowledged) closes gaps.

**The open question (answered below):** some variables are the graph's **outputs**, written by its own
modifiers. Forcing the owner's value of one onto a copy breaks the copy: `NotCasting` forced false flipped
every update and raised `CastStop`; `currentDefaultState` (the state id `JumpLandEnd` returns to) made a copy
drop its hands (`docs/re/systems/actor-ai-and-copies.md`, "How a replayed action reaches a copy's graph",
cause 3). The descriptors used to leave those out by hand.

## Which variables the graph writes

A variable is the graph's own when a node of the graph, active or not, writes it:
- **an output binding:** `hkbVariableBindingSet::Binding` (0x28) has `flags` at 0x24, `FLAG_OUTPUT = 1`
  (hkxCmd's `compat/hkbVariableBindingSetBinding_1.h`). 59474 sets it while resolving the binding: the bound
  member's `hkb.RoleAttribute` (in the exe's class reflection) has `FLAG_OUTPUT` (0x10) in its flags (+2),
  and sets the set's byte 0x24 (`hasOutputBinding`; CommonLib calls it `needsValueAtActivation`). It is
  0 in the files and set lazily, in `hkbBindable::cacheBindables` (58524) on a node's first activation, so
  reading the flag misses nodes that never ran (`InvertNotCasting_IsActiveModifier` on a player who has
  not cast). We call 59474 ourselves on a **copy** of each binding, which only reads the reflection.
  Outputs seen: `BSIsActiveModifier.bIsActive0-4`, `hkbStateMachine.isActive/currentStateId`,
  damping `dampedValue`, `BSInterpValueModifier.result`, `BSSpeedSamplerModifier.speedOut`, foot IK and
  look-at outputs (about 60 members over 30 classes).
- **a state machine's `syncVariableIndex`** (0x7C), written with the current state's id.
- **an expression assignment:** `hkbEvaluateExpressionModifier` (expressions at 0x50, `hkbExpressionData`
  0x18: text, `assignmentVariableIndex` at 0x8), `"Var = ..."`.

The walk uses the engine's own node iterator (ctor 63865, next 63867, freed with 68109 on +0x40 then 63895,
as `sub_140BC9070` / 63948 does; Open Animation Replacer uses it too), per behaviour with
`kIgnoreReferencedBehaviour`, following `hkbBehaviorReferenceGenerator::behavior` (0x50) itself.
**Variable indices in a node are local to its behaviour file** (`hkbBehaviorGraph::data` 0x88 →
`stringData` 0x78 → `variableNames` 0x30): `bowZoomAmt = ...` at index 61 in `1HM_Behavior` is
`weapChangeStartFraction` at 61 project-wide. So writers are matched by name, then mapped to the project's
index (`BShkbHkxDB::animationVariables`). Code: `Code/client/Games/Skyrim/Havok/GraphVariableWriters.cpp`,
`hkbNode.h`; harness command `graph_writers` lists every writer with its node.

People (`DefaultMale`): 97 of 301 variables are the graph's (1468 writing nodes counted with first person).
Among them `NotCasting` and `currentDefaultState`, as hand-found, and 46 more that `Master_Behavior`'s
descriptor synced: `IsAttacking`, `IsBlocking`, `IsSneaking`, `IsCastingRight/Left`, `bInJumpState`,
`bMRh_Ready`, `i1HMState`, `iIsInSneak`, `CastBlend`, `SpeedSampled`, `bAttached`, ...

**Tried:** `TESObjectREFR::LoadAnimationVariables` skipping the descriptor variables the copy's graph writes,
for every descriptor (the walk once per project, cached). Reverted, see "Decision".

**Measured** (`scripts/re/graph_compare.py`: every owned actor against its copy 300 ms later, by name,
during 17 animation scenarios: draw, cast, sneak, quick sheathe, furniture, input draw/cast, melee,
firebolt, flames, equip; 130 samples per player):

| | forced (before) | left to the copy's graph |
|---|---|---|
| scenarios passed | 17 of 17 (one run) | 16 of 17, then 17 of 17 (two runs; the failure: `flames-client-hits-host-npc`, Lydia's copy with no animation state after her spawn drop; passed 4 of 4 alone) |
| graph's variables differing | `CastBlendDamped` 1-6 samples | `CastBlendDamped` 83-130 samples (up to 0.26), nothing else |
| descriptor inputs differing | `PitchOffset`, `weapAdj`, `Pitch` (a few samples) | the same |

So 47 of the 48 graph-written variables the descriptor forced come out the same on the copy by itself.
`CastBlendDamped` is a `BSInterpValueModifier` result: each graph damps toward the same `CastBlend`, and at
rest the value frozen in an inactive node differs (0.03 vs 0.06). Nothing reads it there.

Two things a name-keyed sync must handle, seen here: names are **case-insensitive** (BSFixedString; one game
had `Speed`, the other `speed`), and `TimeDelta` is per-frame engine input on both sides.

## Decision: parked (why)

**What forcing an output does.** A copy's graph runs locally: we send its owner's inputs and replay its
actions, and the graph works out its outputs itself. Overwriting an output with the owner's value fights the
graph, which sets it back from its own state. Usually that is harmless (the graph wins next update). It
breaks when a node reacts to the *change*: `NotCasting` flipping every update raised `CastStop` and ended the
copy's cast; `currentDefaultState` sent the copy to an idle state that was not its own.

**What the skip bought:** nothing visible in vanilla. The two harmful outputs were already out of
`Master_Behavior` by hand, the other 46 were overwritten by the copy's graph anyway (the table above). What
it would cover: outputs still in other descriptors, all untested (below).

**Why it is not in the client:** it guards the hand-written descriptors, which are our own data and can be
fixed as data. The guard costs a walk of the whole graph through four engine functions new to us, on the
first copy of each project, possibly on an animation worker thread under a lock; it has not run on creature
or modded graphs. Small risk for a benefit that is mostly unproven. It earns its place only with a generic
(mod) sync, which is parked too:

**Why the generic sync is parked:** for vanilla it adds nothing (outside the descriptor only `weaponSpeedMult`
and `TimeDelta` ever differed: the copy's own engine sets the rest). For behaviour mods,
`Code/client/ModCompat/BehaviorVar.cpp` already maps the vanilla descriptor onto a modded project by name and
adds a mod's variables from patch files (TDM, TUDM, MCO, Elden Counter, dragons shipped). What a generic sync
would add is a mod's variables without a patch, and dropping the 65 descriptors. And variables are probably
not what a modded copy misses first:
- **Animation events.** A copy replays only actions its owner's engine accepted through `PerformAction`.
  Mods drive their behaviours with events from Papyrus (`SendAnimationEvent`), SKSE plugins (MCO, TDM,
  Precision send graph events directly) and clip annotations; none reach the copy (object script animations
  are off too, `OBJECT_ANIM_SYNC 0`).
- **Open Animation Replacer** picks its replacement clip on each game from its own conditions; no graph
  variable carries the choice (AnimSync Together, Nexus 189136, syncs "OAR condition state" for that).
- **SKSE plugins run on copies too** and may write the same variables (TDM's directional movement).

**Before picking it up again:** run one modded setup (Pandora + MCO, an OAR pack) through the draw, attack
and cast scenarios and see what breaks. If it is variables: the plan below. If events or OAR: those first.

**Outputs still in descriptors (no scenario covers them yet):** `NotCasting` in Vampire Lord (the bug that
ended the player's casts), `currentDefaultState` in steam, falmer, troll, draugr and werewolf, and per
creature the `IsAttacking`/`bAttached` kind. Fix them as data when each gets a scenario: spawn the creature,
`graph_writers` lists its outputs, drop them from its descriptor.

## If picked up: sync the inputs by name

The rest, 204 of 301 for people, are inputs: written by the engine (`SetGraphVariable*`: `Speed`,
`iRightHandEquipped`, `bWantCastRight`, ...), scripts and animation mods (OAR conditions, BDI-injected
variables), or constants. The descriptor covers 37 of them. Plan: the owner sends, with each movement
snapshot, the inputs that changed, by project index (both games load the same behaviour files, the
descriptor key checks the name list matches) or by name; the copy applies them and skips its graph's own
(the reverted skip, cached per descriptor key and project name: projects can share a variable list).
Behind the descriptors first, compared with `graph_compare.py`, then the descriptors removed.

## Later: state reconciliation (shelved prototype)

Each snapshot carries a 4-byte fingerprint of the root behaviour file's active states (sub-behaviours
excluded: random idle picks, locomotion detail); a copy whose fingerprint differs for 3 snapshots asks its
owner for its state **at that snapshot tick** (the owner keeps 1.6 s of them; the copy plays 300 ms behind, so
its owner's current state would run ahead of its queued actions) and loads it, plus the engine's instant
draw or sheathe when the weapon state changes. `git stash list`: "graph reconciliation (fingerprint, owner
history, GraphStateQuery/GraphCorrection relay)": `AnimationGraphState::Fingerprint`, `Movement::
GraphFingerprint`, `LocalAnimationComponent::GraphHistory`, payloads `GraphStateQuery`/`GraphCorrection`
with a host relay and a bridge test. Built, not run. Shelved because it covers only stances, not mods; with
name-keyed variables in place it is a small addition on the same data.
