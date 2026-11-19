# How items enter and change a carried stack (1.6.1170)

Checked in IDA 2026-10-30 for party items ([party-items.md](../../design/party-items.md)).

| ID | RVA | Name | What |
|---|---|---|---|
| 56349 | 0xa36f40 | `ConcreteDelayFunctorFactory<AddItemFunctor>` | `ObjectReference.AddItem` does not add inside the native: it queues `SkyrimScript::AddItemFunctor` (a `DelayFunctor` carrying the calling stack's id at +0x0C), as `MoveTo` does |
| 56095 | 0xa28b40 | `SkyrimScript::AddItemFunctor::Run` (vfunc 1) | The add itself, later on the game thread. The calling script's context (`GuestScripts::NativeScript()`) is gone by then: tag the functor when it is queued, read the tag in the run (the `HookCastRun` pattern) |
| — | 0xa2c110 | the `ObjectReference.AddItem` native | Builds the `AddItemFunctor` and queues it with `SkyrimVM::QueuePostRenderCall` (53955), as `MoveTo` does, not `QueueDelayFunctor` (53954). Corrected 2026-10-30: the story mark was first only in the 53954 hook and never fired in Skyrim. The bookkeeping is now `Papyrus/StoryGifts` (engine-free): both queue hooks and the fake engine's script `AddItem` (post-render queue) call it. GameTests `[party-items]` went red on the old rule; ClientTests `[StoryGifts]` |
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
  weapon or torch its owner wears there (`EquipmentRepair::HandOf`).
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
