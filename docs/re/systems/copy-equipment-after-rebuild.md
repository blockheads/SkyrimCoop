# Copy equipment after a native rebuild (2026-10-30)

`dialogue-options-are-the-partys` reproduces the host's copy of the guest losing four of five
worn items after the guest hands Lucan the claw. Missing: `12E46`, `12E4B`, `12E4D`, `13790`;
its hands are empty. The guest still wears all five. The copy still **carries** its missing
items, with the four armor counts now two. The periodic owner snapshots attempt the missing
equips, but native EquipObject does not put them on. This is not an absent item packet.

Evidence in `v1-copy-desync` (ignored build artifacts):

- `build/coop-test/armor-native-red.log`: exact original failure, with Onmund's focused pool PASS.
- `build/coop-test/armor-diagnostic-native.log`, `build/armor-diagnostic-host.log`: native callers.
- `build/coop-test/magicka-at-failure.jsonl`: inventory stacks with worn flags, graphs and pools.
- `build/coop-test/armor-recovery-native.log`: original failure retained, then the explicit
  UnequipAll probe allows the next unchanged owner snapshot to restore all five items.

The refused automatic outfit equips originate in `AIProcess::Update3DModel_Impl` (39395),
through Actor 19743, TESNPC 24740/24725 and the routine at RVA `3BD380` (418622). That routine
calls InventoryChanges 16100, which inspects and clears worn state without going through our
UnEquip hook, then initializes/adds the NPC outfit and equips it. EquipHook 38929 was checked
against the public CommonLib EquipObject wrapper (38894) and the four-argument internal ABI
in disassembly before adding temporary call-stack diagnostics. Those diagnostics are removed.

Controls are retained: direct claw removal plus gold, the exact leveled reward `C0405`, a
relationship change, and DoReset3D's weight-update request did not independently reproduce the
full conversation's failure. The complete script's precise initiating call is not established.
The observed broken state and native recovery operation are established; no prevent-removal
bit layout is assumed. SPID's [outfit helper](https://github.com/powerof3/Spell-Perk-Item-Distributor/blob/master/SPID/src/Outfits/Helpers.h)
is an additional equipment reference and is included in the reference-fetch list.

The fake models a copy whose native outfit rebuild loses part of its worn set and rejects
incremental equips until the full unequip clears its stale equipment state. GameTests
`[outfit-rebuild]` then fails on the missing armor and weapon after an unchanged owner snapshot.
The pure reconciliation unit tests `[equipment-repair]` fail six assertions first.

Reconciliation now leaves a matching worn set alone. A mismatch uses native UnequipAll, retains
the owner's spells/shout, and applies the complete owner set. It does not change item counts.
The new bridge operation serves production reconciliation; no testing method was added to a
production interface. Pure units pass all 10 assertions, and the equipment flows pass, including
spell preservation and unchanged inventory counts. Full `scripts/check.sh` passes: 597 cases,
6,257 assertions, harness/UI checks, plugin and launcher. Combined native acceptance is pending.

## Queued ammo during follower trade

The first combined recovery build passed the full Lucan hand-in, but follower-trade-to-picker caught Lydia's copy missing 10E2DE. This is her 12-arrow stack, not a missing armor piece. Both carry all 12; only the worn marker differs. The native equipment hook sends actual equips but deliberately omits queued equips. Her bow's actual equip is reported; queued arrows rely on the three-second full snapshot, while the desync check spans 0.8 seconds.

GameTests [queued-ammo] models the queued worn change without a hook event, followed by an actual equip, and fails awaiting the arrows within one second. The snapshot scheduler's coalescing and quiet-heartbeat unit initially fails three assertions. Actual equip events now mark a snapshot pending for the next update, after the native hook has returned and the full worn set is available. Multiple events coalesce; the unchanged heartbeat is still three seconds. This also avoids capturing a pre-equip worn set inside the hook. Native follower-trade-to-picker now passes the full conversation and inventory transfer with the arrow stack present (`build/coop-test/ammo-native-combined.log`).

## Torch default equip slot

The 104-case native batch passes the full claw hand-in and both gold turn-ins, but host-loads-party catches Aela missing 1D4EC and its left-hand light. The generic repair was still passing the right-weapon slot for every non-armor entry. Native lights have their own BGSEquipType; their worn entry is ExtraWorn, not ExtraWornLeft. The equip helper now passes an explicit hand only for weapons (type 41); lights, ammo and armor use slot 0. GameTests `[torch-repair]` reproduces a lost torch beside a retained weapon and fails before the correction; `[repair-slot]` units fail twice first. Inventory counts and the other weapon survive repair.

The short follow-up host-load checks pass3/3, but Aela had no torch in retained samples. A dedicated `npc-torch-repairs-in-default-slot` scenario requires the owner to wear the torch, then verifies initial copy equipment and repair after deliberate copy drift. That controlled scenario passes on the final build in5.7s, including the complete repaired worn set and normal desync check. Specific torch acceptance is complete. Latest gate and combined failures: [batch report](../../reports/2026-10-30-copy-desync.md).
