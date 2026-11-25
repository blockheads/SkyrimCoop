# Piece 10 plan: ragdolls follow the pile rule; scenery is nobody's (2026-11-21, proposed)

Status: **parts a to e built 2026-11-21 (approved by the user).** Duo, 2026-11-21:
`ragdoll-lies-as-host`, `knockdown-lies-as-host` pass; `kill-move-kills-guest` 6 of 6 after the sleep fix (below,
physics-sync.md); `death-spectate-host` 5 of 6 (the miss a wolf's stamina); `copy_holds_furniture_prop` passes its end
comparison; `guest-rejoins-while-joined` passes its steps, its end left with the pheasants' sway (now left out) and a
loaf; `party-shares-a-horse` failed on the mount jump (open, not this piece); `guest_shows_only_hosts_npcs` failed on
townsfolk's animation picks (not this piece), no Skeleton difference. Piece 10 of [one-simulator.md](one-simulator.md). Engine facts:
`docs/re/systems/physics.md`, "A lying ragdoll's layer and group" and "A translating reference"; journal
`docs/re/journal/1.7.104/agent-piece-10.tsv`.

## What was read, measured, inferred

```text
read (IDA)      a lying ragdoll's bodies: layer 32 (L_DEADBIP), the actor's own collision group
                  (42873 -> task 18 -> bhkWorld +0x1A8 with the controller's group; 39895 and KillImpl set layer 32)
read (IDA)      a critter moves by TranslateTo: its cell's update (18898) advances an ExtraRefrPath on this game's
                  clock; only the game whose VM ran the script moves it
read (IDA)      change flag 0x8000000 = leveled inventory rolled (16128): each game's own roll
read (IDA)      Actor::IsInRagdollState (37491): knock state 6 alone does not make it true (its other conditions:
                  +0xE8 bit 31, dead without flags2 0x10, knock 1-5 or 7, flags1 bits 21-24 at 3)
read (esm)      layer 32 meets clutter, weapons, props, debris and other actors' layer 32; never a capsule (30)
read (esm)      the bug list's scenery: CritterMoth* (critterMoth), FCAmbWaterfallSalmon* (FXfakeCritterScript),
                  DoNotPlaceSmallCritterLandingMarkerHelper (a moth's landing marker), HangingPheasant02 (FLOR),
                  ResourceObjectSawmill (FURN), DweButton01 (the "cover plates")
read (our code) between its owner's last frame and its rest a copy's body is steered every update
                  (BodyPlayback shows the last frame's guess), and steering wakes it (78089/78090)
read (our code) an actor's ragdoll rest reaches only games that were there: no spawn carries it, the host's
                  answer to an ask (ReadBodies) skips actor keys
measured before a dead copy sleeps, then wakes at its owner's rest and at the place step 30 steps on, and settles
                  3 to 14 degrees off (bugs.md, 2026-11-12, 11 of 13 traces)
inferred        the two wakes are our own steer and place (the code above fits the trace; no new run)
inferred        the three ladles (bugs.md, "Clutter flags") are pushed by a copy's ragdoll: layer 32 meets clutter
inferred        the shake while falling is Steer's fixed 50 ms against a longer frame (references got SteerSeconds)
not read        what a living actor lying by RagdollInstant is to the engine (knock state, awake or asleep)
not read        why a copy stood 62 units up once in 25 (left out of this piece)
not read        how a grab and a swing reach a layer 32 body (10e needs it)
```

## 10a Scenery out of the world check (harness only; first)

```text
world_state (details)   each reference gets "scripts": its VM script classes, parents included   (new readout)
                        and "on_path": it has an ExtraRefrPath (extra 161)

comparisons.scenery(ref) -> why, or None
  "critter"   a class in its chain is Critter, CritterSpawn or FXfakeCritterScript (placed or created)
  "on path"   on_path
  "marker"    a created reference whose base is a critter's landing marker (its maker's script is a critter's)

world_differences       scenery on either game: not compared, never "host only" / "client only"
reference_differences   pose of a FLOR: never compared (harvested is its flag, compared as before)
                        everything else as today
```

```python
SCENERY_SCRIPTS = {"critter", "critterspawn", "fxfakecritterscript"}
def scenery(ref):
    if SCENERY_SCRIPTS & {s.lower() for s in ref.get("scripts", ())}: return "critter"
    if ref.get("on_path"): return "on path"
```

- Loose clutter stays compared: it is physics, and 10b is its fix.
- Containers' `0x8000000` stays compared: it is piece 3's ("References differ").
- The sawmill's log and the `DweButton01` plates stay compared (the user, 2026-11-21: state, not scenery). Their
  entries stay open in bugs.md as activations (piece 6's kind), not closed here.

Files: `TestHarness/EngineCalls.{h,cpp}` (`ReferenceState::Scripts`, `OnPath`), `TestHarness/CommandsWorld.cpp`,
`scripts/coop_harness/comparisons.py`, `common.py`.
Tests: Python unit tests of `world_differences` (new `scripts/test_world_comparisons.py`): a moth apart, a landing
marker on one game, a salmon posed differently, a pheasant's `b_Lower` turned: no difference; a cabbage moved, a
door's pose, a chest's items: still reported. Duo, to confirm only: the end comparisons of
`copy_holds_furniture_prop` (Riverwood), `party-shares-a-horse` (plains), `host-quits-to-main-menu`.

## 10b A copy's ragdoll in the played group

```text
a copy lies (dead, or knocked down: layer 32)      BodyBridge::Find hands out its bodies (First)
  -> each ragdoll body: group kPlayed (SetBodyGroup); the engine's re-add (task 18) writes the actor's back,
     so it is set again at each First
filter 78553, BodyGroups::Decide, unchanged
  played x world, terrain, statics                 engine: collide
  played x played                                  pass   (its own limbs, another copy's ragdoll, a played object)
  played x a loose object nobody moves             pass   (its owner's never hit that one here)
  played x what this game moves                    collide
the copy stands, is revived or forgotten           its own group given back (kept at the first set, as SetClass
                                                   does for a reference): its bodies can stay in the world keyframed
the owner's game                                   unchanged: its ragdoll meets everything, as vanilla
```

```cpp
// BodyBridge::Find, actor branch
if (first) m_ragdollGroups[acKey] = SetBodyGroup(*pBody, BodyGroups::kPlayed);   // its own, to give back
// where m_ragdolls drops a standing copy's bodies: SetBodyGroup(body, its own)
```

Tests: UnitTests `BodyGroups::Decide` for a layer 32 played body (loose: pass; static: engine; moving: collide;
two played: pass). GameTests "A host NPC dies onto a ladle: the guest's ladle stays where the host's does" (the fake
learns layer 32 pushes loose bodies unless played) and "Two host NPCs die in a heap: each copy lies as its owner".
Duo: `ragdoll-lies-as-host`, `knockdown-lies-as-host`, and one new recorded scenario for open question 8, a corpse
on stairs.

## 10c A copy's ragdoll rests by the pile rule

```text
today     owner's last frame ......... its rest arrives
copy      steered each update (awake)  steered 30 steps -> all placed in one step, awake -> left
                                       Havok settles them again, 3 to 14 degrees off; a new rest: 30 steps again

proposed  BodyPrediction::RagdollRest, one decision per copy per physics step, all its bodies together
  every body near its rest and still     kSleep   SleepBody on its island (refused while another body in it moves)
                                                  done once asleep, or after 2 s (RestSlept)
  off, under half a second               kGlide   steered toward the rest (GlidesToRest)
  off, half a second gone                kPlace   all put there in one step, awake; the next step: kSleep
  done                                   kLeave   untouched: no steer (a steer wakes it)
  done, then awake and past the send limits, no owner frame    the rest again, twice at most
```

```cpp
enum class RagdollRestStep { kGlide, kPlace, kSleep, kLeave };
constexpr RagdollRestStep RagdollRest(bool aAllNear, bool aAllStill, bool aPlaced, bool aDone, float aSeconds) noexcept
{
    if (aDone) return RagdollRestStep::kLeave;
    if (aAllNear && aAllStill) return RagdollRestStep::kSleep;
    if (!aPlaced && aSeconds < kRestGlideSeconds) return RagdollRestStep::kGlide;
    return aPlaced ? RagdollRestStep::kSleep : RagdollRestStep::kPlace;
}
```

- Reuses `GlidesToRest`, `StillToSleep`, `RestSlept`, `SleepBody`. Deletes `StepToRest`, `kRestSettleSteps`, the
  per-body step counts in `CopyRagdollPhysics.cpp`.
- A falling copy's steer takes the frame time (`SteerSeconds(frame seconds)`), as a reference body's: the shake
  hypothesis, judged on the duo by `RagdollTrace`'s turn-backs, kept only if they drop.

Files: `Physics/BodyPrediction.h`, `Games/Skyrim/Havok/CopyRagdollPhysics.cpp`, `Physics/RagdollPending.h`.
Tests: UnitTests `[ragdoll-rest]` (each row above; nothing steers once done). GameTests "A dead copy at its
owner's rest sleeps and stays asleep": the fake learns that a steer wakes a body and that a ragdoll left awake at
a placed pose settles off it; fails today. `[corpse-rest]`, `[death]`, `[held-knock-rest]`, `[rising]` keep passing.
Duo: `death-spectate-host`, `kill-move-kills-guest` (no wake after the first sleep; the lie check passes).

## 10d A ragdoll's rest is a record part (R3, R5)

```text
owner   its actor's ragdoll rests (PhysicsBodies::Rested, an actor)   Declare(id, kRagdoll, {tick, poses})
        awake again, up, revived                                       Declare(id, kRagdoll, {})
host    HostRecords keeps it; every spawn carries it, as every part
copy    Apply -> BodyService: its ragdoll's rest at that tick (today's path from there on, 10c)
        no 3D yet, or not lying yet: it waits; OnSessionId at its 3D applies it
NotifyBodyRests carries reference bodies only
```

Closes the gap behind "A copy that stays off its owner": a body already lying when its copy is made (a join, a cell
load, a guest's hidden NPC shown again) has no rest today and lies where the guest's own physics drops it.
Before building: `body_trace` of a living actor lying by `RagdollInstant` on the host (does it ever read as rested?).

Files: `Structs/RecordPart.h` (`kRagdoll`), a `RagdollPart` struct, `Bridges/PhysicsBodies` (the declare),
`Services/Generic/BodyService` (Register, Apply), `Services/Host/HostPhysics` (actor rests leave the message).
Tests: TPTests the part; HostAuthorityTests a joiner's spawn carries it; GameTests "A guest joining beside a corpse
shows it lying as the host's" and "A lying body whose copy gets its 3D late lies as its owner's".

## 10e A dead body is a pile object: the mover's until it rests (built 2026-11-21)

Checked on the duo first (the user: no code we don't need):

```text
a guest's sword hit on a host corpse   the swing's target is the dead actor (38628, read); the hit is forwarded and
                                       applied on the host ("Applied forwarded hit"), and neither game's corpse moves:
                                       nothing to sync (measured, 4 swings, bodies within 0.1 units before and after)
a guest's grab of a host corpse        40555 takes a dead actor and springs the picked body (read); the guest's copy
                                       went 120 units, the host's never moved: 151 units apart for good (measured;
                                       scenario guest-drags-host-corpse)
```

So only the grab is built, on the object path:

```text
guest   GuestEngine::ReadHeld: a held dead copy -> its ragdoll's bodies, keyed by its session id; watched
        (PhysicsBodies::Watch) so its fall and rest are this game's own reads (OwnBodies::Objects / ObjectsRested)
        GuestHands as for any held object: own while held and until it rests, frames, release, rest
        BodyService: the owner's rest of a corpse it moves is not put on it (Keeper::KeepsRest)
host    HostHands::HostsBody: a ragdoll of an actor the host owns is claimed as an object (first mover, release, rest);
        a player's own ragdoll keeps its path
        BodyService::ActorOf: the host's own corpse plays the frames (CopyRagdollPhysics drives an actor with frames
        queued); HostPhysics does not stream it while a guest moves it
        its rest comes back through the host's own reads: the kRagdoll part (10d)
```

Tests: GameTests "A guest's player drags a host NPC's corpse: it lies where the guest left it, in every game" (fails
with the host's claim off); HostAuthorityTests `[corpse-drag]`. Duo: `guest-drags-host-corpse` (recorded).
Duo, 2026-11-21: `guest-drags-host-corpse` 6 of 6 lie as the host's; 2 of the first 3 failed the end check on the
guest's player "sunk into the floor", the harness's floor probe hitting the corpse under it (fixed, physics.md); 3 of 3
pass after. A cog swap in the middle of a hold drops it (both games' hold state starts over): not a player's case.

## Order

```text
10a  scenery out of the world check      harness; unblocks every town's end comparison
10c  rest by the pile rule               the two rest bugs
10b  played group                        the ladles; open question 8's scenario
10d  the rest as a record part           the late copy
10e  a dead body is the mover's          built; hits checked and left as they are
```

Each: its GameTests flow first, failing; then the change and its deletes; `scripts/check.sh`; only the scenarios
named, on the duo.

## Bugs each part should close (docs/bugs.md)

```text
10a  "Whiterun plains' critters differ", "Riverwood's scenery drifts apart" (fish; the log and plates once measured),
     the fish and plants of "End comparisons in towns" and "After a guest rejoins"
10b  "Clutter flags after a vampire rejoin" (if the trace agrees)
10c  "Copy ragdolls at rest", "A dead player's copy a little off its owner's body"
10d  "A copy that stays off its owner"
10e  no entry: found and fixed in this piece (scenario guest-drags-host-corpse)
not here  "A knocked-down copy placed 62 units off" (bugs.md, once in about 25 runs of knockdown-lies-as-host, cause
     unread): about where a getting-up copy is placed, not its ragdoll; its entry stays as it is
     the sawmill's log, "Cover plates after the host leaves": state, stay open
```

## Decided (the user, 2026-11-21)

1. The rest is a record part (10d).
2. Critters by script class.
3. The sawmill's log and the Dwemer plates are state: still compared.
4. Every physics interaction syncs: 10e.
5. The 62-unit stand-up is not part of this piece.

## Open question

A living actor knocked down stays its owner's while a guest hits it (10e covers dead bodies only): agreed?
