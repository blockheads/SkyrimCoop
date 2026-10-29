# A form's save data in a running game (FormState)

Engine side of `Code/client/Games/FormState.cpp`, world-state sync step 3 (`docs/design/world-state-sync.md`).
IDA 1.6.1170, 2026-10-20; layouts from CommonLibSSE-NG (`BGSSaveFormBuffer.h`, `BGSLoadFormBuffer.h`,
`BGSLoadGameBuffer.h`, `BGSLoadFormData.h`) and checked against the engine's own builders.

## The engine already does this live

A reference's changes are saved into a buffer when its cell unloads and loaded back when the cell
attaches, with no save involved. `FormState` follows those two routines:

| ID | Routine | Steps |
|---|---|---|
| 35570 | `BGSSaveLoadGame::StoreChangesOnUnload` | save buffer ctor (36035); `SetHeader` (36037) with the form id, change flags, form type and the current version (35740); `form->CheckSaveGame` (vfunc 0x0D); `SaveRefLocation` (35715); `form->SaveGame` (0x0E); keep the bytes; dtor (36044) |
| 35569 | `BGSSaveLoadGame::LoadStoredChanges` | set this thread's loading bit (19378, TLS +0x670 bit 0); fill the load buffer; form flag `0x200000`; `LoadRefLocation` (35716); `form->LoadGame` (0x0F); queue the form's `BGSLoadFormData` for 35567; restore the bit |
| 35567 | `BGSSaveLoadGame::InitAndFinishLoadedForms` | per queued form: `InitLoadGame` (0x10), then `FinishLoadGame` (0x11) unless deleted (form flag `0x20`), clearing `0x200000`. Only the header is read: the data pointer is null |

`FormState::Load` runs `InitLoadGame` and `FinishLoadGame` itself, at once, instead of queueing.
`FormState::Save` skips `CheckSaveGame`: it is the save's policy (copies are kept out of saves), not ours.

## The buffers

| `BGSSaveFormBuffer` (0x30) | | `BGSLoadFormBuffer` (0x50) | |
|---|---|---|---|
| 00 | vtable (set by 36035) | 00 | vtable: 35993 sets `BGSLoadGameBuffer`'s, the builder then `BGSLoadFormBuffer`'s (187480) |
| 08 | data, game heap | 08 | data, read with no bounds check (`LoadDataEndian`, 36005) |
| 10 | capacity | 10 | the data's scrap heap when the engine allocated it |
| 14 | position = size written | 18 / 20 / 24 | capacity / size / position |
| 18 | header: numeric id (3), flags (4, at 0x1B), type (0x1F), version (0x20) | 28 | `BGSLoadFormData` (35984): form id, size, uncompressed size, form (0x38), change flags (0x40), old flags (0x44), flags (0x48), info (0x4A), version (0x4B) |
| 28 | form | | |

- The version is the change form format: **78** in this build (35740 returns it). `LoadGame` reads it
  through vfunc 1 (`GetVersion`, 35990, returns +0x4B). Both games run one build, so it is not sent.
- `Reserve` (36056) grows the data on the game heap and **frees the old buffer with it**: a buffer must
  start empty, never point at our memory. The destructor (36044) frees it.

## Form ids inside the data

