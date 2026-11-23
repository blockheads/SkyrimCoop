# Item menus and the stacks they list (2026-11-22)

Read in IDA (1.7.104) for the 2026-11-22 playtest's guest crashes (Fabien, Windows): two of three were an open
`ContainerMenu` reading an extra data list our code had freed.

## How ContainerMenu lists and reads items

```
ProcessMessage 51126, kInventoryUpdate (8), the message's handle the target (405962) or the player's (403520), no form:
  51144: clear the ItemList (0x140904F70), refill it from the player's then the target's InventoryChanges
    0x140913EB0: per item InventoryChanges::GetInventoryItemAt (16106) builds a fresh InventoryEntryData
      whose extraLists hold the container's own ExtraDataList* (0x14023929d): aliases, not copies
  clears ItemList::UpdatePending (+0x50, at 0x140912e74)

hover:  51130 -> ItemList__GetSelectedItem (0x140905290) -> ItemCard::SetItem 51897 -> 16018 -> 11654 -> GetExtraData 12329
take:   51132 -> GetSelectedItem -> ContainerMenu__TransferItem 51141 -> 11756 -> GetExtraData 12329
        then ItemList::Update_Impl (51031, owner the player)
GetSelectedItem returns nothing while UpdatePending is set.
ItemList::Update_Impl (51031): UpdatePending = 1, then SendInventoryUpdateMessage (52849) for its owner.
```

