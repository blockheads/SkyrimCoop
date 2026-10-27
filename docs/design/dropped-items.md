# Dropped items

Status: **built 2026-10-26** (v1.0.0 feature 6, `docs/progress.md`; `.worktrees/dropped-items`, uncommitted). A loose
item exists once in the party's world, the host's, with its extra data; whoever picks it up gets it and it is gone for
everyone. Chests keep their items' extra data too. This is how players hand each other items in 1.0 (no trade menu).

## The idea

```text
guest drops (PlayerCharacter::DropObject 40454, our hook)
  its game makes L (0xFF here), its own physics drops it      the player sees it at once
  -> DropItemRequest{tag = L, item (base, count, extra data), placement}
host: HostItems
  cell loaded here?   no -> waits, tried each update (a pickup of it by its tag gives it back)
  yes -> HostEngine::CreateItem: R made, its count and extra data set
      -> NotifyItemDropped{tag, R} to the dropper: L is R's twin (GuestEngine m_created)
      -> next update: kCreate{R, Stacks = [the item]} to everyone (world-state-sync.md)
dropper: L's body is its own until the host's rest (GuestHands, as a push or a hold)

someone picks it up
  the host's player: the engine picks it up, R deleted -> the deletion to everyone (TakeDeletedCreated)
  a guest: PlayerCharacter::PickUpObject (40533, our hook) leaves a twin or an own drop alone
           -> ItemPickUpEvent -> PickUpRequest{R, or its tag}
  host: R still there? -> HostEngine::TakeItem (DisableImpl, SetDelete): NotifyPickUp{R, item} to that
                          guest, the deletion to everyone
        gone            -> nothing ("already gone"); the twin goes with the deletion
  guest: picks its twin up past the gate (PickUpGranted), or adds the item if the twin is gone;
         its inventory change reaches its copies as any pickup's
```

The host's rule in short:

```text
on DropItemRequest(r) from P:
    id = CreateItem(r.Item, r.Placement)   or wait (its cell not loaded here)
    made[P, r.Tag] = id; send P NotifyItemDropped(r.Tag, id)
    (cannot be made, its form not in the host's game: NotifyItemDropped(r.Tag, none), it stays P's own)
on PickUpRequest(q) from P:
    pending drop (P, q.Tag)? -> forget it, grant its item
    id = q.Object, or made[P, q.Tag] (asked before the answer arrived)
    TakeItem(id) -> grant; none -> another player was first
```

Decisions:

- **The host's game makes the reference** (`docs/design/world-state-sync.md`, "Only the host's game decides"). A
  guest's drop is a request; its own reference shows at once and becomes the twin, so nothing waits on a round trip
  to be seen. A pickup waits for the host's grant (one round trip before the item is in the inventory), which makes
  the race simple: the first request the host's game handles wins, its own player's pickup included (local, so it
  is first whenever it comes before the request).
- **Extra data travels as `Inventory::Entry`** (upstream's, used for copies' inventories): enchantment (a player-made
  one rebuilt from its effects, `EnchantmentItem::Create`), its charge, tempering (`ExtraHealth`), poison and its
  count, soul, and since this feature a custom name (`ExtraTextDisplayData` with owner instance -2; the name without
  the temper suffix, `customNameLength`). skymp keeps the same fields (`reference/plugins/skymp`,
  `skymp5-client/src/sync/inventory.ts`); its name write constructs the extra the same way. Ownership (a stolen
  item) does not travel.
- **A drop the host's game cannot make** (an item it lacks) is answered with no id: the dropper's reference stays
  its own, picked up without asking.
- **A pickup may come before the host's creation went out** (the dropper asks by its tag in the same frame):
  `TakeItem` takes any created item, not only those already sent.
- **A player-made enchantment is the same item in every game** when its effects are (`Inventory::Entry::IsSameEnchantment`):
  its form is each game's own (a created id), so comparing ids made every chest sync rebuild those stacks and make new
  enchantment forms (found in review, 2026-10-26; `TPTests` "A player-made enchantment is the same item in every game").
- **A guest's drop in a cell the host's game has not loaded waits** until it is (the party tether keeps that rare).
  Until then only the dropper sees it; picking it up again gives it back without the host ever making it. A player
  leaving forgets its waiting drops.
- **Physics:** the dropped item is a body like any other once it has the host's id (`physics-sync.md`, "Owners"). On
  the dropper its own simulation shows the fall until the host's rest places it; everyone else plays the host's
  frames.

## Chests

A container's change travels per base item (`kItems`: the count now) and now also with `Stacks`: every stack of that
item with extra data, each its count (`ContainerItems::ReadContainerStacks`). Applying it (`SetContainerStacks`)
takes the stacks with extra data out and puts the wanted ones in when they differ, then sets the plain count. A
guest's proposal (`ContainerChangeRequest`) carries them the same way, and a cell's contents (`kContents`) too.

## As built

