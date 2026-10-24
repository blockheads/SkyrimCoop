# What tells us state changed (events and write hooks)

Discrete state goes out when the engine says it changed, not from a timer (docs/ideas.md, "Events over polling").
The `Games/Skyrim/*Events.cpp` modules queue client events with `World::GetRunner().Trigger`, or coalesce form
ids for HostEngine under a leaf lock (sinks and hooks can run on job threads). The service reads and sends on
the main thread if the value differs from the last sent. The modules are on while
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

## Quests (2026-10-23)

`QuestEvents` coalesces quest ids; `HostEngine` reads just those quests on the next host update, after object
creation has been sent. `HostWorldState` scans once at session start; there is no 500 ms quest timer.

- `TESQuestStageEvent`, `TESQuestStartStopEvent` and `ObjectiveState::Event` (source 23951) mark the quest.
  CommonLibSSE-NG's `ObjectiveState` supplies the layout (objective pointer, two 32-bit states); its objective's
  parent is at +8. In 1.6.1170, 23933 dispatches at 0x394684 before storing the state byte at 0x394689; the sink
  only marks an id, never reads a snapshot there.
- `TESForm::AddChange` / `RemoveChange` (the existing `FormChanges` hooks) cover quest flags, including
  `SetCompleted` (24991), `SetFailed` (24988), start/stop transitions and bulk alias clearing (25049 removes
  change flag 0x10000000). Reset (25014) can clear stages and objectives without recording any flag if neither
  Completed nor Failed was set, so it has a post-hook too.
- Four alias write hooks: 25051 `void(TESQuest*, BGSBaseAlias*)` clears references; 25052
  `void(TESQuest*, uint32_t, TESObjectREFR*)` forces a reference; 25053 `void(TESQuest*, BGSBaseAlias*)` clears
  locations; 25054 `void(TESQuest*, uint32_t, BGSLocation*)` forces a location. Each marks after the original.
  CommonLib's `TESQuest::ForceRefIntoAlias` and `BGSRefAlias::ForceRefTo` supply the reference signature. The
  others match the existing typed quest calls and Papyrus ReferenceAlias/LocationAlias natives: the disassembly
  passes the quest in RCX, the alias pointer or 32-bit id in RDX/EDX, and the fill pointer in R8. Both Clear
  functions read aliasId at +0x18. IDA's incidental RAX result on force/reset is not a C++ return value;
  CommonLib declares both ForceRefIntoAlias and Reset void, and the native branches return no value.
- A quest snapshot's alias fills include cell hints and whether a created reference has gone to guests.
  `QuestChanges` tracks those references: the change funnel, `TESMoveAttachDetachEvent`, and the host's created
  reference insertion/removal invalidate dependent quests. Replacing/clearing an alias replaces its dependencies.
- GitHub/Nexus and the fetched CommonLib/PapyrusExtender sources were searched first; no existing mod supplied
  alias-write notifications. The four writers and Reset were checked read-only in IDA's 1.6.1170 database.

## Globals (2026-10-23)

`GlobalEvents` coalesces written ids. The host reads all nonconstant globals once, then only those ids; the
250 ms global timer is gone. Calendar globals still belong to HostClock and are excluded from these messages.

