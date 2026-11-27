# 2026-11-27: guest crash attaching a cell, a null virtual on a reference (not investigated)

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

**Not known:** what 0xDC54F is and what destroyed or replaced it; whether the moves of that night (the ride rules to
`mounts/HostRides`, the travel rules to `travel/HostArrivals`; the host's sends are byte-identical in
`HostAuthorityTests`) are involved. The crash is on the guest, whose code those moves did not touch.

**Next:** `scripts/coop_test.py repro` with the heap history on (crash debugging: tooling first), running the six
scenarios above in order.
