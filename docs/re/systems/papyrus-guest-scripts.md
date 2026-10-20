# Papyrus: guests' characters are the player (host)

Engine side of `docs/design/party-model.md`, "A guest's actions on the host", rules 1 and 2
(`Code/client/Games/Skyrim/Papyrus/GuestScripts.cpp`). Found in the 2026-10-19 spikes (`docs/research/player-events.md`).

## Rule 1: comparisons

| ID | Engine function | Our hook | Notes |
|---|---|---|---|
| 105220 | `Internal::CodeTasklet` value comparison (CMP_EQ and the others call it) | `HookCompare` | `this` is the running tasklet (`+0x10` its stack). Objects compare by handle: two `Object`s with one handle are equal. On the host, the player (0x14) against a remote player's copy returns 0 (equal), whatever names the player: `Game.GetPlayer()`, a `PlayerREF` property, a variable that stored it. |

Upstream had this hook (`b87e5820`, `HookCompareVariables`) and dropped it for beast form: the check passed
for the copy, then `Game.GetPlayer()` handed the effect to the host's player. That path no longer exists:
the target's owner applies spell effects (`docs/design/copy-contract.md`), so the change effect's script runs
on the guest's game only and the host never sees `Target == Game.GetPlayer()` for a copy
(`client-beast-form-stays-client`, 2026-10-20; below).

## Rule 2: Game.GetPlayer() is the guest whose action started the script

A script stack runs for a guest when:

- its call was queued with that guest's character as an argument (the engine's event for the copy's action:
  `OnActivate`, `OnTriggerEnter`, `OnHit`...), or
- it was queued while a native ran for that guest (an `Activate` raising `OnActivate`, a `SetStage`
  running a fragment), or
- it answers a registration made for that guest (`RegisterForSingleUpdate` → `OnUpdate`; the repeating
  `RegisterForUpdate` until `UnregisterForUpdate`).

| ID | Engine function | Our hook | What it does here |
|---|---|---|---|
| 104943 | `Internal::RawFuncCallQuery` constructor `(callType, objectType, name, self, args)` | `HookCallQueued` | Decides the guest a queued call runs for (above); arguments already built, read without engine calls. |
| 104848 | `VirtualMachine`: queue a call message `(stack, query)` | `HookStackCreated` | The stack is allocated with the query: the call's guest becomes the stack's (`Stack::stackId`, `+0x80`). Seven dispatch paths call it. |
| 104480 | `Stack::Dtor` | `HookStackEnded` | Forgets the stack. |
| 104651 | `NativeFunctionBase::Call(BSTSmartPointer<Stack>&, logger, vm, bool)` | `HookNativeCalled` | Every native: while it runs for a guest, a thread-local carries the guest to what it queues. Registers and unregisters for updates. |
| 55618 | `NativeFunction0<StaticFunctionTag, Actor*>::MarshallAndDispatch` | `HookGetPlayer` | `Game.GetPlayer` (and other no-argument natives returning an Actor, filtered by name): in a stack run for a guest, the result is repacked (55510, then `Variable` assignment 104297) as that guest's character. |

- Guests' characters are known by form id (`SkyrimBridge::SetActorCopy` / `SetActorPlayer` / `DeleteActor`
  keep the list through `GuestScripts::SetGuest`, filled only while hosting), so the constructor never
  resolves a form: it can run with engine locks held (spike 1: building arguments there added lock orders
  the engine never takes). The character is confirmed (`TESForm::GetById`, `IsOtherPlayer`) only where it
  is used, in script threads.
- Measured with the lock-order checker after `plate-counts-client` and the combat, door, lock and travel
  scenarios: no report with these hooks on the stack.

## Tests

Unpatched vanilla scripts, the client acting on its own game; checked on the host.

