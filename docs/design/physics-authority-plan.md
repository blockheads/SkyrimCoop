# Physics sync on Fiedler's authority scheme: the plan (2026-11-23)

Status: **built 2026-11-23** (as built: `physics-sync.md`, top). Changed while building, each from a duo or GameTests
finding: a pair is taken only once the paired body leaves where it lay (`PairedPoses`: Havok's pairs are AABB
overlaps, and the held bottle took the whole bar); a state carries `Placed` (the host's game put it: held there
asleep, never woken; the rack's weapons fell); the host's game drops a rest's placer once done (it put the console's
move back); silence needs 30 quiet updates too (one long frame is not silence); a guest streams its still bodies too;
a confirmation must be a rest at the guest's own pose. Was: **plan, nothing built.** Replaces the layers of `physics-sync.md` (#56 to #101) with one scheme, Glenn
Fiedler's ["Networked Physics in Virtual Reality"](https://gafferongames.com/post/networked_physics_in_virtual_reality/),
taken whole where Havok allows it. The audit behind it: this branch's session, 2026-11-23.

Words (to add to `glossary.md`): **authority** is the game that simulates a body now. **Held** is Fiedler's
"ownership" (a body in a player's hand); we do not say "owner" for it, which the glossary keeps for actors.

## The scheme

```text
every body (a reference's, or a ragdoll's)
  Authority   a player, or none: the host's game by default
  AuthSeq     +1 each time a player takes authority, +1 when it rests
  HoldSeq     +1 each time a player grabs it
  Held        in its authority's hand: nobody else may take it

who has authority
  a player pushes, hits, drops or grabs it        -> that player   (take)
  a body under a player's authority touches it    -> that player   (take)
  it rests in its authority's game, host confirms -> none          (give back)
  the host's game places it (script, console)     -> none          (take, by the world)

the authority's game   simulates it and streams its state
every other game       keeps it simulated, steered onto the stream
the host               arbiter: accepts a state by its numbers, streams the winner
```

One rule runs in every game, the host's included: the host's player is a player. Today the host is "everything not
claimed", a guest has three claim rules, and the host keeps five maps to sort out who meant what.

## One state, one message, both ways

```text
BodyState { Key, Authority, AuthSeq, HoldSeq, Held, AtRest, Position, Rotation,
            LinearVelocity, AngularVelocity (none at rest), Gravity, Knocked }

guest -> host   BodyStatesRequest { Tick, States }   its authoritative bodies
host  -> guest  NotifyBodyStates  { Tick, States }   every other body near it
both unreliable; the stream repairs a loss
```

Replaces `BodyFramesRequest`, `BodyRestsRequest`, `BodyReleaseRequest`, `BodyPoseRequest`, `NotifyBodyFrames`,
`NotifyBodyRests`. A let go is a state with `Held` off. A rest is a state with `AtRest` on.

## Piece 1: `BodyAuthority` (pure, every game)

```text
Take(body):                                  // a touch, a contact, a grab
  if body.Held and body.Authority != me: return
  body.Authority = me; body.AuthSeq += 1
  if grab: body.Held = true; body.HoldSeq += 1

Update(touches, contacts, reads):
  for body in touches: Take(body)            // 79133 push, 38628 hit, drop, grab
  repeat until nothing is added:             // recursively, as Fiedler
    for (mine, other) in contacts:           // Havok's collision entries
      if other.Authority is none: Take(other)
  for group in my bodies joined by contact:  // a pile rests as one (Havok: 61507)
    if every body in it is Still and none was touched this window:
      for body in group: body.AtRest = true; body.AuthSeq += 1   // once

OnHostState(state):                          // the host's stream
  mine = bodies[state.Key]
  if Newer(state, mine) or (Same(state, mine) and state.Authority != me):
    bodies[state.Key] = state                // the host arbitrated: I yield
    return Apply
  if mine.AtRest and state.Authority is none and state.AuthSeq >= mine.AuthSeq:
    mine.Authority = none                    // my rest is confirmed
  return Ignore

Newer(a, b): a.HoldSeq after b.HoldSeq, else a.AuthSeq after b.AuthSeq   // 16 bit, wrapping
```

- A player's own touch takes any body not held. A contact takes only a body with no authority: two players' piles
  that touch would otherwise trade bodies every frame. The article leaves this open.
- Contacts are Havok's own list: `hkpLinkedCollidable`'s collision entries (`hkpRigidBody+0x90`, 16 bytes each:
  agent, partner), filled by `hkAgentNnMachine_CreateAgent` (64648, read in IDA 2026-11-23). No distances.
- Deleted by this: `OwnKnocked`, `OwnNudged`, `OwnIsland`, `LayAt`, `kKnockReach`, `kLeanMs`, `kPileRestMs`,
  `m_pile`, `m_islands`, `m_ownRests`, `m_hostRests`, and the `Keeper` interface's eight calls.

## Piece 2: `HostBodies` (the host's rule; replaces `HostHands` and `HostPhysics`)

```text
table[body] = { Authority, AuthSeq, HoldSeq, Held, last state, heard at }

OnStates(guest, states):
  for s in states:
    row = table[s.Key]
    if row.Held and row.Authority != guest: drop            // held is exclusive
    if Newer(s, row) or (Same(s, row) and row.Authority == guest):
      row = s; row.Authority = guest
      if s.AtRest: row.Authority = none                     // given back: the stream confirms it
    else drop                                               // the stream corrects that guest

the host's own game: its BodyAuthority's takes and rests write the table the same way
the host's game places it (PlacedByGame): row.Authority = none; row.AuthSeq += 1
authority silent 2 s, or gone: row.Authority = none; AuthSeq += 1; Held off
```

Deleted: `m_holds`, `m_guestRests`, `m_taken`, `m_defeated`, `m_released`, `Defeated`, `HostRests`,
`MovedByGuest`, `kDefeatedMs`, `kTakenMs`, `kPlacedApart`. Two takers at once: the first the host hears keeps the
number; the other sees the host's state with its own number under another name, and yields.

## Piece 3: `BodyStream` (pure; replaces `BodySends`)

```text
each update, per receiver, over table rows near it and not its own:
  moving:    priority += prediction error / limits     (2 units, 0.05 rad: as today)
             priority >= 1 at 500 ms old                (a lost frame's repair)
  changed:   AuthSeq, HoldSeq, Held or AtRest differ from what it was sent: top priority,
             three sends in a row (a lost rest is repaired at once)
  at rest:   priority += dt / kRestRefresh              (slow; every resting row comes round)
  send the highest within the budget (8 KB/s); a sent row's priority back to 0
```

- This is Fiedler's priority accumulator with our send-on-error rule as its factor.
- The rows are the bodies some game has seen awake this session, not every body in the cell. A guest whose own
  physics wakes a body the stream has never carried sends one hint (`BodyStatesRequest` with that key and no
  authority); the host adds the row. That is all that is left of "ask the host".
- Deleted: `BodyPoseRequest`, `FollowBodies`, `PhysicsBodies::Follow`, `m_moved`, `m_asked`, `kAsksAtMost`,
  `kOwnMs`, `TrackMoved`, `AskHost`. Expected to go too, checked by their tests: `PhysicsBodies`' "woken still"
  (`Woke`, `LeftRest`) and `m_lastRests` (`kRestAgainMs`): a woken, unmoved body has no error against its last
  state, so nothing is sent.
- This is a periodic resend. It is the scheme's repair for drift, decided 2026-11-23 over "events only".

## Piece 4: applying a state (every game, not the authority)

```text
state of a body that is not mine:
  Held          keyframed, carried onto it by velocity   (the engine's grab refuses it)
  moving        BodyPlayback gives the target 100 ms behind -> Steer; 64+ units off: put there
  AtRest        differs from where it lies, or a newer AuthSeq -> RestPlacer; else nothing
  touching one of my own awake bodies: waits (my pile; the contact rule takes it if it moves)

RestPlacer (pure, tick in; one for references and ragdolls):
  Next(near, still, awake, tick) -> Glide | Place | Sleep | Leave
    Glide   steered to the rest, half a second at most
    Place   put there awake and still: one physics step finds its contacts (61518, 61321)
    Sleep   its island put to sleep once all of it is still (60850), 2 s at most
    Leave   there, asleep
  woken off its rest by this game's own physics after: again, twice at most, then left
```

- `BodyPlayback` loses its own blending (`Offset`, `From`, `StartFrom`): the steer closes the gap.
- Deleted: `RagdollRest`, `BodyBridge::Rest`'s state machine and `m_rested`, `BodyService`'s `m_pendingRests`,
  `m_heldRests`, `m_restsTried`, `m_ragdollRests`, the wall clock in `BodyBridge`.
- The live move row stops moving bodies: `GuestWorldState` applies a `kMove` only to a reference with no dynamic
  body loaded here (and at a cell's load, as today). Deleted: `RestsThere`, `RestComing`, `RestedHere`,
  `MovedHere`, and the live use of `PlaceAtHostRest`, `FreePlaced`.

## Where this is not Fiedler, and why

| Fiedler | Here | The engine fact |
|---|---|---|
| snap the physics state each update | steer by velocity, put only when far | 60898 redoes the body's agents: its contacts start over (read) |
| quantize the simulation both sides | no | the same write, every frame |
| a held cube rides the avatar's hand | its own states, keyframed elsewhere | the grab is a spring from the crosshair (40555); a copy has none |
| a rest is a flag | glide, one awake step, island sleep | 61518, 60850: no contacts until a step; a pile sleeps whole |
| every cube streamed | bodies seen awake, plus hints | a cell holds thousands that never move |
| all bodies collide everywhere | played bodies pass loose ones nobody moves (`BodyGroups`) | correcting a wrong knock costs a rest placement |
| any cube a player touches is theirs | only one physics simulates: a body a script holds keyframed is nobody's to take | `ObjectReference.SetMotionType` (56332): a guest that took an untripped tripwire's keyframed rope had the host play its frames, and the trap never fired (2026-11-24) |
| one authority colour per cube | a moving group per group the body had (`BodyGroups::MovingGroups`) | one nonzero group never collides (78553): a taken rope's links collided with each other under one shared moving group |

Kept as they are: `BodyGroups` and the filter hook (78553), a copy's capsule pushing nothing (79133), the node repair,
`HavokStepFix`, the saved move row for cell loads, a ragdoll's rest as its record part for later spawns.

## Not solved by this

A body this game's physics will not keep at its authority's rest (the leaning ladle, `docs/bugs.md`). The placer
tries twice and leaves it, as `kAsksAtMost` does now. Why it falls is not traced; that is its own step 0, after.

## Build order

```text
A  pure, beside the old code      BodyTolerances.h, BodyAuthority, RestPlacer, BodyStream, sequence compare
                                  UnitTests for each; nothing calls them yet
B  the wire                       BodyState, the two messages; TPTests round trip
C  the engine reads               hkpLinkedCollidable typed (static_assert), ReadTouching under the world's
                                  lock, the push and hit hooks watched on the host too; the fake engine's
                                  Touches in place of Islands
D  the switch (one change)        HostBodies; BodyService and GuestHands on BodyAuthority; BodyBridge runs the
                                  placer's steps; GuestWorldState's rule; the old messages, maps and paths deleted
                                  HostAuthorityTests and GameTests rewritten with it
E  the duo                        the like-vanilla suite and the physics scenarios, against a run of today's
                                  dev taken first; wire_cost for the stream
F  docs                           physics-sync.md rewritten as what is, its history cut; progress.md,
                                  glossary.md, re/systems/physics.md
```

A to C change no behaviour and pass `scripts/check.sh` each. D cannot be split: the wire changes under it.

## Tests

Kept and moved onto the new classes (they state behaviour, not mechanism): UnitTests' prediction, playback,
rest placement, island sleep and ragdoll rest cases; GameTests `[mover]` and `[physics]` flows; HostAuthorityTests
`[Physics]`.

New, each written before its piece:

- UnitTests `[authority]`: a touch takes; a held body is not taken; a contact takes only a body with no
  authority, recursively; a pile rests as one; a rest is kept until the host confirms; the host's newer state wins;
  the same number under another name yields; the sequence wraps.
- UnitTests `[stream]`: a changed row goes first, three times; a resting row comes round within the refresh; a
  moving row is sent on error; the budget holds.
- HostAuthorityTests: two guests take one body, the first heard keeps it and the other is corrected; a guest's
  touch takes a body the host's player moved, and the reverse; the host's game placing takes it; a silent
  authority gives it back; a lost rest state is repaired by the next.
- GameTests: a guest drops onto a pile and the whole pile is its (the fake's `Touches`); a body the guest's
  physics moved alone is put back by the stream with no ask; a host console placement reaches the guest once; the
  host's move row moves no loaded body.

Vanilla parity: `collision_suite`'s scenarios (`*-like-vanilla`, the drops, `guest-pile-release-falls`) three rounds
each against the 40-round baselines, before D and after, plus `guest-grabs-orgnar-bottle`,
`object-rests-beside-orgnar`, `host-cannot-take-guest-hold`, `guest-cannot-take-host-hold`,
`ragdoll-lies-as-host`, `death-spectate-guest`.

## To read before the stage that needs it

- D: the host's cell-load state for bodies (`HostEngine`), `CopyRagdollPhysics.cpp` whole, `FakePhysics.cpp`.
- C: whether a broadphase overlap without touch (the entries are AABB pairs) takes too much; measured on the duo.
- D: `PlayerHands::Watch` on the host.
