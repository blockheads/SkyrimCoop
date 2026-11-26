# How items enter and change a carried stack (1.6.1170)

Checked in IDA 2026-10-30 for party items ([party-items.md](../../design/party-items.md)).

| ID | RVA | Name | What |
|---|---|---|---|
| 56349 | 0xa36f40 | `ConcreteDelayFunctorFactory<AddItemFunctor>` | `ObjectReference.AddItem` does not add inside the native: it queues `SkyrimScript::AddItemFunctor` (a `DelayFunctor` carrying the calling stack's id at +0x0C), as `MoveTo` does |
| 56095 | 0xa28b40 | `SkyrimScript::AddItemFunctor::Run` (vfunc 1) | The add itself, later on the game thread. The calling script's context (`GuestScripts::NativeScript()`) is gone by then: tag the functor when it is queued, read the tag in the run (the `HookCastRun` pattern) |
| — | 0xa2c110 | the `ObjectReference.AddItem` native | Builds the `AddItemFunctor` and queues it with `SkyrimVM::QueuePostRenderCall` (53955), as `MoveTo` does, not `QueueDelayFunctor` (53954). Corrected 2026-10-30: the story mark was first only in the 53954 hook and never fired in Skyrim. The bookkeeping is now `Papyrus/StoryGifts` (engine-free): both queue hooks and the fake engine's script `AddItem` (post-render queue) call it. GameTests `[party-items]` went red on the old rule; UnitTests `[StoryGifts]` |
| 56355 | 0xa37110 | `ConcreteDelayFunctorFactory<RemoveItemFunctor>` | `RemoveItem` is queued the same way |
| 16145 | 0x2395e0 | `InventoryChanges` temper (ours) | `(changes, object, extraList, health)`: splits one off a counted stack (new `ExtraDataList`, count 1) and sets its health (tempering). From `SmithingMenu` 51291 → `sub_14090C850`. In place: no remove/add, so our inventory hooks do not see it |
| 16146 | 0x2398a0 | `InventoryChanges::EnchantObject` | `(object, extraList, enchantment, charge)`: the enchanter's craft, in place in the same way |
| 51415 | 0x9100d0 | `EnchantConstructMenu::RenameItem_Impl` | The enchanter's rename of the new item: a custom name (`ExtraTextDisplayData`) |
| 376348 / 376351 | 0x2008aa0 / 0x2008ab8 | `sAddItemtoInventory` / `sRemoveItemfromInventory` | `SettingT<GameSettingCollection>` (vtable, value, name): "Added" / "Removed", the HUD notice's words |

A party item's improvement (temper, enchant, rename) therefore needs hooks on 16145, 16146 and 51415, not the
inventory add/remove hooks.

Decompiled 2026-10-30 (second pass):

- 16145 and 16146 share one shape: `(InventoryChanges::list, TESBoundObject*, ExtraDataList* source, ...)`. A
  source counted above 1 is split (its count - 1, a copy of its extra data with count 1); no source makes a new
  list. Then `SetHealth` (16145, the float in xmm3) or `ExtraDataList::SetEnchantment(list, enchantment, charge, 0,
  -2)` (16146), and the list is added to the object's `InventoryEntryData` (made if missing). 16146 returns the list.
- 51415 `(menu, InventoryEntryData*, ExtraDataList*, const char* name)`: sets the name only when the list has no
  `ExtraTextDisplayData` name yet. A `[P] ` copy keeps its tagged name through enchanting, so it stays the party's.

Removing (2026-10-30): `InventoryChanges::RemoveItem` (16059) takes from a stack with extra data only when given that
stack's own `ExtraDataList`. A list rebuilt from our entry, with the same data types (harness `stack_extras`: custom
name 153, health 37), removed nothing on a fresh session. `TESObjectREFR::FindCarriedStack` finds the carried list.

