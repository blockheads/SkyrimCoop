# The party's purse

Decided 2026-10-27 (the user: "everyone shares the same gold"), replacing each player's own gold (`party-model.md`,
"Items and gold", 2026-10-22). Items stay each player's own; quest item rewards are unchanged.

One purse: the host's player's gold (Gold001, 0xF). Every player carries it, on every game, and every copy of a
player carries it too. Whoever's gold changes, whoever changed it and however (loot, a sale, training, a fine, a
quest's reward, the console), the purse changes by that much and every player carries the result.

```text
any game: a player's gold changes (our AddInventoryItem / RemoveItem hooks: InventoryChangeEvent)
  -> RequestInventoryChanges (InventoryService, as for any item) -> the host
the host (HostPurse, before HostAuthority's item relay):
  Gold001 on a player's character? (else: an item, relayed as before)
    a guest may change only its own player's (the host any: its scripts on a guest's copy, rule 2)
    the host's own player's change: already in its gold
    another player's: the host's player gets it too, silently (AddPartyGold; a removal takes at most all)
    -> NotifyPartyGold{the host's player's gold} to every game, the host's own too
every game (InventoryService):
  its player and each copy of a player: set to that many, silently (ScopedInventoryOverride: not sent again)
  a player's copy whose spawn items land later (WaitingFor3D removed): set to the last purse too
```

The purse is never stored apart from the host's player: a save keeps it, a host load brings back the save's, and
the guests carry it again when they rejoin.

## Joining

A guest's character carries the purse once its game announces it (`HostAuthority::PlayerEntered` sends the purse
to every game, after the existing characters). The gold a character brings, from its own game or from its record in
the host's save, is not added: it is the party's purse while it plays (its record then keeps the purse, refreshed by
`GuestRecord` when the purse changes).

## Two spends at once

A guest spends on its own game before the host hears of it. Two players spending at the same time each spend from
what their game showed; the host applies both, the purse never below 0. The party can buy a little more than it had
in that moment (100 gold, two players spend 80 and 50: 0 left). The games always agree after.

## Not covered

- A mod currency other than Gold001: each player's own, as any item.

## Every game hears it

The HUD's "Gold (40) Removed" / "Added" (the user, 2026-10-30, for v1). The engine shows it on the game whose player
gained or spent. The host's `NotifyPartyGold` carries the change as `Shown` to every other game (0 to that one, and
for a purse sent with no change; a host script's change on a guest's character no engine showed, so every game does), and `UiService` shows it in the engine's words
(`IPlayerBridge::ShowItemChangeMessage`: `sAddItemtoInventory` 376348 / `sRemoveItemfromInventory` 376351).

## Spending in chat

The user, 2026-10-30: everyone hears when the party's gold goes. When a player's gold drops, the host's
`HostPurse` sends every game a system chat line naming that player, with a phrase picked at random from the user's
list (`HostPurse::cSpendWords`): "Lydia tax-evaded 40 gold". A gain says nothing. Chat shows system lines (any line
the game writes, not one a player typed) in light gold (`chat.rcss`).

## Tests

- Tier 1: `HostAuthorityTests` (`purse_tests.cpp`): a guest's change fills the purse and every game hears it; the host's
  own change is in already; a host script on a guest's copy; a guest cannot change another player's; an NPC's gold
  is an item; a guest entering carries the purse; a spend is told in every game's chat, a gain is not. `encoding.cpp`: `NotifyPartyGold`.
- Tier 2: `GameTests` (`purse_flows.cpp`): the host's and a guest's spending, a reward on the guest's copy, two
  spends together below 0; a joining guest carries the purse, not what it brought, and a second guest's copies too
  (fails without the spawn's fix). `join_flows.cpp`: a guest's record keeps the purse.
- Skyrim: `party-gold-is-one` (the client's Take All, the host's spending, a reward on the client's character); the
  gold scenarios that assumed each player's own now expect the purse: `dialogue-options-are-the-hosts`,
  `dialogue-pick-says-the-hosts-line`, `dialogue-picker-trains`, `chest-take-all`, `guest-pays-party-fine`,
  `guest-container-theft-and-fine`, `guest-guard-arrest-fine`.
