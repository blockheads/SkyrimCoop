# Party travel and mounts

Features 10 and 13, prepared for review on dev (uncommitted), 2026-10-27.
Developed from dev `35924948`; original validation artifacts remain in `.worktrees/party-travel/build/`.
NPC followers have an approved [design](followers.md); their implementation is deferred until after this review.

**Ready for v1.0.0 testing**, accepted by the user on 2026-10-27. The remaining jail cleanup, intermittent
engine lock reports and death/physics failures under packet loss are deferred [known issues](../bugs.md).
The loading-screen onset skew described below is a retained limitation. No further travel fixes are required
before this testing milestone; the validation failures remain documented.

## Travel begins at departure

```text
native queuedTarget is valid (PlayerCharacter +648, PLAYER_TARGET_LOC)
    -> ProcessQueuedTravel (40438) observes it before consuming it
    -> defer this game's load until its next rules update
    -> PlayerTravelQueuedEvent -> PartyTravelRequest -> host
    -> NotifyPartyTravel to the other games, flushed immediately
    -> each follower queues its native MoveTo
    -> leader and followers run their native loads
    -> PositionPlayerEvent finish / EnterCellRequest confirms the actual arrival
```

No network or service work runs inside the engine's cell locks. The main-thread hook copies the destination;
the rules send it through RunnerService before the load is released. A superseding queued destination is
announced in order. Following targets do not announce another trip. Offline travel runs normally.
Arrival reconciliation and the existing rapid-travel handling remain in place.

Important RE correction: **coc does not call 40442** on AE 1.6.1170. `CenterOnCell_Impl` (40437) writes
`queuedTarget` inline and calls 40438. Doors, map/console/scripted fast travel and MoveTo use 40442.
Observing the common consumption point covers both. `PlayerTargetLocation` and its offset have static
assertions and were checked against CommonLibSSE-NG and the disassembly.

Measured native event timestamps, same machine, warm interior loads:

| Behavior | Leader load | Follower load starts after leader starts |
|---|---:|---:|
| Original arrival-based tether, host leads | 521 ms | 677 ms |
| Original arrival-based tether, guest leads | 180 ms | 389 ms |
| Departure announced before native loading, host leads | 8 ms | 21 ms |
| Departure announced before native loading, guest leads | 8 ms | 40 ms |

The last pair measures cached loads, not a cold-load speedup. The regression scenario asserts that the
announcement precedes the leader's native load and that the follower starts loading and arrives. It reports
both phase timelines. Network transit and frame scheduling still separate actual loading-screen onsets;
an 8 ms cached load can finish before another game's loading screen begins. There is no synchronized-time
barrier or promise that loading screens open in the same frame.

## Fast travel advances the party clock

A guest's native Calendar::Update is suppressed by GuestClock. Therefore measuring its clock before/after
the trip cannot recover the trip's duration. Hook 40445 scopes native fast travel; its Calendar::Update
step (36291) supplies the exact game hours from seconds and time scale. A guest sends `TravelTimeRequest`;
HostClock queues the host's native calendar advance through HostEngine/TimeData::Advance on MainLoopTasks,
outside the VM update job while the host may be loading. Existing calendar
replication then reaches every game, including the traveler. The host's own native trip already advances
its calendar and is never applied twice. Non-finite, zero and negative requests are ignored.

This does not forward ordinary frame simulation, wait/sleep or coc's clock step. Native quest MoveTo and jail
continue to use the ordinary tether rather than a separate transport implementation.

