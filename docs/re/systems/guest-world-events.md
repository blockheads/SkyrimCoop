# A guest's world scripts: events dropped, results from the host

Engine side of world-state sync step 5, slice 1 (`docs/design/world-state-sync.md`, 2026-10-21): on a guest,
the world's scripts no longer react to its player's activations and triggers; the host's game runs them
(for the guest's copy, party model rules 1-2) and its results reach the guest.

```
guest: player activates / walks in
  engine → Activate relay (ActivateRequest) / copy moves into the host's trigger
  VM SendEvent(world ref, "OnActivate" | "OnTrigger*") → dropped      WorldEvents.cpp
host: the copy's event runs the script (GuestScripts rules 1-2)
  door it opens (Activate by a reference)  ┐ ActivateEvent / ScriptAnimationEvent /
  animation it plays (PlayAnimation)        │ ReferenceEnabledEvent → HostService
  reference it enables or disables          ┘ → HostEngine (queued) → HostWorldState: NotifyObjectChanges
  spell it casts on the copy                → AddTargetRequest to the owner    MagicTarget.cpp
  items it takes or gives the copy          → inventory changes (as before)
guest: GuestWorldState → GuestEngine::ApplyObjectChange (the door, the animation, enabled state)
```

## Dropping the guest's events

| ID | Engine function | Our hook | Notes |
|---|---|---|---|
| 104800 | `BSScript::Internal::VirtualMachine::SendEvent(VMHandle, const BSFixedString&, IFunctionArguments*)`, vtable slot 0x24 (`VTABLE_BSScript__Internal__VirtualMachine` 252631, slot 0x18 is `BindNativeMethod` 104788) | `WorldEvents::HookSendEvent` | Looks up the scripts bound to the handle (`+0x93A8` map under the `+0x93A0` lock) and sends the event to each through vtable slot 0x27. |
| 54033 | `SkyrimVM::RelayEvent` | none | Sends a reference's event to its aliases (`EventRelayFunctor`) and magic effects (`EffectEventRelayFunctor`); both functors call `[vm+0x120]`, slot 0x24: the same hook sees them. |

The rule: a guest drops `OnActivate`, `OnTriggerEnter`, `OnTriggerLeave` and `OnTrigger` unless the
handle's form (its low 32 bits) is the player, 0x14. Read without engine calls: `SendEvent` runs on the
threads raising events. Off by `GuestWorldState` on joining, back on leaving (`GuestEngine::SetDecidesWorld`, with the
story-manager gate).

## Results back to the guest

Doors, animations, enabled state and locks are world state like step 4's globals and quests: only the host's game
sends them (`HostWorldState`, `NotifyObjectChanges`, one list a frame: the state the change funnel saw change, then
the scripts' events in order; `docs/design/world-state-sync.md`, "As built"), only a guest applies them
(`GuestWorldState`, `GuestEngine::ApplyObjectChange`). `ObjectService` keeps the relays every game makes
(players' activations, locks).

A guest applies each change under `ScopedHostChangeOverride` (`Games/Overrides.h`): the object hooks it goes
through (`HookActivate`, `HookLockChange`, the two animation hooks) raise no event
for it, so nothing the host sent goes back as a guest's change. Before, each kind avoided the echo its own
way (a call past the hook, a thread-local flag in the wrapper, a flag on the event), and a lock sent
back would have looped between the games (the guest's proposal, the host's change, applied again).

- **Doors a script opens** (`secretDoor.Activate(Self)`): `HookActivate` passed on actors' activations only.
  A door activated by a reference raises `ActivateEvent::ByReference`, which `ObjectService` leaves alone; on
  the host `HostService` queues it in `HostEngine` (door, activator reference, open state before). A guest
  activates its door with the same reference, if the door is as the host's was.
- **Script animations**: upstream turned them off in 2022 (`a312f9d9`, `OBJECT_ANIM_SYNC 0`, no reason
  given; both games played every script's animation then). On again as host results, the
  `ScriptAnimationRequest` / `NotifyScriptAnimation` relay deleted. The hooks' signatures checked in IDA: `papyrus::ObjectReference::PlayAnimation` (56205)
  and `PlayAnimationAndWait` (56206) take `(vm, stackId, self, BSFixedString*)` (the native copies its event
  from the pointer).
- **Enabled state** (step 6's first piece): any cause on the host (a script, a quest stage, the console) ends in
  the engine's `TESObjectREFR::Enable` (19800, `(this, bool resetInventory)`) or `Disable` (19801, vtable slot
  0x89; `Actor` overrides it, 37267). Papyrus `Enable`/`Disable` (56158, 56155, and the `NoWait` forms) queue an
  `EnableFunctor` / `DisableFunctor` (56492 / 56491) for after render, which fade and then call those two
  (functors 56110, 56113). Both flip the disabled flag (`0x800`) through `TESForm::SetDisabled` (14646), which
  records `CHANGE_FORM_FLAGS` (`AddChange(1)`): the host reads the state from the change funnel
  (`Forms/FormChanges`, `HostEngine`), not for actors (their owner's) or references the game created (`0xFF`
  ids). A guest enables or disables its reference at once (`EnableImpl`, `DisableImpl`), the host's fade
  already played. The `Enable`/`Disable` hooks (and `ReferenceEnabledEvent`) were deleted for the funnel.
- **Locks** (step 6): every lock change ends in `TESObjectREFR::AddLockChange` (19512, CommonLibSSE-NG), which
  records `CHANGE_OBJECT_EXTRA_LOCK` (`AddChange(0x1000)`; for a load door, on the door it leads to). On the host
  the funnel's change goes out as a `kLock` object change; on a guest its hook's `LockChangeEvent` is a proposal
  (`GuestWorldState` sends `LockChangeRequest`), which `HostAuthority::OnLockChange` gives the host's game
  (`HostGame::SetLock`, `HostEngine::SetLock`): the host's object takes it and every game gets it as the host's
  change. Applying calls `TESObjectREFR::SetLockState`, which records the change past our hook; on the host the funnel sees it. The
  old relay (`NotifyLockChange`, `ObjectService`'s lock handlers) is deleted.
- **Spells the host's world casts on a guest's character**: `MagicTarget::AddTarget` dropped every effect on
  a copy whose caster was a copy or nothing, and every hostile one between players with PvP off. On the host,
  an effect on a guest's character goes to its owner when the caster is not an actor (a statue, a trap) or
  is the copy cast by a script running for a guest; PvP only covers a player's own casts.

## Script casts: Spell.Cast is delayed

| ID | Engine function | Notes |
|---|---|---|
| 55747 | `papyrus::Spell::Cast` | Queues a `SkyrimScript::CastFunctor` (spell `+0x10`, source `+0x18`, target `+0x1C`, blame `+0x20` handles, the calling stack id `+0x0C`) and returns. |
| 53954 | `SkyrimVM`: queue a delayed functor (`BSTSmartPointer<DelayFunctor>*`) | Called inside the native: GuestScripts tags a `CastFunctor` with the native's guest here (`HookFunctorQueued`). |
| 55744 | `CastFunctor::operator()` (vtable slot 1, `Variable operator()()`: `(this, Variable* out)`) | Runs later, after the script may have ended (its stack's tag gone); calls the caster's `CastSpellImmediate` (`MagicCaster` vfunc 1), which reaches `AddTarget` at once. `HookCastRun` carries the functor's guest on the thread meanwhile (`GuestScripts::NativeGuest`). |

`DelayFunctor` (CommonLibSSE-NG): `stackID` at `+0x0C`. Other delayed natives are not tagged (only casts
needed it so far).

## Found along the way

- `MagicTarget::AddTargetData::caster` is a `TESObjectREFR*` (CommonLibSSE-NG, `+0x00`), not an `Actor*`:
  `HookAddTarget` called `GetExtension()` on a statue's or trap's reference. Fixed with `Cast<Actor>`.
- `TempleBlessingScript`'s blessing is neither a healing nor a buff spell to `IsHealingSpell` /
  `IsBuffSpell`: the PvP check took it for a hostile one.
- The Soljund statue's Firebolt from the host now burns the guest's player (`magic_effect_apply`, effect
  `0x12F03`, caster 0 on the guest); a host NPC's arrow (`0x335F3`) hit it in the same run.

## Tests

`sword-box-asks-client` (the guest's sword goes once and comes back once; its secret door opens from the host's
change; it sees the box and the sword in the stone swap both ways: with the host's recording off, the guest's
sword never appears, control 2026-10-21), `statue-fires-at-client` (the guest's trigger never polls; the host's
fire burns its player), `ring-turns-for-client` (the guest's ring script state stays), `shrine-blesses-client`
(the guest's blessing comes from the host's script), BridgeTests "the host's world objects' changes reach the
others in order, once". Harness `ref_enabled` reads a reference's enabled state.
