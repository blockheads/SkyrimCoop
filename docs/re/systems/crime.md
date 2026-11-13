# Crime: theft, witnesses, bounty

Read on Steam 1.7.104 in IDA, 2026-11-04 (names and comments in the database, exported to `ida_notes_1.7.104.tsv`).
How the party shares this is in [shared-party-bounty.md](../../design/shared-party-bounty.md); this page is the engine.

## Theft: StealAlarm (37422)

`Actor::StealAlarm(criminal, ref, item, count, total, owner, allowWarning)`, called by `PickUpObject` (37521) for an
owned loose item and by the container menu on close (`item` null, `total` the menu's value).

```
criminal must be the real player (a teammate is folded into it), else return
value = count * item gold value, else total, else 1
crime = Crime::ctor (42502): type, victim handle +0x08, criminal handle +0x0C, item +0x18
ProcessLists__AddPlayerCrime (41349): kept only if criminal handle == the player's handle
ownerActor = ProcessLists__FindStolenOwnerActor (41335):
    a high-process actor matching the owner (NPC base / leveled base, or faction member)
    when highest detection of the player (41408) + weight factor > 0
no owner actor: player's unwitnessed stolen value (+0x428 map, 40650), return
seen = RequestDetectionLevel(ownerActor, criminal, 3) > 0
if seen && allowWarning && player +0x6D8 (`TheftWarnings`) < 2 (0x14209EAF8):
    Actor__StartStealWarning (37556): a package (type 34) going to g_thePlayer, count + 1, no bounty; return
    (the warning itself: "The steal warning" below)
37369 (not read: a process call, topic 38)
if seen: Actor__StartAlarm(ownerActor, crime) (37554)
witness loop (ProcessLists__GatherCrimeWitnesses 38094, all high actors; asks each at priority 5):
    a witness that detects the criminal (37764, priority 3) in the shared crime faction -> witnessed
    every gathered witness, seen or not: AIProcess__SetPendingCrime (39557), process +0x440 = crime
if witnessed:
    Character__ReportCrimeAddBounty (37679): bounty = 37680(victim, type, faction, value),
        criminal->ModCrimeGold (vfunc 0xB6) and the "N Bounty added" HUD line; only for the player
    ActorProcess__AlertNearbyToCrime (39361) -> 39695: guards in the faction StartCombat(guard, g_thePlayer)
        or StartAlarm(guard, crime)
else: unwitnessed stolen value only
```

The bounty is written by 37679, at StealAlarm's call when an owner or witness detected the criminal then. Later reports:
a type 34 warning ending with the item kept (`Actor::EndInterruptPackage`), and in 39631 a guard searching for the crime
(`kCrimeSearch`) that finds the criminal (0x140703CE5-0x140703D19). Other later witnesses only react (below).

So the party's first two seen thefts only warn; the third alarms and adds bounty. The count is the host player's, so a
guest's and the host's thefts share it. (Also gated by the thief's "stole today" flag, +0xE8 bit 0x8000, cleared by day.)

## After the call: the alarm and each witness act on their own update

The alarm StartAlarm made runs in `AIProcess__UpdateAlarmPackage` (39651), a non-guard's next update:

```
for each crime on the package (+0xE0, next +0xE8):
    attack = ShouldAttack(witness, target)
    owner = crime type <= 1, the player's, and this witness owns what was stolen
if attack: assault the package's target
if owner and the target stole today: Actor__SetAngryWithPlayer, assault the package's target
```

This is what attacked the host on the duo: 16 ms after the guest's theft, `SetAngry` from `0x711B1B` and the assault from
`0x711ADF`, target `0x14`. Under the party rule that is right when the host is the nearest (below).

Every other witness: `AIProcess__ProcessPendingCrime` (39631), also from `UpdateHighProcessAI`, after StealAlarm returned:

```
crime = process +0x440; criminal = crime +0x0C
sees criminal (37764, 3) && shared crime faction && !Actor__IgnoresCrime (37303):
    ShouldAttack(witness, criminal) (not a guard):
        criminal is the player: Actor__SetAngryWithPlayer (37463)
        assault (39359 -> 40079 -> Actor::StartCombat)
    else: voice line, Actor__StartAlarm(witness, crime)
```

An alarm package reaching its target (`AIProcess__ConfrontAlarmTarget`, 40076):

```
target is the real player:
    no bounty in the witness's faction: end
    Actor__CrimeFactionIsHostileNow (37429): SetPlayerEnemy, assault the player
    else: force greet (the arrest)
target is anyone else: SetPlayerEnemy(faction) and assault it, whatever the crime
```

So the arrest exists only for the real player: an alarm aimed at a copy would turn a theft into a hostile faction.

`kAngryWithPlayer` (+0x204 bit 0x800) stays on the NPC, saves included. `Actor__ShouldAttack` (37534) answers yes for the
real player while it is set, before aggression or reaction are read. `Actor__ComputeReaction` (37667) reads the faction's
player-enemy flag, which only a violent crime or the branches above set and `iCrimeEnemyCoolDownTimer` clears.

## Who the reactions target

Every reaction aims at the crime's criminal, which StealAlarm requires to be the real player:

- `Actor__StartAlarm` (37554) reads the criminal from crime +0x0C: an assault (39359) when the witness is aggressive
  toward it (or it is attack-on-sight), otherwise an alarm package (type 21) whose location and target are that
  handle.
- `Actor__StartStealWarning` (37556) builds its package on `g_thePlayer`.
- 39695 starts guards' combat on `g_thePlayer` directly.

So a guest's theft judged on the host (the bridge passes the host's player as the criminal, `CrimeBridge::Judge`) sends
every reaction to the host's player, wherever the guest stands.

## Detection is on request: a witness sees only what it was asked about lately

`RequestDetectionLevel` (37764) = `ActorKnowledge__GetFreshDetectionState` (42702) on the witness's knowledge of the
target (39515, created on first use):

- The knowledge's `DetectionState` (+0x20) is filled only by a later detection pass; a request (42712) queues one
  and stamps +0x44.
- Priority 3 (every crime check) accepts a state at most 2 s old (table `0x1420B8AB8`: {refresh 1 s, max 2 s}).
  Older or missing: it queues an update and answers -1000, unseen, for this call.

StealAlarm judges once, synchronously, so a witness whose state of the criminal is older than 2 s cannot catch it.

What keeps the player's state fresh in vanilla is `AIProcess__UpdateGreetAndPlayerDetection` (`UpdateHighProcessAI`): every
high NPC asks for `g_thePlayer` at priority 3 on each update, at any distance. Nothing asks for another actor, which is
why Hulda's state of the host read 0.5 s old and of the guest's copy 4.7 s.

On the host, `HookProcessKnowledge` redirects those checks to the guest's copy, but only inside `Judge`'s scope. Gather's
priority 5 asks land on the copy; `ProcessPendingCrime` then runs outside the scope and reads the host's body.

## The bounty map moved on 1.7.104 (the playtest's missing bounty)

`PlayerCharacter::GetCrimeGoldValue` (40641) reads the map at +0x3F8 (capacity +0x3FC, entries +0x418), and GoToPrison
(40655) the jail faction at +0x728 and sentence at +0x730: 1.6.1170's offsets plus `SkyrimRuntime::PlayerShift`.
`CrimeEngine::Player` still had the 1.6 values, so `ReadGold` read 8 bytes off and found nothing. The engine wrote the
bounty; the host never saw it change and sent no `NotifyCrimeState`. The fix applies the shift.

Proof: with the shift, the host read a bounty of 8 that earlier thefts had already written and the guest received it;
from 0, a guest's chest theft through the menu made it 1 in both games. With the shift removed, `guest-theft-witnessed`
failed with `{}` in both. Those runs also asked every frame (below), and the scenario then accepted a leftover bounty.

Every "no bounty" read before that fix was blind, so none of them shows a theft went unwitnessed.

## Measured on the duo (2026-11-04)

Harness `crime knowledge` (39763) reads a witness's state without asking for one.

- Bannered Mare, nobody asking: Hulda knew the guest's copy with no state at all, later read it 4.7 s old while her
  state of the nearby host was 0.5 s old.
- Asking for every copy each frame (41408, tried, since removed) held her state of the copy at 0.3 s.
- Same chest and witness, warnings spent, witness perceiving the host unasked: a guest's theft wrote no bounty in 5 of 5
  runs with nobody asking and did in 2 of 2 with `wait_seen` just before. The host's own unasked theft wrote one.
- `guest-theft-witnessed` (removed) passed on a stale sighting: the guest walked out of the witness's view to the chest,
  and a state under 2 s old still counted. With a fresh ask at the theft that is correctly unseen; `guest-theft-aggro`
  stages the theft in view.
- A value 200 chest theft raised the bounty by 1, and one of value 100 by 1 (8 to 9). Not explained.
- `SetPlayerReportCrime` (+0xBEC bit 2) ends StealAlarm when clear. Whether the duo's characters start with it clear
  was not read: `report_crimes` logs it now.
- Harness: a frozen NPC stops detecting (its age only grows). After a hot reload, states of existing copies went stale
  and `Judge` logged nothing until a save load; suspected, not proved: the copies lost their player flag.

## The steal warning (2026-11-13)

Read in IDA. `Actor__StartStealWarning` (37556) clears the owner's run-once package, makes a type 34 package with
procedure 40 (+0xD8), sets its location (`PackageLocation` 29811, `0x140690870`) and target
(`PackageTarget__SetReference` 29855, `0x1406908ba`, type 0 with the ref's handle at +0x8) to `g_thePlayer`, puts it
on the owner (`PutCreatedPackage` 37570) and counts the party's warning (`PlayerCharacter__CountTheftWarning`, +0x6D8).

Procedure 40 is `AIProcess__ProcStealWarning` (39673), from `UpdateHighProcessAI`'s procedure switch:

```
timer (high process +0x200) counts down; out of time: StartAlarm (37554) on the owner's theft
not at the package location: travel to it (walk, run past 500 units)
not facing g_thePlayer (Actor__IsFacingRef 37250, 0x14071846b): TurnBodyToPoint (37811) to its position
then once: owner +0x100 = g_thePlayer's handle (12325), say topic 38 (Steal) to g_thePlayer (39333),
    play default action 55 (39255), and for each theft (type <= 1) on the owner's crime list (extra 0x34):
    Confiscate (16120) what the crime's owner (+0x40) owns from g_thePlayer to the owner (or its +0x168's +0xD0),
    free the crime; nothing is reported
```

The package ending with the theft still listed (the item kept: no given-back) reports it in `EndInterruptPackage`
(37474) and alerts the guards, the bounty a warned theft gets.

`Confiscate` (16120) is (items, from, to, owner filter, every stolen item, show): the 4th is a form compared with
each stack's owner (0x14023b35a). Our hook typed it `uint32_t`, cutting the pointer, so before 2026-11-13 a warning's
given-back found nothing, the host's own included.

Coop: every step names `g_thePlayer`, never the package's target, so a guest's warning faced, warned and took from the
host's player. `CrimeBridge` swaps those seven calls (by Address Library ID and offset, each checked to call the
callee) to put the guest's copy in the player's place: in 37556 inside `Judge`'s criminal scope, in procedure 40 while
the running type 34 package targets a player's copy (`HookWarningStep`). The given-back from a copy finds nothing (a
copy's items keep no ownership), so the host sends it (`NotifyCrimeConfiscation`: owner, to) and the guest's game runs
the same Confiscate on its own player, once the owner's copy is there (`CrimeService` keeps it pending while that
spawn waits); inventory sync moves the item to the owner on the host.

