# What a save keeps of its player beside the character (2026-11-23)

Read in IDA (1.7.104) for bug 17: a guest's favorites reset when the party reloads or the guest rejoins. A guest
loads the host's save and `JoinBridge::ApplyPlayer` puts its character record on the save's player. Whatever the
record leaves out stays the save's player's, the host's. Below is everything the save keeps of its player that is
the player's own and the record did not carry, where the engine keeps it, and how it goes back.

## Where the save writes the player

```
BGSSaveLoadManager::SaveGlobalData 35639 / LoadGlobalData 35642 / RevertGlobalData 35650, by type:
  0    misc stats       MiscStatManager::SaveGame 36834 / LoadGame 36835: u32 count, per stat a name, a category
                        byte, a u32 value; the load sets each stat found by name
  109  MagicFavorites   52012 / 52013 / 52014: spells, then hotkeys, each a count and form ids
  110  PlayerControls   PlayerControls::LoadGame 42363: three handler states, then data+0x28 (2 bytes), version-gated
  113  MenuControls     one byte to two handlers, one at +0x81 (not the player's: left alone)

The player's change form: Actor::SaveGame 37649 then PlayerCharacter::SaveGame (0x14074CEC0):
  TESObjectREFR::SaveGame       its inventory: each stack's ExtraDataList, ExtraHotkey with them
  AIProcess::SaveGame           its active effects through SaveActiveEffectList 34076
  Modifiers +0x230..+0x258      health, magicka, stamina, voice: permanent, temporary, damage
  SavePowerCooldowns 38743      the power cooldown list (Actor+0x188)
  +0x940                        skillTrainingsThisLevel
  PlayerCamera::SaveGame 50846  the camera's states and its current one (0 first person, 9 third)
```

## Each piece

Favorites, items. `ExtraHotkey` (type 0x49, byte +0x10: -1 favorited and unbound, 0-7 a number key) on the stack's
list. `InventoryChanges::SetFavorite` 16098: nothing if the list has one; else a stackable list of the stack, or a new
list split off it, gets `ExtraDataList::SetHotkey` 11664 (-1). When the given list is one of the stack's own, it
takes the *given entry's* first list: the menus pass a fresh entry holding only the selected list
(`GetInventoryItemAt` 16106), so we pass a one-list entry too. `InventoryEntryData::GetFavoriteExtraList` 15998,
`ExtraDataList::GetHotkey` 11666.

Favorites, spells and shouts. `MagicFavorites` (singleton 403337): `spells` (+0x10, sorted by pointer, SPEL 22 or SHOU
119 only: `SetFavorite` 52004) and `hotkeys` (+0x28, `hotkeys[slot]`: `GetHotkeyForm` 52008). `PruneUnknown` 52011
grows `hotkeys` with nulls and drops the favorites and keys whose spell or shout the player lacks
(`Actor::HasSpell`/`HasShout`). `FavoritesHandler::UseHotkey` 51543: the magic key first, else the item whose favorite
list's hotkey is the slot. `MagicMenu::ToggleFavorite` 52045.

Timed effects. The actor's list is `MiddleHighProcessData+0x1A0` (`MagicTarget::GetActiveEffectList` 38795).
`SaveActiveEffectList` 34076: per effect its spell, its effect's index in the spell, its archetype, its own SaveGame
(vfunc 8). `LoadActiveEffectList` 34077 reverts and deletes every current effect first. `ActiveEffect::Dispel` 34061
sets `kDispelled` (flags +0x7C, 0x40000) and, forced, finishes it at once. A timed effect is one with a duration and
time left. Abilities, worn enchantments and diseases have none; they come back with the record's spells and items.

Power cooldowns. Actor+0x188, nodes `{SpellItem*, float seconds left, next}` (24 bytes). `AddCastPower` 38736 sets
one day in real seconds (86400 / timescale) when a power is cast. `SetPowerCooldown` 38738 adds a node unless the
power has one (and then leaves its time alone). `UpdatePowerCooldowns` 38824 counts down and frees at zero.
`ClearPowerCooldowns` 38825 frees them all; `Actor::Revert` calls it, so a load replaces them.

Trainings. `TrainingMenu::Train` 52667 refuses at `iTrainingNumAllowedPerLevel` and increments player+0x940.

## What we do (`PlayerSaveState`, the record's `Own`)

```
ReadPlayer  -> Own: stacks with ExtraHotkey (as Inventory entries + key), globals 0/109/110 as the engine's bytes
               (SaveGlobalData, form ids as session ids), damage modifiers, timed effects (spell, index, elapsed),
               cooldowns, trainings, first person
ApplyPlayer -> after SetActorInventory: SetFavorite + SetHotkey per stack, LoadGlobalData per global, the save's
               timed effects dispelled and the record's cast again (instant caster) with their elapsed time,
               damage restored to the record's, cooldowns cleared and set, trainings, the view
```

An older record (no `Own`) applies nothing: the save's player's stays, as before. A player-made potion's effect has no
session id and is left out. Effects are recast by the player on itself, so their magnitude is this character's.

## Measured on the duo (scenario `guest_keeps_own_state_on_reload`)

Before the fix, after a party reload the guest had the host's spell favorite (Healing on key 2) instead of its own,
no item favorites, no damage, no timed effects, and the host's first-person view. The host's own favorites stayed. Always Run
was unchanged by the reload.
