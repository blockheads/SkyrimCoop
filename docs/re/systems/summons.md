# Summons and script-created references: the host creates

How the engine makes a summon and a script's `PlaceAtMe`, and why a guest's made nothing anyone could see: one-simulator.md's
open question 5, for piece 7. Read in IDA on 1.7.104 (2026-11-21) unless a line says measured.

## A summon

```text
caster casts a summon (MGEF archetype 18) on itself -> MagicTarget::AddTarget -> SummonCreatureEffect
  Start (vfunc 0x14, 34990)     placement: FindPlacement 34994, else the caster's position (+0x90, angle +0x9C);
                                the portal's SummonPlacementEffect (+0xB0); no actor yet
  Update (vfunc 4, 34992)       elapsed >= a setting and not made (+0xB8): CreateSummon, every frame until made
    CreateSummon (34993)        the MGEF's assoc item (+0x70) by TESDataHandler::CreateReferenceAtLocation (13723)
                                in the caster's cell; handle -> +0xA8; process high (+0xB8 set); PromoteReference to
                                the caster; a detection event
    its process high (+0xB9)    AIProcess::SetCommandingActor (39950): middle process +0x218 = the caster, flags2
                                0x10000; AIProcess::AddCommandedActor: the caster's +0x100 list {handle, effect}
                                over its limit (a setting + perk entry 68, Twin Souls) Dispels the oldest effect
  Finish (vfunc 0x15, 34991)    the summon killed (QueueKill) and faded, or disabled and deleted; DemoteReference
```

- **The effect is the summon's life.** Its duration ends it (measured: the familiar died at 60 s, Conjure Familiar's
  duration); a recast past the limit dispels the old one's effect, which ends it.
- **The commander is whoever the effect is on.** Put on any actor as caster and target, the engine makes the summon
  for that actor: it follows it and fights what attacks it (measured 2026-11-21: `add_effect` of Conjure Familiar on
  the host's copy of a guest; the wolf took the bandit attacking the guest's copy as its combat target).

### What a guest's cast did (measured 2026-11-21, before piece 7)

- The guest's own engine made the familiar (`FF000829`, commanded by its player) but it never showed: a guest holds
  every actor that is neither its player nor a copy, with no 3D (`GuestNpcs`, `HookLoad3D` 19727; `actor_place`:
  process high, `has_3d` false). It was never announced (only the host announces NPCs), so the host had none.
- On the host, the copy's replayed cast reached `HookAddTarget` as another game's spell on another game's actor and
  was dropped; `ShouldSync` leaves summon effects out. So neither game showed a familiar.
- The 2026-11-06 suspect (`RunActorDiscovery`'s delete) only takes actors with 3D, which a held one never has.

### Piece 7

```text
guest's player casts a summon on itself
  HookAddTarget: SummonEvent -> SummonRequest; the effect is applied here too (portal, Active Effects timer)
  HookCreateSummon (34993): a guest's engine makes nothing (its Finish with no summon does nothing, 34991)
host: HostEngine::Summon -> MagicTarget::AddTarget on the guest's copy, cast by the copy (past our hook)
  the engine makes the familiar commanded by the copy; the copy's list keeps the guest's limit;
  the effect's end ends it
every game: the familiar's spawn carries its commander (NotifyCharacterSpawn::CommanderId): commanded by that
  player's character there (the caster's game: its own player)
```

The first build kept the effect off the guest entirely; the duo showed what that cost (the user, 2026-11-21): no
portal on the guest. `Start` (34990) makes the portal, so the effect runs on the guest and only `CreateSummon` is
refused. Two timers run, the guest's deciding nothing.

### A copy's effect visuals at its spawn (measured 2026-11-21)

The familiar's spectral look is its own ability's shader (MGEF `ED5F2`, shader `3B6CB`), sent with its spawn
(`EffectIds`) and in `NotifyActiveEffects`. The guest's copy never showed it: `EffectVisualsService` tried at the
spawn and on the list, both before the copy had 3D (`PlayEffectHitVisuals` needs the NiNode), and waited for
`TESObjectLoadedEvent` to try again. The engine raised none for the copy's 3D (no `ReferenceLoadedEvent` at the
copy's "Applied 3D", the familiar's and a staged bandit's copy alike; where the engine sends that event was not
found). `CharacterService` now raises `Copy3DEvent` where it applies a spawned copy's 3D, and the visuals play then.
Any copy spawned while its owner shows an effect had the same gap. The world comparison catches it (`visuals`:
"copy lacks 0003B6CB"), but only while the familiar lives (60 s): `guest-summons-familiar` checks `agree` then.
## A script's PlaceAtMe

