# After the ten pieces: what takes the open bugs down (plan, 2026-11-22)

Status: **proposed, nothing built.** From the audit of 2026-11-22 (PR #92, [bugs.md](../bugs.md): 56 open entries on
dev `aa723513`). About half fall into five shared causes; two are worth a refactor (pieces C and D), the rest go
one bug at a time. Each piece is one PR and follows the bug-fix flow (CLAUDE.md): its step 0 is listed, and a piece
whose step 0 disproves its cause is rewritten, not built.

How sure each cause is: **read** (in our code or IDA), **measured** (a run named in bugs.md), **inferred**.

```text
A  two tools: heap history, measured hook sizes      days       unblocks C and the lock entries
B  the floating body after a death (done, PR #94)   one bug    it failed every scenario run after it
C  a worn list freed twice (step 0 rewrote it)      one bug    the vtable crashes, the no-World hook
D  a copy starts whole                               refactor   9 entries (cause read)
E  copies 57-160 units off at a comparison           one read   6 entries
F  world objects nobody owns                         decision   5 entries
G  the rest, one at a time
```

## Piece A: two tools

**A1, heap history that survives a kill move** ("Heap history crashes the host at a kill move").

```text
HookFormAllocate -> HeapHistory::Capture
  today   RtlCaptureStackBackTrace: Wine's unwinder faults on a frame with no unwind data (a trampoline)
  after   walk frame by frame; stop at the first RtlLookupFunctionEntry that finds nothing
```

```cpp
size_t HeapHistory::Capture(void** apFrames, size_t aMax) noexcept
{
    CONTEXT context; RtlCaptureContext(&context);
    size_t count = 0;
    while (count < aMax && context.Rip)
    {
        DWORD64 imageBase = 0;
        auto* pEntry = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (!pEntry) break;                               // no unwind data: stop here, as Windows does
        apFrames[count++] = reinterpret_cast<void*>(context.Rip);
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, pEntry, &context, ...);
    }
    return count;
}
```

Files: `Code/client/HeapHistory.cpp`. Tests: the harness unit for the capture; the duo:
`spectator-watches-kill-move` with `SKYRIMCOOP_HEAP_HISTORY` set, 4 runs (4 of 4 crashed before).

