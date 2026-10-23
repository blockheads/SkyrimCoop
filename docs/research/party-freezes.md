# Party freezes: a cutscene the party watches, or one player's moment

Research, 2026-10-23. Since step 1 (`docs/design/cutscenes.md`) every script call on the controls or the screen runs
on every game, so a script taking the controls for a moment that is no cutscene freezes the whole party. This lists
every vanilla script that takes the controls, what else the shared calls do, and proposes a generic rule that
tells the two apart. Evidence: the vanilla `.psc` sources (Data/Source/Scripts, 14,302 files) and IDA (1.6.1170).
Nothing was run in game for this.

## What a controls call does (IDA)

- `Game.DisablePlayerControls` (55454) turns its arguments into `ControlMap` flags and calls
  `ControlMap::ToggleControls` (68545): movement → `0x401` (movement and jumping), fighting → `0x40`, looking → `0x2`,
  sneaking → `0x80`, menu → `0x100`. Three arguments go elsewhere: camera switch sets a byte on `PlayerControls`
  (+0x4C plus the POV type), activate is turned off on `PlayerControls`, and **menu also closes six menus**
  (52212: `InventoryMenu`, `MagicMenu`, `TweenMenu`, `MapMenu`, `StatsMenu`, `Book Menu`, each sent a hide message).
- `ToggleControls` is a plain bitmask: `enabledControls` (ControlMap +0x120) gets `|=` or `&= ~`, with no layers or
  counts. It then sends `UserEventEnabledEvent` {new, old} from ControlMap's event source (+0x8), so a change in the
  enabled controls is an event we can sink. Two moments holding the controls at once mix: the first one to give them
  back gives them back for both (vanilla does the same).
- `Game.SetPlayerAIDriven` (55577) calls `PlayerCharacter::SetAIDriven`, which `NativeReach` does not count as the
  controls, so it is never shared today. Its 4 live uses are all inside cutscenes that also take the controls.
- `Scene.Start` (55320) and `Scene.ForceStart` (55319) call the engine's scene start, 24140 and 24141. 24140 marks
  the scene to start, and the scene begins on the next update.

## 1. Every script that takes the controls

Of the 89 scripts named in `docs/research/cutscenes.md`, **81 have a live call**. In 8 the call is commented out:
`DBNightMotherDrivePlayerTrigScript`, `DLC1VampireTurnScript` (Harkon's bite takes no controls), `DLC2_QF_DLC2MQ01`,
`DLC2_QF_DLC2MQ02`, `dunLabyrinthianMazeControlScript`, `QF_MG04MonkSceneQuest`, `RustyMaceTriggerScript` and
`TIF__0003F406`.

"Takes" is the controls the call turns off: **all** is the default (movement, fighting, menu, activate), and **M** means
movement is taken. "Starts from" is what began the stack: a player's action (activation, a trigger, an item, an
effect, a picked line), or the world (a stage or scene fragment, a quest timer, the story manager). The last three
columns are the candidate rules of section 3:

- ✓ right
- **F** freezes the party when it should not
- o leaves the others free when they should be frozen or restricted (the safe way to be wrong)

### One player's moment: 25 scripts

Travel and services:

| Script | Starts from | Takes | Holds | 1 | 2 | 3 |
|---|---|---|---|---|---|---|
| `CarriageSystemScript` (`PlayerIsSitting`, `RideInCart`, `StopRiding`) | a picked line, then the seat (its timer) | all; M + camera switch on a ride | until the fast travel; a ride's whole length | F | ✓ sitting | ✓ |
| `FerrySystemScript.PlayerIsSitting` | the same | all | until the fast travel | F | ✓ sitting | ✓ |
| `RPDefault_CarriageSystem`, `RPDefault_FerrySystem` | the same | all | until the fast travel | F | ✓ sitting | ✓ |
| `DLC2DialogueRRQuestScript.SetSail` | a picked line | all, looking too | the crossing: fade, fast travel, a 7 s+ boat idle | F | ✓ dialogue | ✓ |
| `DLC1SurgeryScript.BeginSurgery` | a picked line | all | until the race menu closes | F | ✓ dialogue | ✓ |
| `DLC2BookDungeonControllerScript` (`ReadBook`, `PlayerInBleedout`) | reading a Black Book (`OnRead`); bleeding out in Apocrypha | M | the trip to or from Apocrypha | F | ✓ book menu | ✓ with an item cause, F without |