Community reference inspected before hooking: [PapyrusExtenderSSE fast-travel/calendar hooks](https://github.com/powerof3/PapyrusExtenderSSE/blob/master/src/Game/HookedEventHandler.cpp).
Our 40445 signature and the existing 36291 hook were checked against the AE disassembly.

## A ridden horse is part of the traveling party

A player owns its horse while riding; every observer keeps a persistent copy during travel. CharacterService
retains the player's mounted horse through discovery removals and arrival cleanup. Its pre-ride persistence
state is restored at handoff/disconnect, so a temporary world horse does not remain permanently promoted.
The bridge uses native SetPersistent (19597), which changes both the flag and the world's persistent-cell
membership. Changing the flag alone leaves stale cell lists, causing save or later cell-load crashes; native
mount cleanup therefore includes a save and subsequent travel. FollowTo carries the owned horse to the
destination as well as queuing the player move.

Native MoveTo breaks the mount interaction. It does not always call Actor::Dismount. On arrival, the service
checks the player's actual interaction and returns ownership if native travel dismounted it. The horse arrives
with the party, unmounted, and belongs to the host again; it is not left behind or kept as a guest-owned orphan.

```text
rider's horse moves -> rider follows -> native interaction ends
    -> DismountRequest(horse, rider, horse's current whereabouts)
    -> host validates ownership and gives it back
    -> NotifyDismount with that placement
    -> move each horse copy there BEFORE the host takes ownership
```

The placement matters: the handoff can reach the host before the last movement snapshot. Taking ownership of
a copy still in the old cell would make discovery remove the horse everywhere. A host dismount also notifies
observers while retaining its own horse. A rider disconnect keeps the existing ownership-return path.
Dismount's wire payload now includes ActorWhereabouts; request and notification share that payload. New
travel requests are appended to the opcode list. Mixed old/new builds are not wire-compatible.

A late join while another player is already mounted still needs mount state in its spawn; it remains the
existing mount loose end in progress.md. This change covers already-present observers, not that separate case.

## Verification

Fast flows cover host/guest riding and ownership return, a horse accompanying a following rider, three-player
tethering before arrival, exact-once guest fast-travel time across midnight, a dead follower, and a following
player in dialogue. The fake models native MoveTo breaking mount interactions and ending conversations.
Encoding checks cover the new requests and handoff placement; host rules cover rejected clock inputs.
A regression also checks the player-copy death fallback after its exact death action was attempted; living
player copies still use the existing replacement path.

Native scenarios added: `host-rides-horse`, `guest-rides-across-border`, `mounted-guest-leaves`,
`party-loads-overlap`, `guest-quest-teleport`, `party-travel-with-menu`, `party-travel-mounted-guest`,
`party-travel-dead-guest`. The border ride uses collision rays to select its route, stages the heading, and holds forward
input to cross it. Existing `party-fast-travels` now also asserts that a guest-led trip advances the host's clock.
Native dialogue and jail regressions use their existing scenarios. Three-player checks use GameTests; the
shared Skyrim harness runs two games.

Integration on dev: `scripts/check.sh` passes after cleanup and transfer (106 s;
`build/party-travel-review-check.log` in the main checkout). No commit was created.

Final fast validation: `scripts/check.sh` passes (`build/final-check.log`): ClientTests 655 assertions / 60 cases,
HostAuthorityTests 1809 / 168, TPTests 240 / 17, GameTests 1743 / 156 across eight shards, UI fixtures and native DLL.
`coop_test.py affected --check` covers all 1378 source files; Python compilation and `git diff --check` pass.

Both scoped network profiles pass for `[travel]~[death],[mounts]` (74 assertions / 11 cases each;
`build/payload-network.log`). The wider affected run including death/physics does **not** pass: delayed
copy-death state, standing-member combat and checkpoint/wipe deadlines remain open
(`build/verified-network.log`, existing entries in `docs/bugs.md`). The new direct player-copy death fallback
regression failed before its guard fix (`build/player-death-red-confirm.log`); it does not resolve all those
loss-conditioned flows.

With the final native persistence setter, host riding, guest riding, mounted following, disconnect return and
guest fast-travel time pass (`build/persistent-cell-native.log`). That sequence also exposed the existing
physics playback/unload lock inversion on a later trip, symbolized in `build/persistent-cell-lock.log`.
The earlier border steering timeout was a scenario staging failure; the final scenario stages the clear heading.
Final native edge run (`build/regression-final-native.log`): border riding, guest quest MoveTo, following
from inventory, journals with simulation, and dead-follower travel pass. Both leader timings pass their early
announcement assertions (10/13 ms cached loads, 29/34 ms follower start offsets), but the load scenario fails
its final audit on a separate native AI/quest lock inversion (`build/final-native-lock.log`). The guest jail
choice puts both players inside the correct locked cell with a positive sentence; release completes, then
fixture cleanup fails because its old guard reference is missing. These are retained failures in `docs/bugs.md`,
not clean native passes. On a fresh duo, `dialogue-ends-on-travel`, `party-loads-overlap`,
`party-quick-travel`, `party-uses-door` and `teleport-keeps-copies` all pass (5/5;
`build/last-regressions-native.log`). The successful early-load rerun does not clear the intermittent lock report.
The duo was stopped and its reservation freed after verifying zero Skyrim processes.

Retained limits: mount state on late join, simultaneous conflicting leaders, actual loading-screen onset skew,
two-game native coverage only, and the death/physics issues above. NPC following remains design-only.
