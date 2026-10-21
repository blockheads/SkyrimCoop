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
          wait / sleep            40485 PlayerCharacter::WaitStep (an hour of game time a step)
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