Worn on add (2026-11-01, S7): `TESObjectREFR::AddOrRemoveItem` puts a worn entry on with the engine's equip (38894).
With `abForceEquip` set (Papyrus `EquipItem`'s `abPreventRemoval`) the player can neither take the item off nor
replace it from the inventory menu, while console `equipitem` still replaces it. Every join dresses the player this
way (`JoinBridge` → `SetActorInventory`), so after joining, a guest's menu equips of body armor and boots did nothing
(the playtest's Hide Armor and Hide Boots over the kept Clothes and Boots). It now equips normally.
`guest-swaps-armor-after-join` shows it.

A fresh roll (2026-11-01, S5): a container's leveled entries rolled by `InitInventoryIfRequired` (16038) hold each
item in its own extra data list with `ExtraLeveledItem` (0x2E; gold also `ExtraCount`), while the changes' entry
for the item counts 0: the item counts through the base container's leveled list. `InventoryChanges::GetItemCount`
(16108, `GetContainerCount`) gives the true count; `TESObjectREFR::GetInventory`, which subtracts each list from the
entry's count, reads such an item as none. After a save and load the items read as usual (a playtest guest could
loot the same chest then). `HostEngine::TransferContainer` counts a plain request with 16108 and removes through the
matching list (`RemoveItem` 16059 takes from a list only when given it). `GetInventory` itself still reads a fresh
roll as empty.

What 16059's target (`apMoveToRef`) does, read 2026-11-19: the removed count goes to it (`AddObjectToContainer`,
vfunc 0x5A) with the list, and only with a target do the reasons mark the list: `kSteal` (1), not gold, stamps
`ExtraOwnership` with the source's owner (an actor's original leveled form, else its base; else the reference's owner): that stamp is
what makes the item stolen. `kStoreInTeammate` (5) from the player stamps the player; `kSelling` (2) raises the
unique-id event. No crime is reported in 16059 (the alarm is `StealAlarm`'s, crime.md). Without a target the count
only drops. So a guest's container theft stamped the host's copy of the guest, whose items the guest's part overwrites,
and the grant carried no owner: the guest's own player held the item unstolen.

Since 2026-11-19 `HostEngine::TransferContainer` gives 16059 no target: a take only leaves the container, a put only
arrives in it (`AddOrRemoveItem`), and the player's own game changes its player. A theft's grant names the owner
(`Inventory::Entry::ExtraOwner`, `TESObjectREFR::GetItemsOwner`: an actor's original leveled form, else its base,
else `TESObjectREFR::GetOwner` 20194 with its door, zone and cell fallbacks), and the looter's game sets it on the item
(`ExtraDataList::SetOwner`, 11609).

A leveled actor's NPC is a temporary form each game makes, but its theft's owner is not: `ExtraDataList::GetLeveledNPC`
(11810) reads `ExtraLeveledCreature`'s first field, the original form the actor was placed from (`LvlBanditBoss`
`3DF17`), a form every game has. Measured: the guest's sweetroll pickpocketed from a placed bandit chief was owned by
`3DF17` (`guest-pickpockets-leveled-npc-stolen`). Only an owner that is itself a created form (none seen) is not sent.


## A roll's taken item (2026-11-20, IDA 1.7.104, measured)

`guest-loots-hosts-roll` failed 2 of 3: the guest's Take All of a chest the host had freshly rolled moved the gold
and none of the items ("take 1 of <item>: 1 there, 0 moved"). Measured with `inventory_entry` before and after: the
item's entry went from 1 to 0 and its roll list stayed, now with `ExtraCount` (types 46, 36), while the count read 1.

- `InventoryChanges::RemoveItem` (16059), given a roll's list (`ExtraLeveledItem`, checked by 0x140164830), lowers the
  entry by the count asked, keeps the list with a count set, and moves a copy (to the target, or freed with none).
- At its end an entry left at 0 is freed, except on a non-actor whose list was a roll's: a chest keeps the 0 entry
  and the list, which marks the leveled index rolled.
- `InventoryChanges::GetItemCount` (16108 → `TESBoundObject::GetCountDelta` 16214) is the base count plus the entry's,
  but reads an entry at 0 over no base count as **one**. So the taken item still counted: `TransferContainer` saw
  nothing taken (the guest got nothing), and the host's row said "holds 1", putting it back in the guest's chest.
- Inferred, not measured: after the cell unloads and loads, the roll's lists are gone and the 0 entry is freed, so the
  take reads right. That would be why the run on the first run's leftovers passed (its take was logged "1 moved").

