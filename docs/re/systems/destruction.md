# Destructible objects: a cut web, a broken barricade

Read in IDA 1.7.104 (2026-11-27, journal `agent-spider-web-desync.tsv`) for the playtest's Bleak Falls Barrow web
(2026-11-26): the host cut the web Arvel hangs in (`1FC43`); the guest's stayed whole, blocked like a wall, and its
own swings did nothing. No SKSE mod syncs destruction (searched `reference/plugins`: higgs only queues damage tasks).

## What a cut web is

The web is an activator with destruction data, not a door or a script toggle. Bleak Falls Barrow01 has three:
`1FC43` (base `MS13FXspiderWebKitDestructible` 8B317, `MS13WebDestructibleSCRIPT`: Arvel's), `800D5` and `AA83F`
(base `FXspiderWebKitDestructible` EC3DE, no script). The E3demo copy of the dungeon has the same at `847B0`,
`758D1`, `75A7D`. Each base: `DEST` health 10, two stages; the last stage swaps the model for the cut one.

```
a swing meets it (Actor::CalculateCurrentHitTargetForWeaponSwing 38628), or an arrow, an explosion, a script's
DamageObject, a physics impact
  TESObjectREFR::DamageObject 19526 (vtable 0x42; (this, damage, allowDestroyed))
    only with form flag 0x1000000 (destructible: SetObjectReference 19785) and, unless allowDestroyed,
    without 0x800000 (destroyed)
    -> BGSDestructibleObjectForm::DamageObject 14158 (form, ref, damage, allowDestroyed)
       skips a disabled or deleted one (flags 0x820) and one with no 3D
       health = ExtraObjectHealth 0x55 (its base's DEST health while it has none) - damage
       the stage by health percent; at 0: TESForm::SetDestroyed(1) 14649 (form flag 0x800000, CHANGE_FORM_FLAGS)
       ExtraDataList::SetObjectHealth (ExtraObjectHealth) and AddChange(0x80000000: CHANGE_REFR_EXTRA_GAME_ONLY)
       a stage with a model: ExtraDataList::SetModelSwap 11967 (ExtraModelSwap 0x58), its 3D loaded again
         (TES 13355): the cut web's model, no blocking collision
       else the stage's damage index on the 3D; the stage's explosion and debris
       TESDestructionStageChangedEvent (old, new): scripts' OnDestructionStageChanged
```

- `TESObjectREFR::ClearDestruction` (14181) mends it: the health and model swap removed, the destroyed flag cleared,
  its 3D loaded again.
- A load keeps it: `Load3D` reads the model swap and the health; a cell reset snaps the health to its stage
  (`ApplyStageFromHealth`, our name, 0x1401D4DD0).
- The health is game-only extra data: `0x80000000` is the only change flag the damage records.

## The desync

Our reference rows sent every change flag but game-only extra data (`ReferenceStates`' `cNotSent`: its whole load
re-runs alias data on a guest). So the guest got only the destroyed flag, from the enabled row
(`GuestWorldState: applied 1 of the host's object changes: enable 1FC43 destroyed`, the guest's playtest log): its web
kept the whole model and collision, and the flag made 19526 refuse its player's swings. Every destructible behaves so.

## The fix: a destruction row, the guest's hits proposed

```
host: any damage -> AddChange(0x80000000) -> funnel -> kDestruction row {object, health} (DestructionRow)
  guest: ApplyDestruction: ClearDestruction if the host's is healthier, then DamageObject(here - host's,
         allowDestroyed) with its 3D (deferred to its TESObjectLoadedEvent without one): the engine's own model swap,
         3D reload, debris; its world events are dropped on a guest (WorldEvents), so the script runs on the host only
guest: its player's damage -> our 19526 hook -> DestructionEvent -> ObjectStateRequest {kDestruction row}
  host: HostObjects::OnObjectState takes it only as damage (DestructionRow::GuestDamages) -> HostEngine::SetObjectState
        -> ApplyDestruction -> the funnel's row back to every game
```

- The row takes `CHANGE_REFR_EXTRA_GAME_ONLY` for a destructible only (`RecordedRows`): other references' game-only
  bookkeeping stays unsent, and a cell's state names a damaged destructible, so a guest arriving later gets it.
- Counts, not deltas, as for containers: the guest's own web is already damaged when it proposes. So two players'
  hits landing within one round trip count once (both games report the same health); a lost hit on a web, accepted
  for duplicate-proof proposals.
- **Capped stages** (14158: a stage with flag 1 stops the damage at its threshold whatever `allowDestroyed` says; both
  Bleak Falls web bases cap their first stage at 99%, measured 10 -> 9.9 -> 2.2 -> 0): a row that jumps stages (a guest
  loading the cell later, a deferred 3D) is applied by calling `DamageObject` again until the health reads as the row.
- **A repair**: `ClearDestruction` (14181) records no change of its own. Our hook records the game-only change when it
  mends one, so the host's funnel sends the whole web's row (a script's `ClearDestruction`, `LumbermillSawScript.Repair`,
  a cell reset).
- **Deferred rows**: the newest row replaces or cancels one waiting for the 3D; the replay runs under
  `ScopedHostChangeOverride` (no proposal of the host's own damage); a save load forgets them
  (`ReferenceStates::ForgetDeferred`).
- A row naming a reference the host created at runtime uses its shared id (`HostIdOf` on the guest, the `0xFF` id on
  the host).
- The replay plays the stage's explosion on the guest too, also when a guest loading the cell later catches up on a
  spent object (a web's is a cobweb puff; a trap's would be a blast). Restoring the stage without its effects needs the
  model swap set by hand (the cell reset's `ApplyStageFromHealth` snaps a partial health to a stage): a loose end.
- **A copy's swing damages nothing.** Every game runs a copy's hit frame (`HitFrameHandler` 42828 -> 38603 -> 38628,
  `actor-state.md`), so before this a copy's swing damaged the web in the game that only shows it (measured: the host's
  copy of the guest took 0.1 off a web, the guest's copy of the host made the guest propose the host's own hit back).
  The damage hook returns at once while 38628 runs for a copy (`PlayerHands::CopySwings`, the swinger 38628's hook
  already keeps); the owner's game damages and sends the row. A copy's arrows and spells are not covered yet (a loose
  end in `docs/progress.md`).
- A swing meets an actor before an object (38603's pick, `actor-state.md`): the E3demo web `75A7D` holds a dead
  skeleton, which takes every swing at it in vanilla too. Not ours.

Measured on the duo (2026-11-27): `arvel-web-cut-reaches-guest` (the host kills Arvel in his web, `847B0`, and cuts
it: the guest's shows the cut model, its pieces following the host's bodies; in the playtest they were "not in this
game"), `guest-web-cut-reaches-host` (the guest cuts `758D1` in four swings: the host took 9.9, 2.2, 0 as proposals,
and nothing from its copy of the guest).

Tests: GameTests "A web the host cuts is cut on the guest", "A web the guest cuts is cut on the host", "A web the host
cut before the guest's 3D loaded is cut when it loads" (control: without the row the first two never see the cut
model); HostAuthorityTests "A guest's cut of a web: the host's game takes it as damage only"; UnitTests
`destruction_row_tests.cpp`. Harness `destruction` reads a reference's health, destroyed flag, stage model and 3D;
verbs `cut` and `expect_cut`.
