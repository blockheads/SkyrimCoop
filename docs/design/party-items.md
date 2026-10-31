# Party items

Decided 2026-10-30 (the user: "it's a fun coop mod!"), prompted by Breezehome's key reaching only its buyer. It
replaces "items are each player's own" (`party-model.md`, 2026-10-22) for keys, quest items and the story's rewards.
Ordinary loot stays the finder's own. v1 (the user, 2026-10-30). First cut built 2026-10-30 (below, "Built").

## The rule

Every player carries every party item. The party owns a count of each, as the purse does with gold
(`party-purse.md`), and anything done to a party item is done to every player's copy.
Gold is never a party item, even the story's (a quest's gold reward): it goes to the purse (2026-10-31,
`quest-gold-turn-in-host`).

```text
a party item comes in (the story gives it, a key or quest item picked up)
  -> the host: the party's count + 1
  -> every game: its player carries it, tagged "[P] <name>"
  -> every game's chat: "Lydia found Lucan's Claw for the party"
a player drops / sells / stores / gives / uses it up / a quest takes it
  -> the host: the party's count - 1
  -> every game: its player carries one fewer
  (the item is wherever it went: on the ground, in a chest, with the merchant)
anyone picks it up / takes it out / buys it back
  -> the host: the party's count + 1 -> every game carries it again
a player tempers / enchants / renames it
  -> the same change on every player's copy
```

What follows from it:

- **Equip:** each player wears its own copy; two players can both use one reward sword.
- **Use up:** a reward potion or arrow drunk or shot by anyone is one fewer for the party.
- **Sell:** the gold goes into the purse.
- **Disenchant:** the item is destroyed, so the party loses it.
- **Join and load:** a guest entering carries the party's items, as it carries the purse.
- **Dialogue judged for a guest:** "has the claw" counts the party's items.

## Which items are the party's

Decided when the item enters a player's inventory:

```text
a key (KEYM)?                                        -> party
an engine-marked quest item (a quest alias holds it)? -> party
added by a quest's script (a quest, alias, dialogue line or scene fragment)? -> party
otherwise: picked up, taken from a container, bought, crafted, pickpocketed,
  or added by an object's own script (ore vein, plant, Hearthfire materials) -> the finder's own
```

## How players tell

- **The name:** a party copy is shown as `[P] Steel Sword` (the user, 2026-10-30), with its own per-copy display
  name. It shows wherever the game shows the item: inventory, containers, barter, favourites, SkyUI and the HUD's
  added/removed notice. A player's own Steel Sword stays a separate entry. A name the player gives it when
  enchanting is kept, with the tag added in front.
- **The chat:** each party item's arrival is told in every game's chat, as a system line (light gold):
  "Lydia found Lucan's Claw for the party", "The party received Key to Breezehome".

## Telling a party copy apart: the `[P]` name

The tag is the mark (decided in the 2026-10-30 plan). `Inventory::Entry` already carries a stack's custom name
(`ExtraName`, from `ExtraTextDisplayData`) and its quest flag (`IsQuestItem`). Both already travel through every
synced item path: drop and pickup, containers, transfers, `AddOrRemoveItem`.

```text
party stack = entry.IsQuestItem || entry.ExtraName starts with "[P] "
```

