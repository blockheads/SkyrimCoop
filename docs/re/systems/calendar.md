# The calendar (game time, days passed)

IDA 1.6.1170, CommonLibSSE-NG `RE::Calendar`; checked 2026-10-20. Our struct: `TimeData`
(`Code/client/Games/Skyrim/TimeManager.h`, singleton 400447).

## Layout

```
Calendar (0x40)
  +0x08..0x30  TESGlobal* GameYear 0x35, GameMonth 0x36 (0-based), GameDay 0x37 (1-based),
               GameHour 0x38, GameDaysPassed 0x39, TimeScale 0x3A
  +0x38        uint32 midnightsPassed   since the game started; not saved (read by sandbox procedures)
  +0x3C        float  rawDaysPassed     whole days; the clock counts on from it
```

## Who writes what

```
Calendar::Update(seconds)                                         36291
  hour = GameHour + seconds * TimeScale / 3600
  while hour > 24:  hour -= 24, day += 1          (date rolls month, year)
  once a midnight passed:
      GameDay/Month/Year written, midnightsPassed++, rawDaysPassed += 1,
      DaysPassed event (36329; one sink: DaysPassedToMiscStatHandler, the misc stat)
  GameDaysPassed = rawDaysPassed + hour / 24
  GameHour = hour

callers   the frame              36564 Main::Update, 36566 (seconds of the frame)
          wait / sleep            40485 PlayerCharacter::WaitStep (900 game seconds per step on the tested build)
          fast travel             40445 (the trip's travel time; measures hours by GameDaysPassed)
          coc                     13328 (+120 s)

save load  35654 LoadGlobalVariables: every global's value, then
           36317 Calendar::SyncRawDaysPassed: rawDaysPassed = floor(GameDaysPassed)
data load  36289 Calendar::Calendar: GameDaysPassed += GameHour / 24, midnights = raw = 0
```