**A2, hook sizes measured, not written** ("A pass-through hook counted as ours"; then "The AI/quest lock pair",
"After the host dies", `host-guard-arrest-jail`'s report).

```text
LockOrder::IgnoreFrames(&HookUpdateAI, 0x50)      the hook grew to +0x59: its frame counts as ours
  after   IgnoreFrames(&Hook) alone: the size from the function's own unwind entry
          (RtlLookupFunctionEntry(&Hook): EndAddress - BeginAddress); no entry -> critical log, the harness fails
```

Files: `BSCore/LockOrder.{h,cpp}`, the 15 `IgnoreFrames` call sites. Tests: the harness unit that counts the
calls gains "every listed hook's size is its unwind entry's". Then the duo: `party-buys-house-guest`,
`party-buys-house-host`, `host-dies-guest-still-fought`, `host-guard-arrest-jail`: each report left is read in IDA
and classified in `docs/re/systems/lock-order.md`, and its entry closed or kept with the pair named.

## Piece B: the floating body after a death (done, PR #94)

Step 0 disproved the plan's cause: no kill move is needed, a plain `player.kill` does it. It was the revive.

```text
frame N   graph update 63587: ragdoll in world -> listed for the physics step (63761, gate 63564)
          our update: Revive -> RemoveRagdollFromWorld 63622 (bodies out)
          bhkWorld::Step: ragdoll pass 63562 on the listed graph -> every body back in, dynamic
then      listed every frame; the standing graph's rigid-body controls (track 4) keep the skeleton on the
          bodies (58291, 58293); a trip leaves them behind: 2,100 units up, 48,000 across
```

- Read in IDA and measured on the duo (harness `ragdoll`, `ragdoll_adds`, `ragdoll_out`): with the old removal in
  the revive, one 63562 pass put the ragdoll back at the revive; the same removal at the frame's start stays out.
- Fix: `DeathService` calls `IPlayerBridge::EndPlayerRagdoll`, which queues `QueueRemoveFromWorld` 36924 on the
  task pool, as vanilla's `RemoveRagdollHandler` 42877 does.
- Tests: scenario `revived-guest-stands-after-travel` (`expect` check `stands`); GameTests "A revived guest's
  skeleton stays on its feet after it travels" and the engine fact's flow. `kill-moves-cross` then
  `host-rides-horse` passes.
- Write-up: `docs/re/systems/physics.md`, "A revived player's ragdoll stays in the world"; `docs/design/death.md`,
  "The ragdoll".

## Piece C: a worn list freed twice (step 0 rewrote it)

**2026-11-22, step 0 disproved the teardown** (`docs/re/systems/inventory-changes.md`, "A worn item removed from an
actor with no process"). Heap history and the quarantine on `guest-ivarstead-fine-mouse-sweep` (hung 3 of 3): no free
by `ObjectReference.Delete` (56154 frees nothing itself: `SetDelete`, `Set3D(null)`, a `DeleteFunctor` after the
render, the collector's job later). The freed block was an item's `ExtraDataList`, freed twice by the engine:

```text
CharacterService::OnDisconnected -> EnableActor (a held copy we disabled)
  Enable 19800: ResetInventory (37322 -> 19802 -> 16038 -> 19690) BEFORE the new AIProcess
    RemoveItem 16059, a worn outfit item, list [142 leveled, 22 worn, 61 cannot wear] beside a twin [142]
      3 extras: 16059 expects the list to survive the unequip
      no process: 19694 -> UnequipItem 37975 -> 38920 strips 61, 16067 strips 22, merges into the twin: FREED
      19694 answers 1 always -> the freed list goes to the nested RemoveItem -> freed AGAIN
```

The cannot-wear extra is ours (`ActorEquipItem` forces the equip). A 32-byte block with two owners: the movement
messages at 92960 / 91958 are the allocator's return, not stale pointers of their own (read).

Built:

```text
HookUnequipForRemove (19694)    after the engine's: does the item's entry still hold the list? no -> remember it
HookRemoveFromInventory (16059) entered next by the nested call: the remembered list -> no list
                                (19694's answer stays: its other caller, 0x14022f49b, reads 1 as "go on")
the clip-load and graph hooks   a WorldGate::Reader, else the engine's own: the cog swap resets the World
                                (PluginUnload.cpp) before it removes the hooks, and engine jobs run between;
                                the unload closes the gate and waits for the readers inside
```

- Tests: the scenario `held_copy_shown_again_frees_no_list_twice` (the hook off: the guest crashed 1 of 1, a vtable in
  a constructor's `this`); ClientTests `[freed-worn-list]`; GameTests "A held copy wearing its outfit beside a twin is shown again: no
  worn list is freed twice" (the fake's `EnableActor` learned the reset and counts the second free); the harness's
  `heap` readout (`double_frees`, with heap history on) and the verb `expect_no_double_free`.
- Dropped: the copy teardown between frames. No step 0 run showed a copy freed mid-frame.
- Found by its runs and fixed with it: a guest deadlock between a copy's clip request, made inside its graph's
  update, and the animation file job's callbacks (`animation-file-loads.md`, "A request under a graph's lock";
  `Games/Animation/DeferredClips.h`). Not the same bug: its only tie is our equip of a copy, whose clip load was the
  callback.

## Piece D: a copy starts whole

**2026-11-22, step 0 rewrote it** (PR #95, `docs/re/systems/actor-state.md`, "What a copy's start leaves out"): no
`StartPart`. Built: a copy on its stream not settled as its owner's is put right (the stuck draw, measured). Not
built: sit state comes with the spawn's graph state already, the last action's window was not found, and no drift
needed the R3 ask. The bow's `[0, bow]` writer, found later (2026-11-23): a copy's 3D load holds a forced bow in the
right hand only (24736, hand-slots.md); the repair checks both hands.

Entries: "A late joiner sees the host's copy stuck drawing", "A copy at Sheathed while its owner draws", "An action
sent before its copy exists", "A seated owner's copy keeps an old action", townsfolk sitting on the host and standing
on the guest ("After a guest rejoins", "A follower's party ahead of its copies"), the werewolf's copy stuck on its
transform, the sheathed bow's hand slot ("A copy without its weapon"), "A refused rider left crouched".

The cause, read in our code and one-simulator.md piece 2:

```text
field of a copy                  written by                          a copy made after the event
sneak, life, kill move, noise    ActorStatePart (the record)         has it
hands, settled (3 or 0)          ActorStatePart::HandsUp             has it
weapon state 1, 2, 4 (mid-draw)  nothing (42824, 42825 write 3, 0)   never: stays where it spawned
sit state, its furniture         the stream's events (42868, 42881)  never
the last action                  a rider on movement updates         one behind, or an old visit's
which hand slot holds a form     the items part's equip, a 3D load   both hands checked (hand-slots.md)
```

And the record's ask (`CopyRecordsService::Ask`, R3) is built and tested but nothing calls it: a copy that is
wrong stays wrong.

The change: the start of a copy (its spawn, and every sync point it takes) carries the result of the events it did
not see, and each part has a check that asks.

```text
owner                               host                    another game
sync point or announcement    ->    kept with the record -> copy starts on it:
  StartPart {                                                 graph    its sync point (as now)
    tick                                                      then StartPart::Apply, same frame:
    weapon state (0-5)                                          weapon state written as the engine's
    sit state + furniture id                                      handlers would leave it
    last action (id, idle)                                      sit state + furniture: placed in it
    hands: form per slot                                        last action record set
  }                                                             slots filled from the items it holds
                                                              furniture not here yet: waits for it (R5)
each second, per copy on its stream, cheap reads only:
  Check(part) says "apart" 2 s running  ->  Ask(id, kind)  ->  owner declares again
```

```cpp
struct StartPart                       // RecordKind kStart; declared at each sync point and the announcement
{
    uint64_t Tick{};
    uint8_t  WeaponState{};            // WeaponState.h, all six
    uint8_t  SitState{};  GameId Furniture{};
    ActionEvent LastAction{};
    GameId   Hands[2]{};
};
bool ApplyStart(Actor& aCopy, const StartPart& acPart)    // only at a start: after it the stream's events own these
{
    if (acPart.SitState && !ReferenceOf(acPart.Furniture)) return false;        // R5
    aCopy.SetWeaponStateAsHandlers(acPart.WeaponState);    // step 0 names the calls
    aCopy.SitIn(ReferenceOf(acPart.Furniture), acPart.SitState);
    aCopy.SetLastAction(acPart.LastAction);
    aCopy.HoldInSlots(acPart.Hands);
    return true;
}
```

One writer per field stays the rule (ClientTests `[actor-state]`): `StartPart` writes only at a start; between
starts the stream's handlers do. The ask is the R3 half: a copy off its owner for 2 s takes a new start.

Step 0 (to read in IDA before the build):

```text
weapon state   what 42824 / 42825 and the draw's start (39004) write beside actorState: the process's slots,
               the 3D's attach node; what a copy at state 2 with no event waits for ("stuck drawing")
sit state      42868 / 42881 / 42835: the furniture handle, the marker, the process's occupied slot; the
               engine's own "already seated" path at a 3D load (IdleChairEnterInstant, measured on Proventus)
the sync point what GraphState restores of these (measured: the graph, not the action record or actor state)
the werewolf   why the copy's graph refuses IdleWerewolfTransformation (its own read: maybe not this piece)
```

Files: `Structs/StartPart.h`, `Structs/RecordPart.h` (`kStart`), `ActorStateService` (declare, apply, the check),
`GraphStream` (declare at a sync point), `CharacterService` (the announcement), `Games/Animation/ActorStateRules.h`.
Deletes: `NotifyCharacterSpawn::LatestAction` and `AnnounceCharacterRequest`'s, `HostCharacters::OnMovement`'s
action record, the hand repair's own compare where the part covers it.
Tests, failing first: GameTests "A copy spawned while its owner sits is seated in its furniture", "A copy spawned
while its owner is drawn shows the weapon drawn", "...mid-draw ends drawn", "A copy made just after its owner's
action holds that action", "A copy apart from its owner for 2 s asks and is put right"; TPTests the part;
HostAuthorityTests "a joiner's spawn carries the start part". Duo: `late-join-client`, `client-angers-host-ally`,
`guest_shows_only_hosts_npcs`, `rejoined-guest-sees-rider`, `party-buys-house-host`.

## Piece E: copies 57-160 units off at a comparison

Entries: "End comparisons in towns" (creatures 57-58 in height, Dorthe 58, Solitude's townsfolk), "A follower's
party ahead of its copies" (Stump 146-159), "A knocked-down copy placed 62 units off", "The host's copy after a
door" (26), the horse and elks left in "A passenger jumps 57 units".

Two shapes, told apart by one run:

```text
expect stays on the same actors, 2 s after the comparison failed
  they agree now      -> moving actors read at two different moments: the comparison reads both games at one
                         session tick (a harness change), the entries close
  still apart         -> a copy held at a stale point. The repeated 57-62 in height is the lead (inferred: one
                         mechanism). Read StepCopy / Place under 37356 and the capsule's ground snap, then fix
```

Files (first branch): `scripts/coop_harness/` comparisons, the status readout's tick. No design for the second
branch until the read.

## Piece F: world objects nobody owns

Entries: "Clutter flags after a vampire rejoin" (the ladles), "Riverwood's scenery drifts apart" (the mill log,
food), "Cover plates after the host leaves", "After a guest rejoins" (references), the containers of "End
comparisons in towns" (change flag `0x8000000` on the host only).

A decision for the user, per kind, before any build:

```text
loose clutter nobody carried (ladles, food)   a  the pile rule for everything: the host declares a rest for
                                                 any body that moved (more messages in towns)
                                              b  each game's own; compared only once a player touched it
the mill log, the Dwemer plates (state)       a  a host row: the activator's animation state and its clock
                                              b  not compared while nothing opened them
the container flag                            read first: what sets kLeveledInventory on the host alone
                                              (16129 under our 16038 hook); likely the check, not a desync
```

## Piece G: one at a time

Order by what a player meets first:

```text
kill moves     a pair that never starts; the attacker's copy not in it; a dragged corpse's copy
dialogue       the still-mouse highlight, hover while the guest picks, a guest's recruitment (read from one
               start: they may share a cause), the barter line
party items    removed without its tag, unnamed at a merchant, held in a list, the starting kit
crime          from "the host is unseen in the guest theft control"
scenarios      frozen speaker, the staging spot, the refused join, the green flash grab, the respawn's press
physics        "Physics against vanilla": its own track (physics-sync.md)
standalone     headtracking, creature wobble, revive's body, the scratch save, quest events, the bought
               horse, sync points asked over and over, mods' random picks
```

## What each piece should close (bugs.md, 2026-11-22)

- **A (4):** the two tools' entries, "The AI/quest lock pair", the lock order left in "After the host dies".
- **B (1, and the scenarios it failed):** "After a kill move the guest's own body floats", closed by PR #94 (any
  death's ragdoll left in the world, not a kill move's).
- **C (2):** "A guest crash through a pointer that holds a vtable" and "A guest crash in the clip-load hook with
  no World"; the wipe's load and the collector's job carry a note (both follow a disconnect).
- **D (9):** the entries above; "A character's base bytes after joining" is beside it (a check reading the base
  where the engine wrote the reference).
- **E (up to 6), F (5):** by the read and the decision.