`TESObjectREFR::GetContainerCount` now counts as CommonLibSSE-NG's `GetInventory` does (what the menus show): the base
count unless the entry is a roll's, plus the entry's, with no special case (`engine/ContainerCount.h`, UnitTests
"A roll's item taken from a chest counts none, not one"). `guest-loots-hosts-roll` 3 of 3, 3 different fresh chests.
GameTests "A guest takes a fresh roll's item: it gets it and the chest is empty on every game": the fake's chest keeps
a roll's entry at 0 and counts through the same `ContainerCount` (control: with 16214's rule it never gets the item).

# 1.7.104: an actor's items as its owner's part (2026-11-19)

Read in IDA for one-simulator.md piece 3 (names and comments in the database, journal
`docs/re/journal/1.7.104/agent-piece-3-items.tsv`); measured on the duo with the harness readout `inventory_entry`.

## Removing (read)

`InventoryChanges::RemoveItem` (16059). Given a list, it takes from that list. Given none (past `LABEL_286`), it takes
only the loose count: base container count + entry delta, minus every list's count (the non-stackable ones summed there,
the stackable ones by `InventoryEntryData::CountStackableListsPruned` 0x14022AF40). So an item held in a list is never
taken by a plain removal. `SetCarriedStacks` (Bridges/ContainerItems) removes through a plain list when the loose count
runs out.

## A base container's leveled lists (measured)

