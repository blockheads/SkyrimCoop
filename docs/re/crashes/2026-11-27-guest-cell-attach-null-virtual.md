# 2026-11-27: guest crash attaching a cell, a null virtual on a reference

**Symptom:** the guest crashed in `party-travel-mounted-guest`, as the party travelled from the river to QASmoke with
the guest riding, after `host-rides-horse`, `guest-rides-host-horse`, `horse-passes-between-riders`,
`two-players-race-for-horse` and `mounted-guest-leaves` in the same session. `EXCEPTION_ACCESS_VIOLATION`, RIP 0,
called from 19187 + 0x9F on the main thread. Crash log
`prefix-client/.../SKSE/crash-2026-11-27-02-33-30.log`. Branch `agent/cleanup` at `bf190e82`. 1 of 3 runs of that
sequence that night; the other two ran whole with no crash.

**What the code does (read in IDA):** 19187 (`TESObjectCELL::sub_19187`) takes a reference by handle as a cell's
references are attached (under 18976, 13427, 13430, 13428, 13288, the player's cell change from 40744/40745) and calls
virtuals on it. The call at +0x99 is `vtable + 0xE8` (slot 29); RIP 0 means that slot was null. The earlier virtual
on the same object (`vtable + 0x380`) had returned, so the object's vtable was readable but not a live reference's.
RBX named it `Character 0x000DC54F` (Skyrim.esm); the temporary cell `FF000A70` was on the stack at `RSP+3C0`.

**Same class as** [2026-11-12](2026-11-12-guest-gc-deletes-null-vtable.md): RIP 0 through a null virtual slot on a
form, on the guest, after the mount scenarios, with the temporary cell `FF000A70` on the stack. That one was in the
garbage collector's job; this one is the cell attach.

**A second crash, the same night** (`crash-2026-11-27-03-25-26.log`, the sixth repeat of the same sequence): an
access violation in `Cast<TESNPC>` (the object's vtable pointer read as null) under `Diagnostics::GetBaseLooks` from
the harness's status command: an actor's base form destroyed while the actor was still in our registry. In both,
the guest had just received the host's spawns on arriving (`NotifyCharacterSpawn` for `DC54F` and eleven more).

**Where it shows (measured, 2026-11-27).** Runs of `host-rides-horse guest-rides-host-horse
horse-passes-between-riders two-players-race-for-horse mounted-guest-leaves party-travel-mounted-guest`, counting the
guest's new crash logs:

| Build | Crashes |
| --- | --- |
| dev `3d763578` | 0 of 6 |
| the cleanup branch after #141 `6916f9df` | 0 of 9 |
| after #142 `1f0af7ec` | 0 of 9 |
| after #143 and #144 `e9cdf8bf` | 0 of 9 |
| after #145 `9488aa4c` | 0 of 9 |
| after the ride rules' move `3de404a9` | 0 of 9 |
| after the travel rules' move `c62c6836` (`bf190e82`, `449f3062`) | 2 of 18 |
| that move reverted `a99879a6` | 0 of 9 |

Two in 18 against none in 60 is suggestive, not proof (about a 5% chance if the commit made no difference). Reading
the travel move line by line found no difference from the code it replaced, and the host's sends were identical in
`HostAuthorityTests`. It was reverted on the evidence alone (`a99879a6`).

**What the heap tools found (2026-11-27, `agent/cleanup-guest-crash`).** The six scenarios with the travel move
applied and the heap history and quarantine on (`SKYRIMCOOP_HEAP_HISTORY=2 SKYRIMCOOP_HEAP_QUARANTINE=256` for the
duo and the scenario run): the guest stopped in the first sequence (crash `07-02-09`) reading `0xDBDB...` in
`InventoryChanges::VisitWornItems` (16096) under the engine's `UnequipAll` (38899), called by our
`SkyrimBridge::ActorResetEquipment` ("Equipment reconcile 1348C: reset worn set", Faendal's copy). The history: the
16-byte list node was made by `AddWornItem` 16066 under our equip of that copy 10 s before and freed by
`UnequipWornList` 16067 17 ms before, inside the same walk. The walk holds the next node; removing the first entry
copies the next node into the head and frees it. Mechanism and fix: [inventory-changes.md](../systems/inventory-changes.md),
"UnequipAll's walk follows a node its unequip freed".

**How that explains the crashes, and what is inferred.** Without the quarantine the freed node is reused at once (the
busiest block size), and the walk goes on through whatever took it, unequipping what it finds: frees that are not its
own, and so blocks handed to two objects. Crash 1 has that shape: at the call `[rbx]` was a heap address (`RAX
0xC9B3010`), yet afterwards the crash logger and our breadcrumbs read a `Character` at `RBX`, a block another object
was writing to. Faendal's copy reset its worn set 7 s before crash 1 and 9 s before crash 2. This link is inferred
from the shape and the timing; no run caught a second free (with the quarantine the first stale read stops the game).

**The travel move** (`c62c6836`, host-side rules) does not reach this path: the guest's reconcile resets a copy's worn
set whenever its hands hold less than the set (73 resets in crash 1's guest log), on any build. So the 2 of 18
against 0 of 60 is most likely chance, which the table's 5% allowed; the move is applied again with the fix.

**A second free, found next (the base-destroyed shape).** With that fix in, the guest still crashed on destroyed
bases: `07-44-25` (no tools: the background loader reading copy `DC8DF`'s base 21 ms after our `RelevelActor`),
`08-07-23` (quarantine: a held copy's base read at the session's end) and `08-28-24` (a guard on 0x268-byte blocks:
the collector reading a freed guarded TESNPC 30 ms after its free). In the two with the history, the base was the
temporary NPC our `RelevelActor` built for that copy, destroyed by the engine's `ExtraDataList::LoadGame` (12095)
while loading the reference's stored changes at a cell's init. Cause: our `SetBaseObject` recorded the temporary's id
as the reference's base change (0x80), which `TESObjectREFR::LoadGame` (19529) looks up and gives to the reference
the cell makes again before 12095 destroys it as that one's own. Mechanism and fix:
[leveled-copy-base.md](../systems/leveled-copy-base.md), "A rebuilt base recorded as the reference's base change".
Crash 2 (a copy's base `TESNPC` with a null vtable) has this shape; crash 1 (a `Character` whose vtable read as a heap
address) is not explained by either free beyond being a heap victim.

**A second, separate guest crash** in the same sequence with the fix (crash `07-18-55`, heap tools on): 63997+0xC1
calling a freed clip-load callback, bugs.md "A guest crashed loading a copy's idle clips" (still open).

**Proof (2026-11-27).** Both fixes and the travel move applied, no heap tools: the six scenarios `--repeat 9`, twice
(108 scenario runs, 09:11 to 10:00). No guest or host crash log. The scenarios' own failures in those runs are end
comparisons (`two-players-race-for-horse`, `horse-passes-between-riders`, `party-travel-mounted-guest`), listed in
`docs/bugs.md` and seen on `dev`. A third batch was not run.
