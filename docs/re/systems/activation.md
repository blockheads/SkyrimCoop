# Activation: the player's screen and the world

Which part of an activation changes the world and which only the activating game's screen: one-simulator.md's open
question 4, for piece 6. Read in IDA on 1.7.104 (2026-11-20) unless a line says measured or inferred.

## The rule the engine already follows

`TESObjectREFR::ActivateRef` (19796) runs the steps every activation shares, then the base form's
`TESBoundObject::Activate` (vtable slot 0x1B8 / 55). Where a form's `Activate` has a screen part it branches on
`activator == gPlayerCharacter3` (this game's player): door, container, furniture, book, talking activator, note,
produce. **The player's branches are the screen** (menus, its dialogue, its sitting); **the rest is the world.** The
activator (17702), items (17670) and lights (17615) have no such branch: theirs is all world but a sound. So a game
that runs `ActivateRef` with another player's copy as the activator runs only the world part. A copy is never
`gPlayerCharacter3`.

```text
ActivateRef 19796 (this, activator, a3, objectToGet, count, defaultProcessingOnly)
  global activation lock (0x1431DF340, recursive)
  refusals: disabled; a nameless door/object; parent-activate-only and the activator no parent   (FindActivateParent)
  unless defaultProcessingOnly or the activator fades out:
    TESActivateEvent  -> Papyrus OnActivate (a guest drops it, WorldEvents.cpp)                  world (scripts)
    ExtraAction kActivate + action ref = activator (conditions' GetActionRef)                    world
    Character::37864 (mounts: board, dismount)                                                   pieces 4-5
    ExtraFlags kBlockActivate -> stop: the script decides everything                             world (scripts)
  an NPC activator: base flag 0x100000 or 19798 allows it
  STAT, SCOL, MSTT, IDLM: refused
  InitChildActivates 19428: its activate-parent children (gates, traps, linked doors)            world
  base->Activate (per form, below)
  done: DispelAlteredStates(activator) (invisibility ends), the activated list                   the activator's own
```

## Per form

- **Door** (`TESObjectDOOR::Activate` 17922):
  - Locked, with the key: unlock + `AddLockChange` (world), then `ActivateRef` again.
  - Locked, no key: the player gets `LockpickingMenu` (screen). An NPC with a lockpick unlocks it (world, `TrespassAlarm`).
  - Then `BGSOpenCloseForm::OpenOrClose` 14284: open state and animation (world).
  - Load door: the player's own → sound and mist menu. An NPC is moved to the destination
    (`SetPosition`, 37438): **a load door must never run with a copy as the activator**.
- **Container** (`TESObjectCONT::Activate` 17887):
  - Locked: the player gets the lockpicking menu. Any other actor unlocks it with no key and no check: a copy's
    replay of a locked chest would unlock it.
  - Unlocked: the player toggles the lid (`OpenOrClose`, whose `Open` event's `HandleOpen` opens the
    `ContainerMenu`: screen and lid). Another actor takes `objectToGet` and raises `StealAlarm`, no lid.
- **Activator** (`TESObjectACTI::Activate` 17702): no branch on the player. `TrespassAlarm` (not an owner and
  detected), its sound, its water's `Activate`, `OpenOrClose` (a lever's pose). All world but the sound.
- **Furniture** (`TESFurniture::Activate` 17420): only for this game's player (sit, sleep menu, crafting). Returns 0
  for a copy, but `ActivateRef` still sends its `OnActivate`. One of its player branches is world, not screen: a
  **lever furniture** (below).
