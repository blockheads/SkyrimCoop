# 2026-10-30: a guest crashes loading the host's save after deleting placed NPCs

## Observed failure

Twice in the follower tag (`crash-2026-10-30-09-25-02`, `09-34-28`, in the client prefix), each time in
`follower-anchor-saved`, the guest crashed while loading the host's save (`SkyrimCoopJoin.ess`):

```
RIP 0 (null function pointer)
19032+0x390  call [rax+0x120]     TESObjectCELL, hands a reference to the cell (r15 = this)
35604+0x4C3                       deferred change-form load
35600+0x4DA                       LoadGame
R12/R14: a placed Character flagged kDeleted|disabled, ParentCell None
         (guard D8C56 "Imperial Soldier", then Elk 846DE)
RCX/R15: the cell, whose vtable pointer points into the heap: a freed cell
```

None of our frames are on the stack. Disassembly from `build/re/SkyrimSE_1.6.1170_unpacked.exe`
(`llvm-objdump --start-address=0x1402ba0b0`): the function keeps its cell argument in `r15` and calls the cell's
vfunc 0x120 at the end.

## What led to it

The guest's log for the second crash (run `20261030-093653-duo`):

1. 09:32:42: the host's harness baseline (`reset_baseline`) deleted about 25 NPCs. It deleted everything not in
   the baseline before taking the party home, so the world's own placed NPCs around the party went with them.
2. On the guest, each copy followed (`FollowOwner`, kDeleted): disabled and marked deleted
   ("Copy 846DE: its owner's actor was deleted").
3. 09:34:27: the guest loaded the host's save and crashed on one of those references.

The host deleted the same references and loaded the same save without crashing.

## Not reproduced

- Vanilla Skyrim (`launch --vanilla`, Riverwood): `disable` + `markfordelete` on placed NPCs (leveled guards
  1B743/1B744, Elk 8480B, Sven 13491), then a load of an earlier save; the same with a cell change before the
  load; the same with a save made after the deletion. No crash.
- The duo: the host deleting the same NPCs (the guest followed with a disable), the guest marking its own
  references deleted, a cell change, then host loads. No crash.
- The follower tag again: no crash.

What makes the cell freed is not known: likely the exterior cell buffer evicting the reference's cell between the
deletion and the load, which these short repros did not reach.

## Fix (2026-10-30, unproven by a repro)

- A guest never marks a plugin's reference deleted: a copy whose owner was deleted is disabled only
  (`CharacterService::FollowOwner`); the host's save carries the deletion. Only references the guest made
  (temporary references) are deleted. A placed reference on a leveled (temporary) base used to take that path:
  `DeleteActor` refused it and left the copy standing with no owner. GameTests `[copies]`, "the host deletes a
  placed NPC" (Vilkas, and a guard on a leveled base).
- The harness baseline takes the party home first, deletes only NPCs a scenario created, and sends the world's
  NPCs back where the game placed them (`scripts/test_late_baseline.py`).

Not changed: the world-state enabled row (`ReferenceStates::ApplyEnabled`) still marks a deleted object deleted on
a guest. Neither crash involved an object.
