# Papyrus: guests' characters are the player (host)

Engine side of `docs/design/party-model.md`, "A guest's actions on the host", rules 1 and 2
(`Code/client/Games/Skyrim/Papyrus/GuestScripts.cpp`). Found in the 2026-10-19 spikes (`docs/research/player-events.md`).

## Rule 1: comparisons

| ID | Engine function | Our hook | Notes |
|---|---|---|---|
| 105220 | `Internal::CodeTasklet` value comparison (CMP_EQ and the others call it) | `HookCompare` | `this` is the running tasklet (`+0x10` its stack). Objects compare by handle: two `Object`s with one handle are equal. On the host, the player (0x14) against a remote player's copy returns 0 (equal), whatever names the player: `Game.GetPlayer()`, a `PlayerREF` property, a variable that stored it. |

Upstream had this hook (`b87e5820`, `HookCompareVariables`) and dropped it for beast form: the check passed
for the copy, then `Game.GetPlayer()` handed the effect to the host's player. Rule 2 is what fixes that.

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