Measured on the duo (`guest-theft-warned`): Orgnar walked to the guest's copy, the guest's game gave the bottle back
to Orgnar (both games: 0 on the guest, 1 on Orgnar), warnings 1, bounty 0, 7 s after the theft.

## The steal warning in a scene (2026-11-13)

Read in IDA. Nothing on the warning's path checks the owner's scene: `StealAlarm` (37422) starts it (0x1406823b5) and
`StartStealWarning` (37556) puts it as the current package. A package-action scene takes it back at its next update:

```
BGSScene::UpdatePhase (24126) -> BGSScene__UpdatePhaseAction (24160), per action of the phase:
    skips an actor in dialogue with the player (MenuTopicManager +0x68); no check for an interrupt package
    active, not done: BGSSceneActionPackage::UpdateAction (24246, vfunc 0x13):
        actor's scene (vfunc 0x4A) is this one and its package is not the current one (+0x20):
        StartAction (24244, vfunc 0x10):
            first package whose conditions pass, not current:
            actor +0x204 |= 1, EndInterruptPackage(actor, 0) unless talking to the player, put the scene's package
EndInterruptPackage (37474), type 34 with the theft still on the owner's crime list:
    ReportCrimeAddBounty (37679), AlertNearbyToCrime (39361): the theft reported, the item kept
```