- **Flora, tree** (`TESProduceForm::Harvest` 14864):
  - Any actor: the produce goes into the activator's inventory, the harvested flag is set (`0x2000`,
    `CHANGE_OBJECT_EMPTY` 0x200000, no row yet) and the switch node is posed.
  - Only this game's player: `TESHarvestedEvent`, the steal alarm, the sound.
  - The flag alone is `SetHarvestedFlag` (14647); the model follows it in `UpdateHarvestModel` (14865, SKSE's name),
    which `Load3D` calls too. Since 2026-11-20 a harvested row carries it (`ReferenceStates`, `kHarvested`): no game
    replays another's activation any more, so without it a watching game kept the plant (the review of PR #87).
    Measured 2026-11-20 in Riverwood: the guest's pick (`EC85C`) and the host's (`ECE8D`) each set flag 0x2000 and
    `CHANGE_OBJECT_EMPTY` on the other game ("harvested ECE8D" applied); the model's pose was not read back.
- **Book** (17840): the player gets `BookMenu` (screen). Any other actor picks it up.
- **Talking activator** (17183), **note** (23764): only this game's player.
- **Items, lights** (17670; `TESObjectLIGH::Activate` 17615 for a carryable light): the activator picks it up (`Actor`
  vfunc 0xCC0). Loose items
  have their own claim (`PickUpRequest`, `HostItems`).

## Lever furniture (playtest 2026-11-22, item 11)

A furniture whose keywords hold default object 182, `KeywordActivatorFurnitureNoPlayer` (CommonLibSSE-NG), is an
activator the player does not enter: Bleak Falls Temple's pillar puzzle lever `AA8C3` (base `B7DB4`, the only one in
`BleakFallsBarrow01`, measured with harness `objects`' `lever`). Read in IDA 2026-11-23 (journal `agent-levers-synced.tsv`):

```text
TESFurniture::Activate 17420 (furniture, ref, activator)
  activator != gPlayerCharacter3 -> return 0             a copy's pull stops here: the lever never moves
  ... (sit marker, sleep, a bed: the player's own)
  keywords (+0x90, vfunc 4) hold default object 182:
    BGSActionData: actor slot = the furniture ref, action = ActionActivate (default object 55), no target
    coop_PickIdleForAction 24602 (global 400451): the furniture's own idle tree picks the idle, its event (+0x28)
    the event -> the ref's own graph (IAnimationGraphManagerHolder +0x38, vfunc 1, NotifyAnimationGraph)
```

Not Papyrus `PlayAnimation`, so `HookPlayAnimation` never saw it and no row carried it either way: a guest's pull
moved only its own lever (the host's copy's activation returned 0; its `OnActivate`, the darts or the door, did run),
and the host's pull moved only the host's. The fix runs the same path: `TESObjectREFR::IsActivatorFurniture` (the
keyword) and `PickActivatorFurnitureEvent` (24602 on fresh action data, as 17420). The host's game, rerunning a
guest's activation (`HostEngine::Activate`), picks the pull, sends it to the lever's graph and records it as a
`kAnimation` row with the puller's character (`Value`); the host's player's own pull is picked in `HookActivate`
before the engine's own (`ActivateEvent::FurnitureEvent`) and recorded the same way. A guest applies the row unless
the puller is its own player (`GuestWorldState`: its engine already played it). Measured 2026-11-23 on the duo (scenario `guest-pulls-lever-furniture`, harness `anim_trace` on `AA8C3`): the guest's
pull reached its lever as input `Lever_PlayerPush` from `26F598` (the notify in 17420), the host's 14 ms later from
our replay; each lever then raised `ObjectActivated` and `LeverPushReset`, and neither got the input twice. The host's
pull, the same the other way. An NPC working a furniture lever
(Hod's sawmill lever `1B752`) enters it through AI: its copy's graph events reach it as a dependent graph
(`furniture-graphs.md`).

## Locks

- `AddLockChange` (19512) records `CHANGE_OBJECT_EXTRA_LOCK` and sends `TESLockChangedEvent` on the reference holding
  the lock. That is itself if it has its own `ExtraLock` (20224). A load door without one uses its linked door's
  (`GetLock` 20223, `GetLockData`, through `ExtraTeleport`), and the change is recorded on that door, in another cell.
- The host's NPCs lock and unlock the doors they own:
  - `UnlockOwnedDoor` 39105 (from `Actor::EvaluatePackage`, `CheckForNewPackageDefault`,
    `BGSProcedureLockUnlock`, papyrus `UnLockOwnedDoorsInCell`) and `LockOwnedDoor` 39106 (from `SetParentCell`,
    `MoveToMiddleHigh`).
  - Each goes through 20226 / 20227 (set the lock flag, `AddLockChange`) and the cells' public state (0x191C00).
  - A guest's NPC copies run no AI, so only the host's game ever calls these.
- "Doors the host's NPCs unlock" (fixed 2026-11-20). Measured on the duo with harness `door_lock`:
  - Whiterun's house door `1A6F5` has no lock of its own. Its lock is the inside door `16607`'s.
  - On the host `16607` was unlocked with `0x1000` recorded; the guest's was locked with nothing recorded. The
    bug's flag `0x10000000` is `CHANGE_REFR_ANIMATION`.
  - The host's unlock came before the guest joined, so no live row carried it. A guest asks for each cell's state
    as it loads, and `16607`'s interior is never one of them.
  - Live rows for inside doors do reach a guest: they are persistent, in memory with their cell unloaded.
  - The fix: the host's cell state sends a lock row for a load door whose linked door's record holds a lock change
    (`ReferenceStates::LinkedLockRows`), and a guest's cell request names such a door the same way (its change while
    away). The guest applies it through `SetLockState` → `GetLock` on the linked door. "Its own lock" is as 20223 and
    19512 judge it: an `ExtraLock` with a lock pointer.
  - Tests: GameTests "A load door the host's NPC unlocked while a guest was away is unlocked when it joins again";
    scenario `late-join-gets-load-door-lock` (control 2026-11-20: without the row it fails at "[client] loaded
    Whiterun with its own lock on 1A6F5").

## Leveled containers

`InitInventoryIfRequired` (16038) is called by `InitItemImpl` (19507) for every reference set up at load. Its
other callers: `CreateReferenceAtLocation`, `ResetInventory` 19802 and `Actor::SetOutfit`. Each game rolls its own
chests when its cell loads. The roll records no container events (`InitLeveledItems` 16129 sets `+0x19`), so no
row carries it. That is "The host's roll for a guest's loot". Opening or activating a chest rolls nothing.

## References

- SkyMP (`reference/plugins/skymp`): its guests block their own activation (`blockActivation`), send `Activate`,
  and the server runs it. A container's menu is sent back to the activating player (`SendOpenContainer`). Its server
  reimplements every form's activation; we run the host's engine instead.
