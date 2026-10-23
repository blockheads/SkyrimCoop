# A guest's world scripts: events dropped, results from the host

Engine side of world-state sync step 5, slice 1 (`docs/design/world-state-sync.md`, 2026-10-21): on a guest,
the world's scripts no longer react to its player's activations and triggers; the host's game runs them
(for the guest's copy, party model rules 1-2) and its results reach the guest.

```
guest: player activates / walks in
  engine → Activate relay (ActivateRequest) / copy moves into the host's trigger
  VM SendEvent(world ref, "OnActivate" | "OnTrigger*") → dropped      WorldEvents.cpp
host: the copy's event runs the script (GuestScripts rules 1-2)
  door it opens (Activate by a reference)  ┐ ActivateEvent / ScriptAnimationEvent /
  animation it plays (PlayAnimation)        ┘ → HostService → HostEngine (queued)
  reference it enables, disables, locks     → change funnel (FormChanges) → HostEngine ┐
  container items in or out                 → ContainerEvents → HostEngine            ┴ HostWorldState: NotifyObjectChanges
  spell it casts on the copy                → AddTargetRequest to the owner    MagicTarget.cpp
  items it takes or gives the copy          → inventory changes (as before)
guest: GuestWorldState → GuestEngine::ApplyObjectChange (the door, the animation, enabled state, lock, items)
```

## Dropping the guest's events

| ID | Engine function | Our hook | Notes |
|---|---|---|---|
| 104800 | `BSScript::Internal::VirtualMachine::SendEvent(VMHandle, const BSFixedString&, IFunctionArguments*)`, vtable slot 0x24 (`VTABLE_BSScript__Internal__VirtualMachine` 252631, slot 0x18 is `BindNativeMethod` 104788) | `WorldEvents::HookSendEvent` | Looks up the scripts bound to the handle (`+0x93A8` map under the `+0x93A0` lock) and sends the event to each through vtable slot 0x27. |
| 54033 | `SkyrimVM::RelayEvent` | none | Sends a reference's event to its aliases (`EventRelayFunctor`) and magic effects (`EffectEventRelayFunctor`); both functors call `[vm+0x120]`, slot 0x24: the same hook sees them. |

The rule: a guest drops `OnActivate`, `OnTriggerEnter`, `OnTriggerLeave` and `OnTrigger` unless the
handle's form (its low 32 bits) is the player, 0x14. Read without engine calls: `SendEvent` runs on the
threads raising events. Off by `GuestWorldState` on joining, back on leaving (`GuestEngine::SetDecidesWorld`, with the
story-manager gate).

## Results back to the guest

Doors, animations and enabled state are world state like step 4's globals and quests: only the host's game sends them
(`HostWorldState`, `NotifyObjectChanges`, one ordered list a frame), only a guest applies them
Doors, animations, enabled state and locks are world state like step 4's globals and quests: only the host's game
sends them (`HostWorldState`, `NotifyObjectChanges`, one list a frame: the state the change funnel saw change, then
the scripts' events in order; `docs/design/world-state-sync.md`, "As built"), only a guest applies them
(`GuestWorldState`, `GuestEngine::ApplyObjectChange`). `ObjectService` keeps the relays every game makes
(players' activations, locks).

A guest applies each change under `ScopedHostChangeOverride` (`Games/Overrides.h`): the object hooks it goes
through (`HookActivate`, `HookLockChange`, the two animation hooks) raise no event
for it, so nothing the host sent goes back as a guest's change. Before, each kind avoided the echo its own
way (a call past the hook, a thread-local flag in the wrapper, a flag on the event), and a lock sent
back would have looped between the games (the guest's proposal, the host's change, applied again).

- **Doors a script opens** (`secretDoor.Activate(Self)`): `HookActivate` passed on actors' activations only.
  A door activated by a reference raises `ActivateEvent::ByReference`, which `ObjectService` leaves alone; on
  the host `HostService` queues it in `HostEngine` (door, activator reference, open state before). A guest
  activates its door with the same reference, if the door is as the host's was.
- **Script animations**: upstream turned them off in 2022 (`a312f9d9`, `OBJECT_ANIM_SYNC 0`, no reason
  given; both games played every script's animation then). On again as host results, the
  `ScriptAnimationRequest` / `NotifyScriptAnimation` relay deleted. The hooks' signatures checked in IDA: `papyrus::ObjectReference::PlayAnimation` (56205)
  and `PlayAnimationAndWait` (56206) take `(vm, stackId, self, BSFixedString*)` (the native copies its event
  from the pointer).
- **Enabled state** (step 6's first piece): any cause on the host (a script, a quest stage, the console) ends in
  the engine's `TESObjectREFR::Enable` (19800, `(this, bool resetInventory)`) or `Disable` (19801, vtable slot
  0x89; `Actor` overrides it, 37267). Papyrus `Enable`/`Disable` (56158, 56155, and the `NoWait` forms) queue an
  `EnableFunctor` / `DisableFunctor` (56492 / 56491) for after render, which fade and then call those two
  (functors 56110, 56113). Both flip the disabled flag (`0x800`) through `TESForm::SetDisabled` (14646), which
  records `CHANGE_FORM_FLAGS` (`AddChange(1)`): the host reads the state from the change funnel
  (`Forms/FormChanges`, `HostEngine`), not for actors (their owner's) or references the game created (`0xFF`
  ids). A guest enables or disables its reference at once (`EnableImpl`, `DisableImpl`), the host's fade
  already played. The `Enable`/`Disable` hooks (and `ReferenceEnabledEvent`) were deleted for the funnel.
- **Locks** (step 6): every lock change ends in `TESObjectREFR::AddLockChange` (19512, CommonLibSSE-NG), which
  records `CHANGE_OBJECT_EXTRA_LOCK` (`AddChange(0x1000)`; for a load door, on the door it leads to). On the host
  the funnel's change goes out as a `kLock` object change; on a guest its hook's `LockChangeEvent` is a proposal
  (`GuestWorldState` sends `LockChangeRequest`), which `HostAuthority::OnLockChange` gives the host's game
  (`HostGame::SetLock`, `HostEngine::SetLock`): the host's object takes it and every game gets it as the host's
  change. Applying calls `TESObjectREFR::SetLockState`, which records the change past our hook; on the host the funnel sees it. The
  old relay (`NotifyLockChange`, `ObjectService`'s lock handlers) is deleted.
  A guest proposes only its player's own: `AddLockChange` is also called by Papyrus `Lock`/`SetLockLevel` (scripts'
  `OnLoad`) and the engine's cell jobs (through 20226/20227: 39105, 39106, 19809, the cell code at `0x2CC7F0`), so
  every cell load recorded dozens on both games and the guest's would set the host's locks. The player's lock
  changes happen inside `ScopedPlayerLockOverride`: `LockpickingMenu::Unlock` (51968, `void (LockpickingMenu*)`,
  hooked; it records the unlock through 20226 before it opens the door) and the player's own `Activate` (a key,
  inside `TESObjectDOOR/CONT::Activate`); `LockChangeEvent::ByPlayer` carries it. The host still sends every one of
  its own (its world decides). `guest-loads-propose-no-locks` (control 2026-10-21: without the check the host took
  25 from the guest's loads).
- **Containers' items** (step 6, slice 1): below.
- **Spells the host's world casts on a guest's character**: `MagicTarget::AddTarget` dropped every effect on
  a copy whose caster was a copy or nothing, and every hostile one between players with PvP off. On the host,
  an effect on a guest's character goes to its owner when the caster is not an actor (a statue, a trap) or
  is the copy cast by a script running for a guest; PvP only covers a player's own casts.

## Containers: changes as counts

A container's items are world state like a lock: the host's change goes out as a `kItems` object change
{container, base item, count now}, a guest's own change (its player took or stored something) is a
`ContainerChangeRequest` with the same three, which `HostAuthority::OnContainerChange` gives the host's game
(`HostEngine::SetItems`): the host's container is set to that count and the change goes to every game as the
host's. Counts, not deltas: the guest's container changed before it proposed, so replaying a delta would
count it twice. Applying adds or removes the difference (`TESObjectREFR::SetContainerCount`) under
`ScopedInventoryOverride`, so it raises nothing (no echo). The player's own inventory stays theirs (actor
inventory sync, as before). Actors and references the game created (`0xFF`) are left out.

A container's activation is still relayed (`ActivateRequest` → `NotifyActivate`) and replayed on the other games
with the activating player's copy as the activator (`SkyrimBridge::ActivateObjectFromActor`). Read in IDA
(2026-10-23) for `chest-take-all`'s "the host's Take All opened the guest's container menu":
`TESObjectCONT::Activate` (17887) opens the menu (or the lockpicking menu) only for the game's own player
(`gPlayerCharacter3`); for another actor it plays the open sound, takes the named item (the replay names none),
and calls `Actor::StealAlarm` if that actor does not own the container. `TESObjectREFR::ActivateRef` (19796) sends
`OnActivate` (a guest drops it, `WorldEvents.cpp`) and records the activator, and hands nothing to the player. So
the replay cannot open a menu. What did (measured in Skyrim 2026-10-23, fixed the same day): the host's open state
reaching the guest (`ReferenceStates::ApplyOpen`, `BGSOpenCloseForm::SetOpenState` 14287). An animated lid's
toggle (`BGSOpenCloseForm::OpenOrClose`, 14284) records its activator in `ExtraOpenCloseActivateRef` (0x68,
set by 11802) and acts on the lid's `Open`/`Close` event (14285): the recorded activator gets the base's
`HandleOpen`/`HandleClose` (a container's opens the menu for the game's player), then it is cleared. The guest's
own close records its player; the host's open, arriving late (its copy replays the guest's activation), interrupts
that lid before its `Close` event, so its `Open` event finds the guest's player and opens its menu again.
`SetOpenState` without its snap clears nothing. `ApplyOpen` now removes the record before playing the host's open
or close (`ExtraDataList::RemoveByType`, 12354): it is nobody's activation in that game. Only Skyrim shows it
(the fake has no lids or menus); `chest-take-all` is its scenario.

The replay's `StealAlarm` (an owned chest, measured the same day): it runs once on the guest with the host's player
copy and returns through the non-player branch before any crime processing; no bounty in any hold.

```
host or guest: an item into or out of a container (menu, console, script, Take All)
  engine add / remove → TESContainerChangedEvent (16149) → ContainerEvents sink → ContainerChangeEvent (queued)
  host:  HostEngine::RecordItems → kItems {container, item, count now} → NotifyObjectChanges → guests
  guest: GuestEngine::ReadContainerItems → ContainerChangeRequest → host: SetItems → kItems → every guest
  guest applying kItems: SetContainerCount (count now − count here; 0: nothing)
```

| ID | Engine function | Notes |
|---|---|---|
| 16149 | `InventoryChanges::SendContainerChangedEvent` (CommonLibSSE-NG) | Sends `TESContainerChangedEvent` {old container, new container, base item, count, reference handle, unique id} to `ScriptEventSourceHolder+0x318` (`EventDispatcherManager::containerChangedEvent`). Called from the add (`sub_140226A20`) and the remove (`sub_1402260A0`), skipped while `InventoryChanges+0x19` is set (`InitLeveledItems` 16129 sets it while rolling). Synchronous: the sink runs inside the change, on its thread. |
| 19708 | `TESObjectREFR::AddObjectToContainer` | Hooked (`HookAddInventoryItem`): `(this, TESBoundObject*, ExtraDataList*, int32 count, TESObjectREFR* from)`, checked. Inlines the same add as 19688. |
| 19688 | `TESObjectREFR::sub_*` (no community name): the add itself | `MakeInventoryChanges` then the add (`sub_140226A20`). The console's `AddItem` (22000) on a non-actor and `Actor::AddObjectToContainer` call it, past our 19708 hook: why containers are seen through 16149, not the inventory hooks. |
| 16039 | `TESObjectREFR::GetInventoryChanges` (our name) | The reference's `ExtraContainerChanges` data, none made. Papyrus `GetItemCount` (56173) reads with it. |
| 16040 | `TESObjectREFR::MakeInventoryChanges` | The same, made empty when missing (no leveled roll). `GetContainerChanges` calls it: our count reads use 16039. |
| 16108 | `InventoryChanges::GetItemCount` (CommonLibSSE-NG; IDA had `GetCountDelta`) | The base container's count (14529) plus the changes' entry count (`TESBoundObject` 16214). |
| 16038 | `TESObjectREFR::InitInventoryIfRequired` | Rolls a container's leveled entries (`InitLeveledItems` 16129) into its changes when first needed; each entry rolls once (the changes' extra lists mark it), so changes made earlier (a script's `AddItem` into an unopened chest) do not stop it. |

A container nothing touched yet has no changes: its count is its base container's (14529, an `int`), leveled
entries not rolled. The guest applies to it the way the engine's own `AddItem` into an unopened chest would.

**The roll (2026-10-21, IDA 1.6.1170).** `InitInventoryIfRequired` (16038, `(ref, ignoreContainerExtra)`) does
anything only for a container with a leveled entry (form type 53) or an actor: makes the changes if missing,
`InitLeveledItems` (16129), `InitFromContainerExtra` unless told not to, `InitScripts`. `InitLeveledItems` sets
the changes' +0x19 (no container events meanwhile), and for each leveled entry whose index no extra list in the
changes marks yet, rolls `TESLeveledList::CalculateCurrentFormList` at the owner's level: an actor base's level,
else `TESObjectREFR::GetCalcLevel` (the encounter zone's). The owner is the changes' +0x8. Two games rolling the
same chest get different items (the inn's merchant chest `78C0F`: 27 items, most counts differing).

## Script casts: Spell.Cast is delayed

| ID | Engine function | Notes |
|---|---|---|
| 55747 | `papyrus::Spell::Cast` | Queues a `SkyrimScript::CastFunctor` (spell `+0x10`, source `+0x18`, target `+0x1C`, blame `+0x20` handles, the calling stack id `+0x0C`) and returns. |
| 53954 | `SkyrimVM`: queue a delayed functor (`BSTSmartPointer<DelayFunctor>*`) | Called inside the native: GuestScripts tags a `CastFunctor` with the native's guest here (`HookFunctorQueued`). |
| 55744 | `CastFunctor::operator()` (vtable slot 1, `Variable operator()()`: `(this, Variable* out)`) | Runs later, after the script may have ended (its stack's tag gone); calls the caster's `CastSpellImmediate` (`MagicCaster` vfunc 1), which reaches `AddTarget` at once. `HookCastRun` carries the functor's guest on the thread meanwhile (`GuestScripts::NativeGuest`). |

`DelayFunctor` (CommonLibSSE-NG): `stackID` at `+0x0C`. Other delayed natives are not tagged (only casts
needed it so far).

## Found along the way

- `MagicTarget::AddTargetData::caster` is a `TESObjectREFR*` (CommonLibSSE-NG, `+0x00`), not an `Actor*`:
  `HookAddTarget` called `GetExtension()` on a statue's or trap's reference. Fixed with `Cast<Actor>`.
- `TempleBlessingScript`'s blessing is neither a healing nor a buff spell to `IsHealingSpell` /
  `IsBuffSpell`: the PvP check took it for a hostile one.
- The Soljund statue's Firebolt from the host now burns the guest's player (`magic_effect_apply`, effect
  `0x12F03`, caster 0 on the guest); a host NPC's arrow (`0x335F3`) hit it in the same run.

## Tests

`chest-take-reaches-others`: the host puts two sweetrolls in a QASmoke chest (`C2CE8`), the guest's chest holds
them; the guest takes everything through the container menu (R, Take All): the host's chest holds what the
guest's does and the guest's player has the sweetrolls once; the host puts one back and takes everything: the
guest's chest follows. Controls 2026-10-21: with the host's `RecordItems` off it fails at "[client] chest C2CE8
holds 2 sweetrolls"; with the guest's proposal off at "[host] chest C2CE8 holds what the client's does". The menu
drops input while its lists slide in (`InventoryLists.currentState` 3-5, harness `menu_var`); the scenario waits
for 1 or 2. Harness `container_items` lists a container's items (base entries but leveled lists, and changes). The
console does not reach QASmoke's `04xxxxxx` references (`04026B71.disable` does nothing).


`sword-box-asks-client` (the guest's sword goes once and comes back once; its secret door opens from the host's
change; it sees the box and the sword in the stone swap both ways: with the host's recording off, the guest's
sword never appears, control 2026-10-21), `statue-fires-at-client` (the guest's trigger never polls; the host's
fire burns its player), `ring-turns-for-client` (the guest's ring script state stays), `shrine-blesses-client`
(the guest's blessing comes from the host's script), HostAuthorityTests "the host's world objects' changes reach the
others in order, once". Harness `ref_enabled` reads a reference's enabled state.