Activities, traps and restrictions:

| Script | Starts from | Takes | Holds | 1 | 2 | 3 |
|---|---|---|---|---|---|---|
| `ccBGSSSE001_FishingSystemScript` | activating a fishing spot | M and looking, activate left (the session); all (the catch) | **the whole session, minutes** | F | F | ✓ (F for ~3 s when a quest fish sets a stage) |
| `MS05BardLutePlay` | activating the lute | M, activate left; third person | 34 s, or until activated again | F | F | ✓ |
| `DLC2StandingStoneScript.WorkOnPillar` | activating a stone | all | ~6 s: a fade, then moved into the pillar | F | F | F: Miraak's voice scene starts while held |
| `DLC01TeleportScript` | a trigger | all | 2 s | F | F | ✓ |
| `dunBorderWallNocturnalTrapDoorSCRIPT` | a trigger | all, looking too | a few seconds: an idle, the floor opens | F | F | ✓ |
| `DA06HammerTriggerScript` | activation | all | 2 s; its stage comes after | F | F | ✓ |
| `DB10PlayerPinnedTriggerScript` | a trigger | all | 2 s (a placeholder message box) | F | F | ✓ |
| `dunDustmansCairnLever` | activation | fighting | until the second lever | ✓ | F (no freeze) | ✓ |
| `C00VilkasTrainingScript.PlayerSheathe` | a picked line | fighting | 5 s | ✓ | ✓ dialogue | ✓ |
| `MQ304PlayerScript` | the player's alias changing location | fighting, sneaking | while in the Hall of Valor | ✓ | F (no freeze) | ✓ with an alias cause |
| `QF_CidhnaMineJailEventScene` | the arrest (story manager) | fighting | the jail intro | ✓ | F (no freeze) | ✓ |

Transformations:

| Script | Starts from | Takes | Holds | 1 | 2 | 3 |
|---|---|---|---|---|---|---|
| `PlayerWerewolfChangeScript.PrepShift` | the beast form quest's stage 0, from the power's effect | camera switch; third person | **the whole beast form** | ✓ | F | ✓ |
| `DLC1PlayerVampireChangeScript.PrepShift` | the same, for Vampire Lord | camera switch; third person | the whole form | ✓ | F | ✓ |
| `DLC1VampireChangeEffectScript` | the effect on the player | camera switch | the change | ✓ | F | ✓ |
| `DLC1MagicMistformFXScript` | the effect | fighting, camera switch, menu, activate | mist form | ✓ | F | ✓ |
| `DLC1VQ02NoFollowDoorAliasScript` | the castle door, activated as a werewolf | fighting | the shift back | ✓ | F | ✓ |
| `C03RampageQuest.StartRampage` | a stage | camera switch; third person, moved | the rampage, an hour of game time | ✓ | F | ✓ |
| `PlayerVampireQuestScript.VampireChange` | the disease's timer | all | ~3 s; a crossfade | F | F | ✓ |

### Story restrictions: 9 scripts, movement left

Story moments that only stop fighting, sneaking or menus while a scene plays. None of them freezes anyone.