- Papyrus `GlobalVariable.SetValue` (55923) is `void(VM*, uint32_t stack, TESGlobal*, float)`: [po3-Tweaks](https://github.com/powerof3/po3-Tweaks/blob/47e8b72118b2e666af3c03fc3baaec8f73972f94/src/Experimental/UpdateGameTimers.cpp)'
  `Papyrus_SetGlobal` replaces that function with the same signature. The 1.6.1170 store is
  `movss [r8+34h], xmm3`; its constant guard stays in the original. The shipped GlobalVariable.psc implements
  Mod and the Value property through SetValue, so they reach it too.
- Console `set` and compiled scripts share interpreter operation 23329 (`0x37F270`; no community method name).
  Its three callers pass context, Script*, 32-bit opcode, target reference, length pointer, 32-bit bytecode offset,
  depth pointer, line, skip, scan; AL is success. Disassembly at 0x37EC7F-0x37ECC9 confirms all ten arguments;
  the prologue and both other callers agree. All are passed through unchanged.
- Opcode 0x15's global target starts with 'G' and a signed 16-bit one-based reference index (0x37F416-0x37F468).
  `ScriptAssignments::GlobalTarget` validates those bytes and excludes skip/scan calls. CommonLib's Script,
  SCRIPT_HEADER and SCRIPT_REFERENCED_OBJECT supply the asserted dataSize/data/reference-list/form offsets.
  The store at 0x37F557 writes that reference's TESGlobal value. The hook marks only that global after execution,
  not every global mentioned by the script; failed or unchanged writes compare equal and send nothing.
- A search for float stores at TESGlobal::value also found `StatsMenu::ModifyPerkPoints` (52538): beast-form
  perk spending/refunds add to default object 320/321 (werewolf/vampire available perks) directly. Its post-hook
  marks those two globals. The callers pass StatsMenu* in RCX and signed delta in EDX, ignore RAX; the body
  uses no other arguments. `GetDefaultObject` (11436, uint32_t -> TESForm*) keeps the engine's initialized check.
- GitHub/Nexus were searched first. po3-Tweaks was already in fetch_references.sh; its SetValue replacement is
  the applicable community implementation. The interpreter store and the beast-perk writer were then read in IDA.

## Actor values and death (2026-10-23)

`ActorValueEvents` watches the engine while HostEngine or GuestEngine lives. The 1 s full-value scan, 250 ms
death scan, 250 ms small-hit batch timer and 500 ms copy-death retry are gone. Only continuous pools retain
their 100 ms comparison; unchanged health/magicka/stamina and maxima are no longer re-sent every second.

- The write funnel is `Actor::SendActorValueUpdate` (38470, 0x6B2A40). Its seven direct callers are the actor
  value owner's SetBaseActorValue (38465), ModifyActorValue (38466), Actor::SetActorValueModifiers (38468),
  inventory weight, and three visitors that update stored actor values (including player/character 38509/38510).
  The setters and modifier function store first, then call it for a nonzero change. ModActorValue,
  RestoreActorValue/DamageActorValue and ValueModifierEffect/perk modifiers reach those writers.
- The hook is `void(Actor*, uint32_t value, float oldValue, float delta, Actor* source)`. The 1.6.1170 prologue
  saves XMM2/XMM3 and passes stack argument 5 to the value's sink; callers provide actor/value in RCX/EDX,
  old value/delta in XMM2/XMM3 and the source on the stack. The existing 38468 binding and
  [ActorValueGenerator's modifier hook](https://github.com/NoahBoddie/ActorValueGenerator/blob/c1d180d3e3c971deb6acabd7ed6886691e4d7992/src/Hooks.h)
  agree on that source. Community names: meh321's database; ActorValueOwner's slots: CommonLibSSE/NG.
  ActorValueGenerator was found through GitHub/Nexus first and added to fetch_references.sh.
- A queued `ActorValueChangeEvent` coalesces actor/value pairs. Only the named values are read; pools' current
  values wait for their tick while maxima follow writes. Non-pool messages retain current-before-maximum order:
  SetActorValue sets the base too, so reversing them can double a copy's permanent modifier. The fake now
  models that: a base write and a permanent modifier both change current and permanent values.
- Copies keep their owner's pool values from spawn and subsequent packets, even while the actor is absent.
  They force the maxima before the current values on a packet, a copy-side value write or ReferenceLoadedEvent.
  Writes compare in the engine; once converged, no further event or owner resend is needed.
- `TESDeathEvent` supplies both dying and final death (actor/killer pointers, isDead at +0x10; CommonLib's
  layout). Resurrect (37321, 0x665AD0) has a post-hook: `void(Actor*, bool resetInventory)`. CommonLib declares
  an additional attach3D argument, but this 1.6.1170 body consumes only DL, never the incoming R8; its virtual
  callers ignore the incidental RAX. The hook raises only when dead/alive changed and captures the previous
  dying action before the original clears it.
- DeathStateChange's request/notification payload now carries an optional owner death-action tick. A dying
  state with an action waits for `Actor::KillWithDeathAction` to report that exact replay attempt. The replay
  notification is raised even when the engine refused it, allowing the existing kill fallback immediately.
  A death without an action waits for the engine's final event, then applies without a retry timer.
- A resurrection carries the preceding death action too. Its desired state is kept after convergence: an old
  delayed action cannot kill the copy again, and a refused resurrection waits for the copy's final TESDeathEvent.
  A newer death action discards an older alive answer. A kill/resurrection in one owner update still sends the
  alive answer with the captured action tick. Copy health at/below zero is dropped from the stale pool cache
  while resurrecting, until the owner's fresh pool value arrives.
- The no-action death payload keeps its pinned bytes (the extra false bit uses its padding). A replay tick adds
  a presence bit and varint to both request and notification. HostAuthorityTests round-trip zero/large ticks
  and verify the relay preserves them; GameTests covers both arrival orders, refused replay/resurrection,
  rapid resurrection, later deaths, pool cap/current order, missing/reloaded copies, quiet pools and frame batches.