| Piece | Where |
|---|---|
| Messages | `Code/encoding/Messages/DroppedItems.h` (`DropItemRequest`, `NotifyItemDropped`, `PickUpRequest`, `NotifyPickUp`); `ObjectChange::Stacks`, `ContainerChangeRequest::Stacks`; `Inventory::Entry::ExtraName` |
| Host's rule | `Services/Host/HostItems` (registered in `HostAuthority`); `HostGame::CreateItem`, `TakeItem`, `SetItems` (a kItems change) |
| Host's engine | `HostEngine::CreateItem` (made past the creation hook, logged as created), `TakeItem`, `ReadCreate` (the item's stack) |
| Guest | `GuestWorldState` (drop, pickup, their answers), `GuestEngine::ReadDrop`, `AdoptDrop`, `ReadPickUp`, `PickUp`, `IsPartyItem` (the pickup gate), `Create` (a twin with its count and extra data); `GuestHands` owns an adopted drop's body |
| Engine | `Bridges/LooseItems` (read, place, make), `Bridges/ContainerItems` (stacks), `ExtraDataList::SetCount` (11617), `SetCustomName` (vtable 186855, SKSE's `s_ExtraTextDisplayVtbl`), `TESObjectREFR::SetItemExtraData`; hooks: `HookDropObject` raises `ItemDroppedEvent` with the made reference (40454's result handle), `PlayerCharacter`'s `HookPickUpObject` asks the gate |
| Harness | `give_item`, `drop`, `store`, `item_stacks` (`TestHarness/CommandsWorld.cpp`) |

Engine notes (IDA, 1.6.1170): `PlayerCharacter::DropObject` (40454) removes the item through the actor's `RemoveItem`
(vtable 0x56, reason 3 dropping, the extra list), which makes the reference, and returns its handle; under a condition
on its cell (not traced) it stores the item in a container there instead (reason 4, no reference, nothing sent). The player's pickup is
`PlayerCharacter::PickUpObject` (40533, vtable 0xCC; `Actor::PickUpObject` 37521 is NPCs'). `ExtraDataList::SetCount`
(11617) removes the count at 1 and takes the list's write lock itself.

Found by the scenarios (2026-10-26):

- **A deleted reference is still picked up.** The host granted the guest its sword and deleted it (`DisableImpl`,
  `SetDelete`), and the host's own activation a moment later still put it in the host's inventory: both players had
  it. `PickUpObject` checks neither flag, so our hook refuses a deleted reference on every game.
- **A stack's count lives in its list.** `AddObjectToContainer(item, list, 2)` with a list holding no `ExtraCount`
  adds two, one with the extra data and one plain (the list stands for one item). Lists for an add get the stack's
  count first (`GetExtraDataFromItem(entry, counted)`, `ExtraDataList::SetCount`); upstream's inventory adds had the
  same gap.
- The fake engine already behaves so (a taken loose item is gone; its entries carry their counts), so neither has a
  fast test: only Skyrim showed them.

## Tests

- Tier 1: `TPTests` "Dropped items keep their extra data on the wire" (and stacks in `NotifyObjectChanges`,
  `ContainerChangeRequest`); `HostAuthorityTests` `[Items]`: a drop made and answered, a drop waiting for its cell
  (made later, given back, forgotten on leaving), a drop the host's game cannot make (told, not retried), two pickups
  (the first wins), a pickup by tag, the host's own, a chest change's stacks.
- Tier 2: `GameTests` `[loose]` (`loose_item_flows.cpp`): the host's drop seen and picked up by a guest; a guest's
  drop made by the host, adopted by the dropper (one item, not two), picked up by the host; two guests racing; a drop
  where the host has no cell, picked up again; a drop the host cannot make, picked up without asking; a chest's stacks
  both ways. Three of them fail with the adoption, the host's check or the guest's handling of an unmade drop taken
  out. The fake guest gates pickups as `GuestEngine` does (`FakeEngine::HostDecides`, kept by the seams).
- Tier 3: `host-drop-reaches-guest`, `guest-drop-reaches-host`, `guest-pickup-race`, `chest-keeps-tempered-item`
  (3/3 shuffled, 2026-10-26; the race went to the guest twice and the host twice; again 2/2 after the review's fixes,
  with `chest-take-all`, `equip-host-player`, `equip-client-player`, `late-join-client`). The harness gives an item extra data
  (`give_item`), drops and stores a stack as the menus do (`drop`, `store`; `store` without a container removes it) and
  reads stacks (`item_stacks`). Regressions run then: equipment, late join, containers, cell state, created
  references and the physics hands scenarios pass.
- `net_check.sh` (`[items]`, `[world]`): pass under both profiles.

## Open

- A guest's NPC-style pickups and the engine's other ways of taking a loose item (a follower's, a script's
  `AddItem(ref)`) are the host's world: only the guest's player's pickup is gated.
- Plugins' placed items (a sword on a table, not created) keep today's path: the guest's own pickup and the
  activation relay.
- Ownership (stolen) and quest-item status do not travel with a dropped item.
