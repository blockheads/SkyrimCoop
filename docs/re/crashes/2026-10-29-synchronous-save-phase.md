# 2026-10-29: a console save races the native VM update

## Observed failure

The six-mod, cap-60 profile hangs before joining, during the guest's startup
`save ProfileActivation`. The command enters at 17:41:53.409; the watchdog reports the main
loop stalled for ten seconds after seven ticks. No acceptance scenario has started.
The bootstrap writes activation saves to record both games' actual master lists.

Evidence in `.worktrees/v1-mod-profiles/build/`:

- `profiles/20261029-174123-playtest/`: exact files/settings, hashes, both prefix logs and final state.
- `guest-startup-hang-{raw,symbolized}.txt` inside that profile: the complete watchdog dump.
- `acceptance-boot-20261029-174123.log`: the failed bootstrap and command timeout.
- `acceptance-binaries/0546138b7756df3c/`: the matching original DLL/PDB.
- `re/startup-hang/decompile.json`: targeted IDA output from an isolated copy of the existing
  1.6.1170 database. No shared database was changed.

The failed processes were preserved through the watchdog dump and file capture, then stopped
by the runner. The following fresh launch succeeds with the same configuration. This is an
intermittent failure, not a dependency attribution.

## Native lock cycle

Both captured stacks and IDA establish the cycle:

| Thread | Holds | Waits for |
| --- | --- | --- |
| Main, saving | `SkyrimVM::frozenLock` | Internal VM `frozenStacksLock` |
| VM worker | Internal VM `frozenStacksLock` | `SkyrimVM::frozenLock` |

`FreezeAndWait` (54015) calls 53958, which holds the Skyrim VM frozen lock while calling
the implementation's `IsCompletelyFrozen` (104770). That function takes its freeze-state
lock. Meanwhile, the worker is inside 104767 holding the latter lock, and calls the freeze
query (53922), which takes the Skyrim VM lock. Both paths reach `BSSpinLock::Lock` (13663).
The offsets agree with the typed community layouts: Skyrim VM `frozenLock` at `0x678`,
internal VM `frozenStacksLock` at `0x9384`. The fix does not read or write these fields.

The worker's co-op frame is `HookVMUpdate`'s call to the original VM function, not a nested
`Plugin::Update`. The earlier frozen-VM guard prevents co-op update re-entry; it does not make
concurrent native save serialization safe.

The console queue runs in `Main::Update` (36564) at `+0xA8F`, after the actor/VM job stages
are started at `+0x82F`. A typed console command normally runs with the console open and the
game paused. The harness runs its queued command with gameplay advancing. The same unsafe
timing is relevant to saves from unpaused menus.

## Community check and change

[Skyrim Souls' SaveHandler](https://github.com/Vermunds/SkyrimSoulsRE/blob/2cbd7d5a0e2a6a04381b4076ac73e8d51332fe32/src/SaveHandler.cpp)
queues synchronous console and menu saves into the save/load manager's event processing.
That reference is already fetched by `scripts/re/fetch_references.sh`.

The local change routes device-2 saves through the existing main-loop task boundary, also
used by `JoinBridge::SaveNow`. A request made outside that drain queues its captured name and
arguments for a later frame. A request already inside it completes synchronously and returns
the real native result, so the join bridge can still read the just-written ESS/co-save.
The phase marker is thread-local: a VM worker cannot borrow the main thread's active drain.
Guest authorization is checked before dispatch and again before execution.

The touched 35727 hook signature is checked against CommonLibSSE-NG's
`BGSSaveLoadManager::Save_Impl` and IDA's 1.6.1170 disassembly: manager in RCX, signed device
in EDX, output flags in R8D, filename in R9. No wire, bridge or harness interface changes.

## Verification

The fast regression uses the extracted direct-dispatch behavior as its red adapter:
**2 of 4 cases fail, with 5 failed assertions**. The corrected dispatcher passes **4/4,
15 assertions**. It checks that an out-of-phase request waits, native failure is returned
synchronously to an in-phase caller, workers queue even while main is draining, and a
nested drain preserves the outer phase. Evidence: `build/save-dispatch-{red,green}.log`.

`scripts/check.sh` passes **567 cases / 6,102 assertions**, harness/ragdoll comparisons, UI
fixtures, the Windows DLL and launcher. The replacement DLL SHA-256 is
`28ed9f11e7a62287b2aa37620d2744a4c846986878e37838afea28c432bc0bff`; its PDB and changed
source files are archived in `build/acceptance-binaries/28ed9f11e7a62287/`.

A fresh six-mod boot completes both early activation saves. Seven native checks then pass:
the frozen-world save regression, host save with the guest's menu open, named load, quickload,
Load-menu selection, saved guest-character retention and full main-menu rejoin. Evidence:
`build/save-dispatch-native-{boot,scenarios}.log` and profile
`build/profiles/20261029-180612-playtest/`. This also checks the join bridge's synchronous
file-read contract with the real engine. No native wait/assertion was relaxed.

Four further fresh profiles (Scrambled Bugs, USSEP, historical all-six and final playtest)
complete their early saves without this hang. Each extended gate fails a separate fade,
animation/body or follower-package check; those failures are retained in the
[profile report](../../reports/2026-10-29-v1-mod-profiles.md). Full acceptance completes
**131 pass / 24 fail / 0 not run** across 155 required cases. The preceding
40-pass/5-fail acceptance results use the original DLL and stay retained separately; final
acceptance uses the replacement DLL throughout. Four ordinary save/join/follower regressions
also pass after a separately tested late-baseline harness correction. Final `scripts/check.sh`
again passes 567 C++ cases / 6,102 assertions, the nine late-baseline tests, existing comparisons,
UI fixtures and DLL/launcher builds; the DLL hash remains unchanged. Evidence:
`build/final-check.log` and the complete profile report.

A later watchdog catches the harness’s status-file `fclose` and resumes after about 12 seconds;
its stack is unrelated to this native VM cycle. That test-environment observation and temporary
control-file storage workaround remain separately documented in the profile report. This change
does not claim to explain every historical corruption or reload failure, and the remaining
acceptance failures prevent a clean v1 profile recommendation.