| Script | Takes | 1 | 2 | 3 |
|---|---|---|---|---|
| `DLC1_QF_DLC1VQ01COPY` Fragment_23 (the Volkihar banquet) | fighting, sneaking | o | ✓ | o |
| `QF_CWAttackCity` Fragment_17 (the jarl surrenders) | fighting | o | ✓ | o |
| `QF_TG09` Fragment_8 (Nocturnal) | fighting, sneaking | o | ✓ | o |
| `DBDawnstarCiceroNoAttackTrigScript` | fighting, 20 s | o | ✓ | o |
| `MG01ClassroomDoorExtScript` | fighting | o | ✓ | o |
| `DLC1VQ03MothpriestFightScript` | fighting, given back at once | o | ✓ | o |
| `MQ202EsbernsDoorScript` | sneaking, given back at once | o | ✓ | o |
| `QF_DA15` Fragment_31 (into Pelagius' mind) | menu | o | ✓ | o |
| `dA16EnableActivateTrigger` | fighting, camera switch, menu | o | ✓ | o |

### Cutscenes the party should watch: 47 scripts, movement taken

| Script | Starts from | 1 | 2 | 3 |
|---|---|---|---|---|
| `QF_MQ101` (several), `SF_MQ101KeepIntroSceneANew`, `SF_MQ101KeepIntroSceneB`, `MQ101QuestScript.CameraBobStart` (package fragments) | the world (Helgen) | ✓ | o on the cart (sitting, AI-driven) | ✓ |
| `QF_MQ105`, `QF_MQ201` (2), `QF_MQ206`, `QF_MQ302`, `QF_MQ303`, `QF_MQ305` | stage fragments | ✓ | ✓; o when seated (`MQ201` carriage, `MQ302` council) | ✓ |
| `QF_DA01`, `QF_DA16` (2), `DA09Script` | stage fragments | ✓ | ✓ | ✓ |
| `QF_TG05SP`, `QF_TG08A`, `QF_TG08`, `QF_RelationshipMarriageWedding` | stage fragments | ✓ | ✓ | ✓ |
| `DLC1_QF_DLC1_BF_DunTempleQST` (Vyrthur), `DLC1_QF_DLC2MQ01` (Miraak's vision), `DLC1_QF_DLC1dunRedwaterDenQST` (knocked out) | stage fragments | ✓ | ✓ | ✓ |
| `DLC2_QF_DLC2MQ04`, `DLC2_QF_DLC2MQ07`, `DLC2_QF_DLC2TTR4b` | stage fragments | ✓ | ✓ | ✓ |
| `SF_CWFinaleSolitudeScene`, `SF_CWFinaleWindhelmSceneA`, `SF_DB09MaroAmbushScene`, `SF_MGRAppBrelyna01CastScene1` and `2`, `SF_CiceroAngryScene` and `SF_CiceroNightMotherTalkScene` (these two only give back) | scene fragments | ✓ | ✓ | ✓ |
| `CWFinaleScript.PlayerEnteredCastle` | a door or a stage; sets stage 100 first | ✓ | ✓ | ✓ |
| `DLC1_TIF__0100D3E0`, `DLC1_TIF__0100E880` | a picked line's start; its end sets a stage | ✓ | o dialogue | ✓ when the stage is set |
| `ccBGSSSE001_TIF__05042DF5` | a picked line; completes an objective | ✓ | o dialogue | o (no stage) |
| `ElderScrollScript.OnEquipped` | reading the scroll; `MQ206.SetStage(20)` while held | ✓ (and own elsewhere: the blind read leaves movement) | ✓ | ✓ promoted at the stage |
| `NightMotherCoffinScript` | a trigger; sets a stage, starts a scene | ✓ | ✓ | ✓ |
| `MG06VisionTriggerScript`, `pDB09BridgeTriggerScript` | a trigger; starts a scene | ✓ | ✓ | ✓ |
| `TG08BEncounterTrigger01Script`, `DA16MistFill`, `DLC2TTR4bSpellEffectScript` | a trigger, activation, an effect; sets a stage while held | ✓ | ✓ | ✓ |
| `DA13GooActivatorScript.BeginVision` | activation; its timer sets the stage | ✓ | ✓ | ✓ (the timer carries the cause) |
| `pDBEntranceQuestScript.OnSleepStart` | sleeping (a quest event, no actor) | ✓ | ✓ | ✓ (the world's) |
| `C02CeremonyTriggerScript` | a trigger; the quest's own update starts the ceremony | ✓ | ✓ | o |
| `AltarofMolagBalScript` | sitting on the altar; the mace's dialogue | ✓ | o sitting | o likely (no stage or scene in its own chain) |
| `DLC1VQ06ReadingTriggerScript`, `DLC1VQ06QuestScript.ScrollRead` | reading the scroll at the Ancestor Glade (a 38 s vision) | ✓ | ✓ | o likely (no stage seen while held) |

What the moments of one player have in common: they start from that player's action and **never involve the
story while they hold the controls**: no scene starts and no quest stage is set before the controls come back.
Travel, the fishing session, the lute, the transformations and the traps all fit this. The story's cutscenes either
run from the story itself (a stage or scene fragment, a quest's timer), or, when a player's action starts them, set a
stage or start a scene while they hold the controls (the Elder Scroll, the coffin, the triggers). Pure restrictions
never take movement.

## 2. Other freezes and intrusions from the shared call set

Besides the freeze itself:

- **Menus are closed on every game.** A shared `DisablePlayerControls` with menu on (the default) hides the
  inventory, magic, tween, map, stats and book menus wherever it runs (52212). One player's lever, lute or fishing cast
  closes everyone's inventory, and activation goes too.
- **One moment's give-back ends another's.** `ToggleControls` keeps no owner or count. A shared
  `EnablePlayerControls()` (every flag on), say from the lute stopping on one game, gives the controls back on a game
  in the middle of another game's cutscene. `PartyNatives::GiveBackControls` covers only a game leaving.
- **The camera is forced on everyone.** `ForceFirstPerson` and `ForceThirdPerson` (14 and 15 scripts) come from
  one-player moments too: fishing (first person, then third person if the player started there), the lute (third),
  `RideInCart` (first), the Elder Scroll's blind read and the Dawnguard scroll (first), and the werewolf and Vampire
  Lord (third). The beast forms also turn camera switching off for their whole length, so while one player is a
  werewolf nobody can switch the camera.
- **Tints from a body:** the werewolf and vampire changes (`WerewolfChange`, `VampireChange`,
  `VampireTransformIncreaseISMD`), fishing's fade to black and its catch depth-of-field, and the carriage, ferry and
  standing stone fades. This was accepted for spells, but here each comes with a freeze.
- **Shakes and rumble without a source** play at full strength everywhere: the Black Book refusing to be read, the
  Elder Scroll's blind read, and `ShakeController` in 49 scripts, mostly traps, which rumbles every gamepad.
- **Music:** `C03`'s rampage adds `CombatMusic` for one player's werewolf hour, and the Elder Scroll adds its sequence
  music.
- **A guest's moment freezes the host too:** in a stack run for a guest, a party call runs on the host and every game
  (`docs/design/cutscenes.md`, "Before rule 2"). A guest pulling a lever on the host freezes the host's player as well.

## 3. Candidate rules

### Candidate 1: the call takes movement

The party's only if the call turns off movement (bit `0x1` of `enabledControls`, read before and after the call as
`Scope` already does for mirrored calls). Calls that leave movement stay their game's.

- Right: the 47 cutscenes, the transformations, the lever, sparring, the Hall of Valor and the blind read.
- Wrong: 15 freezes (travel 7, fishing, the lute, the stone, the teleport, the trapdoor, the hammer, the pinned
  message, the vampire change). The 9 story restrictions are left unshared, which is safe.
- Costs nothing and needs no state, but it cannot see travel or activities: they take everything a cutscene takes.

### Candidate 2: what the player's body is doing at the call

The player's own if, at the call, they are in furniture or a vehicle, AI-driven, or have a menu or conversation open
(sit state, `PlayerCharacter`'s AI-driven flag, `PlayerCamera` 400802's furniture state, `UI` 400327 / ControlMap's
context stack). Otherwise the party's. SmoothCam and True Directional Movement read the same things: movement
controls enabled, AI-driven, the furniture camera. Neither has any idea of a cutscene.

- Right: travel and services (seated, or a line picked in dialogue), sparring.
- Wrong: the fishing session, the lute, the stone, the teleport, the trapdoor, the hammer, the pinned message and the
  transformations all freeze the party: the body is free when the script takes it. It also leaves out cutscenes
  started from dialogue or a seat: the `DLC1` lines, Helgen's cart, the council, the altar.
- Player state is a snapshot. "Is the player a scene actor" (`GetCurrentScene`) holds only in the few scenes that
  cast the player (`MQ105`, the wedding, Helgen). Most cutscenes are NPCs playing to a frozen player.

### Candidate 3: a player's moment, until the story joins it

Every controls or screen call belongs to a **moment**, keyed by its stack's cause: the player whose action started
it (GuestScripts' tags), or the world (no player behind it).

- **The world's moments are the party's** (a stage or scene fragment, a quest's timer, the story manager), as now.
- **A player's moment is that player's alone**, unless the story joins it: its chain sets a stage or starts a scene
  (25004, 24140, 24141), whether the stack is itself a fragment of a stage the chain set, or the stage or scene comes
  while the controls it took are still held. It becomes the party's at that point.
- **Only a moment that takes movement is ever the party's** (candidate 1 as a filter), so restrictions and the beast
  forms stay with their player even when a stage is set during them.

Checked against the list:

- Right: all 25 one-player moments except the standing stone (F, ~4 s: Miraak's voice is a scene) and a quest
  fish's ~3 s catch (F, rare). That needs two additions to the cause: the player's alias (`MQ304`) and an item a
  player holds (`OnRead` names no actor: the Black Books).
- Right: 42 of the 47 cutscenes. The Elder Scroll, coffin and trigger cutscenes become the party's at their stage or
  scene. The blind read stays with the reader.
- Safe misses (o): the C02 ceremony, likely the Molag Bal altar and the Dawnguard scroll, a fishing quest line, and
  the 9 story restrictions. There the story joins outside the chain (a quest polling a variable, dialogue the player
  did not pick), or movement is left.
- On "who started the stack" alone (quest, scene and topic-info fragments for the party; object, alias and effect
  events for the player): the transformations, sparring, surgery and the boat run from fragments or picked lines and
  freeze everyone, and the Elder Scroll, coffin, triggers and visions start from objects and are missed. The cause
  only helps combined with the story signal.

## 4. Recommendation: candidate 3

"A player's moment, until the story joins it" is the only rule that gets travel, activities and transformations
right as well as the story's cutscenes. It reads nothing per script: the cause (GuestScripts' tags), what a native's
code calls (NativeReach), and the engine's controls event.

```
a controls or screen call (HookNativeCalled -> PartyNatives::Scope)
        |
        v
moment = Moments[cause]            cause: a guest | this game's player | the world (0)
        |
  is the moment the party's? --yes--> as today: run here, send to every game
        |no
        v
  run for its player only            (here for this game's player, PlayerNatives::SendToGuest for a guest)
  remember the control calls it held (Scope's before/after of enabledControls)

a stage or scene call in a stack of cause c (NativeReach kWorld, and a new kind for 24140/24141)
        |
        v
  Moments[c].StoryJoined: if it holds movement now -> Promote

Promote(moment): send its held control calls, in order, to every other game (PartyNativeRequest);
                 its later calls are party calls; its give-back is a party call
the moment ends: UserEventEnabledEvent shows its movement back (an event, no timer) -> forget it
```

What it needs:

- **The cause, for every player:** GuestScripts tags stacks with a guest (an event naming the guest's character,
  the calls a tagged native queues, its timers). Add this game's own player (0x14) as a cause on every game, so the
  host's own lever is a player's moment and not the world's. Also count an event whose self is a player's alias
  (`MQ304`'s `OnLocationChange`) or an item a player holds (`OnRead`, `OnEquipped`). On a guest's game every script
  is its own player's (`guest-scripts-option1`), so every moment there is a player's.
- **The story signal:** `TESQuest::SetStage` (25004) is already `NativeReach::Call::kWorld`. Add the scene start
  (24140, `Scene.Start` 55320) and force start (24141, `Scene.ForceStart` 55319) to `CallFunctions` as a new
  `Call::kStory`. On a guest, its world calls already go to the host (`WorldCallRequest`), and the same call promotes
  the moment there. A guest's `Scene.Start` should go to the host the same way (a gap today).
- **The movement check and the moment's end:** `ControlMap` (400863), `enabledControls` at +0x120, bit `0x1`.
  `ToggleControls` (68545) sends `UserEventEnabledEvent` from ControlMap +0x8, so a sink sees the controls come back.
  Nothing polls.
- **Into PartyNatives:** `Scope` asks the moment before deciding `IsParty`. A player's unpromoted moment takes the
  per-player route GuestScripts already has for a guest (`PlayerNatives::SendToGuest`), or runs here only for this
  game's player. `CutsceneService` gains one message for a promotion found on another game than the moment's
  player's: the host sees a guest's moment run on the host reach the story, and that guest's game holds what was
  taken. `GiveBackControls` covers promoted moments as it covers mirrored ones.
- **Screen calls follow their moment:** a player's moment keeps its `ForceThirdPerson`, fades and shakes; a party
  moment shares them. The world's screen calls stay everyone's, as decided ("people getting their game tinted is a
  small issue").

What it misses, and why that is acceptable:

- **Freezes left:** the standing stone (~4 s, a scene inside one player's moment) and a quest fish (~3 s, rare).
  Both are short and give back.
- **Not frozen when they should be:** cutscenes the story joins outside the chain (C02's ceremony, a quest polling
  a variable; likely the Molag Bal altar and the Dawnguard scroll) and story restrictions that leave movement. The
  others still see the scene; they are just not held. That is the cheap way to be wrong, and a scene that casts the
  others is step 2's anyway.
- **Promotion comes late:** the others are frozen from the stage or scene, not from the first call. Screen calls
  before it (the Elder Scroll's first-person switch and its music) are not replayed; its later fades are shared.
- **Two moments still mix** (one bitmask). Keeping one player's give-back off the others' games removes the common
  case. A real fix would be per-cause control layers, which the engine does not have.
- **Menus closed by a shared Disable** stay as they are for real cutscenes, which is right there.

## 5. Built: the simple version (2026-10-23)

Chosen with the user (2026-10-23): candidate 3 without promoting a held moment later. What a player's action starts
is theirs, unless a stage that action's scripts set runs it. A controls call is shared only if it asks for movement.
Design and code: `docs/design/cutscenes.md`, "Whose moment".

What it drops from section 4: promoting a moment when its chain starts a scene or sets a stage *after* taking the
controls. The Elder Scroll still works (the `MQ206` fragment takes the controls itself). MG06's vision and DB09's
bridge leave the others free (`Scene.Start` in the trigger's own stack), and so does C02's ceremony. Add promotion only
if a missed cutscene matters in play.

### Edge cases

Still freezing the party, wrongly:

- **A mod's quest polling the player** on a timer started at load, then taking the controls. No player's action is
  behind it.
- **A stage set as bookkeeping:** a mod sets a stage from a fragment during the player's moment. Vanilla's beast
  forms do this but leave movement alone, so they are safe.
- **A player far away or in combat** is frozen by a cutscene too. That is intended: everyone gets the cutscene
  (decided 2026-10-23).
- **Two moments mixing:** `ToggleControls` is one bitmask.
- **Controls that never come back** when a cutscene's script dies (only a game leaving gives them back).

Not frozen, the safe way to be wrong:

- a scene a trigger or a quest starts outside the chain
- a story restriction that leaves movement
- a package fragment on the player's own package (Helgen's walk: the fragment names the player, so the player is its
  cause)

## Scenarios to build it against

Built: `player-moment-stays-theirs`, `restriction-stays-with-game`, `player-stage-cutscene-reaches-party`. Still to
build, driven as a player would (each with `screen` to read the controls):

- the lute on a guest: the host stays free
- fishing on the host: the guest stays free
- a carriage seat
- a werewolf's camera switch
- the Elder Scroll at the Time-Wound: frozen from stage 20 on every game
- the coffin trigger
- a world fragment: everyone frozen at once (`cutscene-controls-reach-party` today)
- the standing stone: the toucher's alone (with no promotion it is no longer a freeze)

## Sources

- Vanilla scripts (Data/Source/Scripts) for every row in section 1
- IDA 1.6.1170: `papyrus::Game::DisablePlayerControls` (55454), `ControlMap::ToggleControls` (68545), the menu
  helper (52212), `papyrus::Scene::Start`/`ForceStart` (55320, 55319), `BGSScene` start (24140, 24141),
  `papyrus::Game::SetPlayerAIDriven` (55577)
- SmoothCam (mwilsnd/SkyrimSE-SmoothCam) and True Directional Movement (ersh1/TrueDirectionalMovement), added to
  `scripts/re/fetch_references.sh`. No mod detects cutscenes, and Skyrim Together Reborn has no cutscene handling.
