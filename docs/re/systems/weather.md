# Weather

How the engine picks the weather a game shows. Read in IDA (1.7.104) unless marked otherwise.

## Sky's weather fields (`Sky`, CommonLibSSE-NG layout)

- `currentClimate` 0x40: the worldspace's climate (weather list `TESClimate+0x48`, volatility `+0x7C`)
- `currentWeather` 0x48, `lastWeather` 0x50 (the one fading out), `currentWeatherPct` 0x1B8
- `defaultWeather` 0x58: what a non-override `SetWeather` or a climate roll chose
- `overrideWeather` 0x60: a script's or the console's forced weather (wins over regions)
- `region` 0x68: the weather region the last roll used
- `PlayerRegionState::lastKnownWeatherRegion` (+0x48): the player's exterior weather region

## `Sky::UpdateWeather` (ID 26231), every frame

```
interval = 22 * (255 - climate.volatility) / 255 + 1   game hours
if override and hours since lastWeatherUpdate > interval:
    override = null                     the override expires
if override: next = override
else:
    region = interior cell's sky region, else PlayerRegionState weather region
    if region changed:
        next = region.SelectWeather()   weighted roll (TESWeatherList::PickWeighted)
        if current != next and region.HasWeather(current) and rand > elapsed/interval:
            next = current               keep: the current weather is valid here
    elif elapsed > interval:
        next = region ? region.SelectWeather() : climate roll
    no region: defaultWeather, else climate roll, else form 0x15E
if next != current:
    lastWeather = current (none on fast travel / interior: a hard cut)
    current = next, lastWeatherUpdate = now
    for each region in TESDataHandler: region.SetCurrentWeather(current)
currentWeatherPct = elapsed / lerp(weather.transDelta)   the fade
```

- `TESRegion::SetCurrentWeather` (16448): a region's `currentWeather` (+0x38) becomes Sky's weather if the
  region's list holds it, else a roll from the region's own list. So after every change, the engine keeps a
  weather for every region, consistent with the one shown.
- `TESRegion::HasWeather` (16450): the region's weather list (region data type 3) holds the weather with a
  non-zero chance (`TESWeatherList::Contains`).

## Setting it

- `Sky::SetWeather(w, override, accelerate)` (26241): fades from the current weather. `override` sets
  `overrideWeather`; otherwise `defaultWeather`, and clears the region so the next update re-reads it.
- `Sky::ForceWeather(w, override)` (26243): `ResetWeather` (clouds and precipitation cleared), `w` at 100%: a
  hard cut. Clears `lastKnownWeatherRegion`.
- `Sky::ReleaseWeatherOverride` (26244): drops `overrideWeather`, flags a re-roll.
- Callers (xrefs, read): `SetWeather` from the console's `sw` and Papyrus `Weather.SetActive`; `ForceWeather` from
  `SetWeather`, a queued task (0x59, `ForceActive` and the console's `fw`) and the world map's camera (its own weather,
  override). So a call to either is a script's, the console's or the map's, never a roll.
- The console's `fw` is not an override (measured: `fw 81A` leaves `overrideWeather` 0, sets `defaultWeather`). A set
  without override holds until the player's weather region is read again: both calls clear
  `lastKnownWeatherRegion`, and the next cell event refills it (`PlayerRegionState::Handle`, 41264, on
  `BGSActorCellEvent`: the cell's regions holding the player, with weather data, by override flag then priority).
  On that re-read the region counts as changed; the current weather is kept with a chance near 1 right after a set
  (the keep roll above).
- A fade in progress blocks the next change: `UpdateWeather` starts one only while `lastWeather` is none. A fade
  takes game hours scaled by the weather's `transDelta` (minutes of real time at timescale 20, measured).

## What it means for coop

The host's game decides, per region (`HostWeather`, `GuestWeather`):

- Each guest tells the host its weather region (`Sky::PlayerWeatherRegion`: the interior's sky region, else
  `lastKnownWeatherRegion`).
- The host gives a guest its own weather where it is forced (an override, or set and the region not read since:
  the hooks of 26241/26243 note it) or the guest shares its region; elsewhere that region's own weather
  (`TESRegion::currentWeather`, set by the engine on each change, `SetCurrentWeather` when none yet).
- A guest holds it as an override and fades into it (`SetWeather`, override, no accelerate). `ForceWeather`
  would cut (clouds and rain reset) and clear the guest's region. The held override would expire after the
  climate's interval and the guest's game would roll its own: our `UpdateWeather` hook keeps a held override's
  `lastWeatherUpdate` current once its fade is done.
