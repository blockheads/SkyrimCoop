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