So a list freed while the menu lists it (a stack's last item removed, 16059) leaves the menu holding a freed pointer
until 51144 runs, and nothing in the engine runs it for a change made outside the menu.

The other item menus are built the same way (read 2026-11-22):

```
BarterMenu 50945:    ItemList at +0x30; kInventoryUpdate for the player (403520) or BarterMenu::handle (405823)
                     rebuilds (50958); its fill (50960) lists the player's changes and TESDataHandler +0xDB8
                     (merchantInventory), each through 16106: aliases
InventoryMenu 51848: ItemList at +0x48; kInventoryUpdate only for the player rebuilds (RefreshItemList 51866,
                     16106 over the player's changes): aliases
```

An update naming an item (the form non-null) does not rebuild: case 8 adds the item to the menu's changed set (+0x60,
ContainerMenu and InventoryMenu; +0x80 barter). ContainerMenu reads the set only at its next full update: 51144 then
runs the partial `ContainerMenu__UpdateChangedForms` (51146: per item `ItemList__RemoveForm` 0x140903920, then that
item's entries from the player's and the target's changes) instead of the whole list, and empties the set. Who sends
which (read and measured on the duo, 2026-11-23):

```
TESObjectREFR::RemoveItem 19689 (our bridges, InventoryService)  the item's update: SendInventoryUpdateMessage(this, item)
console removeitem (GameFunc::handler::RemoveItem)              a full update (52849 with no form, 0x14034d722)
Papyrus ObjectReference.RemoveItem                              the open loot menu re-listed at once (measured; path not read)
InventoryMenu, the player's item update                         re-listed at once (measured)
ContainerMenu, the player's item update (a host script's take)  re-listed at once (measured)
ContainerMenu, the corpse's item update                         stays listed until a full update (measured: the crashes' path)
```

The engine's own take does both: 19689's item update, then 51132's `Update_Impl` (a full update), whose rebuild re-reads
the set. A full update alone, arriving while the set holds other items, re-reads only those.

403520 is this game's player's handle (it was misnamed `gBarterMenu_TargetRefHandle`): every item menu rebuilds for
an update naming the player, which is how 51132's `Update_Impl(list, player)` refreshes ContainerMenu after a take.
In a session all three run unpaused (`MenuPauseRules.h`), so the world changes under them. Skyrim Souls RE's
`ItemMenuUpdater` (`reference/plugins/SkyrimSoulsRE`) sends 52849 after the engine's own outside changes it knows.

## The crashes (2026-11-22)

A guest's take from a corpse copy goes to the host (`ContainerTransfers`); 1 to 3 s later the copy's items are
reconciled (`InventoryService` -> `SetCarriedStacks`), whose `RemoveItem` frees the taken worn stack's list. The menu,
rebuilt after the take from the copy's old stacks, still lists it. The next hover or take reads it: access violation
in `GetExtraData` +0x68 (the list's presence bits pointer garbage in both dumps). Logs: crash 1 at 02:57:31 (take,
copy `1EBF0`, worn arrows), crash 2 at 03:25:47 (hover, copy `9BCD6`, worn `5BF0F`).

On Linux it did not crash, but the menu kept listing the taken item on the corpse's side
(`guest-loot-menu-follows-corpse`, measured before the fix: "Clothes" listed twice, held once).

## What we do

```
each item menu's ProcessMessage hook (LiveMenuHooks: 51126, 50945, 51848)
  kShow  -> ItemMenuWatch::Shown(kind, menu)      kHide / kForceHide -> Hidden(kind), before the engine frees the list
ItemMenuWatch::RebuildFirst(holder), before a holder's stacks change:
  for each open item menu: the holders it lists (ItemMenuRebuild.h)
     ContainerMenu: its target (405962), the player
     BarterMenu:    merchantInventory's owner (TESDataHandler +0xDB8 -> +0x8), the player
     InventoryMenu: the player
  listed: the item's update for the player (52849; none given: each item the holder has), then
          ItemList::Update_Impl(list, player)   (51031: pending now; its rebuild re-reads the set) -- the engine's take
called from:
  SetCarriedStacks, SetContainerStacks, ApplyContainerContents   ours
  HookRemoveFromInventory (16059), before the real one          every removal, ours or the engine's
  HookResetWornToOutfit (418622), HookResetInventory (37322)     their lists merged or freed without 16059
```

Only the target's side of ContainerMenu was seen stale, and only through `ContainerItems` (the crashes). The 16059,
418622 and 37322 hooks are a guard for other paths through 19689: none of the paths tried showed a stale entry without
them (the console's and Papyrus's removals, a host script's take from this game's player).

The open menus are kept by our hooks, not asked of UI (82074 takes the UI's lock, held all through the UI job's
message pass): 16059 runs on any thread. Our update runs in the VM job, before `Job_UI` in the same frame
([lock-order.md](lock-order.md), "Orders the frame keeps apart"), so no hover comes between our change and the mark.
An engine removal on another job thread may overlap `Job_UI` (not measured): the mark then shrinks the window to the
removal itself, not closes it.

Not covered: lists freed by other engine paths that skip 16059, 418622 and 37322 (none known).

## IDs

| ID | Name | What |
|---|---|---|
| 51126 | ContainerMenu::ProcessMessage | case 8 rebuilds (51144) for the target's or the player's handle |
| 51144 | ContainerMenu list rebuild (ours) | refills the ItemList; clears UpdatePending |
| 51130 | ContainerMenu__ItemSelectCallback (ours) | hover: ItemCard::SetItem on the selected entry |
| 51132 | ContainerMenu__ItemTransferCallback (ours) | take: TransferItem 51141, then Update_Impl |
| 51031 | ItemList::Update_Impl | UpdatePending, then 52849 for its owner |
| 16106 | InventoryChanges::GetInventoryItemAt | a menu entry over the container's own lists |
| 11756 | ExtraDataList__GetReferenceHandleRef (ours) | the crash 1 caller of 12329 |
| 405962 | ContainerMenu::handle | the menu's target |
| 50945, 50958, 50960 | BarterMenu::ProcessMessage, its rebuild, its fill | lists the player and merchantInventory |
| 51848, 51866 | InventoryMenu::ProcessMessage, RefreshItemList | lists the player |
| 403520 | this game's player's ref handle | every item menu rebuilds for it |