- `ObjectReference.PlaceAtMe` (56203, `PlaceAtMe_Native`): `(vm, stackId, self, base, count, forcePersist,
  initiallyDisabled)`. Each of `count` references of `base` at the self's position, angle and cell, by
  `CreateReferenceAtLocation` (13723) directly. A leveled list (LVLI 0x2C, LVLN 0x35) is rolled first at the self's
  level. Returns the last made; `initiallyDisabled` disables it (vfunc 0x89), not an explosion.
- `ObjectReference.PlaceActorAtMe` (`PlaceActorAtMe_Native`): a temporary `Character` of the NPC, its level mod set,
  placed by 13723 directly.

Both call 13723 themselves, so `NativeReach` sees them: 13723 is the `Call::kCreate` sink, ranked before the world
in `ClassifyCall` (a bucket of its own; without it a creation fell to the screen's). A guest's script's call goes to
the host's game (`PartyRules::RouteOf`: `kHost`, `Reply`), its stack waiting as on a latent native (`PlayerNatives`
`Wait`: `WaitOnLatent`, then `kInProgress`). The host's game runs it for that guest and answers with what it returned
(`HostService::OnWorldCall`, `PlayerNativeResult::SessionId`). The guest's script goes on once that reference is here,
the host's create row having made its copy (R5; `GuestWorldState::OnCallResult`), or None after 5 s for one the host
deleted before its row went out.

**Created references in script values (`CreatedIds`).** A script value naming a created reference crosses by its
created id: the host's own are their ids, a guest's copies of them are linked by `GuestEngine` as it makes them.
`GlobalCall::Read` carries such a reference (it refused every created reference that is not an actor), and
`ScriptValues` maps it both ways. So a guest's script can go on using the reference its `PlaceAtMe` returned.

**Enable and Disable are the world's.** `VampireChange` disables its marker right after placing it. Papyrus `Disable`
and `DisableNoWait` (56155, 56156) call `DisableImpl` (56491) directly, which refuses a temporary object (form flag
0x4000), an explosion or one with an enable parent, fades, and queues a `DisableFunctor` through `QueuePostRenderCall`;
`Enable`/`EnableNoWait` (56158, 56160) call `EnableImpl` (56492). Both are `kWorld` sinks marked `OnCreated`: a
guest's script's call on a reference the host's game created is the host's to make, and the host's `kEnabled` row
reaches the guest's copy (measured before: the guest's copy disabled alone, flags 0x908 against the host's 0x108,
`vampirism-is-each-players`). On any other reference it stays the guest's, as before: routed for every reference, a
guest's own startup scripts sent the host about twenty `Enable`/`Disable` calls as it joined (measured), and which
scripts those are is not read.

**A created reference in the host's save (measured 2026-11-21).** A guest joining loads the host's save, which holds
the host's created references under the host's own ids (the guest's `FF000852` was the host's dropped dagger, its base
and place). The host's create row for it then made a second one (`created_by_base`: the dagger 1 on the host, 2 on the
guest; `vampirism-kept-across-rejoin`'s marker). `GuestEngine::Create` takes a reference of the row's base already
under that id as its copy. Not read in IDA: how the save keeps the id.