A quest item whose name comes from its alias refuses a custom name (`SetCustomName` keeps a quest's name). It is the
party's by its quest flag and shows the quest marker. The ownership-field marker considered earlier is not needed.

## Engine facts (IDA 2026-10-30, [inventory-changes.md](../re/systems/inventory-changes.md))

- A script's `AddItem` / `RemoveItem` is queued (`AddItemFunctor`, run 56095). The calling script is known only
  when it is queued, so the functor is tagged there and the tag is read in its run, as `HookCastRun` does.
- Tempering (16145) and enchanting (16146) change a stack in place, and renaming goes through 51415. They do not
  pass through the add/remove hooks, so a party item's improvement needs its own hooks to reach every game.
- Seen in the duo (2026-10-30): a stack named `[P] Iron Sword` (`give_item` with a name) keeps its name in the
  inventory, as a loose item after a drop, and after the pickup. Still to see: the barter and SkyUI menus, and a
  merchant's sale and buyback.
- The failing scenario: `party-buys-house-host` now checks that both players carry `[P] Key to Breezehome`, then has
  the other player open the locked front door (0x1A6F9) with its own copy. It fails at the key (2026-10-30).

## Built (2026-10-30, first cut)

- **Arrival:** `InventoryService::OnInventoryChangeEvent`. A key, quest item or story gift arriving on a player is a
  new party item. The arriving game takes its untagged copy back and sends the tagged one with `NewPartyItem`
  (rules in `Games/PartyItems.h`, ClientTests `[PartyItems]`).
- **The story's gift:** `GuestScripts`. An `AddItemFunctor` queued from a quest's script is marked
  (`HookFunctorQueued`), and its run (56095) sets `StoryGiving()`, which the add hooks copy into
  `InventoryChangeEvent::StoryGiven`.
- **Host:** `HostPartyItems` beside `HostPurse` keeps the host's player's tagged stacks as the counts.
  `NotifyPartyItem` goes to every game with a HUD `Shown`. A new party item is told in chat. HostAuthorityTests
  `[PartyItems]`.
- **Each game:** `InventoryService::OnNotifyPartyItem` sets its player and every player copy to the count, silently,
  and shows the HUD notice.
- **Tests:** GameTests `[party-items]` (a reward reaches everyone tagged, a guest's character's reward, a drop takes
  it from everyone, loot stays own). TPTests `[encoding.party-items]`.
- **Join:** `HostAuthority::PlayerEntered` sends `NotifyPartyItems`, every party stack the host's player carries.
  Each game keeps the list, sets its player and copies to it, and drops a `[P] ` stack the list lacks (a stale one
  a rejoining guest brought). A copy spawned later gets it too (`OnSpawnItemsApplied`, as the purse). GameTests
  "A guest joining carries the party's items", HostAuthorityTests, TPTests.
- **A guest's chest take or store:** the host's chest transfer (`HostItems::OnContainerTransfer`) asks the party
  items after the purse, so a party stack taken by a guest reaches every player (found by `party-item-moves-guest`;
  GameTests "A guest taking a party item from a chest", HostAuthorityTests).
- **Skyrim, 2026-10-30:** `party-item-moves-host` / `-guest` pass: a key reaches every player tagged and chat tells it,
  and a drop, a pickup, a store in a chest and a take each reach every player on both games.
- **Skyrim, 2026-10-30, `--tag party-items` 6 of 7:** `party-reward-reaches-everyone` (Grosta's favour,
  FreeformHeartwoodMill stage 200: its leveled reward reaches every player as `[P] <name>`, chat says so),
  `party-quest-item-host` (the host picks up the Golden Claw while MS13's alias holds it: every player carries one),
  `party-item-sold-host` / `-guest` (a sale empties every player and pays the purse; the other player's buyback gives it
  back to everyone), `party-item-moves-*`. The barter scenarios wait for the item card to show the item before E (an
  E right after the highlight moved was dropped; once more a host sale did not happen, not seen since).
- **Quest items (2026-10-30):** the party holds a quest item on the host's player, where the host's quests see it. A
  guest's pickup of a loose quest object is the host's player's (`HostItems::OnPickUp` → `HostGame::TakeQuestObject`,
  `PlayerCharacter::PickUpGranted`), so its alias follows it; every other player and copy carries `[P] <name>`. One
  rule matches a party item on any game (`PartyItems::SameStack`: base and tag, or the holder's quest stack). A guest
  dropping its copy gets it back (as Skyrim refuses a quest item's drop). The upstream quest-item copy in
  `TESObjectREFR::AddOrRemoveItem` is removed. GameTests "A guest picks up a quest object", HostAuthorityTests,
  ClientTests; Skyrim `party-quest-item-host` / `-guest` pass.
- **Improving (2026-10-30):** hooks on tempering (16145) and enchanting (16146) read the owner's stacks of the item
  before and after; the difference goes out as item changes (`Games/ItemChanges.h` `Diff`). An improved stack is
  another party stack (`SameStack` matches health and enchantment, not a copy's own charge or poison), so every copy
  loses the old and gains the improved one. Renaming needs nothing (51415 never renames a named stack). GameTests
  "Tempering or enchanting a party item improves every player's copy" (red first), ClientTests `[ItemChanges]`,
  `[PartyItems]`; Skyrim `party-item-improved-host` / `-guest` (harness `improve`, as the tables call 16145/16146).
- **Removing a stack with extra data (found by `party-item-improved-*` on a fresh duo):** the engine's removal (16059)
  takes only from the stack whose own list it is given; a list rebuilt from the entry (what `AddOrRemoveItem` passed)
  matched none, though both held the same data (harness `stack_extras`). `TESObjectREFR::AddOrRemoveItem` now finds the
  carried list (`FindCarriedStack`, `ItemChanges::FindStack`); the fake removes only that way, and four GameTests flows
  went red without it.
- **Skyrim:** `party-buys-house-host` passes its actions. Every player carries `[P] Key to Breezehome` and the guest
  opens the locked front door with its own copy.

## Still to build

Nothing for v1 (2026-10-30). Done since the first cut: the join, quest items, the upstream quest-item copy removed, a
guest's house purchase (a synced quest's dialogue lines, re/systems/dialogue.md), improving, and removals naming the
carried stack. The scenario flakes were vanilla (a merchant's raw-deal prompt, a leveled reward rolling nothing) and
are fixed in the scenarios. Skyrim: `--tag party-items` (9 scenarios) passes twice in a row; the item scenarios
(drops, pickups, chests, poison, equipment, corpse loot) pass with the removal change.

## Open

- v1: yes (the user, 2026-10-30). Plan: build in the order of the bug-fix flow (a failing scenario, GameTests, unit tests, then the code).
- Weight: each player carries the party items' weight.
