# 2026-10-29: the save drain re-enters the full co-op update

Follow-up: [lock-history attribution and the join's button pose](2026-10-29-lock-history-and-button-pose.md).
The earlier lock report below combined writer modes with a reader's saved stack; it does not
establish that the save path took those write locks. The checker now keeps their evidence separate.

Skyrim's save path freezes its VM and drains registered updates. Our hook ran the full co-op
update during that drain, allowing two updates of the shared world to be active at once.
A native regression fails on the original code after a single save; the fix restores the VM's
frozen-state check before calling `Plugin::Update`.

## Evidence

The compatibility pass on `ca906283` captured host and guest crashes during saving/loading,
including corrupt plugin containers, a bad pointer in `Net::Server::GetTick`, invalid ENet
outgoing-command links, and invalid NPC-base/behavior-graph pointers during native load cleanup.
Those symptoms alone did not identify the write or establish that one dependency caused them.

The guarded diagnostic run reproduced a host crash at 01:48:28, DLL RVA `0xB3704E`, while ENet
was walking outgoing commands. Its probable stack includes `World::Update` / `Plugin::Update` /
`HookVMUpdate`, then engine `54015` / `35727` / our `HookSaveImpl`: this update came from the
save's VM drain, not the ordinary VM update job.

A separate cold-start run, without memory guards or an attached debugger, runs
`save-does-not-update-frozen-world` and fails after one save:

- One full plugin update ran while `SkyrimVM::isFrozen` was true.
- Two plugin updates were active together (`UpdateTrace`, host thread 384).
- The guest's corresponding counters stayed zero.
- The harness and plugin update did not overlap in that run.
- The save/loading flags were still clear during the bad update: the frozen VM flag, not the
  later global saving flag, identifies this drain. The in-process fake engine does not model this
  native pre-save drain, so the regression lives at the Skyrim boundary.

Evidence: `build/reload-freeze-red-scenarios.log`, `reload-freeze-red-host-trace.json` and
`reload-freeze-red-host-plugin.log`. The preceding guard run is in
`reload-guard-scenarios.log` and `reload-guard-host-plugin.log`. Eight earlier reloads with a
hardware watchpoint passed; that negative capture did not identify or rule out the race.

## IDA and community checks

IDA 9.3, the unpacked **1.6.1170** executable:

| Address Library ID | RVA | Observed behavior |
| --- | --- | --- |
| 53926 | `0x9BFB00` | `SkyrimVM::ProcessRegisteredUpdates`; reads the frozen byte at `this+0x680`. |
| 54015 | `0x9C9DB0` | Sets the VM frozen through 53956, then calls 53926 while waiting for it to drain. |
| 35727 | `0x60FD40` | The save path in the captured stack, calling the freeze/drain operation. |
| 19567 | `0x2DD960` | `TESObjectREFR::IsWater`; dereferences the reference's base at `this+0x40`. |
| 36459 | `0x63B120` | `GarbageCollector::Add`; reaches that base read while cleaning references. |

The `isFrozen` offset agrees with [CommonLibSSE's SkyrimVM layout](https://github.com/powerof3/CommonLibSSE/blob/master/include/RE/S/SkyrimVM.h).
[Upstream's VM hook](https://github.com/tiltedphoques/TiltedEvolution/blob/dev/Code/client/SkyrimVM64.cpp)
also skips its update while the VM is inactive. Our old startup notes had removed an earlier,
incorrect `0x690` guard after resolving a separate compiler ABI defect. This check uses the
verified `0x680` field, represented in the typed engine structure with an offset assertion.

The retained IDA output is `build/re/reload/decompile_reload.json`; the copied database stays
ignored. No engine executable or database is added to the repository.

## Change and validation

`HookVMUpdate` still forwards every native call. It records that the VM ticked, then runs our
services only when the native VM is not frozen. The main-menu update path remains available.
The regression measures actual plugin calls around a real host save; it also rejects overlapping
full updates. Its boundary depends on Skyrim and is tested natively.

Validation after the fix is recorded below. This establishes a concrete race and
its regression; it does not by itself prove that every historical memory-corruption or werewolf
crash had the same cause.

The first post-fix run passes **6/6**: the new save regression, host load, quickload, host Beast
Form/reversion, party-wipe checkpoint recovery and guest-character retention. Both games finish
with zero frozen updates and zero overlapping plugin updates. No debugger, page guards or heap
history is enabled. Evidence: `reload-freeze-green-scenarios.log` and
`reload-freeze-green-{host,client}-trace.json`.

The full gate passes **542 cases / 5,772 assertions**, the harness/ragdoll comparisons, UI fixtures
and the Windows DLL build. It passes again after removing the temporary allocator experiment
(`build/reload-final-check.log`); the final source has no allocator replacement.

The combined profile with Engine Fixes, powerofthree's Tweaks, Scrambled Bugs, Bug Fixes SSE,
Display Tweaks and USSEP completes **23/27**, with no crashes. The four failures concern furniture
animation, the known guest-only vampire marker and two death/revival body poses; see
[the compatibility report](../../mod-compatibility.md#post-fix-combined-profile-2026-10-29).

Three further cycles with Engine Fixes complete host and guest Beast Form/reversion without a
crash. The first two host loads pass; the third restores play but fails the strict lock audit.
The fourth planned cycle is skipped by `--fail-fast`, not counted as passing. Both games' final
frozen/saving/loading and update-overlap counters remain zero. Evidence:
`reload-freeze-cycles-scenarios.log` and `reload-freeze-cycles-{host,client}-trace.json`.

The reported lock pair is `1823d0 > 15ff00` versus `16f48b > 182f95` (Skyrim executable RVAs).
Both stacks were symbolized: a native AI/package worker reads extra data through 12721/11737;
the opposite path is native save serialization through 35727/35599/19528/12094/12729.
Our package/AI and save hooks occur on the stacks. This resembles the existing extra-data
load/save lock family in `scripts/lock_order_known.txt`, but this exact variant has not received
an unchanged-build comparison. No deadlock was observed. The warning remains a failed check;
no lock suppression or speculative locking change was added. Full stacks are retained in
`reload-freeze-lock-order-symbolized.log` and `reload-freeze-cycles-host-plugin.log`.

A final cold start without the additional fix mods passes **5/5**: the new save regression,
host and guest Beast Form/reversion, host load and quickload. A separate main-menu join restores
the guest character but fails the final animated-object world comparison (the existing QASmoke
join difference). It is not a full scenario pass. No crash or overlapping/frozen world update is
observed. Evidence: `reload-freeze-unmodded-scenarios.log`, `reload-freeze-unmodded-menu.log`
and `reload-freeze-unmodded-{host,client}-trace.json`.

These tests use two local games on Proton with the direct session transport, not separate-account
Steam or Windows acceptance. Changes remain in `.worktrees/v1-engine-fixes`; they are not merged
into `dev`. The test games are stopped and the shared duo reservation released.

## Retained harness tools

Only test-harness builds compile these tools. They add no methods to the production services,
bridges or engine structures. The native VM's actual frozen field is part of its typed layout.

- `memory_sites` returns the current world, transport, runner, host network and optional actor/base
  addresses (`{"cmd":"memory_sites","actor":<form id>}`). It also returns update counters:
  calls during frozen/saving/loading states, overlapping updates, and overlaps with harness polling.
  The counters come from observing the real plugin update and harness poll, not a simulated state.

The phase counters run in test builds only. The address query does not add accessors to production
services; an attached debugger resolves private-field offsets from the matching PDB. Resolve actor
addresses again after replacement or a cold start.

Temporary page-guard experiments covered selected plugin allocations and Skyrim NPC allocations.
Their self-check verified allocation/decommit in both games, but they did not identify the original
write. Their source is retained only in ignored `build/reload-plugin-heap-experiment/`, and the
allocator hooks and probe command were removed from the final patch. The existing Skyrim
`HeapHistory` tooling is unchanged.
