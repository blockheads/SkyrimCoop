# Cutscenes: what vanilla does to the player, and how the party sees it

Research, 2026-10-22. Decided before it (`docs/design/party-model.md`, Cutscenes; the user, 2026-10-22: "yes
absolutely the whole party should see cutscenes"): every cutscene plays for the whole party, whoever triggered it,
each player with their own character. Calls split into **party calls** (everyone) and **per-player calls** (the
player whose action started the script, rule 2). Helgen's cart is likely skipped (an alternate start, as most mod
lists do), so nothing here depends on it.

## What a Skyrim cutscene is

No video: the only `Game.PlayBink` in vanilla is an unused E3 demo script (`E3DemodragonFinaleScript`). A
cutscene is a script taking the player over in the engine: controls off, a forced camera, image-space fades, a
shake, music, the player moved or walked by AI.

**89 vanilla scripts take the player's controls** (`DisablePlayerControls` 83, `SetPlayerAIDriven` 6,
`SetCameraTarget` 1, `PlayerMoveToAndWait` 1), about 40 story moments, nearly all seconds to a minute long:

- main quest (~12): Helgen (`MQ101`), the Greybeards (`MQ105`), the Thalmor Embassy (`MQ201`), Esbern's door
  (`MQ202`), the Elder Scroll (`ElderScrollScript`, `MQ206`), Sovngarde and the ending (`MQ302`-`MQ305`)
- factions (~20): the Companions' ceremony and rampage (`C02`, `C03`), the Night Mother and the shack (`DB*`),
  `TG05`/`TG08`/`TG09`, the College (`MG01`, `MG04`, `MG06`), the Civil War finale and city attacks
- Daedric quests (~8): `DA01`, `DA06`, `DA09`, `DA13`, `DA15`, `DA16`, Molag Bal's altar
- Dawnguard (~15): Harkon's bite (`DLC1VampireTurnScript`), the moth priest (`DLC1VQ06*`), Redwater
- Dragonborn (~10): Miraak (`DLC2MQ01`-`07`), the Black Books, the standing stones
- systems (~15): carriages and ferries, werewolf and vampire changes, a wedding, Cidhna Mine, a bard's lute, levers

What they call, over the 89 (calls): `Wait` 210, `SetStage` 177, `MoveTo` 157, `Disable`/`Enable` 256,
`Scene.Start` 121, `EvaluatePackage` 113, `DisablePlayerControls` 100 and `Enable…` 83, `PlayIdle` 86,
**image-space modifiers 102** (`Apply` 41, `ApplyCrossFade` 28, `PopTo` 33), `SetInChargen` 31, `ShakeController`
19, `ForceFirstPerson` 17 and `ForceThirdPerson` 12, `SetVehicle` 12, `SetRestrained` 11, `ShakeCamera` 8,
`SetPlayerAIDriven` 7, **`FadeOutGame` 6**. Fades are image-space modifiers (methods on an `ImageSpaceModifier`
form), not `FadeOutGame`.

## One call, two meanings

The same calls appear outside cutscenes, for one player:

| Call | Scripts | Taking controls too | The others |
|---|---|---|---|
| `Game.ShakeCamera` | 56 | 5 | traps, rumbles, dungeon puzzles, a dragon landing, the Greybeards' call |
| `Game.ShakeController` | 49 | 4 | the same |
| `ImageSpaceModifier.Apply` / `ApplyCrossFade` / `PopTo` | 80 / 40 / 23 | 27 / 13 / 14 | spell visuals (night eye, soul trap, vampire sun, banish), word walls, a dragon's soul, area triggers |
| `ForceFirstPerson` / `ForceThirdPerson` | 14 / 15 | 14 / 14 | |
| `FadeOutGame`, `SetPlayerAIDriven`, `SetCameraTarget` | 3, 6, 1 | all | |

So "party" cannot be a property of the call (nor of the engine objects it reaches: `FadeOutGame` reaches the
UI's queue as `ShowTrainingMenu` does, `ShakeCamera` only the player, as `AdvanceSkill`; the table of natives and
objects is `docs/re/papyrus_natives.tsv`). It is a property of the moment: **a call is the party's while a
cutscene runs**.

## Where the calls end in the engine (IDA, 1.6.1170)

