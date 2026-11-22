# A furniture's own graph and its worker (read in IDA 1.7.104 and measured, 2026-11-22)

A furniture with a behaviour graph (Riverwood's sawmill `71DFC`, `ResourceObjectSawmill`, project
`MillLogPile_Project`) is driven by the graph events of the actor using it. Journal: `agent-world-objects.tsv`.

```text
an actor enters the furniture: AIProcess::SetSitSleepState 39912 (new state 1 or 5)
  the furniture's graph manager made a dependent of the actor's:
    BSAnimationGraphManager__AddDependent 63364 -> AttachDependent 63382 (+0x58 list, count +0x68)
  leaving (state 0): RemoveDependent 63366 -> DetachDependent 63389
  (63365 -> AttachManager 63381 adds to the same list: the mount and tail holders' path, not this one)
from then, every event of the actor's graphs reaches the dependent's graphs:
  input   NotifyAllGraphs 63362: its own graphs, then NotifyAttachedManagers 63372
  output  a graph's event (63588) -> its event source -> the manager's sink 63371 -> 63372
```

- The sawmill's cycle (its behaviour, `scripts/re/behavior_dump.py machines`): idle -`MillLogPileLoadStart`-> loading
  -`MillLogChuteIdle`-> chute -`MillLogChuteCut`-> cut -`MillLogIdleReset`-> idle. Its variable `IsLoaded` is the
  state machine's state id.
- The load is Hod's: his idle `IdleMillLoadStart` (IDLE `71C4A`, conditioned on the mill's `IsLoaded`) plays
  `AnimObjectMillLoadStart`, whose graph event `MillLogPileLoadStart` reaches the mill by 63372 (measured: the mill's
  input from `BC16AC <- BC15E9 <- 2FA638 <- BCF3E1`). The cut is its script's (`ResourceObjectSawMillScript`,
  `PlayAnimationAndWait`), started by the lever furniture `1B752` (`TrapLever`) Hod pushes; the host already relays it.
- The furniture handed to 39912 by a graph's sit event (`Actor::SetSitSleepStateFromGraph`) is the actor's occupied
  furniture, `Character::sub_38708`: middle-high `+0x208` (39916), else the mount. The only writer of `+0x208` read is
  39912 itself, storing the furniture it is given; a copy's AI never calls it with one, so (inferred) a copy's stays
  unset and its furniture gets no dependent. Measured: on the guest Hod's copy sent `MillLogPileLoadStart` and the
  mill got nothing (duo 2026-11-22: host `IsLoaded` 3 -> 4 -> 1, guest 0 throughout).
- A player's own activation (`TESFurniture::Activate` 17420, furniture with default object 182) sends the action's
  event to the furniture's graph directly, not through a dependent.

## A copy's furniture (2026-11-22)

```text
owner: 39912 sends TESFurnitureEvent (ScriptEventSourceHolder +0x528, enter and exit)
  -> AiStateEvents raises AiStateChangeEvent -> its AI state part carries the furniture (GetSeatFurniture: 38708
     while its sit state is set, never a mount)
copy: SetCopyAiFields -> Actor::LinkFurnitureGraphs: RemoveDependent 63366 the last, AddDependent 63364 the new
```

The copy's own replayed events then reach the furniture as its owner's do. GameTests "A copy working a furniture
drives the furniture's own graph as its owner does"; scenario `copy-worker-drives-furniture`.

A link is recorded only once both graph managers exist, with the two managers linked: either 3D still loading, the
copy is linked when it loads (`ReferenceLoadedEvent` for the furniture, `Copy3DEvent` for the copy:
`AiStateService::RelinkSeats`), and a new manager on either side (a reload) is linked again. A furniture the host's
script created goes by its created id (`CreatedIds`), so a guest's twin under its own id is the one linked. GameTests
"A copy sitting in a furniture whose 3D loads after links to it as the 3D loads", "A copy sits in the guest's twin of
a furniture the host's script placed".