A bandit (`3DE8D`) lists three leveled lists in its base container (`37C1B`, `4F78D`, `8FFFB`, count 1 each).
`inventory_entry` on the owner: no changes entry, count 1 (16108 counts the base entry). Our
`TESObjectREFR::GetInventory` lists base entries as they are, leveled lists included, so the spawn's inventory carried
the three forms as items; the copy's `SetActorInventory` (Papyrus `RemoveAllItems`, then adds) left the base entry and
added a changes entry: count 2 on the copy. `GetContainerItems` (the harness's `container_items`) skips a base leveled
list, so it listed the copy's (a changes entry) but not the owner's: "0 (host) vs 2 (guest)". This was "A looted corpse
keeps items on the guest": the loot itself was fine. The items part leaves leveled lists out (`ReadItems`).

## Worn items on a 3D load (read)

```text
Update3DModel_Impl 39395 -> TESNPC::InitWornOn3D 24725 (npc, actor, biped, force)
  no 3D yet or force:
    TESNPC::ShouldResetWornOn3D 24384   saving loading: false only with change 0x8000020 recorded
                                         else: living (IsDead, vfunc 0x99, false) -> true; dead: no change 0x400
    or byte npc+0x242 / form 7 with nothing worn (ShouldRunDefaultWornVisitor)
  -> TESNPC::ResetWornToOutfit 418622
       InventoryChanges::UnequipAllWorn 16100   each worn list off in the changes (16067): no unequip hook
         16067: a list with ExtraCannotWear (0x3D) on a living actor is skipped: it stays worn
       the hands cleared (ActorProcess 39811-39813)
       the base's outfit (npc+0x218): InitOutfitItems if missing, AddWornOutfit
       kPlayerTeammate (boolBits +0xE8, 1 << 26): per biped slot the best armor carried (BestArmorForSlot)
       a4 and living: the best weapons carried (16088), a bow's or crossbow's ammo with it
```

`ExtraCannotWear` is the lock an equip with `forceEquip` puts on the worn list: `EquipNow` (38001) passes
`EquipData+0x21` to `Actor::AddWornItem` (38004), whose list marking sets it (`ExtraDataList::SetCannotWear`); the
console's and Papyrus's `EquipItem` set it for `abPreventRemoval`. A copy's equips (`SkyrimBridge::ActorEquipItem`)
force, so 418622 leaves what we put on a copy worn; it takes off only what the copy's own engine put on.

418622's other callers: `Actor::ResetInventory` (copies skip it, `HookResetInventory`), `Actor::InitLoadGame`,
`SetLevel`. Our hook on 418622 raises `WornResetEvent` after it: a copy wears its part again, an owner reads its items
again.

Measured on a copy with the harness's `reset_worn` (418622 called as 24725 calls it, its worn set read as it returns):

- Aela's copy lost the torch its owner wore (not in her outfit), and its hands were emptied (`state.hands` [0, 0]
  against its owner's torch and bow) while the bow's worn flag stayed. The process's hands are cleared by 418622's
  `ActorProcess` calls (39811-39813, read only as calls). So a copy's repair also checks that each hand holds the
  weapon or torch its owner wears there (`EquipmentRepair::HandsOf`; both hands for a two-hander, hand-slots.md).
- Lydia's copy kept an iron helmet her outfit lacks, and her sword: both were equipped on the copy by its repair,
  forced, so locked (above). Aela's torch was lost all the same; why a forced light comes off is not read.
- A player copy's round trip to another cell (Riverwood inn and back) kept its worn set: no strip logged.

## A copy moved to its owner's cell (measured)

`CharacterService` moving a copy to where its owner is ("not where its owner is: moved there") dropped its left hand's
spell (`copy-holds-spell-after-trip`, as copy-hand-spells.md found), and no 3D load event reached the copy's part
afterwards. The move now raises `WornResetEvent` itself, and a part that finds its copy without 3D applies once the 3D
is in (`InventoryService::m_waiting3D`); a worn set or hand that does not settle is applied again every 0.25 s for up
to 5 s (a hand spell needs the copy's process, copy-hand-spells.md).

## Queued equips (read)

`ActorEquipManager::EquipObject` (38894) -> 38929 (our `EquipHook`). With `EquipData+0x20` (`bQueueEquip`) set,
`Character::QueueOrEquipNow` (37684) queues armor, weapons, ammo, lights and spells on the middle process
(`AIProcess::QueueEquipItem`, `itemstoEquipUnequip` +0x188) unless a menu is open, a save is loading or the process is
above high; else `Character::EquipNow` (38001). `ActorEquipManager::EquipEntryQueued` (0x1406DC220: `PickUpObject`,
`DropObject`, `StartCombat`, AI packages) always queues. `AIProcess::ApplyQueuedEquips` (0x140720D90, from
`Job_Non_render_safe_AI`) drains the list through the public `EquipObject`/`UnequipObject` with `abQueueEquip` 0, so
every queued equip passes our hook again when it lands: the owner reads its items then, no timer needed.

## A worn item removed from an actor with no process: the list freed twice (2026-11-22)

Read in IDA on 1.7.104; the free and the stale use measured on the duo (`guest-ivarstead-fine-mouse-sweep`, heap
history and quarantine on, 2 of 2). Searched `reference/plugins` (EngineFixes, po3-Tweaks, PapyrusExtender): none
patches 16059, 19694 or 16067 (searched by their IDs).

```text
InventoryChanges::RemoveItem 16059, a stack that is worn (stackList = its ExtraDataList, 32 bytes)
  listDiesWithWorn = the list has 1 extra, or 2 and (a count above 1, or 0x140164840's extra, or
                     type 142 the leveled item base: 0x14022f469, 0x1402301a7)
  owner has an AIProcess   ActorEquipManager::UnequipObject 38901
                             returns EquipData+0x24: "the list is gone" (38920 <- 38002 <- 38005 <- 16067 == 0)
  owner has none           19694: vfunc 0xA1 UnequipItem (Actor 37975), then ALWAYS returns 1
                             16059 reads 1 as "the list survived"
  survived and not listDiesWithWorn  ->  stackList kept, passed to a nested RemoveItem (0x1402304ce)
    its entry (0x14022dca8): a list with no extras is destroyed and freed

InventoryChanges::UnequipWornList 16067 (under 37975 and 38901): un-worn, the list is freed (0x140232895) when
  nothing is left on it, or when it merges into an equal list of the same entry
```

So for an owner with no process, a worn list with two or more extras that 16067 frees is used again by 16059, and
freed a second time: the 32-byte free list then hands one block to two objects. An actor has no process while
disabled (19801 frees it).

- **Measured:** the guest, as the host loaded a save: `CharacterService::OnDisconnected` -> `EnableActor` on a held
  copy we had disabled (`19E08`) -> `Enable` 19800 -> `ResetInventory` 37322 -> 19802 -> 16038 -> 19690 -> 37797 ->
  `RemoveItem` 19689 -> 16059 -> (nested) 16059 -> `ExtraDataList` extras count 12336 on the list. The list was freed
  by 16067+0x56a (from `Actor::UnequipItem` 37975) on the same thread, 0 ms before. With the quarantine the read
  hangs on the list's lock (`0xDBDBDBDB`); the list had been made by `AddWornItem` 16066 under our equip
  (`InventoryService::ApplyWorn`) 44 s earlier.
- **Read:** at 92960 and 91958 (`MovementMessagePathFailed` and `MovementMessageActivateDoor` constructors, 32
  bytes) the faulting `this` is what `MemoryManager::Allocate(32)` returned (90880), a vtable's address: a free
  block's link overwritten, so those crashes are the heap's victims, not stale pointers of their own.
  **Inferred:** that the six crashes of 2026-11-22 (bugs.md's "A guest crash through a pointer that holds a vtable",
  now removed) were all this worn list. What is measured: one free and a stale read under the quarantine, and one
  crash of the same shape from this path with the hook off (below), with no second free recorded.
- **Measured, then read:** the list held three extras, `[142 leveled item base, 22 worn, 61 cannot wear]`, beside an
  unworn `[142]` list of the same item (copy `19E08`, item `1BE1A`; a log line at 37975, 3 of 3 runs hung). 16059
  counts three and expects it to survive. The unequip strips `ExtraCannotWear` first (38920 at `0x1406de0ca`,
  `ExtraDataList__SetCannotWear` off), 16067 takes the worn extra, and the `[142]` left merges into its twin: freed.
  The copy's `ExtraCannotWear` is ours: our equip of a copy's worn set forces it (16066's `forceEquip`).
- **Not run:** a plain game. The path has no code of ours, but needs a disabled actor, a worn stack marked cannot
  wear (a script's `EquipItem` with `abPreventRemoval`) with an equal unworn twin, and an inventory reset; the
  vanilla stand-in has no heap tools to show it.
- **Measured 2026-11-22, the hook off** (`held_copy_shown_again_frees_no_list_twice`, heap history on, 1 of 1): the
  guest crashed 2 s after the disconnect in `NiObject`'s constructor (70199) writing `[rcx+8]`, rcx a vtable's
  address: the vtable crashes' shape, from this path alone. No `DOUBLE FREE` was counted. Inferred: the freed block
  was allocated again between the unequip and the nested call (the unequip's events and messages allocate), so the
  second free took a live object's block from under its owner (both frees go through 36098, the sized delete behind
  the thunk `0x1415A743C`, and 68117). With the quarantine the block is not reused and the read hangs instead.
- **Read:** an actor's enable is the way in. `Enable` 19800 (at `0x1402f293d`) calls `ResetInventory` (vfunc 0x8A,
  37322 -> 19802 -> 16038 -> `RemoveOutfitItems` 19690) before it makes the new AIProcess, so every worn outfit item
  of an actor being enabled goes through the no-process branch: a held copy shown again, in a session or at its end.
- **Read:** 19694 has a second caller in 16059 (`0x14022f49b`), where its 1 means "unequipped, go on"; a 0 there
  skips the removal. So its answer is left alone.

**The fix** (`engine/FreedWornList.h`, `TESObjectREFR.cpp`): `HookUnequipForRemove` (19694) looks, after the engine's
call, whether the item's entry still holds the list, and remembers one that is gone (per thread) until the
`RemoveItem` it ran in returns; `HookRemoveFromInventory` (16059) passes no list in its place to each nested call
(16059 makes up to two with the same list: `0x14022f51d`, `0x1402305e0`). Tests: UnitTests
`[freed-worn-list]`; GameTests "A held copy wearing its outfit beside a twin is shown again: no worn list is freed
twice" (the fake's `EnableActor` counts the second free); the duo: `held_copy_shown_again_frees_no_list_twice` (it
boots the games with the heap history itself: `count_double_frees`, then `expect_no_double_free`).

- `ObjectReference.Delete` 56154 frees nothing itself: `SetDelete`, `Set3D(null)` (inline under the reference's
  lock 12358, or task 24 through 36943), then a `DeleteFunctor` after the frame's render -> `GarbageCollector::Add`
  36459 -> `Job_Garbage` (36479). No free of it showed in either run.

## An alias-held item leaving a container (2026-11-23, IDA 1.7.104, read)

The Golden Claw (playtest 2026-11-22, item 15). MS13's `GoldenClaw` alias (11) creates `MS13GoldenClaw` (39647) **in**
Arvel's inventory (ESM: `ALCO 39647`, `ALCA 0x80000001`, the quest-object flag): the alias holds a created reference
(`FF000D80` in the duo's save, harness `quest`) that lives in Arvel's inventory. Measured (`stack_extras`): its list
holds the alias (136) and the reference's handle (28), and no unique id. So the reference's handle (16149's event
+0x10) is this item's path to its script; the unique id's, below, is read but not what these items use. Its script (`MS13GoldenClawScript`) sets stage 40 on
`OnContainerChanged` when the new container is `Game.GetPlayer()`. 189 Skyrim.esm aliases are made this way (131
quest objects), MQ103's Dragonstone in the barrow's boss among them.

```text
an inventory item's VM handle = (uid << 32) | container FormID | 1<<48     SkyrimVM__InventoryItemHandle 0x1409BA460
RemoveItem 16059 with a target (apMoveToRef)
  -> InventoryChanges__AddListFrom 16053 (0x14022C310, read): the list moves whole (alias with it)
     a list holding a unique id (extra 0x9F) gets one in the new container (SetUniqueID 16147 ->
       TESUniqueIDChangeEvent; VM sink 54003 remaps the handle); a list without one keeps none
     SendContainerChangedEvent 16149 {old, new = target, item, count, ref, uid}
       VM sink 53966: OnContainerChanged to the item's reference (event +0x10) and to its uid's handle -> the alias's script
RemoveItem 16059 with no target
  -> InventoryChanges__SendRemovedToNowhereEvents 16065 (0x1402310F0, read)
     TESContainerChangedEvent {old, new = 0, ref, uid = 0}: the script hears new = None
     only if the list holds a uid: it is cleared -> TESUniqueIDChangeEvent {.., 0, 0}: sink 54003 unbinds that handle
     the list (and its alias instance) is freed
```

So `HostEngine::TransferContainer` (no target since 2026-11-19, above) ended an alias-held item's quest role when a
guest took it: its script heard the claw leave for no container (the event's new container is 0), and the guest kept a
plain claw as its own loot. The claw's list holds no unique id (measured), so 54003's unbinding is not shown for it: only
the empty destination is. Measured 2026-11-23 (`golden-claw-guest`, from `LateOutsideHelgen`): "a list found", MS13
stayed at 30 on both games. The host's own take moves the list to its player (the target path) and runs as vanilla.

The fix (`HostEngine::MoveAliasStack`): a guest's take of a stack whose list holds `ExtraAliasInstanceArray` (0x88)
gives 16059 a target, the host's player, where the party holds it (`shared-items.md`). Measured: MS13 reached 40 on
both games, the claw moved to `14`. An intermediate version sent a non-quest-object alias item (Arvel's journal) to the
taker's copy instead; the journal's rows and the copy measurements of that day are from it, and the shared-items plan
replaced it. Arvel's web package ghosts him
(`PF_MS13ArvelWebForcegreetPack` `setGhost()`): the scenario clears it before the kill. A kill move was not tried.

## What the story hears and counts of a player's items (2026-11-23, IDA 1.7.104, read)

For the party's one pack (the story hears, counts and takes the party's items as the player's): where the engine's
quest scripts and conditions meet a player's inventory, and what a guest's copy on the host does there.

```text
a copy's items change on the host (InventoryService::ApplyItems -> SetCarriedStacks / SetContainerCount)
  engine add (19708 -> 19688) / remove (16059): TESContainerChangedEvent 16149 is sent (ScopedInventoryOverride
  silences only our hooks)
  VM sink 53966: OnItemAdded / OnItemRemoved to the container's handle (its filter checked), then RelayEvent 54033
    -> each quest alias holding that reference (TESObjectREFR__HasAliases 19655 / VisitAliases 19656), each
       effect on an actor
  a copy is in no alias: a player alias (it holds 0x14) never hears a guest's items

counts the story asks
  condition GetItemCount 21435 (dialogue, objectives, packages): the subject's InventoryChanges::GetItemCount 16108
  Papyrus GetItemCount 56173 -> 0x140A56FA0: 16108 for an item or each form list entry; a reference: 1 when it
    lies in this container; a keyword: GetItemCountByKeyword 0x140239750
  16108's other callers are the engine's own (ammo, worn marks, lockpicks through PlayerCharacter::GetItemCount)
```

A copy's alias stack (a guest's take of a non-quest-object alias item, `MoveAliasStack`) reads as plain to
`SetCarriedStacks` (its `ReadStack` sees no extra data of ours): kept while the owner's count matches, removed to
nowhere (its alias with it) when the owner's items part holds fewer. Measured 2026-11-23 by the list's extras: the
journal on the guest's copy holds 136 and 28. A quest's fill id proves nothing here: it read `FF000D81` after the
stack was gone too.

A guest's drop (measured the same day): the host made a new journal (`FF000A43`), and the copy's own went to nowhere.
`Actor::DropObject` (37520) removes with `kDropping`, the item's own list and a place; `HostEngine::DropAliasItem`
does that on the copy, and `FF000D81` itself lies in the world again (our creation watch sees it: announcing it too
sent create, delete, create). A later pickup goes into the taker's copy (`TakeAliasItem`,
`Actor::PickUpObject` 37521), its list 136 and 28 again.

A drop's place (read): with no reference to bring back, `InventoryChanges__PlaceDropped` 16062 `0x140230E10` makes one at
the position given, always in the dropper's parent cell and worldspace.

A script's removal: papyrus RemoveItem 56218 queues `RemoveItemFunctor`, run at `0x140A418E0` (vtable slot 1; 56095
is the AddItem functor's). A base item goes through 56647, which clamps an actor's count with
`GetItemCountImpl` `0x140A56FA0`: the party's count must hook only the Papyrus entry 56173, never that body.

## A script's EquipItem (2026-11-23, IDA 1.7.104, read)

Found by Alternate Start's start choices, which dress the player with `Player.EquipItem(ClothesFine...)` from the
quest `ARTHLALRumorsOfWarQuest`'s stage fragments (its Papyrus sources, in the mod's BSA), as vanilla MQ101 does with
the prisoner clothes and DA15 with Pelagius's fine clothes.

| ID | Name | What |
|---|---|---|
| 54661 | `papyrus::Actor::EquipItem` | `(vm, stackId, actor, item, abPreventRemoval, abSilent)`. Not queued: unlike `AddItem` (56349) it runs in the native. An item the actor holds (16133 builds its entry) is equipped, or only marked cannot-wear when already worn. One it lacks is added with 16053 (count 1, no extra list), then equipped by `ActorEquipManager` (38894); the player gets "X equipped." unless `abSilent` |
| 16133 | `InventoryChanges__MakeEntryFor` | `(changes, object, ...)`: a fresh entry of the changes' stacks merged with the base container's count; null when neither holds it. The caller frees it |
| 16053 | `InventoryChanges__AddListFrom` | the add itself: never through `AddObjectToContainer` (19708) or `Actor`'s add, so our add hooks never see it |

Two branches matter (read): an actor with no inventory changes yet (16039 finds none) skips 16133 and the add and goes
straight to the equip, so our count reads the changes without creating them (not 16040, which would send the native
down the add branch). And the equip is queued (`bQueueEquip` 1 at `0x140A0016F`; queued equips: "Queued equips"),
so the arrival is reported unworn and the worn state reaches copies through the equip when it lands.

So an item a script equips into being raised no `InventoryChangeEvent`: the story's clothes stayed the host's own,
never a party item. Our hook on 54661 compares the actor's count before and after the native and reports an added
item (`ScriptEquips::Arrival`): the story's when the native's caller is a quest's script (`GuestScripts::StoryCalling`,
the caller frame's handle as for `AddItem`), and then worn by the story (`RequestInventoryChanges::Wear`,
`NotifyPartyItem::Wear`): every game's player puts its party copy on (`InventoryService::WearPartyItem`): that
stack's own list through 38894 with `abForceEquip` off (`IFormBridge::ActorWearStack`), as the script's default
`abPreventRemoval`, so no `ExtraCannotWear` keeps the player in it ("Worn on add").
Measured on the duo with USSEP and Alternate Start (`quest-dresses-every-player`): before, the host wore 86991,
86993 and CEE84 and the guest's own player none of them.