| Scenario | Script | Rule | What the host shows |
|---|---|---|---|
| `plate-counts-client` | Kolbjorn Barrow pressure plate (`DLC2dunKolbjornPressureTileScript`, a player stored at `OnInit`) | 1: a stored player | the plate goes `inTrigger` under the client's character and back to `Waiting` behind it (harness `script_state`: `FindBoundObject`, VM vfunc 0x1C, then `Object::currentState`, `+0x10`) |
| `sword-box-asks-client` | Rebel's Cairn sword in the stone: the box (`dunRebelsCairnActivator`, `obj == Game.GetPlayer()`) activates the manager (`dunRebelsCairnSwordInStoneManager`), whose new stack asks `Game.GetPlayer()` for the sword, takes it, opens the door; the sword in the stone gives it back | 1, and 2 through a call queued by `Activate` | the secret door opens though the host's player has no sword, closes again, and the host's player never gets the sword (harness `item_count`) |
| `ring-turns-for-client` | A claw door's ring (`HallofStoriesDiskScript`, `actorRef == game.GetPlayer()`, state `positionN`), the E3 demo's Bleak Falls Barrow | 1 | the host's ring turns with the client's three times round (rings only finish turning in view: the players stand before the door) |
| `keyhole-asks-client` | The same door's keyhole (`HallofStoriesKeyholeScript`: `actorRef == game.GetPlayer()`, then `game.GetPlayer().GetItemCount(claw)`), a ring wrong | 1, and 2 in the handler | the keyhole takes the try (`busy`, then back to `mainState`) though the host's player holds no claw; the door stays shut |
| `shrine-blesses-client` | Solitude temple shrines (`TempleBlessingScript`: casts on the activator, messages when it is the player) | 1 | each player gets only their own shrine's blessing on every game, though each game sees the other's character pray |
| `vilkas-counts-client-hits` | The Companions' trial (`C00TrainerScript` on Vilkas's alias: hits by `Game.GetPlayer()` count, anyone else's end the trial; stage 150 gives his sword to `Game.GetPlayer()`) | 1, and 2 through `SetStage` | three hits pass the trial (stage 150, not 110), and the sword does not go to the host's player |
| `statue-fires-at-client` | Soljund's Sinkhole boss trigger (`dunSoljundsBossSCRIPT`): `RegisterForSingleUpdate`, every seventh `OnUpdate` the statue casts Firebolt at `game.getPlayer()` | 1, and 2 through `RegisterForSingleUpdate` | the statue's fire (`FireFXShader`) lands on the client's character; the client's own statue is disabled for the test, so the fire is the host's |
| `client-beast-form-stays-client` | Beast Form (`WerewolfChangeEffectScript`: `Target == Game.GetPlayer()` starts `PlayerWerewolfQuest`, whose `SetRace` changes `Game.GetPlayer()`), upstream's reason for dropping rule 1 | none: the effect is the guest's | the guest changes and changes back, the host shows its copy do the same (respawned for each race), the host's player keeps its race and the host's `PlayerWerewolfQuest` never runs |

