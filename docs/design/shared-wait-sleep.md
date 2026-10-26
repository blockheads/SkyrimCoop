# Shared wait and sleep

v1.0 feature 4: the party rests together. Engine findings: `docs/re/systems/calendar.md` ("The Sleep/Wait
menu").

```text
a player confirms hours in the Sleep/Wait menu
  -> WaitRequest Start -> the host (HostWait)
the host: NotifyWaitState Preparing -> every game opens its menu, paused
every game: Ready once its menu is up
the host: Running -> native rest (sleep: the host and bed's player sleep; other guests wait)
the host's menu steps on its timer -> Progress (remaining seconds, the calendar)
  -> each guest replays one native step for its player only
remaining 0, a cancel, a failure, a leave, an arrival or a load -> Finished
guest: the menu closes -> WaitWorldRequest per attached cell -> the host's rows
```

## Behavior

Any player confirms hours in the native Sleep/Wait menu. The host accepts one request at a time, tells
every game to open its native menu and waits for every menu to be loaded and pausing its world. Only
then does it start the countdown. There is no vote or additional confirmation screen.

The player who used a bed sleeps, and the host represents that sleep to its own quest scripts as the
party's player. Other guests wait for the same hours. Furniture-specific benefits still depend on the
engine's own bed/activation state. Every game starts and completes its own native wait/sleep,
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

## Party rest events: vanilla listeners (2026-10-26)

The party is the Dragonborn (`party-model.md`). A guest's sleep or wait counts as the party's rest.
The host runs the corresponding native rest on its own player, so the host's unmodified scripts see
that player sleep/wait. These events carry times or an interruption flag, not a guest actor; they
use the local player as their story cause instead of inheriting the guest who registered a sleep listener.
The story flag keeps the quest's controls/fades shared with the party, including the abduction.
`ScriptTags::CauseOfCall` enforces this for engine `OnSleepStart`/`OnSleepStop` events in a session;
explicit calls from a running native and other timer/event causes retain their existing rules.
The host's `Game.GetPlayer()` remains its own player. Quest moves, controls and camera actions
follow the existing host-player/party travel rules.

First audited the shipped `Data/Scripts.zip`, extracted under `build/re/scripts/Source/Scripts`.
The active listeners below include the base game, Dawnguard, Hearthfire and Dragonborn. The optional
Survival scripts shipped in the same source archive are listed separately. Registrations and event
handlers were read together; commented-out registrations are not counted as active listeners.

| Quest / system | Registration and event | What its script does to the player |
|---|---|---|
| `DBEntranceQuest` (`00050F2E`), Dark Brotherhood abduction | `QF_DBEntranceQuest_00050F2E.Fragment_11` registers `pDBEntranceQuestScript`; `OnSleepStart` when `pSleepyTime == 1` | Disables controls, forces first person, **moves `Game.GetPlayer()` to the abandoned shack marker**, applies the woozy image-space effect and wake-up idle; after 3 seconds sets `pSleepyTime = 3` for Astrid's greeting. This is a start event, so a test must allow travel to interrupt the rest. |
| `PlayerSleepQuest` (`000FC1A2`) | Its quest fragment registers `PlayerSleepQuestScript`; `OnSleepStop` | Removes/replaces rested abilities: Rested, Well Rested or Lover's Comfort according to the host player's location, marriage and Lover Stone status. Beast blood removes rested bonuses and shows a message. Hearthfire adoption can add Father's/Mother's Love at the current home. The handler does not gate these effects on `abInterrupted`. |
| `FreeformOldHroldanB` (`0006819A`), The Ghost of Old Hroldan | `TiberSeptimBedScript` on the bed alias registers after player activation and sets stage 5; `OnSleepStop` sets stage 10 and unregisters | The stage fragment starts Eydis's scene and enables the ghost. It changes quest/world state, without moving the player or granting a player ability. |
| `DLC1VampireSleep` (`Dawnguard.esm:008E3B`) | Its quest fragment registers `DLC1VampireSleepScript` and adds the bed-activation perk; `OnSleepStop` | Adds/replaces the vampire rested spell when the activation perk recorded a coffin; otherwise removes it. Resets `IsBedCoffin`. This needs the existing bed-activation state as well as the sleep event. |
| Hearthfire coffin references | `BYOHVampireCoffinScript.OnActivate` registers; `OnSleepStop` unregisters (`OnCellDetach` also unregisters) | If the player is a vampire, replaces the coffin-rested spell and shows its message. This is a reference listener, not a separate quest. |
| `DLC2Pillar` (`Dragonborn.esm:01C4E4`), Solstheim sleep-building | Its quest fragment registers `DLC2PillarScript`; `OnSleepStart` | Before `DLC2MQ03B` completes, on Solstheim in a dwelling/exterior, and unless the Brotherhood abduction is armed, rolls a stone-building episode. `DLC2StandingStoneScript.WorkOnPillar` disables controls, fades, forces third person, **moves the player to linked furniture**, starts the Miraak voice scene, restores controls and eventually sets pillar stage 100. |
| Optional Survival exhaustion | `Survival_ExhaustionSleepDetection` player alias registers in `OnInit`; `OnSleepStart` / `OnSleepStop` | Records sleep start, marks the player sleeping, then restores exhaustion by actual elapsed hours, with indoor/quality-sleep rules. Survival exhaustion unregisters the ordinary PlayerSleepQuest while enabled and restores it when disabled; it implements the rested/adoption bonuses itself. |
| Optional Survival hunger/cold | `Survival_NeedHunger` / `Survival_NeedCold` register; inherited `Survival_NeedBase.OnSleepStart` | Marks a sleep period so later need updates use reduced metabolism and skip the relevant sleep-time affliction rolls. Subsequent need stages affect the host player's penalties/abilities. |

`LoveInterestFINScript` also defines `OnSleepStop`: uninterrupted sleep in the marriage home casts
`MarriageSleepAbility` on the player. Its apparent registration in
`QF_RelationshipMarriageFIN_00021382` is commented out; the source scan found no active registration
for this alias, so this is an existing handler rather than a confirmed active vanilla listener.
`SprigganFXSCRIPT` likewise has only a commented-out sleep registration.

**Wait listeners:** no `RegisterForWait`, `OnWaitStart` or `OnWaitStop` definitions/usages occur in the
shipped Papyrus sources. `Form`, `Alias` and `ActiveMagicEffect` expose `RegisterForSleep` and the two
sleep events only. The engine nevertheless broadcasts `TESWaitStartEvent` / `TESWaitStopEvent` for
native consumers; mod listeners should receive the host's ordinary native events. No synthetic
Papyrus wait API is introduced.

Implementation uses the existing native lifecycle, once per accepted rest after every menu is ready:
`HostWait` marks the host as sleeping whenever the accepted party rest is sleep, even when a guest
used the bed. The sleeper also sleeps locally; other guests wait. `WaitBridge::Begin`'s native OK
already calls sleep start 40414 or wait start 40415; native completion/cancellation calls stop 40417.
The host's quest effects are the party-avatar behavior, while furniture-specific effects still
require that game's actual furniture/activation state. Existing hook entry points are unchanged;
the pure script-tagging helper adds the sleep-event cause rule. No quest is patched and no second
start/stop event is injected alongside the native one. A cancelled preparation emits no rest events.

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

Party-event follow-up: host-rule tests cover a guest's sleep through preparation, running, completion
and cancellation, other guests still waiting, and a subsequent guest wait resetting the host's rest
type. ClientTests cover a sleep listener registered by a guest still running on the host player,
including its follow-up timer; ordinary registrations, solo behavior and explicit native calls retain
their existing causes. The existing end-to-end rest flows now expect the host to sleep for a guest's
sleep too.

`guest-sleep-fires-host-script` exercises the unmodified `PlayerSleepQuestScript.OnSleepStop` in
Skyrim. It removes/restores the host's prior rested abilities and the inn bed's owner, checks that a
guest wait grants none, then checks that a guest sleep grants a new Rested/Well Rested/Lover's Comfort
ability on the host. The fixture requires a host without beast blood or the Lover Stone (both block
those bonuses in the vanilla script), uses the ordinary quest registration, and does not inject an
event or call the handler. The full native/UI/DLL gate passes (40 ClientTests, 143 HostAuthorityTests,
10 encoding tests, 99 GameTests); the two new targeted cases cover 132 assertions. Logs:
`build/party-rest-check.log`, `build/party-rest-tags-tests.log`, `build/party-rest-host-tests.log`.
Both delayed/lossy profiles pass all 8 wait flows / 414 assertions (50 ±30 ms / 5% loss and
120 ±80 ms / 10% loss; `build/party-rest-net-check.log`). Python syntax, scenario dry-run and
changed-source scenario selection pass.

Live party-event validation, 2026-10-26: the focused host-script scenario passes, and all six rest
scenarios pass 3/3 shuffled with seed 104 (**18/18**), including the host-script case, health recovery,
calendar and world comparisons (`build/party-rest-host-script-fixed-skyrim.log`,
`build/party-rest-reviewed-skyrim.log`). No event was injected: the host's own PlayerSleepQuest added
Well Rested after the guest slept and added no rested bonus after the control wait.

The first setup tried to remove absent spells; the fixture now removes only existing abilities and
restores only differences. An initial shuffled run stopped on a native AI/quest-alias lock diagnostic
during the inn load, before its rest request (`build/party-rest-shuffled-skyrim.log`). The reporting
stack is the unchanged owned-actor AI pass-through into GetAliasedLocation; its locks match the
already-reviewed GetAliasedRef sibling. The reverse stack was not retained, so no global suppression
or engine/physics change was added. The complete rerun uses that existing report as its normal
baseline and still rejects new diagnostics/world differences. Evidence and native disassembly:
`build/party-rest-native-lock/review.md`.

Calendar/world regressions pass: `host-wait-reaches-guest`, `late-join-gets-calendar`,
`world-changes-reach-guest`, `late-join-gets-cell-state` (`build/party-rest-regressions-skyrim.log`).
The two script-context regressions also pass: `player-moment-stays-theirs` and
`player-stage-cutscene-reaches-party` (`build/party-rest-context-regressions-skyrim.log`).

The physics regression is **not clean**: after the long rest/regression sequence it timed out waiting
30 seconds for host clutter to settle. A fresh control passed, then a second check after guest sleep
reached matching rest positions but failed its flight-trace limit (6.2-unit excess jump, 0 flickers,
0.5-unit final rest difference). Logs: `build/party-rest-regressions-skyrim.log`,
`build/party-rest-physics-control-skyrim.log`. These failures are preserved as validation limitations;
no physics code, thresholds or diagnostic suppression lists were changed for them.

Duo use followed `build/coop-test/DUO.md`: queued behind real-death, claimed after release and zero
processes, then stopped both owned games, verified zero Skyrim processes and released at 2026-10-26 15:13 CDT.
All changes remain uncommitted and unstaged.

Host tests cover the readiness barrier, recipient-specific roles, guest/stale, increasing and duplicate
progress, duration bounds, overlapping requests, cancellation, menu failure, disconnects, arrivals and
timeouts. GameTests use the real transport and services with fake native menus: either game's wait
or sleep, midnight rollover, pending cancellation, a slow menu, engine refusal, world load, disconnect,
four players, partial native hours, batching, restoring solo behavior and refreshing a locally reset
cell row after completion or cancellation. Encoding tests round-trip the id, roles and whole calendar.

`scripts/check.sh` gates the native tests, UI fixtures and Windows DLL. `scripts/net_check.sh '[wait]'`
runs the shared flows under both delay/jitter/loss profiles.

Skyrim scenarios (`scripts/coop_test.py scenario --tag wait`): `shared-wait-host`, `shared-wait-guest`,
`shared-sleep-host`, `shared-sleep-guest`, `guest-sleep-fires-host-script`, `shared-wait-cancel`. The existing
`host-wait-reaches-guest` and `late-join-gets-calendar` remain regression checks. Sleep scenarios use
the Sleeping Giant Inn's bed `5EDA7`, restore its owner and return to QASmoke.
