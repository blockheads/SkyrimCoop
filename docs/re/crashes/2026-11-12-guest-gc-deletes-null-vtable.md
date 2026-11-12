# 2026-11-12: guest crash in the garbage collector's job, a null deleting destructor (not investigated)

**Symptom:** the guest crashed as `guest-rides-across-border` began, 3 s after `mounted-guest-leaves` ended, in a run of
29 scenarios' second round. `EXCEPTION_ACCESS_VIOLATION`, RIP 0, called from
`coop_GarbageCollector_DeleteNextForm` (36479 + 0x73) under `GarbageCollector::sub_36468` and `Job_Garbage` (39077), on a
job thread. Crash log `prefix-client/.../SKSE/crash-2026-11-12-12-22-16.log`.

**What the code does (read in IDA):** 36479 takes the last entry of the collector's array (`+0x20`, count `+0x30`), has
`TESDataHandler` forget it, and calls its deleting destructor (vtable slot 0, argument 1). RIP 0 means that slot was
null: an entry already destroyed, or one that was never a form. A temporary exterior cell `FF000A70` ("Wilderness",
flags `0x200A`) was on the stack at `RSP+130`; whether it is the entry is not known (its RTTI still read as a cell).

**What ran before:** in the same session `host-rides-horse` had failed with the guest's own player's skeleton 2100
units above its feet (bugs.md, "A rider after a spectated kill move": dev's plugin has that too), then
`guest-rides-host-horse` and `mounted-guest-leaves` passed. Our last calls before the crash: `HookLoad3D` 399 ms,
`HookUpdateSynchronizedClip` 6.8 s, `HookDriveToPose` 6.5 s.

**Not known:** which object the entry was; whether the graph input stream's changes of that day (mount pairs and deaths
on the stream) are involved. The same scenario passed 4 of 4 and 1 of 1 before it on those builds, and 4 of 4 again after. First time this stack is seen in this checkout's crash logs.

**Next:** `scripts/coop_test.py repro` with the heap history on (crash-debugging: tooling first), running
`spectator-watches-kill-move host-rides-horse guest-rides-host-horse mounted-guest-leaves guest-rides-across-border`.
