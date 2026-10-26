# Shared wait and sleep

v1.0 feature 4: the party rests together. Engine findings: `docs/re/systems/calendar.md` ("The Sleep/Wait
menu").

```text
a player confirms hours in the Sleep/Wait menu
  -> WaitRequest Start -> the host (HostWait)
the host: NotifyWaitState Preparing -> every game opens its menu, paused
every game: Ready once its menu is up
the host: Running -> every game starts its native wait (the bed's player sleeps)
the host's menu steps on its timer -> Progress (remaining seconds, the calendar)
  -> each guest replays one native step for its player only
remaining 0, a cancel, a failure, a leave, an arrival or a load -> Finished
guest: the menu closes -> WaitWorldRequest per attached cell -> the host's rows
```

## Behavior

Any player confirms hours in the native Sleep/Wait menu. The host accepts one request at a time, tells
every game to open its native menu and waits for every menu to be loaded and pausing its world. Only
then does it start the countdown. There is no vote or additional confirmation screen.

Only the player who used a bed sleeps. The other players wait for the same hours: standing somewhere
else does not grant a bed's sleep benefits. Every game starts and completes its own native wait/sleep,
preserving its engine events and effects. Only the host's countdown runs on a timer. Every completed
native step tells guests to consume one step and supplies the host's whole calendar, including the
date and days passed. A step can be a fraction of an hour: progress uses the engine's remaining
seconds, rather than counting callbacks. GuestClock holds that calendar between steps instead of
projecting wall time. Only the host fast-forwards the world; guests update their own player timers,
regeneration and effects, then let the native menu advance its display and complete the rest.

Any player can cancel for everyone. A refusal to prepare (combat, no player/menu system), disconnect,
character arrival or world load ends the wait. A menu that never becomes ready, or a host that stops
making progress, releases everyone after 15 seconds without progress. A new request has a new id;
stale readiness, cancellation and progress cannot alter it. Invalid hours (outside 1–24) are refused.
An overlapping request leaves the accepted wait in place. Solo/offline behavior remains native.

## Pieces

- `Services/Host/HostWait`: accepts Start, collects Ready from the current roster, authorizes Running,
  accepts decreasing remaining-seconds Progress only from the host, and broadcasts Finished on completion or exit.
- `Messages/WaitMessages`: request action plus wait id and hours; state notification with phase,
  chosen hours and native remaining seconds, recipient's sleeping/driver flags and host calendar. Appended opcodes.
- `Services/Generic/WaitService`: prepares the bridge, acknowledges readiness, starts native waiting,
  forwards host steps and replays guest steps. Progress received before Finished is consumed before
  closing the menu, including when several reliable notifications arrive in one frame.
- `Bridges/IWaitBridge`, `WaitBridge`: isolated native callbacks and typed SleepWaitMenu integration.
  Readiness requires the slider to exist and the menu's pause flag to be set. The host's update runs
  normally; a guest's timer update is suppressed, and each host notification calls the native menu
  update once with the authoritative remaining seconds. Its wait-step hook updates only the guest's
  player, leaving NPC/world/physics fast-forwarding to the host.
- `Services/Guest/GuestClock`: takes shared calendar snapshots immediately, without the initial join
  fade, and suspends projection until Finished. Its existing calendar hook remains unchanged.
- `Services/Guest/GuestWait`: after the shared menu closes, requests the host's current loaded-cell
  state with `WaitWorldRequest`, tagged with that completed wait's id. `HostWait` accepts only the most
  recently completed rest's requests and returns bounded `NotifyWaitWorld` chunks to that guest.
  Only these snapshots defer their record flags until all chunks arrive and their placements finish.
  Unsolicited/stale replies are ignored; a new rest, load, unload or newer ordinary object update drops
  obsolete queued flags. Disconnect destroys the guest service. Ordinary cell loading, object updates
  and physics retain their existing application behavior.

## Validation

Host tests cover the readiness barrier, recipient-specific roles, guest/stale, increasing and duplicate
progress, duration bounds, overlapping requests, cancellation, menu failure, disconnects, arrivals and
timeouts. GameTests use the real transport and services with fake native menus: either game's wait
or sleep, midnight rollover, pending cancellation, a slow menu, engine refusal, world load, disconnect,
four players, partial native hours, batching, restoring solo behavior and refreshing a locally reset
cell row after completion or cancellation. Encoding tests round-trip the id, roles and whole calendar.

`scripts/check.sh` gates the native tests, UI fixtures and Windows DLL. `scripts/net_check.sh '[wait]'`
runs the shared flows under both delay/jitter/loss profiles.

Skyrim scenarios (`scripts/coop_test.py scenario --tag wait`, all pass 2026-10-26): `shared-wait-host`,
`shared-wait-guest`, `shared-sleep-host`, `shared-sleep-guest`, `shared-wait-cancel`. The existing
`host-wait-reaches-guest` and `late-join-gets-calendar` remain regression checks. Sleep scenarios use
the Sleeping Giant Inn's bed `5EDA7`, restore its owner and return to QASmoke.