Saves write a form reference as a 3-byte index into the save's form id array (`WriteFormID` 36048, 36047;
`ReadFormID` 36000). Upstream's hooks (`Games/SaveLoad.cpp`) replace them, while `ScopedSaveLoadOverride`
is held, with network ids: the mod and base id (`FormIds.h`; a created form's as `kCreatedModId`), as two var
ints. So the bytes mean the same on any game with the same mods.

- `ReadFormID` returns true for a reference whose form is gone (a nonzero index that resolves to 0). Upstream's
  hook returned true always; it returns the engine's meaning now.

Not covered yet:

- **The reference location header** (`SaveRefLocation`/`LoadRefLocation`, by `TESObjectREFR::GetLocationKind`,
  35708): written for moved (`0x6`), cell-changed or promoted (`0x2000008`) and created (`0xFF`) references.
  It holds cells and worldspaces as save-local indices (`worldspaceFormIDMap`), which the hooks do not
  replace. `FormState` does not write or read it, so a moved reference's position does not travel.
- **Created forms (`0xFF`)**: each game numbers its own. A reference to one needs the id map of step 4.

## Upstream's version, fixed

`TESNPC::Serialize`/`Deserialize` (the copies' appearance) were the only user, with guessed layouts:

- the load buffer was built with the base class's vtable (35993 alone), so `GetVersion` returned the save
  system's current version (+0x344) instead of the buffer's; the "loadFlag" `0x40` it set was the unread
  version byte;
- the save went into a 32 KB stack buffer, which `Reserve` would have freed with the game heap past 32 KB;
  the network id writer wrote past the capacity instead of growing it;
- no header, no loading bit, no `InitLoadGame`/`FinishLoadGame`.

They now go through `FormState`.

## Tests

- `form-state-round-trips`: the client's character base (`0x7`) and the host's copy of it save the same
  bytes (59 for `Factions | Face | Race`, `0x2000840`): the appearance went through `FormState` both ways.
  Then a host door's lock (REFR `LockExtra`, `0x1000`) is saved, the door unlocked, the bytes loaded: it is
  locked at the same level again and saves the same bytes. Control: the same bytes loaded with no change
  flags leave it unlocked.
- A load goes around the lock-change hook: the other game's door does not follow (what a client applying the
  host's data needs).

## Change records

What the change funnel (`AddChangeFlags`) does and does not see of a reference, found with the world desync check
(`world-changes-reach-guest`, `docs/design/world-state-sync.md` step 7). IDA 1.6.1170, 2026-10-21.

- **A loaded reference's move is recorded once.** `SetPosition` (19790), `SetAngle` (19786) and
  `SetRotationX/Y/Z` (19787-19789) call `AddChange(2)` only while bit `0x10` of the loaded state's word at +0x28
  is clear, then set it: a save reads the position itself. Later moves never reach the funnel.
- **A state back to its default removes its flag.** `FinishLoadGame` (19531) and the open state's code call
  `RemoveChange` (14609, vfunc 0x0B, `TESActorBase`'s override tail-calls it) when a door is closed again. It
  checks the same loading bits as `AddChange` (TLS +0x670 bit 0, the manager's +0x340 bit 0).
- **`BGSSaveLoadGame::GetChange` (35577)** `(manager, form, mask)`: whether the form's record holds any of mask,
  under the record map's read lock.
- **Loading save data records nothing** (the loading bit): a guest marks the flags itself after `FormState::Load`,
  or its cell unloading (`StoreChangesOnUnload`, 35570) drops the change.
- **`TESObjectREFR::FinishLoadGame` (19531)** after a load: the extra data's finish (12097, for flags in
  `0xA6021C40`, among them the alias array 12728, which reads the quests' locks), the open state against its
  default (`0x800000`, 0x400000), a navmesh obstacle for doors, then 3D attach or the disabled 3D's removal
  (`0x820` form flags).
- **Moving a non-actor, three ways.** `MoveTo_Impl` (56626) disables it, sets position and angle, its 3D and
  collision, `MoveRefToNewSpace`, and enables it: on a placeable water (QASmoke's `40248BF`) that crashed the
  cell animations job (`bhkPlaceableWater::IsActive`, 32101). `SetPositionOnAxis` (22053, `(ref, 'X'..'Z',
  value)`) sets the position, the 3D's local translation, `BSFadeNode::UpdateCollisionObject` (78383) and
  `NiAVObject::Update` (70251). `SetRotation` (22054, `(ref, axis, degrees)`) the same for the angle but without
  the update: the 3D's world transform stays until something updates it (the console's `setangle` looks unturned
  until then). Papyrus' `SetPosition` and `SetAngle` reach 22053 and 22054 through the task pool.
- **`BGSOpenCloseForm::SetOpenState` (14287)** `(ref, open, snap)`: plays `Open`/`Close` (a `NiControllerManager`
  sequence, else the graph event), the open state's flags, and with snap the navmesh obstacle. Closing a door
  mid-swing (0.1 s after opening) left the host's own record open: its cell reloaded with the door open.
- **`TESCellFullyLoadedEvent` {cell}** (`ScriptEventSourceHolder+0x210`) is sent once per cell load, interior or
  exterior, by `sub_1402C3830` (19146, `SendFullyLoadedEvent`): when the cell's loaded data (+0x130) counts no
  references still loading (+0x16C, +0x170), it sends the event unless +0x17C says it was sent. The VM's sink
  (53964) walks the cell's references under the cell's lock (+0x120) for `OnCellLoad`.
- **`kStillLoading` (`TESForm` flags +0x10, `0x200000`) clears before the reference's 3D loads** (2026-10-28,
  disassembly). A cell's load (`TESObjectCELL` 18882) clears it per reference once `LoadStoredChanges` (35569) has
  nothing pending; a deferred change load (35717) clears it, then hands the reference to its cell (19032) and on;
  placing (13723, 22532, 56204) sets and clears it in one call. So `TESObjectLoadedEvent` comes after it: a spawn
  waiting for it wakes on that event (`CharacterService::OnReferenceLoaded`), no per-frame check.
  `TESCellFullyLoadedEvent` is sent by `TESObjectCELL::SendFullyLoadedEvent` (19146, reached indirectly).
- **A cell's references stay in memory after it unloads**, `pcb` too (2026-10-21, QASmoke and the Sleeping Giant
  Inn): `TESForm::GetById` finds them, and a change applied while unloaded is there when the cell loads again.
- **Opening a door with lock data re-sets its lock** (the console's `SetOpenState`): an `AddLockChange` and a
  `TESLockChangedEvent` with the lock unchanged.
- **A placed leveled item picks its base once** (`TESObjectREFR::PickLeveledBase`, 20232, our name): for a
  reference with `ExtraLeveledItemBase` (0x90, `XLIB`), rolls the list at `GetCalcLevel` (`TESLeveledList` 14752),
  and with a pick: vfunc 0x84 (`SetObjectReference` 19785: the base at +0x40 and its destructible flag), records
  `0x80`, then `sub_19671`. Called from the cell's reference loads (`sub_1402C9990`, `sub_1402CC7F0`, both under
  `TESObjectCELL` 19084). The pick is per game: two games show different books.


## Animation, activating children, created references (2026-10-21)

- Animation `0x10000000` is recorded on nearly every loaded object with a graph, by each game itself (a cell's
  attach 19130, `Set3D` removes it, `PlayGamebryoAnimation`, `FinishLoadGame`): the flag is no desync, the graph's
  state is. It goes with a cell's state as the graphs' save data (`kGraph`).
- Activating children `0x4000000`: `TESObjectCELL` 19428 puts a reference on `LoadedCellData+0xF0`
  (`activatingRefs`) while its activate-parent children run (`InitChildActivates`, `FinishLoadGame`, the cell's
  load) and records the flag; 19032 and `Revert` remove it. Transient, the host's: not sent.
- A created reference a script deletes (`Delete()`) is often freed without the funnel recording anything (it is
  never saved): the host sweeps the ones it sent each update. Its id can be used again in the same update.
- Form flag `0x400` (persistent) also changes at run time: `BGSDynamicPersistenceManager` promotes a reference while
  a script or quest holds it (15330) and demotes it after; `0x40000` is a look-at target's bookkeeping (19584),
  recording the form flags `0x1`. Both are each game's own.
- A quest's fill of a reference not in memory reads it from its plugin: `QueuedPromoteQuestTask` (25212) →
  `QueuedPromoteReferencesTask` (15319) → 15328 → 15360 (seek the cell or worldspace grid cell's records, load the
  one) → `PromoteReference` 15330 for the quest. A guest calls 15328 directly with the host's reference and parent.

## Updating a copy's appearance (2026-10-27)

A look change uses `FormState::Load` on the copy's existing temporary TESNPC base. Weight is separate, as at spawn;
its runtime race follows the new base, then its 3D and FaceGen tints are rebuilt. This runs through `MainLoopTasks`
between frames, with retries while a save/load holds FormState. The graph descriptor is invalidated so a new race
cannot write variables using the old race's indices. The actor and base form ids remain unchanged.

GameTests models the main-loop restriction and deferred loads. Skyrim's RaceMenu/vampire scenario compares the
same actor's base sections and painted tints; Beast Form checks the different graph and return to human. Findings
and validation: [small-bugs.md](../../design/small-bugs.md).