| Script call | Engine function | Its other callers |
|---|---|---|
| `Game.DisablePlayerControls` (55454) / `Enable…` | `ControlMap::ToggleControls` (68545), `PlayerControls`, `MenuControls` | 13: menu open/close sinks, `PlayerChairEnterHandler`, `Actor::SetLifeState`, console |
| `Game.FadeOutGame` (55456) | the engine's fade (52847; ours: `References.cpp`) | 12: loading, map, sleep/wait, stats menus |
| `ImageSpaceModifier.Apply` | `ImageSpaceModifierInstanceForm::Trigger` (adds to `TES`'s list) | magic effects, the engine's own |
| `Game.ShakeCamera` | engine `ShakeCamera` (from the source, else the player) | traps, explosions |
| `MusicType.Add` | `BSMusicManager` | |

**The engine has no cutscene state to read:** its control, fade and camera functions are the menus', chairs' and
sleep's too, so hooking them would freeze the party on every menu. The calls are caught where the script makes
them: the natives (`HookNativeCalled`, which every game already has).

## Where the scripts run

- **On the host, for a guest** (rule 2): quest and scene fragments, a picked line's script (Harkon's bite from his
  dialogue). Per-player calls already go to that guest's game (`PlayerNatives`); a method on a form
  (`ImageSpaceModifier.Apply`, `MusicType.Add`) still runs on the host's screen.
- **On a guest's own game:** its body's and inventory scripts (`guest-scripts-option1`). `ElderScrollScript` is the
  scroll's item script, run by `OnEquipped` on the reader's game: a guest reading it runs the whole vision locally,
  and its `MQ206.SetStage(20)` is a world change made on a guest (a gap of its own: `host-decides-world`).
- **On the host, for the host:** everything else.

## Three read end to end

- **The Elder Scroll** (`ElderScrollScript`): controls off but looking, first person, an idle, 2D sounds and music,
  camera-attached visual effects (`FXTimeWarpCamAttachEffect.Play(player)`), white fade, the quest stage, a weather,
  two image-space crossfades, then the idle stopped. The vision is the reader's; the party sees it as their own.
- **Harkon's bite** (`DLC1VampireTurnScript.HarkonBitesPlayer`, from a picked line): fade to black, a paired idle
  with the picker, the picker moved to a wake-up marker, `PopTo` a sleepy fade-in, the vampire change (an identity
  change, shared by the party: `party-model.md`), a stage that has Harkon force-greet.
- **The Greybeards** (`QF_MQ105`): controls off, scenes with the player as an actor (`Scene.Start`, packages),
  shouts with shakes and image-space, `MoveTo` of the player, controls back.

## The calls

Natives whose own code calls one of those functions (IDA, depth 0; `NativeReach::CallOf` reads the same at run
time): the controls (`Game.DisablePlayerControls`, `EnablePlayerControls`); the screen (`Game.FadeOutGame`,
`ForceFirstPerson`, `ForceThirdPerson`, `SetCameraTarget`, `ShakeCamera`, `ShakeController`, `PlayBink`;
`ImageSpaceModifier.Apply`, `ApplyCrossFade`, `PopTo`, `Remove`, `RemoveCrossFade`; `MusicType.Add`, `Remove`).
Deeper reaches are not a cutscene's: `Actor.Kill`, `KillSilent`, `SetDontMove`, `SetRestrained`, `SetUnconscious`
(the controls, inside `Actor::SetLifeState`), `Weapon.Fire` (the rumble), `Debug.CenterOnCell`. 1.6's
`DisablePlayerControls` and `EnablePlayerControls` take a ninth argument (`aiDisablePOVType`, `NativeFunction9`).

## The design

Agreed 2026-10-22 and built (step 1) 2026-10-23: `docs/design/cutscenes.md`. Changed the same day: the user chose
"every screen call to everyone" over the cutscene gate below ("One call, two meanings": the tint is accepted). Both open questions answered by the
user ("yes to both"): a cutscene with no player behind it is the party's too, and a guest's own script setting a
quest's stage is sent to the host (`docs/design/cutscenes.md`, "A guest's own script changing the world").

## Test cases

As built: `docs/design/cutscenes.md`, Scenarios. A script's call is driven by the harness (`papyrus_call`, as a
quest's script), not by the console (`disableplayercontrols` is the console's own handler, not the native). The
Elder Scroll at the Time-Wound and the Greybeards are step 2's.
