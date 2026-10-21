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
| 55618 | `NativeFunction0<StaticFunctionTag, Actor*>::MarshallAndDispatch` | `HookGetPlayer` | `Game.GetPlayer` (55469, bound in 55503; other no-argument natives returning an Actor share it, filtered by name, case-insensitively: below): in a stack run for a guest, the result is repacked (55510, then `Variable` assignment 104297) as that guest's character. |
| 53954, 55744 | `SkyrimVM` queuing a delayed functor; `CastFunctor::operator()` | `HookFunctorQueued`, `HookCastRun` | `Spell.Cast` casts later from a functor, maybe after the script ended: the functor is tagged with the native's guest when queued, and the cast runs for it (`docs/re/systems/guest-world-events.md`). |

- Guests' characters are known by form id (`SkyrimBridge::SetActorCopy` / `SetActorPlayer` / `DeleteActor`
  keep the list through `GuestScripts::SetGuest`, filled only while hosting), so the constructor never
  resolves a form: it can run with engine locks held (spike 1: building arguments there added lock orders
  the engine never takes). The character is confirmed (`TESForm::GetById`, `IsOtherPlayer`) only where it
  is used, in script threads.
- Native and event names are compared case-insensitively (`SameName`, `engine-names.md`): a name reads
  back from the engine's string pool in whichever spelling was pooled first.
- Measured with the lock-order checker after `plate-counts-client` and the combat, door, lock and travel
  scenarios: no report with these hooks on the stack.

## Tests

Unpatched vanilla scripts, the client acting on its own game; checked on the host.

| Scenario | Script | Rule | What the host shows |
|---|---|---|---|
| `plate-counts-client` | Kolbjorn Barrow pressure plate (`DLC2dunKolbjornPressureTileScript`, a player stored at `OnInit`) | 1: a stored player | the plate goes `inTrigger` under the client's character and back to `Waiting` behind it (harness `script_state`: `FindBoundObject`, VM vfunc 0x1C, then `Object::currentState`, `+0x10`) |
| `sword-box-asks-client` | Rebel's Cairn sword in the stone: the box (`dunRebelsCairnActivator`, `obj == Game.GetPlayer()`) activates the manager (`dunRebelsCairnSwordInStoneManager`), whose new stack asks `Game.GetPlayer()` for the sword, takes it, opens the door; the sword in the stone gives it back | 1, and 2 through a call queued by `Activate` | the secret door opens though the host's player has no sword, closes again, and the host's player never gets the sword (harness `item_count`) |
| `ring-turns-for-client` | A claw door's ring (`HallofStoriesDiskScript`, `actorRef == game.GetPlayer()`, state `positionN`), the E3 demo's Bleak Falls Barrow | 1 | the host's ring turns three times round as the client turns it; the client's own ring script stays (step 5) (rings only finish turning in view: the players stand before the door) |
| `keyhole-asks-client` | The same door's keyhole (`HallofStoriesKeyholeScript`: `actorRef == game.GetPlayer()`, then `game.GetPlayer().GetItemCount(claw)`), a ring wrong | 1, and 2 in the handler | the keyhole takes the try (`busy`, then back to `mainState`) though the host's player holds no claw; the door stays shut |
| `shrine-blesses-client` | Solitude temple shrines (`TempleBlessingScript`: casts on the activator, messages when it is the player) | 1, and 2 through `Spell.Cast` | each player gets only their own shrine's blessing on every game, though each game sees the other's character pray; the client's comes from the host's script (step 5), cast by its copy and applied by its game |
| `vilkas-counts-client-hits` | The Companions' trial (`C00TrainerScript` on Vilkas's alias: hits by `Game.GetPlayer()` count, anyone else's end the trial) | 1 | three hits pass the trial (stage 150, not 110). Nothing the hits start asks for the player: stage 150's fragment only stops the fight, and `PassTheSword` (`Game.GetPlayer().AddItem`) is stage 200's, set from his dialogue (`QF_C00VilkasTrainingQuest`: 150 → `Fragment_3`, 200 → `Fragment_1`) |
| `statue-fires-at-client` | Soljund's Sinkhole boss trigger (`dunSoljundsBossSCRIPT`): `RegisterForSingleUpdate`, every seventh `OnUpdate` the statue casts Firebolt at `game.getPlayer()` | 1, and 2 through `RegisterForSingleUpdate` | the host's own hit events (harness `engine_events`, `hit`): the statue's Firebolt (`0x12FD0`) hits the client's character and never the host's player, who stands 3,500 units away |
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

