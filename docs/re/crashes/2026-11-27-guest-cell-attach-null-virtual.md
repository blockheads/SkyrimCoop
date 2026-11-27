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

**Not known:** what destroyed the forms; whether the travel move changes anything real (timing, or a path the tests
do not reach) or the guest has a latent lifetime bug that any build can show, as the 2026-11-12 crash suggests.

**Next:** `scripts/coop_test.py repro` with the heap history on (crash debugging: tooling first), running the six
scenarios above in order, on `bf190e82` where it showed.