`Actor__StartAlarm` (37554) and 40257 stop an interruptible scene (flags +0xA0 bit 0x10) before they start their
package (0x14068fc77, `BGSScene::SetPlaying(scene, false)` 24140); the warning does not.

`BGSScene::SetPlaying` (24140, our name, read; no community declaration) is `void(BGSScene*, bool)`: `false` on a
playing scene is the stop (results event, every phase and action ended, the quest told), `true` on an idle one marks
it playing. Every stopping caller passes `false`. The harness `scene_watch` hooks it with that signature.

Measured in vanilla (the stand-in, no plugin, Orgnar's bar scene `BCF05` playing, Orgnar detecting the player at 34,
warnings 0): after the theft the Whiterun bounty was 5 and the player kept the bottle. With our plugin and no session
(the engine's path, harness readouts), the bottle made Orgnar's: warnings 1, bounty 5, kept, the scene still playing
and no scene stop (`scene_watch`). With the bar's own faction ownership the scene stopped at the theft, in vanilla
and with no session; which call stopped it was not read. The party matches vanilla (`guest-theft-in-scene-reported`: the warning
started for the guest's copy, bounty 5 in both games, the guest kept the bottle), so nothing changes:
`guest-theft-warned` stops the scene to test the warning itself.

The bar's bottles have no actor owner. `ProcessLists__FindStolenOwnerActor` (41335) takes the first high actor that
is the owner NPC or in the owner faction, so StealAlarm asks Delphine or Orgnar by run; only that one's sight decides
the warning (a guest's theft with Orgnar seeing it went unwarned once on the duo; Delphine asked first is inferred, not read). The scenario makes the bottle
Orgnar's (`own`).

## The guest's thefts at Orgnar's bar (playtest 2026-11-13)

The guest held E on the bar's bottles; each was picked up, not grabbed (`physics.md`, "Woken still"), so each was a
theft. On the host: seven `Party crime` judgements and seven `StealAlarm` calls, no bounty. On the duo, same bar, the
same build, from warnings 0 (reporting on, `crime read`): the guest's first seen theft raised `TheftWarnings` to 1 and
no bounty (the warning, to the host's player); its second added 6 in both games, and Orgnar and Delphine went for the
host (inferred, not read: the warning count stayed 1, so the owner's own check missed it and a witness's caught it). Which of the playtest's later five any NPC saw is not known: the log has no
detection readout. With warnings skipped for a guest (measured, same bar, warnings 0, Orgnar seeing the guest at level 17):
the first theft added 5 in both games and the warning count stayed 0 (`guest-theft-at-bar-bounty`).

## A created item's theft names its owner (2026-11-07)

Read in IDA. The player's pickup is `PlayerCharacter::PickUpObject` (40533), not `Actor::PickUpObject` (37521). With
an owner the player is not (`IsAnOwner` 20210), it picks the reference `StealAlarm` gets (0x14074C7CE):

```
item has a source file, form flag 0x1 or extra 2 (a plugin's) -> the item itself
else (a created item)  -> ProcessLists__FindHighOwnerOf (41382, our name): the first high-process actor, any cell,
                          owning the item and dealing in it (Character__DealsInItem 37382: any non-key item for a non-vendor)
                          none -> no StealAlarm, the item is only marked stolen
StealAlarm(player, that reference, base, count, 0, the item's owner form, true)
```

`IsAnOwner` (20210) on an actor reference reads only its own `ExtraOwnership`; with none it answers true
(0x140300EA3). `Actor::PickUpObject` (37521, NPCs) alarms with the item itself, then frees a created one
(`GarbageCollector::Add`, 0x14068B5B7).

Measured on the duo (`guest-steals-created-item`, before the fix): the guest's alarm named the owner's copy (sent by its
session id) and the host's `Judge` stopped at its permission recheck, `IsAnOwner(owner NPC, player)` true: "the party
owns it", no log line before 2026-11-07. The item's session id never mattered: the item is not on the wire.

The fix: a theft carries the owner form `StealAlarm` was given (`CrimeRequest::Owner`). For a theft naming an actor,
`Judge` skips the ownership recheck and the same-cell check (41382 reads every loaded cell; `CrimeRules::ChecksFor`),
asks only that the actor is loaded (has a process), and passes the carried owner to `StealAlarm` (`AlarmOwner`: an
actor's `GetOwner` is not the item's). `FindHighOwnerOf` already checked that the actor owns the item.

## Who an NPC fights for a party crime (2026-11-10)

Read in IDA. Every reaction names the host's player, so `HostAggro` must tell an NPC fighting the party for a crime
from one fighting it for anything else; then `PartyThreat` sends it to the nearest player. What the engine leaves on
an NPC that knows a party crime:

```
FactionEnemy     crime faction +0x58 bit 3 (24504): a violent crime, 37679's SetPlayerEnemy, an escape
Victim           extra 0x34 kPlayerCrimeList: the crimes against this actor
                 (Crime__Release 42503 unlinks a crime from its victim, crime +0x08; ExpireOldCrimes 41350)
AngryWithPlayer  +0x204 bit 0x800 (37463): a theft's owner alarm (39651), a pending-crime witness (39631)
Witness          the crime's witness array (+0x28, count +0x38; Crime__AddWitness 42506, Crime__HasWitness 42505):
                 every alarm (37422, 37423, 37425-37428), Actor__StartAlarm, ProcessPendingCrime and each guard
                 AlertCrimeResponders (39695) alerts; KnownCrime (41354) with criminal = g_thePlayer finds it,
                 asked per crime type 0-6 (41350 walks 7 lists; 41354's own guard accepts 7, past the lists)
```

`CriminalInterest` read only the first two. A theft's witness is angry and nothing else for a nonviolent crime,
and a guard answering the alarm is only on the witness list: 39695 calls `StartCombat(guard, g_thePlayer)` for the
crime the player is fleeing (player +0xB18), else `StartAlarm` (an assault or an alarm package on the criminal).
So those fights were not crime fights and kept Skyrim's pick: the host, wherever the thief stood (the user's
playtest, 2026-11-08). Now all four count (`CrimeRules::PartyIsCriminalTo`); the witness walk runs only when the
cheap three are clear. `HookCrimeAttack`/`HookCrimeReaction` read the same answer, so such an NPC also sees every
copy as the criminal and puts it among its targets. An alarm package walking to the host ends in the arrest
(40076), which is the party's: unchanged.

## Every player hears the bounty (2026-11-10)

Read in IDA: `Character__ReportCrimeAddBounty` (37679) writes the bounty (crime +0x58) and, when it is non-zero,
shows `"%d %s %s"`: the bounty, `sAddCrimeGold` (378208, "bounty added to"), the faction's name, queued
(`SendHUDMessage::ShowHUDMessage` 52933). Only for the player, so only the host's game showed it; a guest's game got the
totals through `NotifyCrimeState` and `ApplyGold` (vfunc 0xB5), which shows nothing. `PlayerCharacter::SetEscaping`
(40660) shows the same line for a jail escape; not relayed.

Measured on the duo (`guest-theft-bounty-message`, before the fix): the host showed "5 bounty added to Whiterun",
the guest's HUD showed nothing, both held a bounty of 5.

The fix: the host hooks 37679 and keeps each report naming its player with a bounty (`CrimeReport`: faction,
bounty); `HostCrime` sends them in `NotifyCrimeState.Reports` with the totals, and each guest shows the engine's line
(`CrimeBridge::ShowReport`). A report is shown once, never resent with a snapshot.

## Pickpocket (2026-11-08)

Read in IDA. A sneaking player's Activate on an NPC opens `ContainerMenu` in mode 2 (`TESNPC::Activate` 24715,
0x1403C0C7B: the race allows it, the NPC is not attack-on-sight and was not caught on before). Each take or place in that
menu is `ContainerMenu__TransferItem` (51141):

```
mode 2: AttemptPickpocket(player, victim, entry, count, taking) (40654), false: nothing moves
    chance = ComputePickpocketSuccess (26379), clamped to fPickPocketMinChance..fPickPocketMaxChance
    roll < chance: stat, skill advance, true
    else, not jailed (+0x728 prison faction): Actor__PickpocketAlarm(player, victim, 0, 0) (37423); HUD line; false
taken: victim->RemoveItem (Actor::RemoveItem 37797 -> TESObjectREFR::RemoveItem 19689), reason kSteal, to the player
```

`Actor__PickpocketAlarm(criminal, victim, item, count)` makes a crime of type 1 on the victim, forces detection both ways,
starts its alarm, asks the witnesses, and the victim reports it (`ReportCrimeAddBounty`), angry with the player. The
player's caught try passes no item and count 0: nothing was taken. (`TESNPC::Activate` passes an item only for an NPC
pickpocketing, 0x1403C0BA9.)

What SkyrimCoop does: the take is a container transfer like any guest's menu take (`ContainerTransfers`, Steal), so the
host moves it and both games apply it once. The caught alarm goes to the host naming only its victim (`HookPickpocket`),
and the host raises it on its NPC with no item (`CrimeBridge::Judge`). Before 2026-11-08 the guest's hook, the request's
check and `Judge` each wanted an item and a count, so a caught pickpocket never left the guest
(`guest-pickpockets-lucan`; `guest-pickpocket-party-alarm` drove 37423 with gold and 5, a shape the game never makes).

## What SkyrimCoop does about it

The party rule (`party-model.md`, Crime): one player's crime flags the whole party, and a hostile NPC attacks whoever is
near it.

- `CrimeEngine::Player` applies `PlayerShift` (above).
- A guest's crime is the host's player's in the engine, as Skyrim needs: record, bounty, alarm, arrest and the angry flag.
  `HookCrimeAttack` / `HookCrimeReaction` make every party body equally hostile to an NPC that knows a party crime.
- Who it attacks: `HostAggro` counts an NPC fighting a player for a party crime (`CrimeBridge::PartyIsCriminalTo`) as a
  party fight, so `PartyThreat` picks by distance, first pick at once. In such a fight a player the NPC sees is a target
  without having hit it (`CanTarget`): the combat group showed the guest known at detection 60-100 with stealth points
  left, which the damage rule turned away. The 1.2 switch ratio still applies, so a nearly equal distance keeps the target.
- `CrimeService` holds a guest's theft 0.5 s, asking every witness each frame (41408), then judges: the greeting pass
  does this for the real player, and a copy's state has to be fresh in that one call. Actions after it wait behind it;
  a lock check is answered at once.
- A guest's seen theft is warned like the host's (2026-11-13, "The steal warning" below): the warning's calls that name
  `g_thePlayer` get the guest's copy, and the given-back runs on the guest's game.
- Tried and removed: remembering which guest did a crime and running 39631 / 39651 in its scope so only that guest was
  attacked (wrong for the party rule, and one alarm can hold several players' crimes).

## IDs

| ID | Name (ours) | What |
|---|---|---|
| 37422 | Actor::StealAlarm | the theft judgment above |
| 37423 | Actor__PickpocketAlarm | a caught pickpocket's alarm; the player's names no item |
| 40654, 26379 | PlayerCharacter::AttemptPickpocket, AIFormulas::ComputePickpocketSuccess | the pickpocket roll |
| 51141 | ContainerMenu__TransferItem | a container menu's take or place; pickpocket first in mode 2 |
| 40533 | PlayerCharacter::PickUpObject | the player's pickup: a created item's alarm names its owner actor |
| 37521 | Actor::PickUpObject | an NPC's pickup: StealAlarm with the item, then a created one is freed |
| 20210 | TESObjectREFR::IsAnOwner | an actor reference with no ownership reads as anyone's |
| 41382 | ProcessLists__FindHighOwnerOf | a created item's owner among the high-process actors |
| 37382 | Character__DealsInItem | false for a key; a vendor's buy list |
| 41335 | ProcessLists__FindStolenOwnerActor | owner actor in high process |
| 41349 | ProcessLists__AddPlayerCrime | keeps only the player's crimes |
| 42502 | Crime__ctor | crime record, criminal +0x0C, faction +0x60, bounty +0x58 |
| 37679 | Character__ReportCrimeAddBounty | writes the bounty (ModCrimeGold 0xB6), HUD line; hooked: `CrimeReport` |
| 378208 | gGameSetting_sAddCrimeGold | the HUD line's "bounty added to" |
| 40660 | PlayerCharacter::SetEscaping | an escape's bounty, the same HUD line |
| 41354 | KnownCrime (coop_RealKnownCrime) | a player crime this witness knows (`CriminalInterest`'s Witness) |
| 42505, 42506 | Crime__HasWitness, Crime__AddWitness | the crime's witness array |
| 42503, 41350, 37675 | Crime__Release, ProcessLists__ExpireOldCrimes, Actor__ForgivePlayerCrimes | crimes leave victims' lists, angry cleared |
| 37554 | Actor__StartAlarm | witness's alarm or assault on the criminal |
| 39651 | AIProcess__UpdateAlarmPackage | a running alarm assaults its target |
| 39631, 39557 | AIProcess__ProcessPendingCrime, AIProcess__SetPendingCrime | a witness's deferred judgment |
| 40076 | AIProcess__ConfrontAlarmTarget | alarm at its target: arrest, or assault |
| 39632 | AIProcess__UpdateGreetAndPlayerDetection | keeps detection of the real player fresh |
| 37463, 37429, 24526 | Actor__SetAngryWithPlayer, Actor__CrimeFactionIsHostileNow, TESFaction__PlayerEnemyIsActive | what makes a witness attack |
| 39359, 40079 | ActorProcess::unk_Assault_39359, AIProcess__StartAssault | the assault gate and StartCombat |
| 37556 | Actor__StartStealWarning | first-theft warning package to g_thePlayer |
| 39673 | AIProcess__ProcStealWarning | procedure 40: reach, face, warn, take back (all on g_thePlayer) |
| 39674 | AIProcess__ProcSayWarning | procedure 20: says topic 38 / 41 to the process target |
| 37250, 37811 | Actor__IsFacingRef, Character::TurnBodyToPoint | the warning's facing |
| 39333, 39255 | AIProcess__SayTopicTo (named from its callers, body not read), AIProcess__PlayDefaultAction (read) | the warning's line and its take animation |
| 29811, 29855 | PackageLocation__SetReference, PackageTarget__SetReference (both read) | the warning's location and target |
| 24246, 24244 | BGSSceneActionPackage::UpdateAction, StartAction (read) | a scene re-takes its actor, ending an interrupt package |
| 24140 | BGSScene::SetPlaying (ours, read) | (scene, false) stops a scene (harness `scene_watch`) |
| 24160 | BGSScene__UpdatePhaseAction (ours, read) | each action's start, update and end |
| 16120 | coop_RealConfiscate | (items, from, to, owner filter, all stolen, show): the given-back and the fine's |
| 39155 | ActorProcess::GetPackageThatIsRunning (read) | run-once package, else the current one (+0x20) |
| 39361, 39695 | ActorProcess__AlertNearbyToCrime, ProcessLists__AlertCrimeResponders | guards respond |
| 40650, 40652 | PlayerCharacter__AddUnwitnessed/WitnessedStolenValue | stolen-value stats, +0x428 |
| 37764 | Actor::RequestDetectionLevel | fresh detection level or -1000 |
| 42702, 42712 | ActorKnowledge__GetFreshDetectionState, request | freshness rule |
| 39763 | HighProcessData__FindKnowledge | knowledge lookup without creating (harness `knowledge`) |
| 41408 | ProcessLists::RequestHighestDetectionLevelAgainstActor | asks every high actor about one target |
| 40641, 40655 | PlayerCharacter::GetCrimeGoldValue, GoToPrison | where the player's crime offsets were read |
| 55578 | papyrus::Game::SetPlayerReportCrime | +0xBEC bit 2; StealAlarm and the other alarms return early when clear |