So `GameDaysPassed` is derived: whatever writes it alone (the console's `set GameDaysPassed`, a script)
is overwritten on the next frame by `rawDaysPassed + GameHour / 24`. Wait, sleep and fast travel are all
`Calendar::Update` with a bigger step; none writes the globals another way.

## How co-op uses it

- The host reads the calendar with `rawDaysPassed` as the whole days (`TimeModel::DaysPassed`):
  `floor(GameDaysPassed)` rounds up to the next day in the last seconds before midnight once days passed
  is in the hundreds (float spacing 3e-5 days at 300).
- A guest's `Calendar::Update` is off (`HookSimulateTime`). It writes the host's calendar every frame as
  `Update` leaves it (`GuestEngine::SetCalendar`, from `GuestClock`): the globals, `rawDaysPassed` = the host's whole days,
  `GameDaysPassed = rawDaysPassed + hour / 24`. When the guest leaves, its own clock counts on from the
  host's days, not from its save's.
- Set time (`/settime`, `HostEngine::SetHour`) runs the host's clock on with `Calendar::Update`
  to that hour, the next day's if it is earlier, as a wait does.
- Not done on a guest: `midnightsPassed` and the DaysPassed event (its Days Passed misc stat stays its
  own). Calling `Update` with the gap would count every day of a join or a long wait into the stat.
- The old guest code added each frame's delta to `GameDaysPassed`: host jumps never arrived, and from a
  few hundred days on a frame's delta (4e-6 days) is under the float's spacing, so it stopped adding.

## The Sleep/Wait menu (shared rest)

How the party rests together: `docs/design/shared-wait-sleep.md`. The hooks are in `Bridges/WaitBridge.cpp`,
the layouts in `Interface/Menus/SleepWaitMenu.h` and `PlayerCharacter::sleepSeconds`. Checked against the AE
1.6.1170 disassembly, CommonLibSSE-NG's `SleepWaitMenu` and `FxDelegateArgs`, and SkyrimSoulsRE's OK callback.

```text
OK callback (FxDelegateArgs: handler +0x18, args +0x28)           52488
  hours = args[0] (GFxValue number +0x10)
  sleeping ? StartSleep 40414 (bed spell, TESSleepStartEvent)
           : StartWait 40415 (TESWaitStartEvent)
  timer = 0, active = true
Cancel callback                                                    52489
  queues the menu's Hide
ProcessMessage                                                     52486
  update (0): active and a second passed -> WaitStep 40485, slider, label;
              done -> Hide
  show (1):   sleeping = BSUIMessageData +0x20
  hide (3):   an active wait stops (StopSleepWait 40417)
WaitStep                                                           40485
  Calendar::Update by one engine step, NPCs, physics, projectiles,
  effects, Sky; sleepSeconds -= step, or StopSleepWait 40417 at 0
```

- `SleepWaitMenu`: timer +0x30, root GFxValue +0x38, sleeping +0x50, active +0x51, size 0x58.
- **A step is 900 game seconds, not an hour.** The menu takes four steps per slider hour, and only the step
  that crosses an hour resets its one-second timer. So progress is the remaining seconds
  (`PlayerCharacter::sleepSeconds`, +0x8EC, CommonLibSSE-NG's INFO_RUNTIME_DATA), never a count of steps.
- **A guest's rest is its player's only.** WaitStep would fast-forward the guest's NPCs, physics and
  projectiles (`0x140649580`), which are the host's. The guest's step instead moves the effects clock
  (ProcessLists 400315; GetSystemTimeClock 41337, SetSystemTimeClock 41338) by the elapsed game seconds over
  the time scale, then runs the player's update 38785 `void(PlayerCharacter*, float)`, then StopSleepWait 40417
  at zero. A guest's `HookSimulateTime` still stops Calendar::Update, so GuestClock applies the host's calendar.
- **Starting another game's menu:** the vanilla ActionScript (`reference/plugins/skyrimui/src/sleepwaitmenu/
  SleepWaitMenu.as`) disables the controls before calling OK. The bridge sets the hours, refreshes the label
  and question, sets `disableControls` (Cancel stays enabled), and calls the native OK with its own
  FxDelegateArgs.
- **The world after a rest:** the fast-forward can reset a guest's reference rows without unloading their
  cell (seen in QASmoke: `32AFE`, `32AFF`, `32B00`, `101976`). The guest asks for the host's state of each
  attached cell, and applies the record flags only after the placements settle; otherwise a late node or
  body update marks a corrected reference moved again.

Sources: [CommonLibSSE-NG SleepWaitMenu](https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/main/include/RE/S/SleepWaitMenu.h),
[SkyrimSoulsRE SleepWaitMenuEx](https://github.com/Vermunds/SkyrimSoulsRE/blob/master/src/Menus/SleepWaitMenuEx.cpp).


## Party rest events (2026-10-26)

Listener audit and player effects: `docs/design/shared-wait-sleep.md`, "Party rest events". The host's
player is the party's Dragonborn. `HostWait` therefore sets `Sleeping` for both the actual sleeper
and the host when any player requests sleep; other guests wait. A guest wait starts a host wait.
`WaitBridge` keeps the native menu path: it emits one matching native start/stop pair on the host's
own player, without a second manually broadcast script event or a guest cause.

Rechecked the AE disassembly:

- **40414**, `0x14072E7F0`, starts sleep on `(PlayerCharacter*, int hours)`. Sets remaining seconds
  and the waiting flag, applies a furniture spell if the player's occupied furniture provides one
  (null furniture is allowed), then sends `TESSleepStartEvent` with current and desired end time in
  game days through `ScriptEventSourceHolder + 0xD68`. Starting the host from a guest's bed does not
  require placing the host in furniture.
- **40415**, `0x14072E940`, starts wait on `(PlayerCharacter*, int hours)`. Sends `TESWaitStartEvent`
  (same two game-day times) through `+0x1130`, marks the native rest as wait, and sets remaining seconds.
- **40417**, `0x14072EA20`, stops the current rest. Native sleep/wait state selects `TESSleepStopEvent`
  at `+0xDC0` or `TESWaitStopEvent` at `+0x1188`. The interruption flag comes from the engine's
  interruption latch, not this function's second argument. The cancel wrapper **40416**,
  `0x14072EA00`, sets that latch before calling 40417; native completion uses the ordinary stop path.

CommonLibSSE-NG's `ScriptEventSourceHolder` confirms all four sources. `SkyrimVM` consumes the two
sleep events; the shipped Papyrus `Form`, `Alias` and `ActiveMagicEffect` expose `RegisterForSleep`,
`OnSleepStart` and `OnSleepStop`, with no corresponding wait registration/events. No vanilla quest
wait-event handlers were found. Native wait events remain available to native/mod consumers.

A sleep listener can have registered during a guest interaction (for example an activated bed).
`ScriptTags` otherwise keeps that registration's guest cause. Engine sleep events in a session now
use the local player as their story cause, so a host quest acts on the host player, its controls
and fades remain the party's, and follow-up timers keep that context. Other registrations and explicit calls from a running native are unchanged.
No existing Skyrim hook implementation is edited for this rule.

## Guest fast-travel time (2026-10-27)

40445 scopes native fast travel around its call to Calendar::Update (36291). Even while GuestClock suppresses
that update, its seconds times TimeScale / 3600 give the trip's game hours. A guest sends those hours to
HostClock, which advances the host's native calendar. Host trips are already applied and are not forwarded.
Ordinary frame time and rest do not enter this route. `docs/design/party-travel.md` records validation.
