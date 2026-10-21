# A guest's world scripts: events dropped, results from the host

Engine side of world-state sync step 5, slice 1 (`docs/design/world-state-sync.md`, 2026-10-21): on a guest,
the world's scripts no longer react to its player's activations and triggers; the host's game runs them
(for the guest's copy, party model rules 1-2) and its results reach the guest.

```
guest: player activates / walks in
  engine → Activate relay (ActivateRequest) / copy moves into the host's trigger
  VM SendEvent(world ref, "OnActivate" | "OnTrigger*") → dropped      WorldEvents.cpp
host: the copy's event runs the script (GuestScripts rules 1-2)
  door it opens (Activate by a reference)  → ActivateRequest, ActivatorRef   TESObjectREFR.cpp, ObjectService
  animation it plays (PlayAnimation)        → ScriptAnimationRequest          TESObjectREFR.cpp
  spell it casts on the copy                → AddTargetRequest to the owner    MagicTarget.cpp
  items it takes or gives the copy          → inventory changes (as before)
HostAuthority: reference activations and script animations only from the host
```

## Dropping the guest's events

| ID | Engine function | Our hook | Notes |
|---|---|---|---|
| 104800 | `BSScript::Internal::VirtualMachine::SendEvent(VMHandle, const BSFixedString&, IFunctionArguments*)`, vtable slot 0x24 (`VTABLE_BSScript__Internal__VirtualMachine` 252631, slot 0x18 is `BindNativeMethod` 104788) | `WorldEvents::HookSendEvent` | Looks up the scripts bound to the handle (`+0x93A8` map under the `+0x93A0` lock) and sends the event to each through vtable slot 0x27. |
| 54033 | `SkyrimVM::RelayEvent` | none | Sends a reference's event to its aliases (`EventRelayFunctor`) and magic effects (`EffectEventRelayFunctor`); both functors call `[vm+0x120]`, slot 0x24: the same hook sees them. |

The rule: a guest drops `OnActivate`, `OnTriggerEnter`, `OnTriggerLeave` and `OnTrigger` unless the
handle's form (its low 32 bits) is the player, 0x14. Read without engine calls: `SendEvent` runs on the
threads raising events. Off by `GuestWorldState` on joining, back on leaving (`GuestEngine::SetDecidesWorld`, with the story-manager gate),
as the story-manager gate.

## Results back to the guest

- **Doors a script opens** (`secretDoor.Activate(Self)`): `HookActivate` passed on actors' activations only.
  A door activated by a reference raises `ActivateEvent::ByReference`; `ObjectService` sends it with
  `ActivatorRef` (the reference's session id) and `ActivatorId` 0; `HostAuthority::OnActivate` passes it on
  from the host only; a guest replays it with that reference as the activator
  (`SkyrimBridge::ActivateObjectFromActor`, any reference now).
- **Script animations**: upstream turned them off in 2022 (`a312f9d9`, `OBJECT_ANIM_SYNC 0`, no reason
  given; both games played every script's animation then). On again; `HostAuthority::OnScriptAnimation` passes
  on the host's only. The hooks' signatures checked in IDA: `papyrus::ObjectReference::PlayAnimation` (56205)
  and `PlayAnimationAndWait` (56206) take `(vm, stackId, self, BSFixedString*)` (the native copies its event
  from the pointer).
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
relay), `statue-fires-at-client` (the guest's trigger never polls; the host's fire burns its player),
`ring-turns-for-client` (the guest's ring script state stays), `shrine-blesses-client` (the guest's blessing
comes from the host's script), BridgeTests "a script's door and animations come only from the host's world".