The ring, keyhole, shrine and trial scripts are the ones upstream shipped patched copies of (`SkyrimTogetherUtils.IsPlayer`,
`IsRemotePlayer`, the trial's player checks commented out); the patches and the natives are gone, and these
scenarios run the vanilla scripts. Vilkas is brought to the home cell's stage and held there by the harness
(`docs/re/systems/actor-ai-and-copies.md`, "Holding an NPC still (test harness)").

Each scenario has to leave the world as it found it: these scripts run once per state, so a run that stops
halfway leaves the next one nothing to trigger. The sword scenario takes the sword back; the Soljund trigger
re-arms when its cell detaches (it stays in `polling`). Control: with `SKYRIMCOOP_DISABLE_HOOKS=GuestScripts` (2026-10-20)
all three fail at rule 1's step (the plate stays up, the door stays shut, the trigger never polls). Rule 2
has no control of its own (the hooks go out together); its scenarios are built so that only the client's
character can pass: the host's player has no sword, and the client's statue is disabled.

Found while writing them:

- Both games still run the client's world scripts (world-state sync step 5): the client's own box takes a
  sword on its game, and the host's `AddItem` to the copy reaches the client's player too, so the client ends
  with two swords where one went back. The sword scenario gives the client two for that.
- A NiController door (the secret door) only plays its `Open` / `Close` sequence while a camera sees it, and
  `GetOpenState` reports `opening` (2) until then (`BGSOpenCloseForm::GetOpenState`, 14288: the sequence's
  state). A scenario that waits on such a door stands the players where they see it. Harness `door_open`
  (`BGSOpenCloseForm::SetOpenState`, 14287) puts a door back in one game.
- The copy took Red Eagle's sword into its hand when it got one on the host, while its player held another
  sword: the next baseline check found the copy's hands differing from its player's (not followed up).

### Beast form (2026-10-20)

The chain on the guest's game, as `client-beast-form-stays-client` waits on it (harness `engine_events`):

```
Shout key (a power fires on release) → spell_cast 92C48 Beast Form
  → magic_effect_apply 92C45 → WerewolfChangeEffectScript.OnEffectStart
       Target == Game.GetPlayer() → PlayerWerewolfQuest.Start()   (2BA16; stage 0 PrepShift: controls, UI)
       VFXSpell.Cast(Target)      → magic_effect_apply F8209 → WerewolfTransformVisual.OnEffectStart
            PlayIdle IdleWerewolfTransformation (2A59D), then on its "SetRace" graph event
            or after Utility.Wait(10): SetStage(1) → InitialShift → SetRace(WerewolfBeastRace CDD84)
  setstage 100 → ShiftBack: SetRace(C00's PlayerOriginalRace), Wait(5), Shutdown → SetBeastForm(false)
```

- The first change of a Companions member is the rampage's: `CompanionsHousekeepingScript.PlayerIsWerewolfVirgin`
  (C00, 0x4B2D9) makes the effect only clear the flag. It is false unless the Companions setup ran; the
  scenario sets it with harness `script_var` (console `setpqv` is not needed, and `player.cast` casts nothing
  in this setup: no engine event at all).
- `WerewolfTransformVisual` waits out its 10 seconds whatever happens: changed back before that, it calls
  `SetStage(1)`, which starts the quest again and changes the player again. The scenario changes back only
  once no stack runs that script (harness `script_stacks`).
- The guest respawns its copies on each race (`CharacterService::OnBeastFormChange`: entering by the race
  poll, leaving by the `SetBeastForm(false)` hook). The respawn carries the owner's state now
  (`RequestRespawn::HasState`: latest action, graph state, values, inventory): the host rebuilt the new copy
  from the record of its last cell entry before (its magicka from before the harness's `setav`), and the
  host's copy, read for it, is already gone when the host asks (`CharacterService::RespawnCopy`).

### Harness readouts for scripts

| Command | What it reads | Engine |
|---|---|---|
| `script_state` | a script's state on a form | `FindBoundObject` (VM vfunc 0x1C), `Object::currentState` `+0x10` |
| `script_var` | a script variable, or an auto property's `::<Name>_var`; sets an int, float or bool | `Object::type` `+0x08`, variables after the object (`+0x30`), the type's `VariableInfo` list after its user flags in `data` (`+0x30`), a parent's variables first (CommonLibSSE-NG `Object::GetVariable`) |
| `script_stacks` | every running stack (running, in a latent wait, queued) as its frames' `Script.Function` | `Internal::VirtualMachine::allRunningStacks` `+0x9320` under `runningStacksLock` `+0x9318`; `Stack::top` `+0x60`, `StackFrame::previous` `+0x08`, `owningFunction` `+0x10` |
| `engine_events` | the script events (spell cast, magic effect apply, effect start/finish, quest start/stop, equip), numbered | `EventDispatcherManager` sinks (14298) |

`script_stacks` reads frames another thread may be changing; it runs where harness commands run (the game
thread, outside the VM's update), where the VM leaves its stacks alone.