- Both games ran the client's world scripts until world-state sync step 5 (2026-10-21): the client's own box
  took a sword on its game, and the host's `AddItem` to the copy reached the client's player too, so the
  client ended with two swords where one went back. Now the client's box runs nothing: one sword goes and
  comes back once, and the scenario checks it.
- A NiController door (the secret door) only plays its `Open` / `Close` sequence while a camera sees it, and
  `GetOpenState` reports `opening` (2) until then (`BGSOpenCloseForm::GetOpenState`, 14288: the sequence's
  state). A scenario that waits on such a door stands the players where they see it. Harness `door_open`
  (`BGSOpenCloseForm::SetOpenState`, 14287) puts a door back in one game.
- The copy took Red Eagle's sword into its hand when it got one on the host, while its player held another
  sword: the next baseline check found the copy's hands differing from its player's (not followed up).

### Rule 2 never ran (found and fixed 2026-10-20)

`sword-box-asks-client` ("[host] secret door C149D opens") and `keyhole-asks-client` ("[host] the keyhole takes
the claw") failed on every fresh duo. Logged from the hooks during the keyhole run:

```
OnActivate queued on the keyhole, args [the copy]  → call tagged for the copy (rule 2's tagging works)
stack created for that call                        → stack tagged for the copy
GetPlayer runs in that stack                       → name reads 'getplayer'; strcmp(name, "GetPlayer") != 0
                                                   → result not repacked: the host's player
actorRef == game.GetPlayer()                       → true (rule 1)
game.GetPlayer().GetItemCount(claw)                → 0: the host's player has no claw → message, no "busy"
```

The sword box failed the same way (`ActivateBox` asks `Game.GetPlayer()` for the sword). The fix compares
names case-insensitively (`SameName`), also for the timer natives and their events, which matched only
because they happened to be pooled in the spelling we wrote. Why the pool holds `getplayer` is not known
(something pooled that spelling first); it was the same on every fresh duo since before the beast form work.

`vilkas-counts-client-hits` and `statue-fires-at-client` passed with rule 2 dead too:

- The trial's sword is given at stage 200, from dialogue, which the scenario never reaches. The scenario
  now claims rule 1 only. Rule 2 through `SetStage` has no scenario.
- The statue's fire on the client's character was the client's own: the client's game runs the trigger
  too (world-state sync step 5) and its statue fired at its own player though the scenario had disabled it
  (`6AE8C.disable` on the client; why it still fires is not known). The copy on the host mirrors that hit.
  With rule 2 dead the host's statue aims at the host's player: its Firebolt leaves at a heading of 2.37
  (toward the host's player) instead of 1.52 (toward the client's character), read from
  `Projectile::LaunchData` (`angleZ` `+0x48`, CommonLibSSE-NG). The scenario now checks the host's own hits
  (harness `engine_events`, `hit`: `TESHitEvent`, target, cause, source `+0x10`, projectile `+0x14`) and fails
  with rule 2 off (control, 2026-10-20).

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
| `engine_events` | the script events (spell cast, magic effect apply, effect start/finish, hit, quest start/stop, equip), numbered | `EventDispatcherManager` sinks (14298) |

`script_stacks` reads frames another thread may be changing; it runs where harness commands run (the game
thread, outside the VM's update), where the VM leaves its stacks alone.
