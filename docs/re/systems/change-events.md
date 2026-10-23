# What tells us state changed (events and write hooks)

Discrete state goes out when the engine says it changed, not from a timer (docs/ideas.md, "Events over polling").
Each `Games/Skyrim/*Events.cpp` module turns an engine event or a hook on the one function that writes the state
into a client event, queued with `World::GetRunner().Trigger` (sinks and hooks can run on job threads), and the
service reads and sends on the main thread if the value differs from the last sent. The modules are on while
`HostEngine` or `GuestEngine` exists (a session). Findings from 2026-10-22, 1.6.1170, IDA instance 83wk.

## Combat (AiStateEvents)

- `TESCombatEvent` is a combat *state* change (none, combat, searching). A target switch inside a fight sends none.
- `CombatController::SetTarget` (33235, `0x559630`) is the only writer of an NPC's target: it writes the controller's
  `targetHandle` (`+0x2C`) and the attacker's `Actor::currentCombatTarget` (`+0x104`). Both callers sit under
  `Actor::UpdateCombat` (the controller's update `0x558B70` and `UpdateTarget` 33236); their return value is unused,
  so the hook is `void(CombatController*, Actor*)`. It sends `TESCombatEvent` itself only when the target goes to none.
  The attacker is read as SetTarget reads it: `pCachedAttacker` (`+0xD0`) while `handleCount` (`+0xC8`) is set, else
  `attackerHandle` (`+0x28`).
- Other writers of `+0x104` (a scan for `mov [reg+104h], r32`): `Actor::StopCombat`, two `PlayerCharacter` functions
  (the player's own target; VATS) and actor construction.
- The player has no combat controller: `PlayerCharacter::UpdateInCombat` (41238) recounts its in-combat bit
  (`PlayerFlags` byte 3 bit 5, `+0xBE3 & 0x20`) each update, with no event. Hooked: a flip raises the player's AI state.
  Return value unused at its three callers (update, `FinishLoadGame`, a combat group update).
- Lever for a switch in a scenario: an NPC fighting two targets keeps the first; the first stepping 1500 units away
  makes him take the closer one within about 2 s, combat state staying 1. Lifting the player 2000 units also works
  but leaves the host's Fader Menu stuck open after the fall (the next scenario's input waits fail).
- After a console `stopcombat`, a thawed Vilkas takes up the fight with the host's player again within 0.2-1.8 s
  (vanilla: same with these hooks left out). Scenario cleanups freeze him first.

## Effect visuals (EffectEvents)

- `TESActiveEffectApplyRemove` (Papyrus `OnEffectStart`/`OnEffectFinish`) is sent from `ActiveEffect::Update`
  (34092) only when the effect's form has flag `0x400000` (a scripted effect). Flames' fire damage sends none, so it
  cannot drive effect sync. `TESMagicEffectApplyEvent` is sent for every application (each concentration tick) but
  not for an effect whose conditions hold again, nor for ends.
- The engine's own hit visuals start in 34094 (ours: `ActiveEffect::StartHitEffects`, from `Update` when the effect
  starts or holds again: it starts the controller, or queues `ApplyVisuals` 34065 as a task off the main thread) and
  stop in `ActiveEffect::StopHitEffects` (34050: `Update`'s finish branch, dispels, the destructor). Both hooked,
  `void(ActiveEffect*)`; the stop raises only when `pHitEffects` (`+0x60`) was set (the destructor after a normal end
  finds it cleared). The target is `pTarget` (`+0x50`) through `MagicTarget::GetOwnerActor` (34529, `this - 0xA0`
  when the `MagicTargetIsActor` vfunc says so).
- `Update`'s finish branch sets `kRemoved` (`0x20000`) before stopping the visuals; the effect stays listed until it
  is freed, so the owner's read skips `kInactive | kRemoved`.
- A hit effect lasts as long as its effect only with the MGEF's FX Persist (`EffectSettingData::flags` bit 12,
  what `ActiveEffectReferenceEffectController::GetEffectPersists` answers); otherwise it plays once. The listed
  (shared) effects are the FX Persist ones, which copies play until stopped: a 3600 s blessing (Gift of Charity,
  0xEEF58) glows a moment on the owner and stayed on copies before. A one-shot one goes out when `StartHitEffects`
  starts it (`EffectFlashRequest`), and the copy plays it with a lifetime of 0, as the engine's own ends: the
  controller's effects get a lifetime of `FLT_MAX` (ReferenceEffect constructor, `0x7F7FFFFF` at `+0x10`) and a
  reference effect is finished once its age passes a lifetime that is not negative (34769); finished, a shader
  fades out over its own pass (`ShaderReferenceEffect::Update` 34913: fill and edge fade-in, full and fade-out
  times, the longer of the two, `TESEffectShader` 17339) and ends (read in IDA, 2026-10-22).
