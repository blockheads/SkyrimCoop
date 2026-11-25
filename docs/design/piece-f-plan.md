# Piece F plan: world objects nobody owns (2026-11-22: F1-F3 built, F4 read)

Piece F of [bugs-refactor-plan.md](bugs-refactor-plan.md). The user's decisions (2026-11-22): containers' flag
ignored when the items agree; loose clutter's nudges negligible, ignored; the mill gets a host row (a); the cover
plates need more investigation first.

## What was read, measured, inferred

```text
read (IDA)     0x8000000 = CHANGE_REFR_LEVELED_INVENTORY, recorded only by AddLeveledEntries 16128 (the roll);
                 the host rolls world containers at load, a guest skips the roll (HookInitLeveledItems) and is
                 given the host's items as counts (0x20): same items, flag on the host only
read (IDA)     0x10000000 (animation) is recorded once, when a reference's graph is set up for its 3D (20103),
                 not per animation event: the graph row goes once
read (code)    a guest's own nudge under BodySendLimits (2 units, 0.05 rad) is never asked about
                 (GuestHands::TrackMoved); the world check fails at 1 unit (position) and 0.02 rad (bodies)
read (esm)     the mill log 71DFC: FURN ResourceObjectSawmill, ResourceObjectSawMillScript; its activate parent
                 1B752 is a lever FURN (LeverPushAnimatingMetal, TrapLever)
read (psc)     the mill's cycle: waiting -MillLogPileLoadStart-> busy -MillLogChuteIdle-> ready
                 -lever activates it-> WorkResource: PlayAnimationAndWait(MillLogChuteCut) -MillLogIdleReset-> waiting
read (code)    a script's PlayAnimation(AndWait) on the host is relayed and played on the guest (kAnimation)
inferred       the log's load is started by its worker (Hod) using the mill furniture: an engine path, not a script
not read       how an actor's furniture use sends events to the furniture's own graph
not read       whether Hod's copy, entering the furniture on the guest, plays the load there too (out of phase)
inferred       the plates: a graph restored without NiAVObject::Update on another path (the 2026-10-29 shape)
```

## F1 The containers' flag (harness only)

```text
reference_differences, field "changes"
  items read on both games and equal  ->  0x20 and 0x8000000 masked (how they came, not what they are)
```

```python
if field == "changes":
    if "items" in host and "items" in guest and host["items"] == guest["items"]:
        a, b = (a or 0) & ~CHANGES_INVENTORY_KINDS, (b or 0) & ~CHANGES_INVENTORY_KINDS   # 0x20 | 0x8000000
```

Files: `scripts/coop_harness/comparisons.py`, `common.py`. Tests: `scripts/test_world_diff.py`: a chest with the same
items and `0x8000000` on one game: no difference; different items: still reported.

## F2 Loose clutter's nudges (harness only)

```text
a loose physics object (simulated: its body dynamic) on both games
  position and bodies compared at the sync's own limits: 2 units, 0.05 rad
                                       (BodySendLimits; was 1 unit position, 0.02 rad bodies)
  0x2 (moved) on one game only, the bodies within those limits  ->  masked (the host's rest placed it there)
```

```python
LOOSE_TOLERANCE, LOOSE_TURN_TOLERANCE = 2.0, 0.05      # BodySendLimits: what the sync itself leaves
if simulated(host) and simulated(guest):
    collision_difference(a, b, turn_tolerance=LOOSE_TURN_TOLERANCE)
    if the bodies agree: changes ^= CHANGES_MOVED where only one side has it
```

Left failing, on purpose: a body further off than the sync's limits (the 5 units / 0.3 rad ladle, 2 of ~500 flows):
that is past what the guest asks about, so a real miss. **Question:** keep those failing, or close the clutter
entries outright?

Files: `comparisons.py`, `common.py`. Tests: `test_world_diff.py`: a ladle 0.04 rad turned with `0x2` on the guest
only: no difference; 0.1 rad turned: reported.

## F3 The mill log: a copy's furniture gets its events, as the owner's does (built 2026-11-22, approved)

Step 0 done ([furniture-graphs.md](../re/systems/furniture-graphs.md)): the load is Hod's own graph event, which
reaches the mill only because the engine made the mill's graph manager a dependent of Hod's when he sat
(`SetSitSleepState` 39912 -> `AddDependent` 63364). A copy's furniture field (middle-high `+0x208`) is never set, so
the copy's identical event reaches nothing. The cut already reaches every game (the relayed script animation).

```text
owner (host)                          copy (guest), today            copy, after
sits: 39912 -> AddDependent(mill)     sits by its replayed graph     sits by its replayed graph
IdleMillLoadStart -> event -> mill    same event -> nothing          same event -> mill
stands: RemoveDependent(mill)         -                              RemoveDependent(mill)
```

```text
owner: its occupied furniture changes (39912's furniture, AI state already read by AiStateService)
  -> the copy's AI state row carries it (no new message: a field of the row)
guest, the row applied to the copy
  furniture set   -> AddDependent(copy's manager, furniture's manager)      (63364, as 39912)
  furniture gone  -> RemoveDependent                                         (63366)
  the copy unloads / leaves -> RemoveDependent
```

Why not a host row of the mill's state (the first plan's a): the mill's graph would be put to the host's state while
the copy's events still go nowhere, and each furniture with moving parts would need it. This uses the engine's own
link, for every furniture.

Bug-fix flow: scenario through the MCP (Riverwood: wait for Hod's load, `expect` the mill's `IsLoaded` agrees);
GameTests flow (the fake learns: a sitter's events reach its furniture's graph only through a dependent); UnitTests
for the rule; then the fix.

## F4 The cover plates: investigation only

```text
duo, host-quits-to-main-menu's place (QASmoke), the seven DweButton01 (ACTI 1BA5A)
  1  world_state twice a few seconds apart on one game: do they animate untouched?
  2  after the host quits: each plate's graph state (graph_state) and CoverPlate01's local and world
     transforms on both games
       local equal, world not   -> a graph restored without NiAVObject::Update on that path (find the path)
       graph states differ      -> which game's state is the vanilla one, and what set the other
  3  IDA: the path that restored them (load, cell attach, our ApplyGraph), read and named
```

Result: plain findings back to the user, then a fix plan. Nothing built in F4 before that.

## Order

```text
F1, F2   harness, no Skyrim, one change         (fast)
F4       one duo read                            (while F1/F2 wait for nothing)
F3       IDA, then the duo trace, then the flow  (the longest)
```
