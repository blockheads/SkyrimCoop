# What crash breadcrumbs cost: VirtualQuery on every hook call

Found 2026-11-01 on the Windows duo, after the Fabien playtest lagged (fights halved the host's frame rate).

## What breadcrumbs do

`Code/client/Breadcrumbs.cpp` records every call of every hook of ours (`Hooks::SetHookObserver` wraps each hook in a
stub that calls `Breadcrumbs::RecordHook` with the hook's name and first argument), so a crash log shows what our
code did just before. A call on an object not among the hook's last 24 got a description of the object: its class
from MSVC RTTI and, for a form, its id (`Describe` → `Rtti::ClassName` and `Rtti::IsForm`).

Reading an arbitrary first argument must never fault (our crash handler takes any access violation for a crash), so
`Rtti::Locator` checks the address with `VirtualQuery` first. Each description made two (`ClassName`, then `IsForm`).

## Why that was slow

- Hooks run far more than expected, mostly on the engine's job threads: in the 3-actor QASmoke cell, 32-38 thousand
  calls a second per game; in a 15-wolf fight, 46-54 thousand. The busiest: `HookIsTrue` (condition evaluation),
  `HookCompareFilterInfo` (Havok's collision filter, on the physics jobs), `HookGetCurrentScene`, `HookIsInCombat`,
  `HookResolve`.
- Most calls are on a new object (conditions, collision pairs), so most paid two `VirtualQuery` calls.
- `VirtualQuery` is a system call (`NtQueryVirtualMemory`) that walks the process's address-space tree; Skyrim maps
  so much that each one took tens of microseconds.
- Every call also took one global spin lock.

Sampled stacks (`coop_test.py profile --stacks`) put 40% of each job thread in
`Breadcrumbs::RecordHook` → `Rtti::ClassName` → `VirtualQuery`.

## Measured (Windows duo, QASmoke, 2026-11-01)

| | idle CPU | idle fps | 15 wolves: host fps | 15 wolves: guest fps |
|---|---|---|---|---|
| before | 283-309% of a core | 54 | 28.7 | 45.2 |
| hook breadcrumbs off | 123-127% | 59 | 59.8 | 58.1 |
| fix | 123-126% | 59 | 59.8 | 58.6 |

(The games are vsync-capped near 60.)

## The fix

- A hook describes a new object at most every 50 ms (`cDescribeEveryMs`); the rest keep the object's address and
  are described when the crash handler dumps them, marked `(now)`.
- One `VirtualQuery` per description (`Rtti::ClassName(..., bool& aIsForm)`).
- Calls are still all counted per hook (`Breadcrumbs::Totals`, which `coop_test.py profile` reports as rates).

A hook that runs per object on the job threads should stay cheap; `coop_test.py profile` shows any that is not
(docs/design/testing.md, Profiling).