- A one-shot shader is finished from its start: `ShaderReferenceEffect::Init` (vfunc 0x36, `0x5CFB40`) sets
  `finished` to `!controller->GetEffectPersists()` (controller vfunc 8, `0x5CDCA0`) and the effect plays its pass
  while finished; `ShaderReferenceEffect::Update` keeps `finished` as `ReferenceEffect::IsFinished` (34769) or no
  controller. So `ProcessLists::GetPlayingVisualFormIds`, which skips finished effects, never lists a flash; the
  harness's `mod.visuals_shown` lists every reference effect still in the process list.
- In the MGEF record's DATA the hit shader is field 8 and the enchant shader field 9: Arkay's blessing
  (`FortifyHealthFFSelf`, `0xFB98D`) has only an enchant shader (`0x92DEC`); its glow is `CureDiseaseEffect`'s, an
  instant FX Persist effect (progress.md, Copies loose ends). Gift of Charity's `AlchFortifyPersuasion` (`0xD6947`,
  an hour, `HealMysticFXS` `0x4E220`) is a one-shot one.
- `EffectSetting` (ours, asserted against CommonLib): `flags` `+0x68`, `effectShader` `+0x98`, `hitEffectArt` `+0xF0`.
- A copy's visuals drop with its 3D: `TESObjectLoadedEvent` (ReferenceEvents) replays them.
- An NPC moved to its unloaded home cell loses its active effects (Oakflesh ended), so "a copy made later" is
  tested with the guest leaving and joining again instead.

## The player (PlayerEvents)

- Race: `TESSwitchRaceCompleteEvent` (`NiPointer<TESObjectREFR> subject`, size 8) after `Actor::SwitchRace`.
- Level: `TESActorBaseData::SetLevel` (14385) is the one write (`+0x10`), from the level-up (41566), the console's
  `advlevel`/`setlevel`, `DuplicateNPCStats`, template copies and `LevelData::SetLevel`. The level-up sends
  `LevelIncrease::Event` (source 40319) with the new level *before* writing it, which is likely why the old level-up
  hook "was weird"; `setlevel` sends no event at all. Hooked; the form is `this - 0x30` (`TESActorBase::actorData`),
  the player's base is 0x7. Return value unused by every caller.

## Factions (FactionEvents)

- `Actor::GetFactions` reads the base's list and `ExtraFactionChanges`. Their writers: `Actor::AddToFaction` (37686,
  the console), `Actor::RemoveFromFaction` (37688, console and Papyrus), 37677 (ours: `Actor::SetFactionRank`, under
  Papyrus `SetFactionRank`/`AddToFaction`, `ModFactionRank` 37678 and alias factions), `Character::SetCrimeFaction`
  (37381) and Papyrus `RemoveFromAllFactions` (54735, clears the lists itself). The engine sends no event. All hooked
  with the original's return passed through (the Papyrus natives tail-jump into them).
- `Character::sub_37668`, called after every write, is a broad cache reset (kills, moving to low process) and was not
  used.

## Equipment (not done)

- `TESEquipEvent` is sent for every equip and unequip, whoever makes it; the copies' drift the snapshot repairs
  happens in the copy's game, so the owner's event does not cover it (docs/ideas.md).
- The weapon state (`ActorState+0xC` bits 5-7) has no single writer: a scan for the `0xFFFFFF1F` mask finds only
  `ActorState` vfunc 21 (38979, drawn or sheathed) and `Actor::DrawWeaponMagicHands` (37279); the intermediate
  states are written elsewhere. Left on the per-frame read.

## Quests and globals (not done)

- Quests: `TESQuestStageEvent`, `TESQuestStartStopEvent` and `ObjectiveState::Event` (source 23951) cover stages,
  running and objectives; alias and location fills (`BGSRefAlias::ForceRefTo`, `Clear`, location aliases) send
  nothing. `QuestState` carries all of them.
- Globals: written by Papyrus `GlobalVariable.SetValue`/`Mod`, the console's `set` and compiled script operations;
  no event and no single writer known.
